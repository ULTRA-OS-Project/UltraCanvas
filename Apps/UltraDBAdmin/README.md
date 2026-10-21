# UltraDBAdmin

Browse and query the databases UltraDatabase reaches - SQLite files and
PostgreSQL servers - in the spirit of phpMyAdmin, as a desktop application.
The design, the roadmap and the decisions behind it are in
[`Docs/Research/UltraDBAdminDesignProposal.md`](../../Docs/Research/UltraDBAdminDesignProposal.md).

This is **stage 1, a browser with a SQL console, and stage 2's editing:
rows in Browse, the table's structure in Structure.** The console runs
whatever SQL it is given, so a connection marked *read only* (the database
refuses every change, and neither Browse nor Structure offers editing) is
the way to make it safe.

| | |
|---|---|
| **Connections** | SQLite files and PostgreSQL servers (host or socket directory, port, database, user, password, TLS posture), added and edited in a dialog, kept in `connections.json` in the data folder. A password is stored in the app's own encrypted vault (`vault/`, UltraVault's device-key vault) and never written to the list; the connection hands UltraDatabase only the vault key. *Production* draws a connection in red; *read only* makes the database refuse changes. |
| **Tree** | Each connection's schemas, and in each its tables, views, materialized views, foreign tables and sequences (`UltraDb_ListSchemas`, `UltraDb_ListObjects`), read when the node is expanded. A server that cannot be reached says why in the tree; collapsing and expanding tries again, **Refresh** reconnects. |
| **Browse** | A table's rows 200 at a time, never the whole table: one bound `LIMIT ? OFFSET ?` query per page, in primary-key order or sorted on the server by a click on a column header (then the key), with the exact row count beside **Previous** / **Next**. Every name in it is quoted by `UltraDb_QuoteIdentifier`. A table without a key says its order is not stable. A sequence shows its one row of state. |
| **Editing** | In Browse, **Add row…**, **Edit row…** (or a double-click) and **Delete row** stage a change; nothing is written yet. The grid marks it - an edited cell reads its new value followed by `*`, a row to be deleted starts with `DEL` - and the bar counts it. **Preview SQL…** shows the statements, **Commit** runs them all in one transaction (every `UPDATE` and `DELETE` must hit exactly one row, or nothing is written), **Revert** drops them. A row is addressed by its primary key as read, so a view, a sequence and a table without a key are not editable here, and say why. The row form has a field per column with **NULL** and, for a new row, **Default**; values are typed as text and the database converts them to the column's type. A commit that deletes, and any commit on a *production* connection, asks first; opening another table or closing the window with changes staged asks before dropping them (`engine/DbAdminEditBuffer`). |
| **Structure** | The table's columns (type, nullability, default, key position, identity / generated, collation, comment); its primary key, unique constraints, indexes, foreign keys and triggers; a view's definition (`UltraDb_DescribeTable`). |
| **Structure editing** | **Add column…**, **Edit column…** (or a double-click: name, type, NOT NULL, default), **Drop column**, **Add index…**, **Drop index** and **Rename table…** stage a change; the lists mark it (`NEW`, `DROP`, a changed value followed by `*`) and the bar counts it. **Preview SQL…** shows the DDL, **Apply** runs it all in one transaction, **Revert** drops it. The statements run in a fixed order - drop indexes, change definitions under the old names, rename columns, drop columns, add columns, create indexes, rename the table - so none needs a name another has not made yet. PostgreSQL: `ALTER TABLE` throughout, a type change converting with `USING CAST(column AS type)`. SQLite: its own `ALTER TABLE` where it has one (rename, add and drop a column, rename the table); a change of a column's type, NOT NULL or default is SQLite's documented rebuild - a new table from the old `CREATE TABLE` with only that column's definition rewritten (CHECK, foreign keys, COLLATE, AUTOINCREMENT and WITHOUT ROWID kept as written), the rows copied with their rowids, the old table dropped, the new one renamed, its indexes, triggers and AUTOINCREMENT counter put back, with `foreign_keys` off around the transaction and `foreign_key_check` before it commits. SQLite's refusals are said when a change is staged, not at Apply (a NOT NULL column added without a default, dropping a key column or one an index uses). A type and a default are SQL, checked to be one fragment - no `;`, no comment, closed quotes and parentheses - before they are written in. A drop, a rebuild and anything on a *production* connection ask first; a SQLite table that changed since it was read is not touched; staged rows in Browse must be committed or reverted first (`engine/DbAdminStructureEdit`, `engine/DbAdminSqliteTable`). |
| **SQL** | A console that runs the selection, or the whole script (**Run** or Ctrl+Enter), on the connection picked in the tree, and lists what every statement did (`UltraDb_ExecBatch`); a click on one shows its rows, sortable. **Stop** cancels the running statement (`UltraDb_CancelQuery`). The history dropdown puts an earlier script back. |

## Threads and sessions

No query runs on the UI thread. The tree, Browse and Structure queue theirs on
one worker (`engine/DbAdminJobQueue`), one after another on the connection's
own session; the SQL console runs on a thread of its own (`SqlRunner`) on a
second session to the same database (`ConsoleName`), so a script that runs
for minutes holds up neither the tree nor a page, and Stop cancels exactly the
console's statement. Results come back with `PostToUIThread`; one that arrives
after another table was picked is recognised and dropped.

Editing or refreshing a connection re-registers its sessions straight away,
even while a page loads or a script runs: UltraDatabase keeps a connection
until the last call on it returns, so what is running finishes on the old
settings and the next query uses the new.

Import and export, editing in the grid cell itself, the ER view and server
administration are the later stages of the proposal.

## Data folder

`$XDG_DATA_HOME/UltraDBAdmin` (`~/.local/share/UltraDBAdmin`),
`~/Library/Application Support/UltraDBAdmin` or `%LOCALAPPDATA%\UltraDBAdmin`,
readable by its owner alone:

- `connections.json` - the connection list, no secrets;
- `vault/` - the passwords, encrypted (`dbadmin.vault`, unlocked by the
  device key beside it, as UltraMail's and UltraPress's vaults are).

## Layout

```
Apps/UltraDBAdmin/
├── main.cpp                      the application, the window, the event loop
├── engine/                       no UI - compiled headless by Tests/UltraDBAdmin
│   ├── DbAdminConnections        the connection list and its vault
│   ├── DbAdminEditBuffer         staged row edits, their SQL, the commit
│   ├── DbAdminFormat             a value as one line of a grid cell
│   ├── DbAdminJobQueue           the worker the window's queries run on
│   ├── DbAdminPaths              the data folder
│   ├── DbAdminSqlRunner          a script on a worker thread, Stop, history
│   ├── DbAdminSqliteTable        a SQLite CREATE TABLE read and rewritten, for the rebuild
│   ├── DbAdminStructureEdit      staged structure edits, their DDL per engine, Apply
│   └── DbAdminTablePager         the Browse view's pages
└── ui/
    ├── DbAdminWindow             tree, tabs, status line
    ├── DbAdminModels             list models over result sets
    ├── DbAdminRowDialog          the row form, the SQL preview
    ├── DbAdminStructureDialog    the column and index forms
    └── DbAdminConnectionDialog   add / edit a connection
```

## Tests

`Tests/UltraDBAdmin` (`-DULTRACANVAS_BUILD_ULTRADBADMIN_TESTS=ON`, target
`UltraDBAdminEngineTests`): the connection store and that its file holds no
password, the console's own session, cell formatting, paging and sorting with
quoted names, the SQL runner including Stop, the job queue's order and shutdown, and the
staged row edits (the SQL they become, composite keys and quoted names, a
commit that rolls everything back when one row has gone), and the staged
structure edits: the CREATE TABLE reader and rewriter, the order of the DDL,
SQLite's rebuild keeping rows, rowids, CHECK constraints, indexes,
triggers, foreign keys and the AUTOINCREMENT counter, a rebuild the data
refuses leaving the table exactly as it was - on SQLite in temporary
folders, and on PostgreSQL (type changes with USING, a failed cast rolling
back a rename with it) where `ULTRAFIBU_TEST_PG_*` name a server, as in CI.
