# UltraDBAdmin

Browse and query the databases UltraDatabase reaches - SQLite files and
PostgreSQL servers - in the spirit of phpMyAdmin, as a desktop application.
The design, the roadmap and the decisions behind it are in
[`Docs/Research/UltraDBAdminDesignProposal.md`](../../Docs/Research/UltraDBAdminDesignProposal.md).

This is **stage 1: a browser with a SQL console.** Browse and Structure only
read; the console runs whatever SQL it is given, so a connection marked *read
only* (the database refuses every change) is the way to make it safe.

| | |
|---|---|
| **Connections** | SQLite files and PostgreSQL servers (host or socket directory, port, database, user, password, TLS posture), added and edited in a dialog, kept in `connections.json` in the data folder. A password is stored in the app's own encrypted vault (`vault/`, UltraVault's device-key vault) and never written to the list; the connection hands UltraDatabase only the vault key. *Production* draws a connection in red; *read only* makes the database refuse changes. |
| **Tree** | Each connection's schemas, and in each its tables, views, materialized views, foreign tables and sequences (`UltraDb_ListSchemas`, `UltraDb_ListObjects`), read when the node is expanded. A server that cannot be reached says why in the tree; collapsing and expanding tries again, **Refresh** reconnects. |
| **Browse** | A table's rows 200 at a time, never the whole table: one bound `LIMIT ? OFFSET ?` query per page, in primary-key order or sorted on the server by a click on a column header (then the key), with the exact row count beside **Previous** / **Next**. Every name in it is quoted by `UltraDb_QuoteIdentifier`. A table without a key says its order is not stable. A sequence shows its one row of state. |
| **Structure** | The table's columns (type, nullability, default, key position, identity / generated, collation, comment); its primary key, unique constraints, indexes, foreign keys and triggers; a view's definition (`UltraDb_DescribeTable`). |
| **SQL** | A console that runs the selection, or the whole script (**Run** or Ctrl+Enter), on the connection picked in the tree, and lists what every statement did (`UltraDb_ExecBatch`); a click on one shows its rows, sortable. **Stop** cancels the running statement (`UltraDb_CancelQuery`). The history dropdown puts an earlier script back. |

## Threads and sessions

No query runs on the UI thread. The tree, Browse and Structure queue theirs on
one worker (`engine/DbAdminJobQueue`), one after another on the connection's
own session; the SQL console runs on a thread of its own (`SqlRunner`) on a
second session to the same database (`ConsoleName`), so a script that runs
for minutes holds up neither the tree nor a page, and Stop cancels exactly the
console's statement. Results come back with `PostToUIThread`; one that arrives
after another table was picked is recognised and dropped.

Editing, import and export, the ER view and server administration are the
later stages of the proposal.

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
│   ├── DbAdminFormat             a value as one line of a grid cell
│   ├── DbAdminJobQueue           the worker the window's queries run on
│   ├── DbAdminPaths              the data folder
│   ├── DbAdminSqlRunner          a script on a worker thread, Stop, history
│   └── DbAdminTablePager         the Browse view's pages
└── ui/
    ├── DbAdminWindow             tree, tabs, status line
    ├── DbAdminModels             list models over result sets
    └── DbAdminConnectionDialog   add / edit a connection
```

## Tests

`Tests/UltraDBAdmin` (`-DULTRACANVAS_BUILD_ULTRADBADMIN_TESTS=ON`, target
`UltraDBAdminEngineTests`): the connection store and that its file holds no
password, the console's own session, cell formatting, paging and sorting with
quoted names, the SQL runner including Stop, and the job queue's order and
shutdown - on SQLite in temporary folders.
