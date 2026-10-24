// Apps/UltraDBAdmin/engine/DbAdminSqlScript.cpp
// Version: 0.3.0 - StatementSpans, StatementAt, ExplainSql
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSqlScript.h"

#include "DbAdminSqliteTable.h"

#include <algorithm>
#include <set>

namespace UltraDBAdmin {
namespace {

std::string Upper(std::string text) {
    for (auto& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

bool IsWord(const SqlToken& token, const char* word) {
    return token.kind == SqlToken::Kind::Word && Upper(token.text) == word;
}

bool IsSemicolon(const SqlToken& token) { return token.kind == SqlToken::Kind::Punct && token.text == ";"; }

// What only psql can run: its backslash commands, and COPY ... FROM stdin,
// whose rows follow the statement as raw lines. Looked for line by line,
// before tokenizing: those rows are not SQL and would not tokenize.
std::string NeedsPsql(const std::string& script) {
    size_t at = 0;
    while (at < script.size()) {
        size_t end = script.find('\n', at);
        if (end == std::string::npos) end = script.size();
        const std::string line = script.substr(at, end - at);
        const size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string::npos) {
            if (line[first] == '\\') {
                return "it uses psql's backslash commands (" + line.substr(first, line.find_first_of(" \t\r", first) - first) +
                       "), which only psql runs";
            }
            const std::string upper = Upper(line.substr(first));
            if (upper.rfind("COPY ", 0) == 0 && upper.find("FROM STDIN") != std::string::npos) {
                return "its rows are COPY ... FROM stdin, which only psql reads - dump it again with "
                       "pg_dump --inserts, or load it with psql";
            }
        }
        at = end + 1;
    }
    return std::string();
}

} // namespace

bool ScriptFromBytes(const std::vector<uint8_t>& bytes, std::string& script, std::string& error) {
    size_t from = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) from = 3;
    if (std::find(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end(), 0) != bytes.end()) {
        error = "it is not a text file (it holds a NUL byte)";
        return false;
    }
    script.assign(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.end());
    return true;
}

namespace {

// The statements of a script, as token ranges [first, last) without their
// ';' - split where SQLite and psql split: at ';', except inside a CREATE
// TRIGGER body, which ends at END;.
std::vector<std::pair<size_t, size_t>> SplitTokens(const std::vector<SqlToken>& tokens) {
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t start = 0;
    while (start < tokens.size()) {
        if (IsSemicolon(tokens[start])) { ++start; continue; }
        // A trigger's body has statements of its own; the trigger ends at END;.
        const bool trigger = IsWord(tokens[start], "CREATE") &&
                             ((start + 1 < tokens.size() && IsWord(tokens[start + 1], "TRIGGER")) ||
                              (start + 2 < tokens.size() &&
                               (IsWord(tokens[start + 1], "TEMP") || IsWord(tokens[start + 1], "TEMPORARY")) &&
                               IsWord(tokens[start + 2], "TRIGGER")));
        size_t end = start;
        while (end < tokens.size()) {
            if (IsSemicolon(tokens[end]) && (!trigger || (end > start && IsWord(tokens[end - 1], "END")))) break;
            ++end;
        }
        ranges.emplace_back(start, end);
        start = end + 1;
    }
    return ranges;
}

// The script's tokens without its comments; false when it does not tokenize.
bool CodeTokens(const std::string& script, std::vector<SqlToken>& tokens, std::string& error) {
    std::vector<SqlToken> all;
    if (!TokenizeSql(script, all, error)) return false;
    tokens.clear();
    for (auto& token : all)
        if (token.kind != SqlToken::Kind::Comment) tokens.push_back(std::move(token));
    return true;
}

} // namespace

ScriptSummary SummarizeScript(const std::string& script) {
    ScriptSummary summary;
    summary.unsupported = NeedsPsql(script);
    if (!summary.unsupported.empty()) return summary;
    std::vector<SqlToken> tokens;
    std::string error;
    if (!CodeTokens(script, tokens, error)) {
        summary.unsupported = "it does not read as SQL: " + error;
        return summary;
    }

    static const std::set<std::string> risky = {"DROP", "DELETE", "TRUNCATE", "ALTER", "UPDATE"};
    static const std::set<std::string> control = {"BEGIN", "COMMIT", "END", "ROLLBACK", "SAVEPOINT", "RELEASE"};
    for (const auto& [start, end] : SplitTokens(tokens)) {
        ++summary.statements;
        const std::string first = Upper(tokens[start].text);
        if (tokens[start].kind == SqlToken::Kind::Word) {
            if (control.count(first) ||
                (first == "START" && start + 1 < end && IsWord(tokens[start + 1], "TRANSACTION")))
                summary.ownTransactions = true;
            if (risky.count(first)) {
                auto it = std::find_if(summary.risky.begin(), summary.risky.end(),
                                       [&](const auto& entry) { return entry.first == first; });
                if (it == summary.risky.end()) summary.risky.emplace_back(first, 1);
                else ++it->second;
            }
        }
    }
    return summary;
}

std::vector<StatementSpan> StatementSpans(const std::string& script) {
    std::vector<StatementSpan> spans;
    std::vector<SqlToken> tokens;
    std::string error;
    if (!CodeTokens(script, tokens, error)) return spans;
    for (const auto& [start, end] : SplitTokens(tokens)) {
        StatementSpan span;
        span.begin = tokens[start].begin;
        span.end = tokens[end - 1].end;
        span.semicolon = end < tokens.size() ? tokens[end].end : span.end;
        spans.push_back(span);
    }
    return spans;
}

bool StatementAt(const std::string& script, size_t offset, StatementSpan& out) {
    const std::vector<StatementSpan> spans = StatementSpans(script);
    if (spans.empty()) return false;
    // The statement the caret is in or right after (on its ';', or in the
    // blank lines below it); before the first, the first.
    out = spans.front();
    for (const auto& span : spans) {
        if (span.begin > offset) break;
        out = span;
    }
    return true;
}

std::string ExplainSql(SqlEngine engine, const std::string& statement) {
    // Never EXPLAIN ANALYZE: that runs the statement, an UPDATE included.
    return (engine == SqlEngine::Sqlite ? "EXPLAIN QUERY PLAN " : "EXPLAIN ") + statement;
}

std::string RiskyText(const ScriptSummary& summary) {
    std::string text;
    for (const auto& [word, count] : summary.risky)
        text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + word;
    return text;
}

std::string StatementTable(const std::string& statement) {
    std::vector<SqlToken> tokens;
    std::string error;
    if (!TokenizeSql(statement, tokens, error)) return std::string();
    int depth = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const SqlToken& token = tokens[i];
        if (token.kind == SqlToken::Kind::Punct && token.text == "(") ++depth;
        if (token.kind == SqlToken::Kind::Punct && token.text == ")") --depth;
        if (depth != 0 || !IsWord(token, "FROM")) continue;
        // A name, or schema.name: words and quoted names joined by dots.
        std::string name;
        size_t k = i + 1;
        while (k < tokens.size() &&
               (tokens[k].kind == SqlToken::Kind::Word || tokens[k].kind == SqlToken::Kind::QuotedName)) {
            name += tokens[k].text;
            if (k + 2 < tokens.size() && tokens[k + 1].kind == SqlToken::Kind::Punct && tokens[k + 1].text == ".") {
                name += ".";
                k += 2;
                continue;
            }
            break;
        }
        return name;
    }
    return std::string();
}

} // namespace UltraDBAdmin
