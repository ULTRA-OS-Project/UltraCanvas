// Apps/UltraDBAdmin/engine/DbAdminSqlScript.cpp
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

ScriptSummary SummarizeScript(const std::string& script) {
    ScriptSummary summary;
    summary.unsupported = NeedsPsql(script);
    if (!summary.unsupported.empty()) return summary;
    std::vector<SqlToken> all;
    std::string error;
    if (!TokenizeSql(script, all, error)) {
        summary.unsupported = "it does not read as SQL: " + error;
        return summary;
    }
    std::vector<SqlToken> tokens;
    for (auto& token : all)
        if (token.kind != SqlToken::Kind::Comment) tokens.push_back(std::move(token));

    static const std::set<std::string> risky = {"DROP", "DELETE", "TRUNCATE", "ALTER", "UPDATE"};
    static const std::set<std::string> control = {"BEGIN", "COMMIT", "END", "ROLLBACK", "SAVEPOINT", "RELEASE"};
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
        start = end + 1;
    }
    return summary;
}

std::string RiskyText(const ScriptSummary& summary) {
    std::string text;
    for (const auto& [word, count] : summary.risky)
        text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + word;
    return text;
}

} // namespace UltraDBAdmin
