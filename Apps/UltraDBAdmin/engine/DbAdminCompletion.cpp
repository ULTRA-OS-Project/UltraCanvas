// Apps/UltraDBAdmin/engine/DbAdminCompletion.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminCompletion.h"

#include "DbAdminExport.h"        // QuotedName
#include "DbAdminSqlScript.h"     // StatementAt
#include "DbAdminSqliteTable.h"   // TokenizeSql, UnquoteName

#include <algorithm>
#include <cctype>
#include <set>

namespace UltraDBAdmin {
namespace {

// The keywords offered, in upper case; the engine's own after the shared.
const char* const kKeywords[] = {
    "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "NULL", "IS", "IN", "LIKE", "BETWEEN", "EXISTS", "AS", "ON",
    "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "USING", "GROUP", "BY", "ORDER", "HAVING", "LIMIT",
    "OFFSET", "DISTINCT", "UNION", "ALL", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE", "ALTER",
    "DROP", "TABLE", "VIEW", "INDEX", "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "DEFAULT", "UNIQUE", "CHECK",
    "CASE", "WHEN", "THEN", "ELSE", "END", "ASC", "DESC", "BEGIN", "COMMIT", "ROLLBACK", "WITH", "TRUNCATE",
    "EXPLAIN", "COUNT", "SUM", "AVG", "MIN", "MAX", "COALESCE", "CAST", "TRUE", "FALSE",
};
const char* const kPostgresKeywords[] = { "ILIKE", "RETURNING", "SCHEMA", "SERIAL", "NOW", "INTERVAL" };
const char* const kMysqlKeywords[] = { "SHOW", "DESCRIBE", "AUTO_INCREMENT", "DATABASE", "IFNULL", "NOW" };
const char* const kSqliteKeywords[] = { "PRAGMA", "AUTOINCREMENT", "VACUUM", "IFNULL", "GLOB" };

// Where a name is expected after these.
const std::set<std::string> kTableWords = { "FROM", "JOIN", "INTO", "UPDATE", "TABLE" };
const std::set<std::string> kColumnWords = { "SELECT", "WHERE", "ON", "AND", "OR", "BY", "SET", "HAVING",
                                             "WHEN", "THEN", "ELSE", "RETURNING", "NOT", "DISTINCT" };

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool IsKeyword(SqlEngine engine, const std::string& word) {
    const std::string upper = Upper(word);
    for (const char* k : kKeywords)
        if (upper == k) return true;
    auto in = [&upper](const auto& list) {
        for (const char* k : list)
            if (upper == k) return true;
        return false;
    };
    return engine == SqlEngine::Postgres ? in(kPostgresKeywords)
           : engine == SqlEngine::MySql  ? in(kMysqlKeywords)
                                         : in(kSqliteKeywords);
}

bool IsWordByte(unsigned char c) {
    return c >= 0x80 || std::isalnum(c) || c == '_' || c == '$';
}

// A name as it must be written: bare where the engine reads it as it is.
std::string Written(SqlEngine engine, const std::string& name) {
    bool bare = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0])) && name[0] != '$';
    for (unsigned char c : name) {
        // PostgreSQL folds a bare name to lower case: an upper-case letter needs quotes.
        if (!(std::isalnum(c) || c == '_' || c == '$') || (engine == SqlEngine::Postgres && std::isupper(c))) {
            bare = false;
            break;
        }
    }
    return bare && !IsKeyword(engine, name) ? name : QuotedName(engine, name);
}

bool SameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && Lower(a) == Lower(b);
}

// A table the statement names: FROM orders o, JOIN crm.customers AS c.
struct TableRef {
    std::string schema, table, alias;
};

bool IsNameToken(const SqlToken& t) {
    return t.kind == SqlToken::Kind::Word || t.kind == SqlToken::Kind::QuotedName;
}

bool IsPunct(const SqlToken& t, char c) {
    return t.kind == SqlToken::Kind::Punct && t.text.size() == 1 && t.text[0] == c;
}

std::vector<TableRef> TableRefs(SqlEngine engine, const std::vector<SqlToken>& tokens) {
    std::vector<TableRef> refs;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != SqlToken::Kind::Word) continue;
        const std::string word = Upper(tokens[i].text);
        if (word != "FROM" && word != "JOIN" && word != "UPDATE" && word != "INTO") continue;
        size_t j = i + 1;
        while (j < tokens.size()) {
            if (!IsNameToken(tokens[j]) || (tokens[j].kind == SqlToken::Kind::Word && IsKeyword(engine, tokens[j].text)))
                break;
            TableRef ref;
            ref.table = UnquoteName(tokens[j]);
            ++j;
            if (j + 1 < tokens.size() && IsPunct(tokens[j], '.') && IsNameToken(tokens[j + 1])) {
                ref.schema = ref.table;
                ref.table = UnquoteName(tokens[j + 1]);
                j += 2;
            }
            if (j < tokens.size() && tokens[j].kind == SqlToken::Kind::Word && Upper(tokens[j].text) == "AS") ++j;
            if (j < tokens.size() && IsNameToken(tokens[j]) &&
                !(tokens[j].kind == SqlToken::Kind::Word && IsKeyword(engine, tokens[j].text))) {
                ref.alias = UnquoteName(tokens[j]);
                ++j;
            }
            refs.push_back(ref);
            // FROM a, b: the list goes on.
            if (word == "FROM" && j < tokens.size() && IsPunct(tokens[j], ',')) {
                ++j;
                continue;
            }
            break;
        }
    }
    return refs;
}

// A schema's tables, by a name in any case.
const std::map<std::string, std::vector<std::string>>* FindSchema(const CompletionCatalog& catalog,
                                                                   const std::string& name) {
    for (const auto& [schema, tables] : catalog.schemas)
        if (SameName(schema, name)) return &tables;
    return nullptr;
}

const std::vector<std::string>* FindColumns(const CompletionCatalog& catalog, const std::string& schema,
                                            const std::string& table) {
    const auto* tables = FindSchema(catalog, schema.empty() ? catalog.defaultSchema : schema);
    if (!tables) return nullptr;
    for (const auto& [name, columns] : *tables)
        if (SameName(name, table)) return &columns;
    return nullptr;
}

} // namespace

std::string CompletionPrefix(const std::string& script, size_t offset) {
    size_t start = std::min(offset, script.size());
    while (start > 0 && IsWordByte(static_cast<unsigned char>(script[start - 1]))) --start;
    return script.substr(start, std::min(offset, script.size()) - start);
}

UltraDbResult LoadCompletionCatalog(const std::string& connection, SqlEngine engine, const std::string& defaultSchema,
                                    CompletionCatalog& out, size_t maxColumns) {
    out = CompletionCatalog();
    out.defaultSchema = defaultSchema;
    const std::string limit = std::to_string(maxColumns);
    UltraDbResultSet rows;
    UltraDbResult result = UltraDbResult::Ok();
    if (engine == SqlEngine::Sqlite) {
        UltraDbResultSet databases;
        result = UltraDb_Query(connection, "PRAGMA database_list", {}, databases);
        if (!result) return result;
        rows.SetColumns({ "s", "t", "c" });
        for (const auto& database : databases) {
            const std::string schema = database["name"].AsString();
            out.schemas[schema];
            UltraDbResultSet columns;
            result = UltraDb_Query(connection,
                                   "SELECT m.name AS t, p.name AS c FROM " + QuotedName(engine, schema) +
                                       ".sqlite_master m JOIN pragma_table_info(m.name, ?) p"
                                       " WHERE m.type IN ('table', 'view') AND m.name NOT LIKE 'sqlite!_%' ESCAPE '!'"
                                       " ORDER BY m.name, p.cid LIMIT " + limit,
                                   { UltraDbValue(schema) }, columns);
            if (!result) return result;
            for (const auto& row : columns) rows.AddRow({ UltraDbValue(schema), row["t"], row["c"] });
        }
    } else {
        const std::string system = engine == SqlEngine::Postgres
                                       ? "('pg_catalog', 'information_schema')"
                                       : "('mysql', 'information_schema', 'performance_schema', 'sys')";
        UltraDbResultSet schemas;
        result = UltraDb_Query(connection,
                               "SELECT SCHEMA_NAME AS s FROM information_schema.SCHEMATA WHERE SCHEMA_NAME NOT IN " +
                                   system + (engine == SqlEngine::Postgres ? " AND SCHEMA_NAME NOT LIKE 'pg!_%' ESCAPE '!'" : ""),
                               {}, schemas);
        if (!result) return result;
        for (const auto& row : schemas) out.schemas[row["s"].AsString()];
        // The default schema's first, so the limit leaves out others'.
        result = UltraDb_Query(connection,
                               "SELECT TABLE_SCHEMA AS s, TABLE_NAME AS t, COLUMN_NAME AS c FROM information_schema.COLUMNS"
                               " WHERE TABLE_SCHEMA NOT IN " + system +
                                   " ORDER BY CASE WHEN TABLE_SCHEMA = ? THEN 0 ELSE 1 END, TABLE_SCHEMA, TABLE_NAME,"
                                   " ORDINAL_POSITION LIMIT " + limit,
                               { UltraDbValue(defaultSchema) }, rows);
        if (!result) return result;
    }
    for (const auto& row : rows) {
        const std::string schema = row["s"].AsString();
        if (engine == SqlEngine::Postgres && schema.rfind("pg_", 0) == 0) continue;
        out.schemas[schema][row["t"].AsString()].push_back(row["c"].AsString());
    }
    return UltraDbResult::Ok();
}

std::vector<std::string> CompletionsAt(SqlEngine engine, const std::string& script, size_t offset,
                                       const CompletionCatalog& catalog, size_t limit) {
    offset = std::min(offset, script.size());
    const std::string prefix = CompletionPrefix(script, offset);
    StatementSpan span;
    const size_t begin = StatementAt(engine, script, offset, span) && span.begin <= offset - prefix.size()
                             ? span.begin
                             : 0;
    // The statement up to the word; its tokenizing fails inside a string or
    // a comment never closed, and a comment just closed is not code either.
    std::vector<SqlToken> before;
    std::string error;
    if (!TokenizeSql(script.substr(begin, offset - prefix.size() - begin), before, error)) return {};
    if (!before.empty() && before.back().kind == SqlToken::Kind::Comment &&
        before.back().end == offset - prefix.size() - begin)
        return {};
    // The whole statement, for the tables it names (FROM may come after).
    const size_t end = std::max(offset, span.end > begin ? span.end : script.size());
    std::vector<SqlToken> statement;
    if (!TokenizeSql(script.substr(begin, end - begin), statement, error)) statement = before;
    std::vector<SqlToken> code;
    for (const SqlToken& t : before)
        if (t.kind != SqlToken::Kind::Comment) code.push_back(t);
    statement.erase(std::remove_if(statement.begin(), statement.end(),
                                   [](const SqlToken& t) { return t.kind == SqlToken::Kind::Comment; }),
                    statement.end());
    const std::vector<TableRef> refs = TableRefs(engine, statement);

    std::vector<std::string> candidates;
    std::set<std::string> keywords;       // offered only for the letters they begin with
    auto add = [&candidates](const std::string& text) { candidates.push_back(text); };
    auto addColumns = [&](const std::vector<std::string>* columns) {
        if (columns)
            for (const std::string& column : *columns) add(Written(engine, column));
    };

    const bool qualified = !code.empty() && IsPunct(code.back(), '.');
    if (qualified) {
        // name. or schema.table.
        if (code.size() < 2 || !IsNameToken(code[code.size() - 2])) return {};
        const std::string name = UnquoteName(code[code.size() - 2]);
        std::string outer;
        if (code.size() >= 4 && IsPunct(code[code.size() - 3], '.') && IsNameToken(code[code.size() - 4]))
            outer = UnquoteName(code[code.size() - 4]);
        if (!outer.empty()) {
            addColumns(FindColumns(catalog, outer, name));
        } else {
            bool found = false;
            for (const TableRef& ref : refs)
                if (!ref.alias.empty() && SameName(ref.alias, name)) {
                    addColumns(FindColumns(catalog, ref.schema, ref.table));
                    found = true;
                    break;
                }
            if (!found) {
                const std::vector<std::string>* columns = FindColumns(catalog, "", name);
                for (const TableRef& ref : refs)
                    if (!columns && SameName(ref.table, name)) columns = FindColumns(catalog, ref.schema, ref.table);
                if (columns) {
                    addColumns(columns);
                } else if (const auto* tables = FindSchema(catalog, name)) {
                    for (const auto& [table, unused] : *tables) add(Written(engine, table));
                }
            }
        }
    } else {
        // The clause the word is in: the last keyword that says what comes.
        std::string clause;
        for (size_t i = code.size(); i-- > 0;) {
            if (code[i].kind != SqlToken::Kind::Word) continue;
            const std::string word = Upper(code[i].text);
            if (kTableWords.count(word) || kColumnWords.count(word)) {
                clause = word;
                break;
            }
        }
        const SqlToken* last = code.empty() ? nullptr : &code.back();
        const bool afterSeparator = !last || last->kind == SqlToken::Kind::Punct ||
                                    (last->kind == SqlToken::Kind::Word && IsKeyword(engine, last->text));
        const auto* defaultTables = FindSchema(catalog, catalog.defaultSchema);
        auto addTables = [&]() {
            if (defaultTables)
                for (const auto& [table, unused] : *defaultTables) add(Written(engine, table));
            for (const auto& [schema, unused] : catalog.schemas)
                if (!SameName(schema, catalog.defaultSchema)) add(Written(engine, schema));
        };
        if (kTableWords.count(clause) && afterSeparator) {
            addTables();
        } else if (kColumnWords.count(clause)) {
            for (const TableRef& ref : refs) addColumns(FindColumns(catalog, ref.schema, ref.table));
            for (const TableRef& ref : refs) add(Written(engine, ref.alias.empty() ? ref.table : ref.alias));
            if (refs.empty()) addTables();
        } else if (!kTableWords.count(clause)) {
            addTables();
        }
        // Keywords in the case the word is typed in.
        const bool lower = !prefix.empty() && std::islower(static_cast<unsigned char>(prefix[0]));
        auto addKeywords = [&](const auto& list) {
            for (const char* k : list) {
                add(lower ? Lower(k) : std::string(k));
                keywords.insert(candidates.back());
            }
        };
        addKeywords(kKeywords);
        if (engine == SqlEngine::Postgres) addKeywords(kPostgresKeywords);
        else if (engine == SqlEngine::MySql) addKeywords(kMysqlKeywords);
        else addKeywords(kSqliteKeywords);
    }

    // Those that begin as typed, then those that contain it; each once, and
    // not the word itself, already written in full.
    const std::string wanted = Lower(prefix);
    std::vector<std::string> first, then;
    std::set<std::string> seen;
    for (const std::string& candidate : candidates) {
        if (!seen.insert(candidate).second || candidate == prefix) continue;
        std::string bare = candidate;
        if (bare.size() >= 2 && (bare.front() == '"' || bare.front() == '`')) bare = bare.substr(1, bare.size() - 2);
        const std::string name = Lower(bare);
        if (name.rfind(wanted, 0) == 0) first.push_back(candidate);
        else if (!wanted.empty() && !keywords.count(candidate) && name.find(wanted) != std::string::npos)
            then.push_back(candidate);
    }
    first.insert(first.end(), then.begin(), then.end());
    if (first.size() > limit) first.resize(limit);
    return first;
}

} // namespace UltraDBAdmin
