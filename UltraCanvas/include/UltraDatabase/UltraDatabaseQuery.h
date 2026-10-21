// include/UltraDatabase/UltraDatabaseQuery.h
// The query surface. Values are always passed separately from SQL text and
// bound by the driver, so user input never becomes part of a SQL string.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

#include <cstddef>
#include <string>
#include <vector>

// ---- One-shot queries ------------------------------------------------------

// Run a row-returning statement (typically SELECT) on the named connection.
// Placeholders in `sql` are filled from `params` in order.
UltraDbResult UltraDb_Query(const std::string& connection,
                            const std::string& sql,
                            const UltraDbParams& params,
                            UltraDbResultSet& out);

// Convenience overload for parameterless queries.
UltraDbResult UltraDb_Query(const std::string& connection,
                            const std::string& sql,
                            UltraDbResultSet& out);

// Run a non-row-returning statement (INSERT/UPDATE/DELETE/DDL). The result's
// affectedRows / lastInsertId carry the outcome. With no params, `sql` may
// contain several statements separated by ';'.
UltraDbResult UltraDb_Exec(const std::string& connection,
                           const std::string& sql,
                           const UltraDbParams& params = {});

// ---- Prepared statements ---------------------------------------------------

// Compile a single statement for repeated execution. Returns a handle (or
// UltraDbInvalidHandle on failure — pass an optional UltraDbResult to learn
// why). Finalize the handle when done.
UltraDbHandle UltraDb_Prepare(const std::string& connection,
                              const std::string& sql,
                              UltraDbResult* error = nullptr);

// Execute a prepared statement with fresh parameters, discarding any rows.
UltraDbResult UltraDb_ExecPrepared(UltraDbHandle statement,
                                   const UltraDbParams& params = {});

// Execute a prepared row-returning statement with fresh parameters.
UltraDbResult UltraDb_QueryPrepared(UltraDbHandle statement,
                                    const UltraDbParams& params,
                                    UltraDbResultSet& out);

// Release a prepared statement handle.
UltraDbResult UltraDb_Finalize(UltraDbHandle statement);

// ---- Scripts ---------------------------------------------------------------

// Run a script of ';'-separated statements and report each one: its text, its
// outcome and its rows. Stops after the first statement that fails (it is the
// last entry, carrying the error) and returns that error, or success. What a
// SQL console runs a pasted script with; UltraDb_Exec with no parameters runs
// a batch too, but reports only the last statement. PostgreSQL scripts are
// split on the client, as psql splits them (UltraDatabasePostgresSql.cpp), and
// each statement is its own autocommit statement unless the script BEGINs.
UltraDbResult UltraDb_ExecBatch(const std::string& connection,
                                const std::string& sql,
                                std::vector<UltraDbStatementResult>& out);

// ---- Cancelling ------------------------------------------------------------

// Stop the statement now running on the named connection, from another thread
// - the Stop button of a console. The running call returns with
// UltraDbResultCode::Cancelled. Success means the request was made, including
// when nothing was running (then nothing happens); a connection not yet
// opened has nothing to cancel. It also reaches a statement that began on
// the name before the name was re-registered or closed - such a statement
// runs on to its end on the old connection, and is still the name's to stop.
// ConnectionNotFound only when the name has neither a connection nor such a
// statement.
UltraDbResult UltraDb_CancelQuery(const std::string& connection);

// ---- Cursors ---------------------------------------------------------------

// A forward-only cursor over a query's rows, fetched a page at a time, so a
// table of ten million rows can be exported or shown without being held.
// SQLite steps the statement as pages are asked for. PostgreSQL declares a
// server-side cursor: WITH HOLD outside a transaction (the server keeps the
// result; the connection stays free for other statements), WITHOUT HOLD
// inside one (it ends with the transaction). Close it when done - an open
// SQLite cursor keeps its read snapshot.
UltraDbHandle UltraDb_OpenCursor(const std::string& connection,
                                 const std::string& sql,
                                 const UltraDbParams& params = {},
                                 UltraDbResult* error = nullptr);

// The next page: up to `maxRows` rows into `out` (cleared first; its columns
// are set even when no row is left). `exhausted` is set once the last row has
// been read; a further fetch then returns no rows.
UltraDbResult UltraDb_FetchRows(UltraDbHandle cursor, size_t maxRows,
                                UltraDbResultSet& out, bool& exhausted);

// Release a cursor. Safe on a cursor whose transaction already ended.
UltraDbResult UltraDb_CloseCursor(UltraDbHandle cursor);
