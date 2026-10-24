- **UltraDatabase 0.5.0: the MySQL / MariaDB driver.** A `mysql` (or
  `mariadb`) connection reaches a MariaDB or a MySQL server through MariaDB
  Connector/C (libmariadb, LGPL-2.1, linked as the shared library), built in
  wherever the library is found. Parameters are bound to server-side
  prepared statements, never written into the SQL; the integer types come
  back as Int, FLOAT and DOUBLE as Double, binary columns as Blob, DECIMAL
  exact as text; each result column names its source table. TLS is verified
  by default, the password comes from UltraVault, `LOAD DATA LOCAL` is off.
  Scripts are cut as the mysql client cuts them, DELIMITER included
  (UltraDb_ExecBatch), and `UltraDb_MysqlScriptStatements` says where each
  statement of a script is, for an editor; a cursor streams from a session of its own, or inside
  a transaction reads what the transaction wrote; UltraDb_CancelQuery sends
  KILL QUERY. Schema introspection reads information_schema - a schema is a
  database - with each column's default as the DDL writes it on both
  servers.
- **UltraDatabase: server administration** (`UltraDatabaseServer.h`, the
  design proposal's G10). `UltraDb_ServerSessions`, `UltraDb_ServerSettings`,
  `UltraDb_ServerSizes`, `UltraDb_StopSessionSql` and `UltraDb_StopSession`
  give PostgreSQL's and MySQL's sessions, settings and sizes in one shape,
  and stop a session's query or the session - never the asking one. SQLite
  answers Unsupported. Tested against PostgreSQL, MariaDB and (in CI) MySQL 8
  in `Tests/UltraDatabase` (test_mysql.cpp, test_server.cpp).
