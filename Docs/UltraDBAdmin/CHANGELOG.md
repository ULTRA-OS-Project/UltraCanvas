#### 2026-10-20 *0.1*
- **First version: a database browser with a SQL console, on UltraDatabase**
  (stage 1 of `Docs/Research/UltraDBAdminDesignProposal.md`). Connections
  to SQLite files and PostgreSQL servers, added and edited in a dialog and
  kept in `connections.json` in the app's data folder, with every password
  in the app's own encrypted vault, never in the file. A tree of each
  connection's schemas, tables, views and sequences, read when it is
  expanded; a table's rows 200 at a time with the exact count, in key order
  or sorted on the server by a click on a column header; its columns, keys,
  indexes, foreign keys, triggers and a view's definition; and a SQL console
  (Ctrl+Enter) that runs a script on a session of its own, shows what each
  statement did and the rows of the one picked, can be stopped, and keeps a
  history. No query runs on the UI thread: an unreachable server or a long
  script never freezes the window. A connection marked *production* is
  drawn in red, one marked *read only* refuses every change.
