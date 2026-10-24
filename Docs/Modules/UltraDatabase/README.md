# UltraDatabase

**Unified database access layer for ULTRA OS.**
Sibling of `UltraCanvas` (UI), `UltraNet` (networking) and `UltraAI`
(AI capabilities).

UltraDatabase gives every ULTRA OS app one central place to define,
open and use database connections — embedded or networked — behind a
single, safe, uniform API. Apps name a connection once and query it
from anywhere; which engine actually backs that connection (SQLite,
PostgreSQL, MySQL/MariaDB, …) is a configuration detail, not something
the app code hard-codes.

> Status: **Stage 1 implemented, the PostgreSQL and MySQL / MariaDB drivers
> from Stage 2, schema introspection, server administration, streaming
> cursors, per-statement scripts, cancelling and statement observers.**
> The SQLite core, connection registry, parameterized queries, prepared
> statements, transactions and versioned migrations are built and tested —
> `UltraCanvas/{include,core}/UltraDatabase/`, library target `UltraDatabase`
> (`libultradatabase.a`), test suite `Tests/UltraDatabase`
> (`ULTRACANVAS_BUILD_DATABASE_TESTS=ON`, 84 tests, the PostgreSQL and MySQL
> ones against a live server where one is configured). The **PostgreSQL**
> driver (`UltraDatabasePostgresDriver.cpp`, over libpq) is built whenever
> libpq is found at configure time; it takes `?` placeholders, verifies TLS by
> default and reads the password from UltraVault. UltraFIBU's multi-user mode
> runs on it, and CI tests it against a real server (`Tests/UltraFIBU`). The
> **MySQL / MariaDB** driver (`UltraDatabaseMysqlDriver.cpp`, over MariaDB
> Connector/C) is built whenever libmariadb is found, and speaks to MariaDB
> and MySQL servers alike: the same `?` placeholders, as server-side prepared
> statements, TLS verified by default, the password from UltraVault. Async
> queries and pooling described below are still the Stage 2/3 plan.

---

## Why it exists

Persistence is exactly the kind of concern an OS owns once. Without a
shared module, every app re-implements the same things and gets the
hard parts wrong:

- **Connection management** — opening, pooling, reconnecting, closing;
  keeping one place that knows how to reach each database.
- **Different engines, one API** — an app should read/write rows the
  same way whether the data lives in an embedded SQLite file or a
  remote PostgreSQL cluster, and should be able to *switch* without a
  rewrite (prototype on SQLite, deploy on Postgres).
- **Safety** — parameterized queries so SQL injection is not even
  possible by default; TLS for networked engines; credentials pulled
  from **UltraVault**, never embedded in app code or config files.
- **Threading** — database calls are blocking I/O; every app otherwise
  invents its own worker-thread dance and freezes the UI when it gets
  it wrong.

UltraDatabase centralises all of that behind a stable C-style API
(`UltraDb_*` free functions + opaque `UltraDbHandle`s), the same shape
as UltraNet. It **bundles SQLite** for the zero-configuration embedded
case and wraps the native client libraries of the other engines
(`libpq`, `libmariadb`, …) behind a driver-plugin architecture, so
engines can be added or swapped without touching the core or the
callers.

---

## Core idea: named connections

The heart of the module is a **connection registry**. A connection is
described once — as a small config object or an entry in a config file
— and given a **name**. Everything else in the app refers to it by that
name. Credentials are a UltraVault key, never literal secrets.

```cpp
#include <UltraDatabase/UltraDatabase.h>

// Register a named connection (typically once, at startup).
UltraDbConnectionConfig mail;
mail.name       = "mail";                      // referenced everywhere by this
mail.driver     = "sqlite";                    // embedded, zero-config
mail.database   = "~/.local/share/UltraMail/erika/mail.db";
UltraDb_RegisterConnection(mail);

UltraDbConnectionConfig crm;
crm.name        = "crm";
crm.driver      = "postgresql";                // networked
crm.host        = "db.internal";
crm.port        = 5432;
crm.database    = "customers";
crm.credentials = "vault:crm-readwrite";       // resolved via UltraVault
crm.tls         = UltraDbTls::Require;
UltraDb_RegisterConnection(crm);
```

Switching the CRM app from Postgres to MySQL later is a one-line change
to `driver`/`host` in this config (or in `connections.json`) — no query
code changes, because the query API is engine-independent.

Connections are **pooled**: `UltraDb_RegisterConnection` does not open a
socket; the pool opens and reuses physical connections lazily as
queries run, and reconnects on drop. `UltraDb_CloseConnection(name)`
drains and removes a pool.

Re-registering or closing a name never pulls a connection out from under a
call that is using it, on any thread. Each call holds its connection until it
returns, and so does every open statement, cursor and transaction handle. The
old connection closes when the last of them ends; every call that starts after
the change uses the new registration. Closing a handle (`UltraDb_Finalize`,
`UltraDb_CloseCursor`) while another thread's call on it runs works the same
way: the handle is gone for later calls, and the running one finishes.
`UltraDb_CancelQuery(name)` still reaches a statement that began on the name
before it was re-registered or closed, so a Stop button keeps working while
the connection's settings change under it.

---

## Engines at a glance

### Core (bundled, in module)

| Engine | Use | Backing library |
|---|---|---|
| **SQLite** | Embedded, single-file, zero-config; the default for app-local storage | bundled amalgamation |
| SQLite (encrypted) | At-rest encryption for local data | SQLCipher-compatible, opt-in |

SQLite ships inside the module so every app has working local
persistence with no external server and no extra dependency.

### Driver-supplied (plugins under `Plugins/UltraDatabase/`)

| Engine | Category | Backing library |
|---|---|---|
| PostgreSQL | Relational (SQL) | `libpq` - built into the module when found |
| MySQL / MariaDB | Relational (SQL) | `libmariadb` (MariaDB Connector/C, LGPL-2.1, linked dynamically) - built into the module when found |
| Microsoft SQL Server | Relational (SQL) | ODBC / FreeTDS |
| DuckDB | Embedded analytics (OLAP) | bundled/optional |
| Redis / Valkey | Key-value cache | `hiredis` |
| MongoDB | Document store | native driver |

Each engine is added through the `IDatabaseDriverPlugin` interface in
`UltraDatabase/UltraDatabasePlugins.h`, exactly the way UltraNet adds
SMTP/MQTT/SSH — the core binary does not change to add an engine. The
relational drivers expose the full SQL query API below; non-relational
drivers (Redis, Mongo) expose their natural verbs plus a common
key/document convenience layer.

---

## Architecture

```
App / Module (UltraMail, ULTRA Store, UltraAI, ...)
  │
  ├─► UltraDb_RegisterConnection / Query / Exec / Prepare / Transaction*
  │     │
  │     ├─► Connection registry + pool          (named connections)
  │     │
  │     ├─► SQLite core                          (bundled, embedded)
  │     │
  │     └─► Driver manager
  │            └─► IDatabaseDriverPlugin         (postgresql, mysql, ...)
  │                   └─► libpq / libmariadb / ODBC / ...
  │
  ├─► UltraVault                                 (connection credentials)
  └─► UltraNet                                   (TLS transport for remote engines, opt.)
```

- Single C-style entrypoint surface (`UltraDb_*`).
- Opaque `UltraDbHandle` (`uint64_t`) for prepared statements,
  transactions, streaming cursors and async queries. Zero is invalid.
- `UltraDbResult` for every blocking operation — explicit error code,
  message, driver, SQLSTATE, affected-row count, timing.
- Sync **and** (planned) async variants for queries; cancellation via
  `UltraDb_CancelQuery(connection)` from any thread.
- Engine additions are **driver-only** — the core does not change to
  support a new DBMS.

---

## Query API

Uniform across relational engines. **Parameter binding is mandatory in
spirit** — the API always takes values separately from SQL text, and
the placeholder style (`?`, `$1`, `:name`) is normalised per driver, so
callers never concatenate user input into SQL.

```cpp
// One-shot read. Values are bound, never interpolated.
UltraDbResultSet rs;
if (UltraDb_Query("mail",
        "SELECT uid, subject, flags FROM messages "
        "WHERE folder = ? AND (flags & ?) = 0 "
        "ORDER BY date DESC LIMIT ?",
        { "INBOX", UltraDbFlag_Seen, 50 }, rs)) {
    for (const auto& row : rs) {
        uint32_t     uid     = row["uid"].AsU32();
        std::string  subject = row["subject"].AsString();
    }
}

// One-shot write. Returns affected-row count in the result.
UltraDbResult r = UltraDb_Exec("mail",
    "UPDATE messages SET flags = flags | ? WHERE uid = ?",
    { UltraDbFlag_Answered, uid });

// Prepared statement, reused in a loop.
UltraDbHandle stmt = UltraDb_Prepare("mail",
    "INSERT INTO messages(uid, folder, subject, date, flags) "
    "VALUES(?, ?, ?, ?, ?)");
for (const auto& m : batch)
    UltraDb_ExecPrepared(stmt, { m.uid, m.folder, m.subject, m.date, m.flags });
UltraDb_Finalize(stmt);

// Transaction — all-or-nothing.
UltraDbHandle tx = UltraDb_Begin("mail");
UltraDb_ExecInTx(tx, "UPDATE folders SET uidnext = ? WHERE name = ?", { next, "INBOX" });
UltraDb_ExecInTx(tx, "INSERT INTO messages(...) VALUES(...)", { ... });
UltraDb_Commit(tx);        // or UltraDb_Rollback(tx)

// Async query (planned, Stage 2) — result delivered on a worker thread.
UltraDbHandle h = UltraDb_QueryAsync("crm",
    "SELECT * FROM contacts WHERE name ILIKE ?", { "%" + term + "%" },
    [](const UltraDbResultSet& rs) {
        // runs on the UltraDatabase worker pool — post to UI thread
    });

// Streaming cursor for large result sets: a page at a time, never all of it.
UltraDbHandle cur = UltraDb_OpenCursor("crm", "SELECT * FROM events WHERE day > ?", { since });
UltraDbResultSet page;
bool exhausted = false;
while (!exhausted && UltraDb_FetchRows(cur, 1000, page, exhausted))
    for (const auto& row : page) { /* ... */ }
UltraDb_CloseCursor(cur);

// A pasted script, every statement with its own result (a SQL console).
std::vector<UltraDbStatementResult> results;
UltraDb_ExecBatch("crm", script, results);   // stops after the first failure

// Stop it from the UI thread: the running call returns Cancelled.
UltraDb_CancelQuery("crm");
```

**Cursors** step the statement as pages are asked for on SQLite; on
PostgreSQL they are server-side - `WITH HOLD` outside a transaction, so the
connection is free for other statements between pages, `WITHOUT HOLD`
inside one, where they end with it. **Scripts** are split as psql splits
them on PostgreSQL (semicolons outside text, comments and `BEGIN ATOMIC`
bodies) and by SQLite itself on SQLite; each statement reports its text,
outcome and rows. **Cancelling** uses `sqlite3_interrupt` and `PQcancel`,
both safe from another thread while the statement holds the connection.

**Watching statements.** `UltraDb_AddStatementObserver` reports every
statement that runs through the calls above, on any connection, once it has
ended: its kind (query, exec, transaction, a script's statement, a cursor,
introspection), its text, how many values were bound - never the values -
the rows it returned, whether it ran in a transaction, its time and its
outcome. An admin tool's SQL log is one observer (UltraDBAdmin's); a debug
trace another. With no observer added nothing is timed or copied.

```cpp
const int id = UltraDb_AddStatementObserver([](const UltraDbStatementEvent& e) {
    // On the thread that ran it, outside every lock of the module: quick and thread-safe.
    std::fprintf(stderr, "%s: %zu rows, %s\n", e.connection.c_str(), e.rows, e.sql.c_str());
});
UltraDb_RemoveStatementObserver(id);
```

**Values.** `UltraDbValue` is a small variant: null, bool, int64,
double, text (UTF-8), blob (bytes), and timestamp. Typed accessors
(`AsU32`, `AsString`, `AsBlob`, `IsNull`, …) with defined coercion
rules. Column access by name or index.

**Schema & migrations.** `UltraDb_Migrate(name, steps)` runs an ordered,
versioned list of migration steps inside a transaction and records the
applied version in a `ultradb_schema_version` table, so an app brings
its own schema up to date on launch regardless of engine.

### Introspection, names and column metadata

`UltraDatabaseSchema.h` asks the engine what a database holds — the first
step of the UltraDBAdmin plan (`Docs/Research/UltraDBAdminDesignProposal.md`,
stage 0) and what anything that reads a schema it did not write needs. One
engine-neutral model, filled by each driver from its own catalog: SQLite's
`sqlite_master` and table-valued pragmas, PostgreSQL's `pg_catalog`.

```cpp
std::vector<std::string> schemas;               // SQLite: "main", "temp", ATTACHed
UltraDb_ListSchemas("crm", schemas);            // PostgreSQL: namespaces, minus pg_*

std::vector<UltraDbObjectSummary> objects;      // tables, views, materialized views,
UltraDb_ListObjects("crm", "public", objects);  // foreign tables, sequences

UltraDbTableSchema t;                           // columns (type, null, default, identity,
UltraDb_DescribeTable("crm", "public", "orders", t);  // generated, collation, comment),
                                                // primary key, foreign keys with actions,
                                                // indexes, triggers, view text, size

// A NAME cannot be bound, so it is quoted - by the engine's rules, through
// the one function that does it. Never splice a name in any other way.
std::string table;
if (UltraDb_QuoteQualifiedName("crm", t.schema, t.name, table))
    UltraDb_Query("crm", "SELECT * FROM " + table + " WHERE id > ? LIMIT ?", { lastId, 50 }, rs);

// What each result column is and where it came from.
for (const UltraDbColumnInfo& c : rs.ColumnInfo())
    ;  // c.declaredType ("numeric(12,2)"), c.sourceSchema / sourceTable / sourceColumn
```

On MySQL and MariaDB a schema is a database: one connection lists every
database its user may see (but the server's own: `mysql`,
`information_schema`, `performance_schema`, `sys`), read from
`information_schema`; a column's default comes back as it would be written
in the DDL on both servers (`'none'`, `0`, `current_timestamp()`), whatever
each reports. Names are quoted with backticks.

`UltraDb_ListConnections()` lists the registered connection names. A driver
that cannot introspect answers `Unsupported`. On PostgreSQL the column
metadata costs one catalog query per table the first time and nothing after
(cached per connection, emptied by DDL on that connection; inside a
transaction the lookup runs under a savepoint, so it cannot abort the
caller's work). SQLite reports a column's source table where the library
has `SQLITE_ENABLE_COLUMN_METADATA` (checked at configure time).

**Numbers.** PostgreSQL `numeric` comes back as `Text`, exactly as stored
(`AsDouble` still converts it for a caller that wants a double); `float8`
and `float4` as `Double`. Numbers cross the driver dot-decimal in both
directions whatever the process's locale, and `AsString()` of a `Double` is
dot-decimal too: the shortest text that reads back exactly (`3.14`, not
`3,140000`). MySQL's `DECIMAL` is `Text` too, and so is an unsigned
`BIGINT` too large for an `Int`; a BIT column is the `Int` its bits make, a
`BINARY` / `VARBINARY` / `BLOB` column a `Blob`, a date or time its text.

### Server administration

`UltraDatabaseServer.h` is what a server is doing and how it is set up - the
Server tab of UltraDBAdmin (the proposal's G10). One shape for PostgreSQL
and MySQL / MariaDB, fixed column names whatever the engine; SQLite has no
server and answers `Unsupported`.

```cpp
UltraDbResultSet rows;
UltraDb_ServerSessions("crm", rows);          // id, user, database, application, client,
                                              // state, running (s), waiting, query, this_session
UltraDb_ServerSettings("crm", "mem", rows);   // name, value, unit, category, description, source
UltraDb_ServerSizes("crm", "public", rows);   // kind, name, bytes, data_bytes, index_bytes

std::string sql;                              // what a confirmation shows first:
UltraDb_StopSessionSql("crm", id, false, sql);  // SELECT pg_cancel_backend(42) / KILL QUERY 42
bool stopped = false;
UltraDb_StopSession("crm", id, false, stopped); // false: gone, or not this user's to stop
```

The asking session is never stopped from itself. A settings filter matches
the name or the description in any case, `%` and `_` typed in it taken as
themselves. MySQL's settings come from `performance_schema` (with where each
value came from), MariaDB's from `information_schema.SYSTEM_VARIABLES` (with
a description for each).

---

## Threading

Database calls are blocking I/O, so UltraDatabase owns a **worker
thread pool** (sized per connection pool). The synchronous `UltraDb_*`
calls block the *calling* thread; the `*Async` variants run on the pool
and deliver results via a callback. As in UltraNet, **async callbacks
run on a UltraDatabase worker thread — callers must marshal to their
own loop** (`UltraCanvasApplication::PostToUIThread(fn)`) and must not
block. The UI never waits on the database.

Every call may come from any thread. Calls on one connection are serialised
by the driver, one statement at a time. `UltraDb_CancelQuery` is the exception:
it reaches a running statement without waiting. Registering, re-registering
and closing a name are safe while other threads' calls run on it (see *Core
idea* above).

---

## Security

- **Injection-proof by default.** SQL text and values travel
  separately; there is no "format a query string" entry point in the
  public API.
- **Credentials via UltraVault.** `credentials` on a connection is a
  vault key (`vault:...`); the module resolves it at connect time.
  Config files and `connections.json` never contain passwords — the
  same rule UltraNet follows. The **application opens the vault**
  (`UltraVault::Initialize`, with its own file and passphrase) before it
  connects; the driver only reads from it, and reports "UltraVault is not
  open" rather than quietly opening an empty one. The password is wiped
  from the driver's memory once the client library has it. Empty
  `credentials` means the server's own ways of knowing the user: libpq's
  peer / trust / `.pgpass`, MariaDB's `unix_socket` and MySQL's
  `auth_socket` over the server's socket file (a `host` that is a path).
- **No client files for the server.** The MySQL driver turns `LOAD DATA
  LOCAL` off: with it on, a server could ask for any file the program can
  read.
- **TLS for networked engines.** `UltraDbTls::{Disable,Prefer,Require,
  VerifyFull}` with verification on by default; certificate handling
  reuses the platform trust store (and UltraNet's TLS stack where a
  driver tunnels over it).
- **At-rest encryption** for SQLite via the SQLCipher-compatible path,
  opt-in per connection.
- **Least privilege.** Read-only connections (`readOnly = true`) are
  enforced by the driver where supported.

---

## Module layout

```
UltraCanvas/                          (or wherever the build places it)
├── include/UltraDatabase/
│   ├── UltraDatabase.h               umbrella include
│   ├── UltraDatabaseCore.h           init, config, result, handles, registry
│   ├── UltraDatabaseConnection.h     RegisterConnection / pool / Close
│   ├── UltraDatabaseQuery.h          Query / Exec / Prepare / cursor / async
│   ├── UltraDatabaseValue.h          UltraDbValue, UltraDbRow, UltraDbResultSet
│   ├── UltraDatabaseTransaction.h    Begin / Commit / Rollback
│   ├── UltraDatabaseMigrate.h        versioned schema migrations
│   ├── UltraDatabaseSchema.h         introspection, quoting names
│   ├── UltraDatabaseServer.h         sessions, settings, sizes, stopping a session
│   └── UltraDatabasePlugins.h        IDatabaseDriverPlugin
├── core/UltraDatabase/               (.cpp implementations)
│   ├── UltraDatabaseSqlite*.cpp      the SQLite driver and its introspection
│   ├── UltraDatabasePostgres*.cpp    the PostgreSQL driver (libpq), its SQL lexing, introspection
│   ├── UltraDatabaseMysql*.cpp       the MySQL / MariaDB driver (libmariadb), its script splitter, introspection
│   └── UltraDatabaseServerViews.cpp  server administration for both server engines
├── OS/<Platform>/UltraDatabaseSupport.cpp
└── Plugins/UltraDatabase/            (later drivers as loadable plug-ins)
    ├── mssql/
    ├── redis/
    └── ...
```

CMake target: `UltraDatabase`. Header include style:
`<UltraDatabase/UltraDatabase*.h>`.

---

## Integration with sibling modules

| Caller | Uses UltraDatabase for |
|---|---|
| **UltraMail** | Per-account message index, folder state, address book, settings — a local SQLite connection named per account (see `Docs/UltraMail/Concept.md`). |
| **UltraCanvas apps** | App settings, recent-files lists, caches, undo history — embedded SQLite with zero setup. |
| **ULTRA Store** | Package catalog, install state, update metadata. |
| **UltraAI** | Embedding / vector stores (SQLite-vec locally, pgvector on Postgres) and prompt/response history. |
| **FileLoader** | Optional metadata/thumbnail cache. |

UltraDatabase depends on **UltraVault** for credential lookup and,
optionally, on **UltraNet** to carry a driver's traffic over a managed
TLS transport. It has no dependency on UltraCanvas (UI) — it is a
headless data module usable from any ULTRA OS process.

---

## Conventions

- **Naming:** `UltraDb_<Action><Target>()` (e.g. `UltraDb_Query`,
  `UltraDb_RegisterConnection`, `UltraDb_ExecPrepared`). Types use
  `UltraDb<Type>` (`UltraDbResult`, `UltraDbValue`, `UltraDbHandle`).
  Driver plugins use `IDatabaseDriverPlugin`. Callbacks use `on<Event>`.
- **Errors:** every blocking call returns `UltraDbResult`. Operator
  `bool` for quick success checks; structured fields (code, message,
  driver, SQLSTATE, affected rows, timing) for diagnostics.
- **Handles:** zero (`0`) is invalid. Prepared statements,
  transactions and cursors are handles; always finalize/close them.
- **Security defaults:** parameter binding always; TLS verification
  ON for networked engines; credentials from UltraVault.
- **Threading:** async callbacks run on the UltraDatabase worker
  pool — marshal to your own loop and do not block.
- **Reserved:** never write `sqlite3_*`, `PQexec`, `mysql_query`, a raw
  `Connection` class, or string-built SQL at app level — always go
  through the `UltraDb_*` API so the engine stays swappable and queries
  stay parameterized.

---

## Status

| Component | State |
|---|---|
| Public API (Stage 1) | Implemented |
| SQLite core (system libsqlite3) | Implemented |
| Connection registry (lazy open) | Implemented |
| Query / Exec / Prepare / values | Implemented |
| Transactions + versioned migrations | Implemented |
| Schema introspection, identifier quoting, result-column metadata | Implemented (SQLite, PostgreSQL, MySQL / MariaDB) |
| Streaming cursors, per-statement scripts, cancelling | Implemented (SQLite, PostgreSQL, MySQL / MariaDB) |
| Server administration (`UltraDatabaseServer.h`) | Implemented (PostgreSQL, MySQL / MariaDB) |
| Statement observers (`UltraDb_AddStatementObserver`) | Implemented |
| Test suite (84 tests) | Passing |
| Connection pool | Planned (Stage 2) |
| PostgreSQL driver (libpq) | Implemented — built when libpq is found; TLS `verify-full` by default; password from UltraVault |
| MySQL / MariaDB driver (libmariadb) | Implemented — built when libmariadb is found; server-side prepared statements; TLS verified by default; password from UltraVault; `LOAD DATA LOCAL` off |
| Async queries | Planned (Stage 2) |
| Other drivers (MSSQL, Redis, Mongo, DuckDB) | Tracked separately (Stage 3) |

**Suggested rollout** (mirrors UltraNet's staged approach):
1. **Stage 1** — SQLite core, connection registry, `Query`/`Exec`/
   `Prepare`/values/transactions/migrations. Enough for every app's
   local storage (and all of UltraMail).
2. **Stage 2** — async queries + worker pool, the driver-plugin manager.
   (The PostgreSQL and MySQL / MariaDB drivers, with their UltraVault and
   TLS wiring, and streaming cursors are done.)
3. **Stage 3** — remaining drivers (MSSQL, Redis, MongoDB, DuckDB),
   at-rest encryption, read-replica/failover options.

---

## Reference

- **UltraNet** — sibling module and the template for this API shape:
  `Docs/Modules/UltraNet/README.md`.
- **Module registry** — `Masterfile_modules.md` (UltraDatabase entry).
- **UltraMail** — first application consumer:
  `Docs/UltraMail/Concept.md`.
- **Credential storage** — UltraVault (`UltraAI/Docs/UltraVault.md`).

---

*Part of ULTRA OS · MIT license · ULTRA OS Development GmbH*
