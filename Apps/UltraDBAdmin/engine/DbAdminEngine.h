// Apps/UltraDBAdmin/engine/DbAdminEngine.h
// The SQL dialect a connection speaks - what decides how a name is quoted, a
// value written, a plan made - kept apart so a header that needs only the
// word does not pull in what uses it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

namespace UltraDBAdmin {

// MySql is MySQL and MariaDB alike.
enum class SqlEngine { Sqlite, Postgres, MySql };

} // namespace UltraDBAdmin
