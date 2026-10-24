// Apps/UltraDBAdmin/engine/DbAdminServer.h
// The Server tab (Docs/Research/UltraDBAdminDesignProposal.md, 5.2 Server,
// stage 4): what the database itself is doing and how it is set up.
//   PostgreSQL, MySQL / MariaDB - read through UltraDatabase's server
//                administration (UltraDatabaseServer.h, G10): Sessions -
//                who is connected and what each runs, a query stopped or a
//                session ended; Settings, with a filter on the name and
//                description; Sizes: the databases, then the tables of a
//                schema with their data and indexes. Shown as the grid wants
//                them: how long a query has run as h:mm:ss, a size readable.
//   SQLite     - the file: page size and count, free pages, its size,
//                journal mode, encoding, auto-vacuum, user_version,
//                application_id, foreign_keys, the library's version.
// Every view is a result set the window shows in a grid.
// Version: 0.2.0 - the server engines through UltraDatabase 0.5.0; MySQL / MariaDB
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminEngine.h"

#include <UltraDatabase/UltraDatabase.h>

#include <cstdint>
#include <string>
#include <vector>

namespace UltraDBAdmin {

enum class ServerView { Sessions, Settings, Sizes, FileInfo };

// The views an engine has, in the order the tab offers them.
std::vector<ServerView> ServerViews(SqlEngine engine);
const char* ServerViewText(ServerView view);

// One view's rows. `schema` picks the tables Sizes lists; `filter` narrows
// Settings (any case, name or description). Call it on a worker.
UltraDbResult ReadServerView(const std::string& connection, SqlEngine engine, ServerView view,
                             const std::string& schema, const std::string& filter, UltraDbResultSet& out);

// Sessions' column that holds the session's id (a PostgreSQL process id, a
// MySQL thread id) - what UltraDb_StopSession takes.
const char* SessionIdColumn();

} // namespace UltraDBAdmin
