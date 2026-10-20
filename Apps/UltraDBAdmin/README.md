# UltraDBAdmin

Browse and query the databases UltraDatabase reaches - SQLite files and
PostgreSQL servers - in the spirit of phpMyAdmin, as a desktop application.
The design, the roadmap and the decisions behind it are in
[`Docs/Research/UltraDBAdminDesignProposal.md`](../../Docs/Research/UltraDBAdminDesignProposal.md).

This is **stage 1: a read-only viewer.**

| | |
|---|---|
| **Connections** | SQLite files and PostgreSQL servers, kept in `connections.json` in the data folder. A password is stored in the app's own encrypted vault (`vault/`, UltraVault's device-key vault) and never written to the list; the connection hands UltraDatabase only the vault key. |
| **Tree** | Each connection's schemas, and in each its tables, views, materialized views, foreign tables and sequences (`UltraDb_ListSchemas`, `UltraDb_ListObjects`). |
| **Browse** | A table's rows a page at a time, never the whole table: one bound `LIMIT ? OFFSET ?` query per page, in primary-key order or sorted by a column (then the key). Every name in it is quoted by `UltraDb_QuoteIdentifier`. A table without a key says its order is not stable. |
| **Structure** | The table's columns (type, nullability, default, identity / generated, collation, comment), primary key, foreign keys, indexes and triggers (`UltraDb_DescribeTable`). |
| **SQL** | A console that runs a script on a worker thread - the window stays responsive - and shows every statement's result (`UltraDb_ExecBatch`). **Stop** cancels the running statement (`UltraDb_CancelQuery`). The history keeps what was run. |

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
password, cell formatting, paging and sorting with quoted names, and the SQL
runner including Stop - on SQLite in temporary folders.
