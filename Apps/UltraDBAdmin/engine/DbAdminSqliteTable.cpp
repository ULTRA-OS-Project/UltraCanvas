// Apps/UltraDBAdmin/engine/DbAdminSqliteTable.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminSqliteTable.h"

#include <algorithm>
#include <set>

namespace UltraDBAdmin {
namespace {

char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string Upper(const std::string& text) {
    std::string out = text;
    for (auto& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

bool WordChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           c >= 0x80;
}

bool Digit(char c) { return c >= '0' && c <= '9'; }

bool IsWord(const SqlToken& token, const char* word) {
    return token.kind == SqlToken::Kind::Word && Upper(token.text) == word;
}

bool IsPunct(const SqlToken& token, char c) {
    return token.kind == SqlToken::Kind::Punct && token.text.size() == 1 && token.text[0] == c;
}

bool IsName(const SqlToken& token) {
    return token.kind == SqlToken::Kind::Word || token.kind == SqlToken::Kind::QuotedName ||
           token.kind == SqlToken::Kind::String;
}

// The words that start a column constraint, and so end a type name.
bool StartsConstraint(const SqlToken& token) {
    static const std::set<std::string> words = {"CONSTRAINT", "PRIMARY", "NOT", "NULL", "UNIQUE", "CHECK",
                                                "DEFAULT", "COLLATE", "REFERENCES", "GENERATED", "AS"};
    return token.kind == SqlToken::Kind::Word && words.count(Upper(token.text)) > 0;
}

// Index just past the parenthesised group opening at `at`, or 0 when it is
// not one or does not close.
size_t SkipGroup(const std::vector<SqlToken>& tokens, size_t at, size_t end) {
    if (at >= end || !IsPunct(tokens[at], '(')) return 0;
    int depth = 0;
    for (size_t i = at; i < end; ++i) {
        if (IsPunct(tokens[i], '(')) ++depth;
        else if (IsPunct(tokens[i], ')') && --depth == 0) return i + 1;
    }
    return 0;
}

// Reads one column definition, tokens [from, to).
bool ParseColumn(const std::vector<SqlToken>& t, size_t from, size_t to,
                 SqliteColumnDef& column, bool& autoincrement, std::string& error) {
    column = SqliteColumnDef();
    column.name = UnquoteName(t[from]);
    column.begin = t[from].begin;
    column.end = t[to - 1].end;
    column.nameEnd = t[from].end;
    size_t k = from + 1;
    while (k < to && t[k].kind == SqlToken::Kind::Word && !StartsConstraint(t[k])) ++k;
    if (k > from + 1 && k < to && IsPunct(t[k], '(')) {
        const size_t after = SkipGroup(t, k, to);
        if (!after) { error = "the type of " + column.name + " does not close its parenthesis"; return false; }
        k = after;
    }
    column.typeBegin = k > from + 1 ? t[from + 1].begin : column.nameEnd;
    column.typeEnd = k > from + 1 ? t[k - 1].end : column.nameEnd;

    auto word = [&](size_t i, const char* w) { return i < to && IsWord(t[i], w); };
    auto conflict = [&]() {
        if (word(k, "ON") && word(k + 1, "CONFLICT") && k + 2 < to) k += 3;
    };
    while (k < to) {
        const size_t start = k;
        if (word(k, "CONSTRAINT")) {
            if (k + 2 >= to) { error = "a CONSTRAINT name in " + column.name + " names nothing"; return false; }
            k += 2;
        }
        const std::string kw = Upper(t[k].text);
        std::string kind = kw;
        if (t[k].kind != SqlToken::Kind::Word) {
            error = "\"" + t[k].text + "\" in the definition of " + column.name + " is not understood";
            return false;
        }
        if (kw == "PRIMARY") {
            if (!word(k + 1, "KEY")) { error = "PRIMARY without KEY in " + column.name; return false; }
            k += 2;
            if (word(k, "ASC") || word(k, "DESC")) ++k;
            conflict();
            if (word(k, "AUTOINCREMENT")) { ++k; autoincrement = true; }
        } else if (kw == "NOT") {
            if (!word(k + 1, "NULL")) { error = "NOT without NULL in " + column.name; return false; }
            kind = "NOT NULL";
            k += 2;
            conflict();
        } else if (kw == "NULL" || kw == "UNIQUE") {
            ++k;
            conflict();
        } else if (kw == "CHECK") {
            const size_t after = SkipGroup(t, k + 1, to);
            if (!after) { error = "CHECK without its expression in " + column.name; return false; }
            k = after;
        } else if (kw == "DEFAULT") {
            ++k;
            if (k >= to) { error = "DEFAULT without a value in " + column.name; return false; }
            if (IsPunct(t[k], '(')) {
                const size_t after = SkipGroup(t, k, to);
                if (!after) { error = "the default of " + column.name + " does not close"; return false; }
                k = after;
            } else if (IsPunct(t[k], '+') || IsPunct(t[k], '-')) {
                k += 2;
            } else {
                ++k;
            }
        } else if (kw == "COLLATE") {
            k += 2;
        } else if (kw == "REFERENCES") {
            k += 2;
            if (k < to && IsPunct(t[k], '(')) {
                const size_t after = SkipGroup(t, k, to);
                if (!after) { error = "REFERENCES in " + column.name + " does not close"; return false; }
                k = after;
            }
            for (;;) {
                if (word(k, "ON") && (word(k + 1, "DELETE") || word(k + 1, "UPDATE"))) {
                    k += 2;
                    if (word(k, "SET") || word(k, "NO")) k += 2;
                    else ++k;
                } else if (word(k, "MATCH")) {
                    k += 2;
                } else if (word(k, "NOT") && word(k + 1, "DEFERRABLE")) {
                    k += 2;
                } else if (word(k, "DEFERRABLE")) {
                    ++k;
                } else if (word(k, "INITIALLY")) {
                    k += 2;
                } else {
                    break;
                }
            }
        } else if (kw == "GENERATED" || kw == "AS") {
            kind = "GENERATED";
            if (kw == "GENERATED") {
                if (!word(k + 1, "ALWAYS") || !word(k + 2, "AS")) {
                    error = "GENERATED without ALWAYS AS in " + column.name;
                    return false;
                }
                k += 2;
            }
            const size_t after = SkipGroup(t, k + 1, to);
            if (!after) { error = "the expression of " + column.name + " does not close"; return false; }
            k = after;
            if (word(k, "STORED") || word(k, "VIRTUAL")) ++k;
        } else {
            error = "\"" + t[k].text + "\" in the definition of " + column.name + " is not understood";
            return false;
        }
        if (k > to) { error = "the definition of " + column.name + " ends too soon"; return false; }
        column.constraints.push_back({kind, t[start].begin, t[k - 1].end});
    }
    return true;
}

bool SingleValue(const std::vector<SqlToken>& t) {
    static const std::set<std::string> keywords = {"NULL", "TRUE", "FALSE", "CURRENT_TIME", "CURRENT_DATE",
                                                   "CURRENT_TIMESTAMP"};
    if (t.size() == 1) {
        switch (t[0].kind) {
            case SqlToken::Kind::Number:
            case SqlToken::Kind::String:
            case SqlToken::Kind::Blob:
                return true;
            case SqlToken::Kind::Word:
                return keywords.count(Upper(t[0].text)) > 0;
            default:
                return false;
        }
    }
    return t.size() == 2 && (IsPunct(t[0], '-') || IsPunct(t[0], '+')) && t[1].kind == SqlToken::Kind::Number;
}

} // namespace

bool TokenizeSql(const std::string& sql, std::vector<SqlToken>& out, std::string& error) {
    out.clear();
    const size_t n = sql.size();
    size_t i = 0;
    auto quoted = [&](char close, bool doubled, SqlToken::Kind kind, size_t start) {
        size_t j = i + 1;
        for (;;) {
            if (j >= n) { error = "a quote opened and never closed"; return false; }
            if (sql[j] == close) {
                if (doubled && j + 1 < n && sql[j + 1] == close) { j += 2; continue; }
                break;
            }
            ++j;
        }
        out.push_back({kind, start, j + 1, sql.substr(start, j + 1 - start)});
        i = j + 1;
        return true;
    };
    while (i < n) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { ++i; continue; }
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
            size_t j = sql.find('\n', i);
            if (j == std::string::npos) j = n;
            out.push_back({SqlToken::Kind::Comment, i, j, sql.substr(i, j - i)});
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
            const size_t j = sql.find("*/", i + 2);
            if (j == std::string::npos) { error = "a comment opened and never closed"; return false; }
            out.push_back({SqlToken::Kind::Comment, i, j + 2, sql.substr(i, j + 2 - i)});
            i = j + 2;
            continue;
        }
        if (c == '"')  { if (!quoted('"', true, SqlToken::Kind::QuotedName, i)) return false; continue; }
        if (c == '`')  { if (!quoted('`', true, SqlToken::Kind::QuotedName, i)) return false; continue; }
        if (c == '[')  { if (!quoted(']', false, SqlToken::Kind::QuotedName, i)) return false; continue; }
        if (c == '\'') { if (!quoted('\'', true, SqlToken::Kind::String, i)) return false; continue; }
        if ((c == 'x' || c == 'X') && i + 1 < n && sql[i + 1] == '\'') {
            const size_t start = i;
            ++i;
            if (!quoted('\'', false, SqlToken::Kind::Blob, start)) return false;
            continue;
        }
        if (Digit(c) || (c == '.' && i + 1 < n && Digit(sql[i + 1]))) {
            size_t j = i;
            while (j < n && (WordChar(static_cast<unsigned char>(sql[j])) || sql[j] == '.')) {
                if ((sql[j] == 'e' || sql[j] == 'E') && j + 1 < n && (sql[j + 1] == '+' || sql[j + 1] == '-') &&
                    sql.compare(i, 2, "0x") != 0 && sql.compare(i, 2, "0X") != 0)
                    ++j;
                ++j;
            }
            out.push_back({SqlToken::Kind::Number, i, j, sql.substr(i, j - i)});
            i = j;
            continue;
        }
        if (WordChar(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < n && WordChar(static_cast<unsigned char>(sql[j]))) ++j;
            out.push_back({SqlToken::Kind::Word, i, j, sql.substr(i, j - i)});
            i = j;
            continue;
        }
        out.push_back({SqlToken::Kind::Punct, i, i + 1, std::string(1, c)});
        ++i;
    }
    return true;
}

bool CheckSqlFragment(const std::string& text, const std::string& what, std::string& error) {
    std::vector<SqlToken> tokens;
    if (!TokenizeSql(text, tokens, error)) { error = what + " has " + error + "."; return false; }
    if (text.find('\0') != std::string::npos) { error = what + " contains a NUL byte."; return false; }
    int depth = 0;
    for (const auto& token : tokens) {
        if (token.kind == SqlToken::Kind::Comment) { error = what + " cannot contain a comment."; return false; }
        if (IsPunct(token, ';')) { error = what + " cannot contain \";\": it would end the statement."; return false; }
        if (IsPunct(token, '(')) ++depth;
        if (IsPunct(token, ')') && --depth < 0) { error = what + " closes a parenthesis it never opened."; return false; }
    }
    if (depth != 0) { error = what + " leaves a parenthesis open."; return false; }
    return true;
}

std::string UnquoteName(const SqlToken& token) {
    if (token.kind != SqlToken::Kind::QuotedName && token.kind != SqlToken::Kind::String) return token.text;
    const std::string& raw = token.text;
    if (raw.size() < 2) return raw;
    const char close = raw.back();
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        out += raw[i];
        if (raw[i] == close && close != ']' && i + 2 < raw.size() && raw[i + 1] == close) ++i;
    }
    return out;
}

bool SameSqliteName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
    return true;
}

std::string SqliteDefaultClause(const std::string& expression) {
    std::vector<SqlToken> tokens;
    std::string error;
    if (TokenizeSql(expression, tokens, error)) {
        if (SingleValue(tokens)) return "DEFAULT " + expression;
        // Already one parenthesised expression.
        if (!tokens.empty() && SkipGroup(tokens, 0, tokens.size()) == tokens.size()) return "DEFAULT " + expression;
    }
    return "DEFAULT (" + expression + ")";
}

bool ParseCreateTable(const std::string& sql, SqliteCreateTable& out, std::string& error) {
    out = SqliteCreateTable();
    std::vector<SqlToken> all;
    if (!TokenizeSql(sql, all, error)) return false;
    std::vector<SqlToken> t;
    for (auto& token : all)
        if (token.kind != SqlToken::Kind::Comment) t.push_back(std::move(token));
    const size_t n = t.size();
    size_t k = 0;
    if (k >= n || !IsWord(t[k], "CREATE")) { error = "it is not a CREATE TABLE statement"; return false; }
    ++k;
    if (k < n && (IsWord(t[k], "TEMP") || IsWord(t[k], "TEMPORARY"))) ++k;
    if (k < n && IsWord(t[k], "VIRTUAL")) { error = "it is a virtual table, whose module decides its columns"; return false; }
    if (k >= n || !IsWord(t[k], "TABLE")) { error = "it is not a CREATE TABLE statement"; return false; }
    ++k;
    if (k + 2 < n && IsWord(t[k], "IF") && IsWord(t[k + 1], "NOT") && IsWord(t[k + 2], "EXISTS")) k += 3;
    if (k >= n || !IsName(t[k])) { error = "the table has no name"; return false; }
    ++k;
    if (k + 1 < n && IsPunct(t[k], '.') && IsName(t[k + 1])) k += 2;
    out.headEnd = t[k - 1].end;
    if (k >= n || !IsPunct(t[k], '(')) { error = "it is CREATE TABLE ... AS SELECT, which declares no columns"; return false; }
    const size_t close = SkipGroup(t, k, n);
    if (!close) { error = "its column list does not close"; return false; }

    // The items between the parentheses, split at the commas of depth one.
    size_t itemStart = k + 1;
    int depth = 0;
    bool constraintsBegun = false;
    for (size_t i = k + 1; i < close; ++i) {
        const bool last = i == close - 1;
        if (IsPunct(t[i], '(')) ++depth;
        else if (IsPunct(t[i], ')')) --depth;
        if (!last && !(depth == 0 && IsPunct(t[i], ','))) continue;
        const size_t itemEnd = i;       // exclusive: the comma, or the closing parenthesis
        if (itemEnd == itemStart) { error = "an empty item in the column list"; return false; }
        const SqlToken& first = t[itemStart];
        const std::string head = Upper(first.text);
        const bool tableConstraint = first.kind == SqlToken::Kind::Word &&
                                     (head == "CONSTRAINT" || head == "PRIMARY" || head == "UNIQUE" ||
                                      head == "CHECK" || head == "FOREIGN");
        if (tableConstraint) {
            constraintsBegun = true;
        } else {
            if (constraintsBegun) { error = "a column follows the table constraints"; return false; }
            if (!IsName(first)) { error = "\"" + first.text + "\" does not name a column"; return false; }
            SqliteColumnDef column;
            if (!ParseColumn(t, itemStart, itemEnd, column, out.autoincrement, error)) return false;
            out.columns.push_back(std::move(column));
        }
        itemStart = i + 1;
    }
    if (out.columns.empty()) { error = "it declares no columns"; return false; }
    for (size_t i = close; i + 1 < n; ++i)
        if (IsWord(t[i], "WITHOUT") && IsWord(t[i + 1], "ROWID")) out.withoutRowid = true;
    return true;
}

bool RewriteCreateTable(const std::string& sql, const std::string& newTable,
                        const std::vector<ColumnRewrite>& changes, std::string& out, std::string& error) {
    SqliteCreateTable parsed;
    if (!ParseCreateTable(sql, parsed, error)) return false;
    struct Splice { size_t begin, end; std::string text; };
    std::vector<Splice> splices;
    splices.push_back({0, parsed.headEnd, "CREATE TABLE " + newTable});
    for (const auto& change : changes) {
        const SqliteColumnDef* column = nullptr;
        for (const auto& c : parsed.columns)
            if (SameSqliteName(c.name, change.column)) column = &c;
        if (!column) { error = "the table definition has no column " + change.column; return false; }
        std::string text = sql.substr(column->begin, column->nameEnd - column->begin);
        const std::string type = change.setType
                                     ? change.type
                                     : sql.substr(column->typeBegin, column->typeEnd - column->typeBegin);
        if (!type.empty()) text += " " + type;
        for (const auto& constraint : column->constraints) {
            if (change.setNotNull && (constraint.kind == "NOT NULL" || constraint.kind == "NULL")) continue;
            if (change.setDefault && constraint.kind == "DEFAULT") continue;
            text += " " + sql.substr(constraint.begin, constraint.end - constraint.begin);
        }
        if (change.setNotNull && change.notNull) text += " NOT NULL";
        if (change.setDefault && change.hasDefault) text += " " + SqliteDefaultClause(change.defaultExpression);
        splices.push_back({column->begin, column->end, std::move(text)});
    }
    std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin > b.begin; });
    out = sql;
    for (const auto& splice : splices) out.replace(splice.begin, splice.end - splice.begin, splice.text);
    return true;
}

UltraDbResult ReadSqliteTableSource(const std::string& connection, const std::string& schema,
                                    const std::string& table, SqliteTableSource& out) {
    out = SqliteTableSource();
    std::string qs;
    UltraDbResult result = UltraDb_QuoteIdentifier(connection, schema.empty() ? std::string("main") : schema, qs);
    if (!result) return result;
    const std::string master = qs + ".sqlite_master";
    UltraDbResultSet rows;

    result = UltraDb_Query(connection, "SELECT sql FROM " + master + " WHERE type = 'table' AND name = ?",
                           {UltraDbValue(table)}, rows);
    if (!result) return result;
    if (rows.Empty() || rows.Row(0)[0].IsNull())
        return UltraDbResult::Error(UltraDbResultCode::NotFound, "no table " + table + " in " + schema);
    out.createSql = rows.Row(0)[0].AsString();

    result = UltraDb_Query(connection,
                           "SELECT name, sql FROM " + master +
                               " WHERE type = 'index' AND tbl_name = ? AND sql IS NOT NULL ORDER BY rowid",
                           {UltraDbValue(table)}, rows);
    if (!result) return result;
    for (const auto& row : rows) out.indexes.emplace_back(row[0].AsString(), row[1].AsString());

    result = UltraDb_Query(connection,
                           "SELECT sql FROM " + master +
                               " WHERE type = 'trigger' AND tbl_name = ? AND sql IS NOT NULL ORDER BY rowid",
                           {UltraDbValue(table)}, rows);
    if (!result) return result;
    for (const auto& row : rows) out.triggers.push_back(row[0].AsString());

    result = UltraDb_Query(connection,
                           "SELECT DISTINCT m.name FROM " + master +
                               " AS m, pragma_foreign_key_list(m.name, ?) AS f"
                               " WHERE m.type = 'table' AND m.name <> ? AND f.\"table\" = ? COLLATE NOCASE"
                               " ORDER BY m.name",
                           {UltraDbValue(schema.empty() ? std::string("main") : schema), UltraDbValue(table),
                            UltraDbValue(table)},
                           rows);
    if (!result) return result;
    for (const auto& row : rows) out.referencedBy.push_back(row[0].AsString());

    result = UltraDb_Query(connection, "PRAGMA foreign_keys", rows);
    if (!result) return result;
    out.foreignKeys = !rows.Empty() && rows.Row(0)[0].AsInt64() != 0;
    result = UltraDb_Query(connection, "PRAGMA legacy_alter_table", rows);
    if (!result) return result;
    out.legacyAlterTable = !rows.Empty() && rows.Row(0)[0].AsInt64() != 0;

    SqliteCreateTable parsed;
    std::string error;
    if (ParseCreateTable(out.createSql, parsed, error) && parsed.autoincrement) {
        result = UltraDb_Query(connection,
                               "SELECT 1 FROM " + master + " WHERE type = 'table' AND name = 'sqlite_sequence'", rows);
        if (!result) return result;
        if (!rows.Empty()) {
            result = UltraDb_Query(connection, "SELECT seq FROM " + qs + ".sqlite_sequence WHERE name = ?",
                                   {UltraDbValue(table)}, rows);
            if (!result) return result;
            if (!rows.Empty() && !rows.Row(0)[0].IsNull()) out.sequence = rows.Row(0)[0].AsInt64();
        }
    }
    return UltraDbResult::Ok();
}

} // namespace UltraDBAdmin
