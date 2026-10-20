#### 2026-10-20 *0.1*
- **First version: a read-only database browser on UltraDatabase** (stage 1
  of `Docs/Research/UltraDBAdminDesignProposal.md`). Connections to SQLite
  files and PostgreSQL servers, kept in `connections.json` in the app's
  data folder with every password in its own encrypted vault, never in the
  file. A tree of each connection's schemas, tables and views; a table's
  rows a page at a time, in key order or sorted by a column; its columns,
  keys and indexes; and a SQL console that runs a script on a worker
  thread, shows every statement's result, can be stopped, and keeps a
  history.
