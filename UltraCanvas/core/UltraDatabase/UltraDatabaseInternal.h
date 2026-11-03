// core/UltraDatabase/UltraDatabaseInternal.h
// Internal shared declarations for the UltraDatabase core: the connection
// manager (registry + handle tables) and the built-in SQLite driver
// accessor. Not a public header.
// Version: 0.4.0 - the encrypting SQLite VFS's internals (UltraDatabaseSqliteCipher.cpp)
// Version: 0.3.0 - the MySQL / MariaDB driver's internals
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabase/UltraDatabaseCore.h"
#include "UltraDatabase/UltraDatabasePlugins.h"
#include "UltraDatabase/UltraDatabaseQuery.h"
#include "UltraDatabase/UltraDatabaseSchema.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace ultradb_internal {

// The encrypting VFS (UltraDatabaseSqliteCipher.cpp, UltraDatabaseEncryption.h).
// Its name, or nullptr when the build has no UltraCrypt or SQLite no VFS.
const char* SqliteCipherVfsName();
// The full path SQLite will hand the VFS's xOpen for `path`.
std::string SqliteCipherFullPath(const std::string& path);
// The key the next xOpen of `fullPath` as a database takes; nullptr removes it.
void SqliteCipherSetPendingKey(const std::string& fullPath, std::shared_ptr<const UltraCryptSecureBuffer> key);
// Before opening `path` with `key`: Ok when the file is missing or empty
// (`isNew`) or encrypted and the key opens it; an error naming why not.
UltraDbResult SqliteCipherPrepare(const std::string& path, const UltraCryptSecureBuffer& key, bool& isNew);
// Right after opening: the page geometry of a new file, temporary data in memory.
UltraDbResult SqliteCipherConfigure(sqlite3* db, bool isNew);

// Returns the process-wide built-in SQLite driver singleton and guarantees it
// is registered exactly once. Called by the manager before resolving drivers.
IUltraDbDriverPlugin* BuiltinSqliteDriver();

// The PostgreSQL driver, when the build found libpq. Returns nullptr
// otherwise, so a caller can tell "not built" from "failed to connect".
IUltraDbDriverPlugin* BuiltinPostgresDriver();

// The MySQL / MariaDB driver, when the build found libmariadb; nullptr
// otherwise.
IUltraDbDriverPlugin* BuiltinMysqlDriver();

// The "relay" driver (UltraDatabaseRelay.h), always built: it needs no
// library, only the transport an application installs.
IUltraDbDriverPlugin* BuiltinRelayDriver();

// What MySQL treats as text or a comment, starting at `i` - a '...' or "..."
// string (a backslash escapes in it), a `name`, a # or "-- " comment, a
// /* */ comment (not nested) - and the index just past it, or `i` itself when
// none starts there. See UltraDatabaseMysqlSql.cpp.
size_t MysqlSkip(const std::string& sql, size_t i);

// A MySQL script cut into its statements as the mysql client cuts it: at the
// terminator outside text and comments, DELIMITER lines setting it (and not
// sent); with the standard ';', not inside a CREATE's BEGIN ... END body.
// Each from its first token (comments before it left out) to its
// terminator, without it; empty ones dropped.
std::vector<std::string> MysqlStatements(const std::string& sql);
// The same statements as where they are in `sql` (UltraDb_MysqlScriptStatements).
std::vector<UltraDbScriptStatement> MysqlStatementSpans(const std::string& sql);

// Rewrite UltraDatabase's `?` placeholders into PostgreSQL's `$1, $2, ...`,
// leaving a `?` inside a string literal, a quoted identifier, a dollar-quoted
// body or a comment untouched. Declared here so the suite can test it even in
// a build without libpq - see UltraDatabasePostgresSql.cpp.
std::string PostgresPlatzhalter(const std::string& sql);

// A PostgreSQL script cut into its statements, as psql cuts it: at the
// semicolons outside text, comments and SQL-standard function bodies. Each
// trimmed, without its ';'; empty ones dropped. For UltraDb_ExecBatch.
std::vector<std::string> PostgresAnweisungen(const std::string& sql);

// Expand a leading "~" in a filesystem path to $HOME (used by the SQLite
// driver for database paths).
std::string ExpandUserPath(const std::string& path);

// A number in a database's text form - a PostgreSQL float8 or numeric column,
// a Text value coerced with AsDouble - read with '.' as the decimal point
// whatever LC_NUMERIC says. strtod follows the locale, and the Linux backend
// calls setlocale(LC_ALL, "") for XIM, so in any GUI program on a
// comma-decimal desktop it read "3.14" as 3. Also takes PostgreSQL's spellings
// of the special values ("NaN", "Infinity", "-Infinity"). Leading whitespace
// and trailing text are ignored, as strtod ignores them. Returns false, and
// leaves `out` alone, when the text does not start with a number. Defined in
// UltraDatabaseValue.cpp, so it is compiled and tested without libpq.
bool ParseDoubleClassic(const std::string& text, double& out);

// The other direction: a double as a parameter on the wire, dot-decimal and
// with enough digits to read back as the same double. std::to_string, which
// the PostgreSQL driver used before, both follows the locale ("3,140000",
// which the server rejects) and keeps six decimals (1e-9 became "0.000000").
std::string FormatDoubleClassic(double value);

// ---- Schema introspection, per engine ----
// Plain SQL over the connection interface, so each lives in a file of its own
// (UltraDatabaseSqliteSchema.cpp, UltraDatabasePostgresSchema.cpp) and the
// PostgreSQL catalog queries compile even in a build without libpq. The
// drivers' ListSchemas / ListObjects / DescribeTable overrides call these.

// How values of a SQLite column with this declared type come back - SQLite's
// affinity rules, plus BOOLEAN as a bool.
UltraDbType SqliteValueTypeForDeclaredType(const std::string& declaredType);

UltraDbResult SqliteListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out);
UltraDbResult SqliteListObjects(IUltraDbConnection& conn, const std::string& schema,
                                std::vector<UltraDbObjectSummary>& out);
UltraDbResult SqliteDescribeTable(IUltraDbConnection& conn, const std::string& schema,
                                  const std::string& table, UltraDbTableSchema& out);

// How values of a PostgreSQL type come back from the driver, by type OID.
UltraDbType PostgresValueTypeForOid(unsigned int typeOid);

UltraDbResult PostgresListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out);
UltraDbResult PostgresListObjects(IUltraDbConnection& conn, const std::string& schema,
                                  std::vector<UltraDbObjectSummary>& out);
UltraDbResult PostgresDescribeTable(IUltraDbConnection& conn, const std::string& schema,
                                    const std::string& table, UltraDbTableSchema& out);

// How values of a MySQL column come back, by information_schema's DATA_TYPE
// and COLUMN_TYPE (an unsigned BIGINT can outgrow an Int: text).
UltraDbType MysqlValueTypeForDataType(const std::string& dataType, const std::string& columnType);

UltraDbResult MysqlListSchemas(IUltraDbConnection& conn, std::vector<std::string>& out);
UltraDbResult MysqlListObjects(IUltraDbConnection& conn, const std::string& schema,
                               std::vector<UltraDbObjectSummary>& out);
UltraDbResult MysqlDescribeTable(IUltraDbConnection& conn, const std::string& schema,
                                 const std::string& table, UltraDbTableSchema& out);

// ---- Server administration, per engine (UltraDatabaseServerViews.cpp) ----
// Plain SQL over the connection interface, in the columns
// UltraDatabaseServer.h fixes; the drivers' ServerSessions / ServerSettings /
// ServerSizes / StopSession overrides call these.

UltraDbResult PostgresServerSessions(IUltraDbConnection& conn, UltraDbResultSet& out);
UltraDbResult PostgresServerSettings(IUltraDbConnection& conn, const std::string& filter, UltraDbResultSet& out);
UltraDbResult PostgresServerSizes(IUltraDbConnection& conn, const std::string& schema, UltraDbResultSet& out);
UltraDbResult PostgresServerStatus(IUltraDbConnection& conn, UltraDbResultSet& out);
std::string PostgresStopSessionSql(int64_t id, bool endSession);
UltraDbResult PostgresStopSession(IUltraDbConnection& conn, int64_t id, bool endSession, bool& stopped);

UltraDbResult MysqlServerSessions(IUltraDbConnection& conn, UltraDbResultSet& out);
UltraDbResult MysqlServerSettings(IUltraDbConnection& conn, const std::string& filter, UltraDbResultSet& out);
UltraDbResult MysqlServerSizes(IUltraDbConnection& conn, const std::string& schema, UltraDbResultSet& out);
UltraDbResult MysqlServerStatus(IUltraDbConnection& conn, UltraDbResultSet& out);
std::string MysqlStopSessionSql(int64_t id, bool endSession);
// `ownSession` is the asking connection's thread id, refused as a target.
UltraDbResult MysqlStopSession(IUltraDbConnection& conn, int64_t id, bool endSession, int64_t ownSession,
                               bool& stopped);

} // namespace ultradb_internal
