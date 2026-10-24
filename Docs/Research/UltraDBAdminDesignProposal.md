# UltraDBAdmin — a phpMyAdmin-style manager for UltraDatabase (Investigation and Proposal)

**Date:** 2026-10-20
**Status:** Proposal — investigation only, no implementation yet. Decisions
2026-10-20: see §9.
**Scope:** a new application, `Apps/UltraDBAdmin`, that does for
every database UltraDatabase can reach what phpMyAdmin does for MySQL: browse
and edit rows, see and change table structure, run SQL, search, import and
export, look at relations, and administer the server. Also: what the
UltraDatabase module and the framework have to gain first, because most of
what such a tool needs is missing from the module today, and it belongs in
the module rather than in the application.

This document follows the shape of the other investigations in this directory
([`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md),
[`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md)). Everything said
about the repository was checked against the tree on this date; file
references are to the current `main`. What is said about phpMyAdmin and the
other tools is from their published behaviour as the author knows it; a
detail that could not be checked from this environment is marked
**[unverified]**.

---

## 1. Recommendation

**Build a native desktop application, not a web one.** phpMyAdmin is a web
application because in 1998 the browser was the only cross-platform UI a
shared host offered. UltraCanvas already gives one on every platform, and a
native client avoids what has made phpMyAdmin the most attacked admin page on
the internet: a login form and a SQL console on a public URL. Remote servers
are reached the way DBeaver, pgAdmin 4 desktop and TablePlus reach them —
directly, over TLS, or through an SSH tunnel. Where a browser is all there is, the
same application runs compiled to WebAssembly against an authenticated relay
on the server (§8.1) — still not a page that turns form fields into SQL.

**Put the database knowledge in UltraDatabase, the windows in the app.** The
application needs to list a database's tables, columns, keys, indexes, views,
routines and users; to page through a table of ten million rows without
loading it; to cancel a runaway query; and to quote a table name safely when
it builds `ALTER TABLE`. None of that exists in the module yet (§4), and every
one of those is needed by someone else too: the ER diagram's planned "build
from a live database" (`UltraCanvasERDiagramProposal.md`, X4), the migration
runner, UltraFIBU's support tooling. So stage 0 of this project is a set of
module additions — a schema-introspection API, column metadata, paged
cursors, cancellation, an identifier quoter — each tested in
`Tests/UltraDatabase` against SQLite and the PostgreSQL CI server, before a
single window is drawn.

**Engines: SQLite and PostgreSQL first, MySQL/MariaDB as soon as its driver
exists.** Those two are the drivers UltraDatabase has today
(`UltraDatabaseSqliteDriver.cpp`, `UltraDatabasePostgresDriver.cpp`). A tool
that is to be "like phpMyAdmin" is expected to open a MySQL server, so the
MySQL/MariaDB driver — already the module's Stage 2 plan — is on this
project's critical path, not a nice-to-have. Every feature is written against
the engine-neutral API; what is genuinely engine-specific (users and grants,
server variables, process list) goes through a per-driver admin interface, so
an engine that lacks one simply hides the tab.

**Safe by default.** Credentials live in UltraVault, never in the connection
file. TLS is `VerifyFull` unless the user lowers it per connection, with a
visible warning. A destructive statement (`DROP`, `TRUNCATE`, `DELETE` or
`UPDATE` without `WHERE`) asks first, naming the object and the row count.
Every change the GUI makes is shown as the SQL it will run before it runs —
phpMyAdmin's best habit, kept. A connection can be marked read-only (the
module already enforces this: `UltraDbConnectionConfig::readOnly`, and the
PostgreSQL driver sets `default_transaction_read_only`), and a "production"
flag colours its tabs red.

---

## 2. What was asked, restated

"An app to manage the UltraDatabase with similar functionality to
phpMyAdmin." Read as:

- **Manage** — not only look: create and alter tables, edit rows, import and
  export data, manage users, see what the server is doing.
- **The UltraDatabase** — the module is a client library over several
  engines, not a database server of its own. So the app manages *the
  databases UltraDatabase connects to*: local SQLite files (including every
  ULTRA OS app's own store — UltraMail's, UltraFIBU's, UltraSocial's) and
  networked PostgreSQL / MySQL servers.
- **Similar to phpMyAdmin** — its feature set and its workflow (left tree,
  per-table tabs, "show me the SQL"), not its delivery as a PHP web app.

A useful side effect: ULTRA OS apps already keep their data in SQLite through
UltraDatabase (`Apps/UltraMail/engine/UltraMailLocalStore.cpp`,
`Apps/UltraFIBU/engine/UltraFIBUStore.cpp`,
`Apps/UltraSocial/engine/UltraSocialStore.cpp`,
`Apps/EmailCleaner/engine/EmailCleanerStore.cpp`). A built-in admin tool is
also the support tool for those apps: open the store read-only, see the
migration version, look at a row.

---

## 3. Survey — what the established tools do

### 3.1 phpMyAdmin's feature set

Grouped as phpMyAdmin groups them (server → database → table):

| Level | Feature | Notes |
|---|---|---|
| Server | Databases list, create/drop database | with collation |
| | SQL console | multi-statement, history, bookmarks |
| | Status / monitor | live charts of queries, connections, traffic; process list with kill |
| | Variables, character sets, engines, plugins | read; variables editable |
| | User accounts & privileges | global / database / table / column grants |
| | Replication | primary / replica status and setup |
| | Export / import whole server | |
| Database | Structure: table list with rows, size, engine, collation, overhead | bulk actions: empty, drop, check, optimise, repair, analyse |
| | Search across all tables | words / phrase / regex, per table |
| | Query by example | builder from table/column pickers |
| | Routines, events, triggers | create / edit / drop, with a SQL-body editor |
| | Designer | drag-and-drop ER view, define foreign keys by drawing |
| | Operations | rename, copy, change collation |
| | Export / import | SQL, CSV, JSON, XML, ODS, XLSX, LaTeX, PDF …; compression |
| Table | Browse | paged grid, sort by header, inline edit, edit/copy/delete row, grid editing, BLOB download, show binary as hex |
| | Structure | columns (type, length, default, null, collation, attributes, A_I, comment), indexes, partitions, "propose table structure" |
| | SQL tab pre-filled | `SELECT * FROM t WHERE 1` |
| | Search | per-column operators, "zoom search" scatter chart |
| | Insert | form per column with type-aware inputs and functions (`NOW()`, `UUID()`) |
| | Relations | foreign keys with ON DELETE / ON UPDATE, display column |
| | Tracking | DDL/DML change log per table [unverified detail] |
| | Triggers | per table |
| | Operations | rename, move, copy, change engine, alter order, maintenance |
| | Charts | quick chart from a query result |
| All | "Show the SQL" | every GUI action prints the statement it ran |

### 3.2 The others, and what each does better

| Tool | Platform | What to borrow |
|---|---|---|
| **Adminer** | one PHP file | many engines through one UI — the engine-neutral model this app has by construction |
| **DB Browser for SQLite** | native (Qt) | "Write changes / Revert changes": edits are staged in a transaction until the user commits — the right default for a local file |
| **pgAdmin 4** | web + desktop shell | schema tree that knows schemas, sequences, extensions; EXPLAIN as a graph |
| **DBeaver** | native (Java) | connection folders, SSH tunnels per connection, result-set filters, data compare, ER from live DB |
| **TablePlus** | native | staged edits shown as pending SQL, coloured "production" connections, very fast grid |
| **DataGrip** | native (JetBrains) | schema-aware autocompletion in the console |

The design below takes phpMyAdmin's feature list and these tools' habits —
staged edits, coloured production connections, SSH tunnels, schema-aware
completion — which are the parts phpMyAdmin is weakest at.

---

## 4. What the tree has, and what it lacks

### 4.1 UltraDatabase today (verified)

| Capability | State | Where |
|---|---|---|
| Named connection registry | yes; register, open, close, info by name | `include/UltraDatabase/UltraDatabaseConnection.h` |
| Enumerate registered connections | **no** — `UltraDb_GetConnectionInfo` needs the name | same |
| Drivers | `sqlite` (bundled), `postgresql` (when libpq is found) | `core/UltraDatabase/UltraDatabase{Sqlite,Postgres}Driver.cpp` |
| MySQL / MariaDB | **no** — Stage 2 plan (since added: UltraDatabase 0.5.0, stage 3 below) | `Docs/Modules/UltraDatabase/README.md`, `master_dependencies.yaml` |
| Parameterised query, exec, prepared statements | yes | `UltraDatabaseQuery.h` |
| Transactions | yes, handle-based | `UltraDatabaseTransaction.h` |
| Versioned migrations | yes | `UltraDatabaseMigrate.h` |
| Value model | `Null, Bool, Int, Double, Text, Blob` | `UltraDatabaseValue.h` |
| Column metadata in a result | **names only** — no type, nullability, source table/column or key | `UltraDbResultSet::Columns()` |
| Schema introspection | **none** | — |
| Cursors / streaming / paging | **no** — every result is fully materialised in memory | `UltraDbResultSet` |
| Async query, cancel | **no** — documented as Stage 2 | README |
| Multi-statement batch | runs, but keeps **only the last** result (SQLite keeps the last row-returning statement's rows; PostgreSQL's `PQexec` returns only the last result) | `ExecuteDirect` in both drivers |
| Read-only connections | yes; PostgreSQL sets `default_transaction_read_only` | `UltraDatabasePostgresDriver.cpp:519` |
| Credentials from UltraVault | yes (`credentials = "vault:..."`) | PostgreSQL driver, `include/UltraVault/UltraVault.h` |
| TLS posture | `Disable / Prefer / Require / VerifyFull` (default `VerifyFull`) | `UltraDatabaseCore.h` |
| Identifier quoting | **none** public | — |

### 4.2 Gaps that block an admin tool, and where each fix belongs

Every one of these is a module change, not an app change, and each has a
second customer besides this app.

- **G1 — Schema introspection.** List schemas/databases, tables, views,
  columns (type as the engine spells it, nullability, default, collation,
  auto-increment, comment), primary keys, unique and foreign keys (with
  `ON DELETE` / `ON UPDATE`), indexes, triggers, routines, sequences, and
  approximate row count and size. One engine-neutral model, filled per driver
  from `sqlite_schema` + `PRAGMA table_info / foreign_key_list / index_list`,
  `information_schema` + `pg_catalog`, and (later) MySQL's
  `information_schema`. Second customers: `UltraCanvasERDiagram`'s planned
  live import (X4 in its proposal) and a migration diff tool. Sketch in §6.1.
- **G2 — Column metadata on results.** For an editable grid the app must know,
  for each result column, the declared type, whether it is nullable, and the
  base table and column it came from (`PQftable` / `PQftablecol`;
  `sqlite3_column_table_name` / `sqlite3_column_origin_name`, which need
  `SQLITE_ENABLE_COLUMN_METADATA` in the bundled amalgamation's build). Only
  then can it say "this grid is editable, keyed by `id`" or "this is a join,
  read-only". Add `UltraDbColumnInfo` alongside the existing column names, so
  no caller breaks.
- **G3 — Paged / streaming results.** Opening a 10-million-row table must not
  read 10 million rows. Two mechanisms, both needed: a streaming cursor (the
  README's planned `UltraDb_OpenCursor` / `UltraDb_FetchRow`, over
  `sqlite3_step` and libpq single-row mode or a server-side `DECLARE CURSOR`)
  for export and for the SQL console; and keyset paging (`WHERE pk > ? ORDER
  BY pk LIMIT ?`) for the Browse grid, which the app can build once G1 tells it
  the key and G5 lets it quote the names.
- **G4 — Cancel and run off the UI thread.** A console without a Stop button
  is not usable. `UltraDb_CancelQuery(handle)` (README, Stage 2) maps to
  `sqlite3_interrupt` and `PQcancel`. Until async lands the app can run each
  query on its own worker thread — the module's connections already lock
  internally — but the cancel entry point is a module change.
- **G5 — Identifier quoting.** The module's rule is "SQL uses parameter binding
  only", and it is right; but a table or column *name* cannot be bound. An
  admin tool must generate `ALTER TABLE "Order Items" ADD COLUMN ...`, and the
  quoting differs per engine (`"x"` with `""` doubling for SQLite and
  PostgreSQL, `` `x` `` for MySQL). Add
  `UltraDb_QuoteIdentifier(connection, name)` on `IUltraDbConnection`, with a
  default for the SQL standard and a MySQL override, and make it the only
  sanctioned way to put a name into SQL text. Without it the app would grow
  three hand-written quoters, which is how injection holes start.
- **G6 — Every result of a batch.** The console runs what the user pastes,
  often several statements. It needs each statement's result in turn (rows or
  affected count, and which statement failed), not just the last one. A
  `UltraDb_ExecBatch(connection, sql, callback-per-statement)` over
  `sqlite3_prepare_v2`'s tail loop (already in the SQLite driver) and
  `PQsendQuery` + `PQgetResult` for PostgreSQL.
- **G7 — Exact numbers.** The PostgreSQL driver turns `numeric` into a
  `double` (`WertAusFeld`, `UltraDatabasePostgresDriver.cpp:194`), so a
  `NUMERIC(20,4)` balance of `12345678901234.5678` shows rounded — in a tool
  whose job is to show what is stored, that is wrong. Either keep `numeric` as
  `Text` or add a decimal tag to `UltraDbType`; UltraFIBU (money) has the same
  interest. The same line parses with `std::strtod`, which follows
  `LC_NUMERIC`; it is already on `scripts/locale_numbers_baseline.txt`
  (lines 45–46), and in a GUI process — where the Linux backend calls
  `setlocale(LC_ALL, "")` — a comma-decimal desktop reads `3.14` as `3`. This
  app would be the first GUI program to show raw PostgreSQL floats, so the fix
  (`ParseFloatClassic`) has to land first.
- **G8 — MySQL / MariaDB driver.** Over MariaDB Connector/C (`libmariadb`,
  LGPL-2.1-or-later — accepted 2026-10-20; what that obliges is §9.1, and it
  is recorded in `Docs/Dependencies.md`, `master_dependencies.yaml` and
  `THIRD_PARTY_LICENSES.md` when the driver lands), with `?`
  placeholders natively, `ssl-mode=VERIFY_IDENTITY` by default and the vault
  for the password.
- **G9 — SSH tunnel.** UltraNet's SSH plugin
  (`Plugins/UltraNet/ssh/SshPlugin.cpp`) implements `IRemoteAccessPlugin` —
  `OpenShell` and `ExecuteCommand` — but no port forwarding. A database behind
  a bastion is the normal case for production. Add a local-forward call to
  the plugin (`libssh2_channel_direct_tcpip`), and an
  `options["ssh"] = "vault:..."` convention on the connection config that the
  driver manager resolves before the driver opens the socket.
- **G10 — Server administration.** Users, roles and grants; server variables;
  the process list with kill; server status counters. These are irreducibly
  engine-specific, so rather than an engine-neutral API, add an optional
  `IUltraDbAdminExtension` that a driver may return. SQLite returns none (and
  the app instead shows `PRAGMA`s: page size, journal mode, integrity check,
  `VACUUM`). PostgreSQL implements it over `pg_roles`, `pg_stat_activity`,
  `pg_settings`, `pg_cancel_backend` / `pg_terminate_backend`.
- **G11 — List connections.** `UltraDb_ListConnections()` — trivial, but the
  left tree needs it.

### 4.3 The framework's UI elements (verified against
`Docs/UltraCanvas/UltraCanvasUIElements.md`)

| Need | Element | State |
|---|---|---|
| Server → database → table tree | `UltraCanvasTreeView` | exists |
| Per-object tabs (Browse, Structure, SQL …) | `UltraCanvasTabbedContainer` | exists |
| Data grid over a huge table | `UltraCanvasListView` with an `IListModel` (virtualised, model-driven, resizable columns, sort indicator) | exists; **read-only** — no in-place cell editor |
| Editable grid for a small result | `UltraCanvasSpreadsheet` (in-place cell editing) | exists; holds all cells in memory, so not for big tables |
| Structure editor (columns × attributes) | `UltraCanvasSpreadsheet` or a ListView + row form | exists |
| SQL editor | `UltraCanvasTextArea` with `SetProgrammingLanguage("SQL")` (rules in `UltraCanvasSyntaxTokenizer.h`, `CreateSqlRules`) and `ShowAutoComplete(suggestions)` | exists |
| Insert / edit row form | `CreateFormGrid` / `AddFormRow` (`UltraCanvasFormLayout.h`), date and number pickers | exists |
| Relations / designer | `UltraCanvasERDiagram` (`Plugins/Diagrams/UltraCanvasERDiagram.h`: `AddEntity`, `AddAttribute`, `AddRelationship`, layouts, Crow's Foot) | exists; **no** import from a database yet |
| Server monitor charts | `UltraCanvasLineChartElement` and the chart plugins | exists |
| CSV import / export dialogs | `UltraCanvasCSVImportDialog`, `UltraCanvasCSVExportDialog` | exist, but bound to an `UltraCanvasSpreadsheet` source |
| BLOB viewer | `UltraCanvasMediaViewer` (image, PDF, text …); `UltraCanvasTextArea` in `TextAreaEditingMode::Hex` | both exist |
| Open / save `.sql`, `.csv`, `.xlsx`, `.json` | `UltraCanvasFileLoader` — dialogs, readers/writers, format inventory | dialogs and text formats exist and are required by the house rules; XLSX / ODS are written today only by `UltraCanvasSpreadsheet::SaveToFile`, so a table export to them needs F2 |

Two framework additions follow, both worth having outside this app:

- **F1 — Editing in `UltraCanvasListView`.** An editor delegate per column
  (text, number, date, dropdown, checkbox), committed through
  `IListModel::SetData`, which the model already declares. The Browse grid
  needs it for tables too big for the spreadsheet. Today the house rules would
  otherwise push the app towards a hand-painted grid, which they forbid.
- **F2 — Tabular import/export over a model, not a spreadsheet.** Let the two
  CSV dialogs, and FileLoader's spreadsheet writers (XLSX, ODS), take a row
  source / sink interface, so that a 2-GB export streams from a cursor (G3)
  rather than filling a spreadsheet first. Added to FileLoader, per the house
  rule that what FileLoader cannot yet do is added there.

---

## 5. The application

### 5.1 Main window

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Menu / toolbar:  New connection · Refresh · SQL · Commit · Revert · Stop │
├───────────────────────┬──────────────────────────────────────────────────┤
│ Connections (tree)    │ [orders ×] [SQL 1 ×] [customers ×]   ← doc tabs  │
│ ▾ ● Local apps        │ ┌ Browse │ Structure │ SQL │ Search │ Insert │   │
│   ▾ UltraMail (ro)    │ │ Export │ Import │ Relations │ Triggers │ Ops ┐ │
│     ▸ Tables (12)     │ │                                              │ │
│ ▾ ● fibu-prod  🔴     │ │   grid / form / editor for the chosen tab    │ │
│   ▾ public            │ │                                              │ │
│     ▾ Tables          │ │                                              │ │
│        orders         │ └──────────────────────────────────────────────┘ │
│        customers      │ Pending changes (3) — UPDATE orders SET … [Show] │
│     ▸ Views           ├──────────────────────────────────────────────────┤
│     ▸ Functions       │ SQL log:  SELECT … 12 ms · 50 rows               │
│     ▸ Sequences       │                                                  │
└───────────────────────┴──────────────────────────────────────────────────┘
```

- **Left:** `UltraCanvasTreeView` — connection folders → connection →
  database/schema → object kinds → objects. Context menus carry the object's
  actions (open, structure, drop, empty, export, copy name).
- **Right:** a `UltraCanvasTabbedContainer` of open objects, each with its own
  inner tab row for the views below, the way phpMyAdmin's table pages work.
- **Bottom:** the SQL log — every statement the app ran, with time and row
  count, copyable. This is the "show me the SQL" promise, kept for every
  action, not just some.
- **Pending changes bar:** edits are staged (§5.3); the bar shows how many and
  opens them as SQL.

### 5.2 The views

| View | phpMyAdmin equivalent | Built from |
|---|---|---|
| **Browse** | Browse | ListView + paged model (G3), sort by header (`ORDER BY`, server-side), quick filter row per column (bound parameters), NULL shown distinctly, BLOB cell opens the media viewer or hex; editing via F1 or a row form; "copy as INSERT / CSV / JSON" |
| **Structure** | Structure | columns grid (name, type, length, null, default, auto-inc, collation, comment) + indexes + foreign keys; editing produces `ALTER TABLE` through the engine's DDL generator, previewed. SQLite's limited `ALTER` is handled by the documented 12-step rebuild (create new, copy, drop, rename) inside one transaction, shown as SQL |
| **SQL** | SQL | TextArea (SQL mode), schema-aware completion from G1, run all / run selection / run statement under caret, one result tab per statement (G6), EXPLAIN, Stop (G4), history and saved snippets |
| **Search** | Search / database search | per-column operators for one table; "find this text in every table" across a database — built as parameterised `LIKE` per text column, cancellable |
| **Insert** | Insert | form per column from G1: type-aware input, NULL checkbox, "use default", value functions offered per engine (`CURRENT_TIMESTAMP`, `gen_random_uuid()`) |
| **Export** | Export | table / database / query result → SQL dump, CSV, JSON, XLSX, ODS, through FileLoader; streaming (G3, F2); options: structure only, data only, `DROP IF EXISTS`, batch size |
| **Import** | Import | `.sql` script (runs through G6 with progress and stop-on-error), CSV / XLSX into a table with column mapping and a preview |
| **Relations** | Relations / Designer | `UltraCanvasERDiagram` filled from G1 for a schema or a table's neighbourhood; later, drawing a link proposes the `ALTER TABLE … ADD FOREIGN KEY` |
| **Triggers / Routines** | Triggers, Routines, Events | list + SQL-body editor; create/replace previewed |
| **Operations** | Operations | rename, copy (structure / structure + data), empty, drop; SQLite: `VACUUM`, `ANALYZE`, `PRAGMA integrity_check`, journal mode |
| **Server** | Status, Variables, Processes, User accounts | through G10; hidden when the driver has no admin extension |
| **Chart** | Display chart | a query result's numeric columns into a line / bar chart |

### 5.3 Editing model: staged, transactional, previewed

phpMyAdmin commits each cell edit immediately. DB Browser for SQLite and
TablePlus stage edits and commit on request, which is far safer. Proposal:

- Default **staged**: grid and form edits become pending statements, shown in
  the pending bar; **Commit** runs them in one transaction
  (`UltraDb_Begin` / `UltraDb_ExecInTx` / `UltraDb_Commit`), **Revert** drops
  them. A per-connection switch offers auto-commit for those who want
  phpMyAdmin's behaviour.
- Every generated statement uses bound values (`UPDATE "t" SET "c" = ? WHERE
  "id" = ?`) and quoted identifiers (G5). Rows are addressed by primary key;
  a table without one is read-only in the grid (as in DB Browser), with an
  explanation — no `WHERE` over all columns guessing at a unique row.
- **Destructive guard:** before a statement the console classifies as
  destructive runs on a non-local connection, a dialog names it and, where
  cheap, the rows it touches (`SELECT count(*)` with the same `WHERE`).

### 5.4 Connections

- A connection is an `UltraDbConnectionConfig` plus app-only fields: folder,
  colour, "production" flag, SSH tunnel, auto-commit, row-page size. Stored in
  the app's own settings file (an internal file, so read and written directly,
  per *Core conventions*); the password is only ever a `vault:` key
  (`UltraVault::Put` / `Get`). Paths in it go through `PathToUtf8` /
  `PathFromUtf8`.
- **"Local apps" folder, discovered:** the stores of installed ULTRA OS apps
  (UltraMail, UltraFIBU, UltraSocial, EmailCleaner …), opened **read-only by
  default**, because writing to a live app's store behind its back is how
  support sessions corrupt data. Discovery reads each app's documented data
  directory; it does not need the apps' code. **Writing is allowed only after
  the owning app is confirmed closed and a backup copy has been taken**
  (decided 2026-10-20): "Unlock for writing" checks that no process of that
  app is running and that the file is not locked (SQLite's own lock, tried
  with `BEGIN IMMEDIATE`), copies it with SQLite's online-backup API to
  `<store>.ultradbadmin-<timestamp>.bak` beside it, and only then reopens the
  connection read-write. The lock is held for the whole editing session, so
  the app cannot start writing underneath; closing the tab releases it and
  offers "Restore backup".
- Open a SQLite file from the file manager or Open dialog (FileLoader's
  dialog; `.db`, `.sqlite`, `.sqlite3`).
- Test connection, with the precise error (`UltraDbResult::message`,
  `sqlState`), and certificate details when TLS fails.

### 5.5 Application layout in the tree

Following `Apps/UltraFIBU` (engine / ui / cli split):

```
Apps/UltraDBAdmin/
├── CMakeLists.txt               option ULTRACANVAS_BUILD_DBADMIN
├── README.md
├── UltraDBAdmin.desktop
├── main.cpp
├── engine/                      no UI; unit-tested headless
│   ├── DbAdminConnections.{h,cpp}     connection list, folders, vault keys
│   ├── DbAdminTableModel.{h,cpp}      IListModel over keyset paging
│   ├── DbAdminDdl.{h,cpp}             DDL generation per engine (uses G1, G5)
│   ├── DbAdminStagedEdits.{h,cpp}     pending statements, commit / revert
│   ├── DbAdminSqlClassify.{h,cpp}     destructive-statement detection
│   ├── DbAdminExport.{h,cpp}          dump / CSV / JSON over a cursor
│   └── DbAdminImport.{h,cpp}
├── ui/
│   ├── DbAdminWindow.{h,cpp}          tree + tabs + log
│   ├── DbAdminBrowseView.{h,cpp}
│   ├── DbAdminStructureView.{h,cpp}
│   ├── DbAdminSqlView.{h,cpp}
│   ├── DbAdminRelationsView.{h,cpp}
│   └── DbAdminServerView.{h,cpp}
└── Tests/                       or Tests/UltraDBAdmin, against SQLite + the PG CI server
```

Packaging follows *Packaging a new app* in `AGENTS.md` (macOS bundle, MSIX,
AppImage/desktop file).

---

## 6. Proposed module API (sketches, not yet written)

### 6.1 Schema introspection (G1)

```cpp
// include/UltraDatabase/UltraDatabaseSchema.h   (proposed)
struct UltraDbColumnSchema {
    std::string name;
    std::string declaredType;     // as the engine spells it: "varchar(80)", "INTEGER"
    UltraDbType valueType;        // how values come back
    bool        nullable = true;
    bool        autoIncrement = false;
    std::optional<std::string> defaultExpression;
    std::string collation, comment;
};
struct UltraDbForeignKeySchema {
    std::string name;
    std::vector<std::string> columns, refColumns;
    std::string refSchema, refTable, onDelete, onUpdate;
};
struct UltraDbIndexSchema { std::string name; std::vector<std::string> columns; bool unique, primary; };
struct UltraDbTableSchema {
    std::string schema, name;
    UltraDbObjectKind kind;       // Table, View, MaterializedView, ...
    std::vector<UltraDbColumnSchema> columns;
    std::vector<std::string> primaryKey;
    std::vector<UltraDbForeignKeySchema> foreignKeys;
    std::vector<UltraDbIndexSchema> indexes;
    int64_t approxRows = -1, sizeBytes = -1;
};

UltraDbResult UltraDb_ListSchemas(const std::string& connection, std::vector<std::string>& out);
UltraDbResult UltraDb_ListObjects(const std::string& connection, const std::string& schema,
                                  std::vector<UltraDbObjectSummary>& out);
UltraDbResult UltraDb_DescribeTable(const std::string& connection, const std::string& schema,
                                    const std::string& table, UltraDbTableSchema& out);
std::string   UltraDb_QuoteIdentifier(const std::string& connection, const std::string& name); // G5
```

Backed by an optional `IUltraDbConnection::Introspect()` returning an
`IUltraDbSchemaReader`, so a driver without one reports `Unsupported` and the
module's own core does not change per engine — the pattern
`BeginTransactionSql` and `RowLockSuffix` already follow.

### 6.2 Cursors, batches, cancel (G3, G4, G6)

As the module README already names them — `UltraDb_OpenCursor`,
`UltraDb_FetchRow`, `UltraDb_CloseCursor`, `UltraDb_CancelQuery` — plus
`UltraDb_ExecBatch(connection, sql, onStatementResult)`. This project would
pull those Stage 2 items forward rather than invent new ones.

---

## 7. Security

- No network listener in the desktop build. The app is a client; there is no
  web login to attack. The browser build's relay (§8.1) is the one listener,
  and it is designed around that.
- TLS `VerifyFull` by default; lowering it is per connection and shown as a
  warning badge on the connection.
- Passwords and SSH keys in UltraVault only; the settings file holds `vault:`
  keys. Export of connection settings never contains a secret.
- All values bound; all identifiers through G5. The SQL console necessarily
  runs what the user typed — that is its purpose — but nothing the GUI builds
  is string-concatenated.
- Read-only by default for discovered app stores and for any connection the
  user marks so; enforced by the driver, not just by greying out buttons.
- Query history is local and can be switched off per connection (statements
  can contain personal data); it is never sent anywhere.
- Destructive-statement confirmation (§5.3), stronger on "production"
  connections.

---

## 8. Roadmap

| Stage | Delivers | Depends on |
|---|---|---|
| **0 — module** | G1 introspection (SQLite, PostgreSQL), G2 column metadata, G5 quoting, G11 list connections, G7 numeric/locale fix; tests in `Tests/UltraDatabase` | — |
| **1 — read-only viewer** | app skeleton; connections with vault; tree; Browse (paged, sort, filter); Structure (read); SQL console with results, history, cancel; export CSV/JSON/SQL through FileLoader | 0, G3, G4, G6 |
| **2 — editing** | staged edits, insert form, row edit, delete; Structure editing → DDL (incl. SQLite rebuild); import SQL / CSV; destructive guard | 1, F1, F2 |
| **3 — MySQL / MariaDB** | the driver (G8) with introspection; the app needs no change beyond engine-specific DDL spellings | 0, dependency approval |
| **4 — relations & server** | ER view from the schema (fills the ER diagram's X4), routines/triggers, server tab through G10 for PostgreSQL then MySQL, SSH tunnel (G9), monitor charts | 2, G9, G10 |
| **5 — comfort** | schema-aware completion, EXPLAIN view, database-wide search, data compare between two connections, saved query bookmarks | 4 |
| **6 — browser build** | the same app compiled to WebAssembly, talking to the UltraDBAdmin relay on the server (§8.1) | 4 |

**Stage 0 is done** (UltraDatabase 0.2.0, 2026-10-20): G1 introspection
for SQLite and PostgreSQL (`UltraDatabaseSchema.h`: `UltraDb_ListSchemas`,
`UltraDb_ListObjects`, `UltraDb_DescribeTable`), G2 result-column metadata
(`UltraDbResultSet::ColumnInfo`), G5 quoting (`UltraDb_QuoteIdentifier`,
`UltraDb_QuoteQualifiedName`), G7 dot-decimal numbers and exact `numeric`,
G11 `UltraDb_ListConnections` - tested in `Tests/UltraDatabase` against
SQLite and a live PostgreSQL server.

**Stage 1 has begun with its module prerequisites** (UltraDatabase 0.3.0):
G3 `UltraDb_OpenCursor` / `UltraDb_FetchRows` / `UltraDb_CloseCursor`
(server-side cursors on PostgreSQL), G4 `UltraDb_CancelQuery(connection)`
(`sqlite3_interrupt`, `PQcancel`), G6 `UltraDb_ExecBatch` (every statement of
a script with its own result, split as psql splits on PostgreSQL). The
application followed (UltraDBAdmin 0.1, `Apps/UltraDBAdmin`): connections
with their vault, the tree, Browse, Structure and the SQL console with Stop.

**Stage 2 has begun with row editing** (UltraDBAdmin 0.2): Browse stages
edits, new rows and deletions by primary key, previews them as SQL and
commits them in one transaction (§5.3), with the destructive guard for
deletions and production connections. **Structure editing followed**:
columns added, changed, renamed and dropped, indexes
added and dropped and the table renamed, staged, previewed as DDL and
applied in one transaction - `ALTER TABLE` on PostgreSQL, and on SQLite its
own `ALTER TABLE` plus the documented 12-step rebuild, working from the
table's own `CREATE TABLE` so nothing introspection cannot see (CHECK, table
options) is lost. **Import followed**: rows from CSV files
and workbooks through FileLoader's spreadsheet formats and the framework's
Text Import dialog, mapped onto a table and inserted all or none; SQL files
(`.sql`, `.sql.gz`) summarised and run on the console as one transaction.
**The rest of stage 2 followed**: Browse filters per column (bound
values), editing a cell in place, blobs viewed (picture, text or hex), saved
and loaded, and rows copied as CSV, JSON or INSERT statements - stage 2 is
complete.
**Export, stage 1's last item, followed**: a table through a cursor, or a
query's rows, to CSV / TSV (the framework's Text Export dialog and writer),
JSON, SQL INSERTs (with a SQLite table's own CREATE TABLE) or a workbook.
**The rest of stages 1 and 2 followed** (UltraDBAdmin 0.3): a tab per open
object, each keeping its page, filters and staged changes; the SQL log of
every statement the app runs (UltraDatabase 0.4.0's statement observers,
`UltraDb_AddStatementObserver`); the tree's context menu with the
Operations of 5.2 - create, copy (structure, or structure and rows), rename,
empty and drop a table, SQLite's VACUUM, ANALYZE, integrity and quick check,
optimize and journal mode, PostgreSQL's VACUUM (ANALYZE) and ANALYZE - each
shown as SQL first; Search across every table of a schema; a schema exported
as one SQL file (structure, data, DROP IF EXISTS), a PostgreSQL table's
definition read from its catalog; the console's Run statement, Explain and
saved snippets; the row form's field per type and its value functions; and
auto-commit per connection.

**Stage 4 followed, before stage 3** (decided 2026-10-23): the Relations
tab - the ER diagram filled from the schema introspection
(`UltraCanvasERDiagram`, crow's foot or Chen; the tables in layers, parents
first; a table double-clicked opens) - the ER diagram's X4, the live
import; the Triggers / Routines tab - triggers on both engines,
PostgreSQL's functions, procedures and aggregates, each edited as its own
CREATE OR REPLACE in the SQL tab, dropped, or started from a template; and
the Server tab for PostgreSQL - sessions with Stop query and End session,
settings with a filter, the sizes of databases and tables - and SQLite's
file. Read straight from the catalogs (`pg_stat_activity`, `pg_settings`,
`pg_proc`, `pg_trigger`, `sqlite_master`, PRAGMAs) rather than through a
G10 module extension; the module gains one when MySQL needs the same views.
SSH tunnels (G9) and monitor charts are still to come.

**Stage 3 followed** (UltraDatabase 0.5.0): the MySQL / MariaDB driver
(G8) over MariaDB Connector/C, linked dynamically (LGPL-2.1), with its
introspection, cancel, cursors and a script splitter that reads `DELIMITER`
as the mysql client does; and G10 after all - the server administration
(`UltraDatabaseServer.h`: sessions, settings, sizes, a session stopped) in
the module, filled by the PostgreSQL and the MySQL driver alike, which the
Server tab now reads for both instead of its own catalog queries. The app
needed more than DDL spellings: backtick quoting everywhere it builds SQL
(the engine passed to every name it quotes), `LIKE ... ESCAPE '!'` and
`CAST AS CHAR` for the filters and Search, `MODIFY COLUMN` restating a
column (its `AUTO_INCREMENT`, collation and comment), and DDL run statement
by statement - MySQL commits around each, so no plan of Structure or the
operations can promise all-or-nothing there, and each says so. The dump is
mysqldump's shape; routines are edited as `DROP` and `CREATE` between
`DELIMITER` lines. MariaDB 10.11 locally and MySQL 8 in CI run the same
tests.

**SSH tunnels (G9) followed** (UltraDatabase 0.6.0): the UltraNet SSH
plug-in forwards a local port (`UltraNet_OpenSshTunnel`) and now checks
every server's host key - known_hosts or a fingerprint the user trusted - which
it did not before; UltraDatabase opens a connection's tunnel before its
driver connects, from the connection's `ssh_*` options as planned, through an
opener the application installs (`UltraDatabaseSshTunnel.h` over UltraNet), so
the module links no network library; and UltraDBAdmin's connection dialog has
the bastion, the sign-in (agent, password or key file, in the vault) and the
host key, trusted on first use as ssh does. Monitor charts are still to come.

Stage 1 alone already replaces the "install DB Browser for SQLite" step for
supporting ULTRA OS apps.

### 8.1 The browser build and its relay

Decided 2026-10-20: the browser-reachable form is **the same application
compiled to WebAssembly**, not a separate web UI. A page in a browser cannot
open a TCP connection to a database, and must not hold its password, so the
drivers do not run in the browser at all:

```
Browser ── UltraDBAdmin.wasm ──WSS──► UltraDBAdmin relay (on the server) ──► UltraDatabase ──► SQLite / PostgreSQL / MySQL
```

- **One seam in the app.** Every database call the app makes goes through an
  `IDbAdminBackend` in `engine/`: in the desktop build it calls `UltraDb_*`
  directly; in the WASM build a `RelayBackend` sends the same requests
  (introspect, query page, exec batch, cancel, begin / commit) over a
  WebSocket (`UltraNetWebSocket.h`). The UI is identical in both. This is
  why the seam has to exist from stage 1, even though the relay comes last.
- **The relay** is a small headless program, `Apps/UltraDBAdmin/relay`,
  linking UltraDatabase and nothing from the UI. It listens on TLS only;
  authenticates with the ULTRA account (the UltraNet OAuth2 sign-in service
  the other apps use), never a form of its own; and exposes **only the
  connections configured on the server**, by name - the browser cannot name
  a host, so the relay is not an open proxy into the network. Credentials
  stay in the server's vault; the browser never sees a password.
- **Permissions per user and connection** (read-only / read-write / admin),
  enforced in the relay; read-only maps to the driver's read-only mode, not
  to hidden buttons.
- **Limits:** a statement time-out, a row cap per page, a rate limit, and an
  audit log of every statement with the user who ran it.
- **What the browser build cannot do:** open a local SQLite file from the
  user's disk as a live connection (upload a copy instead), and SSH tunnels
  (the relay is already on the inside).

This is the only listener in the design, and the reason it is not a
phpMyAdmin-style page: it carries no SQL from a form into a string, has no
login page of its own, and reaches only an allow-list.

---

## 9. Decisions (2026-10-20)

| Question | Decision |
|---|---|
| Name | **`UltraDBAdmin`** (`Apps/UltraDBAdmin`) |
| Browser-reachable mode | **Yes**: the same app compiled to WebAssembly, talking to an authenticated relay on the server - stage 6, §8.1 |
| MySQL / MariaDB priority | **Not a priority**: stays stage 3, after editing |
| libmariadb's LGPL-2.1 | **Accepted** - obligations below |
| Writing to app stores | **Only after the owning app is confirmed closed, with a backup copy taken first** - §5.4 |

### 9.1 What LGPL-2.1 obliges

LGPL is not new to the project: Cairo, Pango, GTK, GLib, FFmpeg, libvips,
libsndfile, OpenZWave and others are already LGPL-2.1 dependencies
(`Docs/Dependencies.md`), and are shipped the same way libmariadb would be.
UltraCanvas's own code stays MIT; the LGPL covers only the library.

What a distributor of an UltraDBAdmin binary that links libmariadb must do:

1. **Link it dynamically**, as a separate `.so` / `.dylib` / `.dll` the user
   could replace with a modified build of the same interface (§6(b)). The
   suite already does: the macOS triplets build everything shared
   (`VCPKG_LIBRARY_LINKAGE dynamic`, `MacOS/deps/triplets/`), Linux uses the
   system package, Windows MSYS2's DLLs. A **static** link would instead
   oblige shipping the app's object files so the user can relink against a
   changed libmariadb (§6(a)) - avoid it; CMake should ask for the shared
   library by name, as the OpenZWave link does (`THIRD_PARTY_LICENSES.md`).
2. **Ship the licence text** and a notice that the program uses libmariadb
   under the LGPL-2.1 - an entry in `THIRD_PARTY_LICENSES.md` and in each
   package's licence file.
3. **Make libmariadb's source available** - the exact version shipped, with
   any changes made to it - alongside the binary or by a written offer valid
   for three years. Linking an unmodified upstream release and pointing to
   its tagged source is the usual way.
4. **Changes to libmariadb itself are LGPL** and must be published; the
   UltraDatabase driver that *calls* it is not a modification and stays MIT.
5. **Do not forbid** reverse engineering for debugging such modifications in
   any end-user licence terms (§6).

What it does not oblige: publishing UltraDBAdmin's or UltraCanvas's source,
licensing them under the LGPL, or charging nothing.

Two edge cases to note:

- **Signed, sealed packages** (macOS notarised bundles, MSIX in `WindowsApps`)
  make swapping the library awkward for the user. The project already ships
  Cairo, Pango and FFmpeg in those same packages; libmariadb adds no new
  exposure, but the question is the same one for all of them.
- **The WebAssembly build links statically** - WASM has no shared libraries.
  In this design it does not matter: the browser build contains no database
  driver at all (§8.1); libmariadb runs only in the relay, a normal
  dynamically linked server program.

Why not MySQL's own client (libmysqlclient): it is GPL-2.0 with Oracle's
FOSS exception, which is stricter for anyone redistributing a closed
derivative. MariaDB Connector/C speaks to MySQL servers as well
[unverified for MySQL 8.4's newest auth plugins; to be tested in CI against
both servers].

---

## See also

- [`Docs/Modules/UltraDatabase/README.md`](../Modules/UltraDatabase/README.md) — the module and its Stage 2/3 plan
- [`Docs/UltraCanvas/UltraCanvasERDiagramProposal.md`](../UltraCanvas/UltraCanvasERDiagramProposal.md) — X4, live-database introspection
- [`Docs/UltraCanvas/UltraCanvasUIElements.md`](../UltraCanvas/UltraCanvasUIElements.md) — the element catalogue
- [`UltraFIBUDesignProposal.md`](UltraFIBUDesignProposal.md) — the first multi-user PostgreSQL app on UltraDatabase
