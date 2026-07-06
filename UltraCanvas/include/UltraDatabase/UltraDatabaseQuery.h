// include/UltraDatabase/UltraDatabaseQuery.h
// The query surface. Values are always passed separately from SQL text and
// bound by the driver, so user input never becomes part of a SQL string.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

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
