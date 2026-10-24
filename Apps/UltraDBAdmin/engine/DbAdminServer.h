// Apps/UltraDBAdmin/engine/DbAdminServer.h
// The Server tab (Docs/Research/UltraDBAdminDesignProposal.md, 5.2 Server,
// stage 4): what the database itself is doing and how it is set up.
//   PostgreSQL - Sessions: who is connected and what each runs
//                (pg_stat_activity), a query cancelled or a session ended
//                (pg_cancel_backend, pg_terminate_backend); Settings
//                (pg_settings, with a filter on the name and description);
//                Sizes: the databases, then the tables of a schema with
//                their indexes and TOAST.
//   SQLite     - the file: page size and count, free pages, its size,
//                journal mode, encoding, auto-vacuum, user_version,
//                application_id, foreign_keys, the library's version.
// Every view is a result set the window shows in a grid; a value typed into
// the settings filter is bound.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DbAdminOperations.h"      // OperationPlan
#include "DbAdminStructureEdit.h"   // SqlEngine

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

// Sessions' column that holds the session's process id.
const char* SessionPidColumn();

// pg_cancel_backend (stop the running query) or pg_terminate_backend (end
// the session) of the session `pid`, as the confirmation shows it and
// RunPlan runs it: its one row says whether PostgreSQL did - false when
// there was no such session, or it was not this user's to stop.
OperationPlan StopSessionPlan(int64_t pid, bool terminate);

} // namespace UltraDBAdmin
