// Apps/UltraDBAdmin/engine/DbAdminSqlScript.h
// A SQL file to import, read before it runs: is it text, how many
// statements, which of them change or remove what is there (DROP, DELETE,
// TRUNCATE, ALTER, UPDATE), does it manage transactions of its own (a SQLite
// .dump is BEGIN TRANSACTION ... COMMIT), and does it need what only psql
// has (pg_dump's COPY ... FROM stdin, backslash commands). The import dialog
// says all of that before anything runs; the console runs the script itself
// (SqlRunner, UltraDb_ExecBatch).
//
// Statements are counted as SQLite and psql split them: at ';' outside
// quotes, comments and PostgreSQL's dollar quotes, and not inside a CREATE
// TRIGGER body, which ends at END;.
// Version: 0.2.0 - StatementTable
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraDBAdmin {

struct ScriptSummary {
    size_t statements = 0;
    bool ownTransactions = false;     // BEGIN, COMMIT, ROLLBACK, SAVEPOINT... of its own
    // Statements that change or remove what is there, by their first word, in
    // the order first met: {"DROP", 2}, {"DELETE", 1}.
    std::vector<std::pair<std::string, size_t>> risky;
    // Why it cannot run here, or "": what needs psql, or text that does not
    // read as SQL (a quote never closed).
    std::string unsupported;
};

// The file's bytes as a script: a UTF-8 byte-order mark dropped. False, with
// `error`, for bytes that are not text (a NUL byte).
bool ScriptFromBytes(const std::vector<uint8_t>& bytes, std::string& script, std::string& error);

ScriptSummary SummarizeScript(const std::string& script);

// "3 DROP, 1 DELETE" - the risky statements in a few words; "" when none.
std::string RiskyText(const ScriptSummary& summary);

// The table a SELECT reads, as written after its first FROM - orders,
// "main"."orders", public.customers - or "" when the statement has none (a
// SELECT of values, a subquery after FROM). Rows copied as INSERT statements
// go back into it.
std::string StatementTable(const std::string& statement);

} // namespace UltraDBAdmin
