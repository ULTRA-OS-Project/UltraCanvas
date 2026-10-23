- **Tabs of open objects.** Each table or view picked in the tree opens in a
  tab of its own above Browse, Structure and SQL, and keeps its page, order,
  filters and staged changes while another is shown; a tab with changes
  staged is marked `*` and asks before it closes. Ten at most.
- **The tree's right-click menu and its operations.** A table: open, structure,
  export, copy its name, **Copy table** (with or without its rows), **Rename**,
  **Empty table**, **Drop**, Analyze / Vacuum. A schema or a SQLite
  connection: **New table** (a line per column), **Search every table**,
  **Export as SQL** (the whole schema: structure, rows, or both, with DROP IF
  EXISTS if asked), and SQLite's maintenance - integrity check, quick check,
  analyze, optimize, vacuum, journal mode. Every operation shows its SQL
  before it runs, writes no CASCADE, and is offered only where the connection
  is not read only.
- **Search.** A new tab finds a text in every table and view of a schema - a
  count per column, the text bound - and opens a hit filtered on its column.
  Stop acts between tables.
- **The SQL log.** Under the tabs, every statement the app ran, with its time,
  connection, duration, rows and error (never its bound values); copied as
  SQL, or double-clicked back into the console.
- **The console.** **Run statement** (Ctrl+Shift+Enter) runs the statement
  under the caret; **Explain** shows its plan (EXPLAIN QUERY PLAN / EXPLAIN,
  never ANALYZE); **Snippets** keeps SQL under a name.
- **The row form** offers a field per type - true / false for a boolean, the
  pattern for a date - says before Commit what a column cannot take ("12,5"
  in a number column), and sets a column to its value, NULL, its default or a
  database function (CURRENT_TIMESTAMP, gen_random_uuid() ...).
- **Auto-commit** per connection: a row change is written as it is made; a
  deletion and anything on a production connection still ask.
- The cell editor follows the grid when it scrolls, and ends, staging what
  was typed, when its cell scrolls out of view.
- An export of a PostgreSQL table as SQL now begins with the table's own
  definition, read from the catalog, so the file rebuilds the table there too.
