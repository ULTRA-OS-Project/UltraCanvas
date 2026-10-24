// core/UltraDatabase/UltraDatabaseMysqlSql.cpp
// The MySQL / MariaDB driver's SQL lexing: the statement splitter
// UltraDb_ExecBatch cuts a script with, and UltraDb_MysqlScriptStatements
// hands an editor.
//
// Its own translation unit, compiled whether or not libmariadb was found, for
// the reason the PostgreSQL one is (UltraDatabasePostgresSql.cpp): it is pure
// string handling, and a mistake in it - a ';' inside a string, a comment or a
// trigger's body taken for the end of a statement - corrupts a script quietly,
// so its tests must run on every machine.
//
// What MySQL treats as text differs from PostgreSQL: a backslash escapes the
// next character inside a string, a name is quoted with backticks, `#` starts
// a comment, and `--` does only when a space or a control character follows.
// And a script for the mysql client changes its statement terminator with
// DELIMITER, which is not SQL at all: the client consumes the line and splits
// at the new terminator. Dumps and the usual way of writing a trigger or a
// procedure depend on it, so the splitter does the same.
// Version: 0.2.0 - the statements as spans of the script (UltraDb_MysqlScriptStatements)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDatabaseInternal.h"

#include <cctype>
#include <string>
#include <vector>

namespace ultradb_internal {

namespace {

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// The word starting at `i`, upper-cased, and where it ends.
std::string WordAt(const std::string& sql, size_t i, size_t& end) {
    std::string word;
    end = i;
    while (end < sql.size() && IsWordChar(sql[end]))
        word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[end++]))));
    return word;
}

bool IsComment(const std::string& sql, size_t i) {
    const char c = sql[i];
    return c == '#' || (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') ||
           (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*' &&
            !(i + 2 < sql.size() && (sql[i + 2] == '!' || sql[i + 2] == 'M')));
}

} // namespace

size_t MysqlSkip(const std::string& sql, size_t i) {
    const size_t n = sql.size();
    if (i >= n) return i;
    const char c = sql[i];

    // '...' and "...": a backslash escapes the next character, and a doubled
    // quote character stays inside.
    if (c == '\'' || c == '"') {
        size_t k = i + 1;
        while (k < n) {
            if (sql[k] == '\\') { k += 2; continue; }
            if (sql[k] == c) {
                if (k + 1 < n && sql[k + 1] == c) { k += 2; continue; }
                return k + 1;
            }
            ++k;
        }
        return n;
    }

    // `name`, a doubled backtick inside.
    if (c == '`') {
        size_t k = i + 1;
        while (k < n) {
            if (sql[k] == '`') {
                if (k + 1 < n && sql[k + 1] == '`') { k += 2; continue; }
                return k + 1;
            }
            ++k;
        }
        return n;
    }

    // # to the end of the line; -- only before a space or a control
    // character ("--1" is minus minus one).
    if (c == '#' || (c == '-' && i + 1 < n && sql[i + 1] == '-' &&
                     (i + 2 >= n || static_cast<unsigned char>(sql[i + 2]) <= ' '))) {
        const size_t lineEnd = sql.find('\n', i);
        return lineEnd == std::string::npos ? n : lineEnd;
    }

    // /* ... */, which does not nest. /*! ... */ and /*M! ... */ are
    // executable comments - SQL the server runs - but their text is stepped
    // over the same way.
    if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
        const size_t close = sql.find("*/", i + 2);
        return close == std::string::npos ? n : close + 2;
    }
    return i;
}

std::vector<UltraDbScriptStatement> MysqlStatementSpans(const std::string& sql) {
    std::vector<UltraDbScriptStatement> statements;
    std::string delimiter = ";";
    size_t start = 0;
    bool content = false;         // anything but whitespace and comments yet
    bool firstWord = true;        // the statement's first word not yet seen
    bool create = false;          // the statement is a CREATE ...
    int depth = 0;                // BEGIN / CASE ... END inside a CREATE

    // A statement from `start` to `until`, its terminator ending at `after`.
    auto finish = [&](size_t until, size_t after) {
        if (content) {
            // From its first character that is neither space nor a comment.
            size_t a = start, b = until;
            while (a < b) {
                if (IsSpace(sql[a])) { ++a; continue; }
                const size_t skipped = IsComment(sql, a) ? MysqlSkip(sql, a) : a;
                if (skipped == a) break;
                a = skipped;
            }
            while (b > a && IsSpace(sql[b - 1])) --b;
            statements.push_back({a, b, after});
        }
        content = false;
        firstWord = true;
        create = false;
        depth = 0;
    };

    const size_t n = sql.size();
    for (size_t i = 0; i < n;) {
        // DELIMITER at the start of a statement: the client's command, the
        // rest of its line the new terminator. Not sent, not a statement.
        if (!content && IsWordChar(sql[i]) && (i == 0 || !IsWordChar(sql[i - 1]))) {
            size_t end = 0;
            if (WordAt(sql, i, end) == "DELIMITER" && end < n && (sql[end] == ' ' || sql[end] == '\t')) {
                size_t lineEnd = sql.find('\n', end);
                if (lineEnd == std::string::npos) lineEnd = n;
                size_t a = end, b = lineEnd;
                while (a < b && IsSpace(sql[a])) ++a;
                while (b > a && IsSpace(sql[b - 1])) --b;
                // The first token of the rest of the line.
                size_t t = a;
                while (t < b && !IsSpace(sql[t])) ++t;
                if (t > a) delimiter = sql.substr(a, t - a);
                start = i = lineEnd;
                firstWord = true;
                continue;
            }
        }

        const size_t next = MysqlSkip(sql, i);
        if (next > i) {
            if (!IsComment(sql, i)) content = true;
            if (!IsComment(sql, i)) firstWord = false;
            i = next;
            continue;
        }

        // The terminator: with the standard ';' only outside a compound
        // body, as the depth counts it; with another, wherever it stands - a
        // script that set one has written its bodies for it.
        if (sql.compare(i, delimiter.size(), delimiter) == 0 && (delimiter != ";" || depth == 0)) {
            finish(i, i + delimiter.size());
            start = i = i + delimiter.size();
            continue;
        }

        const char c = sql[i];
        if (IsWordChar(c) && (i == 0 || !IsWordChar(sql[i - 1]))) {
            size_t end = 0;
            const std::string word = WordAt(sql, i, end);
            if (firstWord) create = word == "CREATE";
            firstWord = false;
            if (create && delimiter == ";") {
                if (word == "BEGIN" || word == "CASE") {
                    ++depth;
                } else if (word == "END") {
                    // END IF, END LOOP, END WHILE and END REPEAT close what
                    // was never counted; END and END CASE close a level. The
                    // word after END belongs to it: END CASE opens nothing.
                    size_t k = end;
                    while (k < n && IsSpace(sql[k])) ++k;
                    size_t after = 0;
                    const std::string following = k < n ? WordAt(sql, k, after) : std::string();
                    const bool closesBlock = following == "IF" || following == "LOOP" ||
                                             following == "WHILE" || following == "REPEAT";
                    if (!closesBlock && depth > 0) --depth;
                    if (closesBlock || following == "CASE") end = after;
                }
            }
            content = true;
            i = end;
            continue;
        }
        if (!IsSpace(c)) {
            content = true;
            firstWord = false;
        }
        ++i;
    }
    finish(n, n);
    return statements;
}

std::vector<std::string> MysqlStatements(const std::string& sql) {
    std::vector<std::string> statements;
    for (const auto& span : MysqlStatementSpans(sql)) {
        statements.push_back(sql.substr(span.begin, span.end - span.begin));
    }
    return statements;
}

} // namespace ultradb_internal

std::vector<UltraDbScriptStatement> UltraDb_MysqlScriptStatements(const std::string& script) {
    return ultradb_internal::MysqlStatementSpans(script);
}
