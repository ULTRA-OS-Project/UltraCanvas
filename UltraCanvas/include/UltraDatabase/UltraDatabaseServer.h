// include/UltraDatabase/UltraDatabaseServer.h
// What a database server is doing and how it is set up - the server
// administration an admin tool shows (UltraDBAdmin's Server tab): the
// sessions connected and what each runs, a running query stopped or a
// session ended, the server's settings, and the sizes of its databases and
// tables. One engine-neutral shape, filled by each server driver from its own
// catalog (PostgreSQL's pg_stat_activity, pg_settings and size functions;
// MySQL's and MariaDB's processlist, system variables and
// information_schema.TABLES). An embedded engine has no server: SQLite
// answers Unsupported, and a tool shows its file's PRAGMAs instead.
//
// Each call fills a result set with fixed column names, so a caller reads
// them the same way whatever the engine:
//
//   Sessions  id (Int), user, database, application, client, state
//             ("active", "idle", "idle in transaction", ...), running
//             (seconds since the query or session began, Int), waiting
//             (what it waits on, "" when nothing), query, this_session
//             (Boolean: the connection asking).
//   Settings  name, value, unit, category, description, source - what the
//             engine knows of each; "" where it keeps nothing.
//   Sizes     kind ("database" or "table"), name (a table as schema.table),
//             bytes, data_bytes, index_bytes (Int; NULL where unknown).
//
// doc-check: example
//   #include <UltraDatabase/UltraDatabase.h>
//
//   UltraDbResultSet sessions;
//   if (UltraDb_ServerSessions("crm", sessions)) {
//       for (const auto& row : sessions)
//           if (row["running"].AsInt64() > 600 && !row["this_session"].AsBool()) {
//               std::string sql;
//               UltraDb_StopSessionSql("crm", row["id"].AsInt64(), false, sql);   // shown first
//               bool stopped = false;
//               UltraDb_StopSession("crm", row["id"].AsInt64(), false, stopped);
//           }
//   }
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"
#include "UltraDatabaseValue.h"

#include <cstdint>
#include <string>

// The client sessions connected to the server, the active ones first.
UltraDbResult UltraDb_ServerSessions(const std::string& connection, UltraDbResultSet& out);

// The server's settings whose name or description holds `filter`, in any
// case (all of them for ""); % and _ in it are matched as themselves.
UltraDbResult UltraDb_ServerSettings(const std::string& connection, const std::string& filter,
                                     UltraDbResultSet& out);

// Each database on the server, then each table of `schema` (none for ""),
// largest first.
UltraDbResult UltraDb_ServerSizes(const std::string& connection, const std::string& schema,
                                  UltraDbResultSet& out);

// The statement that stops session `id`'s running query - or, with
// `endSession`, ends the session - as UltraDb_StopSession runs it: for a
// confirmation to show before anything is done.
UltraDbResult UltraDb_StopSessionSql(const std::string& connection, int64_t id, bool endSession,
                                     std::string& sql);

// Stop session `id`'s query, or end the session. `stopped` says whether the
// server did: false when there is no such session, or it is not this user's
// to stop - an answer, not an error.
UltraDbResult UltraDb_StopSession(const std::string& connection, int64_t id, bool endSession,
                                  bool& stopped);
