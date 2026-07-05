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

> Status: **Stage 1 implemented.** The SQLite core, connection registry,
> parameterized queries, prepared statements, transactions and versioned
> migrations are built and tested — `UltraCanvas/{include,core}/UltraDatabase/`,
> library target `UltraDatabase` (`libultradatabase.a`), test suite
> `Tests/UltraDatabase` (`ULTRACANVAS_BUILD_DATABASE_TESTS=ON`, 14 tests).
> The networked drivers (PostgreSQL, MySQL, …), async queries, pooling and
> streaming cursors described below are the Stage 2/3 plan.

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
| PostgreSQL | Relational (SQL) | `libpq` |
| MySQL / MariaDB | Relational (SQL) | `libmariadb` |
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
- Sync **and** async variants for queries; cancellation via
  `UltraDb_CancelQuery(handle)`.
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

// Async query — result delivered on a worker thread, marshal to UI.
UltraDbHandle h = UltraDb_QueryAsync("crm",
    "SELECT * FROM contacts WHERE name ILIKE ?", { "%" + term + "%" },
    [](const UltraDbResultSet& rs) {
        // runs on the UltraDatabase worker pool — post to UI thread
    });

// Streaming cursor for large result sets (rows pulled on demand).
UltraDbHandle cur = UltraDb_OpenCursor("crm", "SELECT * FROM events", {});
UltraDbRow row;
while (UltraDb_FetchRow(cur, row)) { /* ... */ }
UltraDb_CloseCursor(cur);
```

**Values.** `UltraDbValue` is a small variant: null, bool, int64,
double, text (UTF-8), blob (bytes), and timestamp. Typed accessors
(`AsU32`, `AsString`, `AsBlob`, `IsNull`, …) with defined coercion
rules. Column access by name or index.

**Schema & migrations.** `UltraDb_Migrate(name, steps)` runs an ordered,
versioned list of migration steps inside a transaction and records the
applied version in a `ultradb_schema_version` table, so an app brings
its own schema up to date on launch regardless of engine.

---

## Threading

Database calls are blocking I/O, so UltraDatabase owns a **worker
thread pool** (sized per connection pool). The synchronous `UltraDb_*`
calls block the *calling* thread; the `*Async` variants run on the pool
and deliver results via a callback. As in UltraNet, **async callbacks
run on a UltraDatabase worker thread — callers must marshal to their
own loop** (`UltraCanvasApplication::PostToUIThread(fn)`) and must not
block. The UI never waits on the database.

---

## Security

- **Injection-proof by default.** SQL text and values travel
  separately; there is no "format a query string" entry point in the
  public API.
- **Credentials via UltraVault.** `credentials` on a connection is a
  vault key (`vault:...`); the module resolves it at connect time.
  Config files and `connections.json` never contain passwords — the
  same rule UltraNet follows.
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
│   └── UltraDatabasePlugins.h        IDatabaseDriverPlugin
├── core/UltraDatabase/               (.cpp implementations + bundled SQLite)
├── OS/<Platform>/UltraDatabaseSupport.cpp
└── Plugins/UltraDatabase/
    ├── postgresql/
    ├── mysql/
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
| Test suite (14 tests) | Passing |
| Connection pool | Planned (Stage 2) |
| PostgreSQL / MySQL drivers | Planned (Stage 2, Tier 2 plugins) |
| Async + streaming cursors | Planned (Stage 2) |
| Other drivers (MSSQL, Redis, Mongo, DuckDB) | Tracked separately (Stage 3) |

**Suggested rollout** (mirrors UltraNet's staged approach):
1. **Stage 1** — SQLite core, connection registry, `Query`/`Exec`/
   `Prepare`/values/transactions/migrations. Enough for every app's
   local storage (and all of UltraMail).
2. **Stage 2** — async queries + worker pool, streaming cursors, the
   driver-plugin manager, PostgreSQL and MySQL/MariaDB drivers,
   UltraVault + TLS wiring.
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

*Part of ULTRA OS · MIT license · Cloverleaf UG*
