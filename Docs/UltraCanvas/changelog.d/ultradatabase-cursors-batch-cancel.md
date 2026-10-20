- **UltraDatabase 0.3.0: streaming cursors, per-statement scripts and
  cancelling** - the module half of UltraDBAdmin's stage 1
  (`Docs/Research/UltraDBAdminDesignProposal.md`).
  - `UltraDb_OpenCursor` / `UltraDb_FetchRows` / `UltraDb_CloseCursor` read
    a query's rows a page at a time, so a table of millions of rows is never
    held in memory. SQLite steps the statement as pages are asked for;
    PostgreSQL declares a server-side cursor - `WITH HOLD` outside a
    transaction, leaving the connection free between pages, `WITHOUT HOLD`
    inside one.
  - `UltraDb_ExecBatch` runs a script and reports every statement - its
    text, outcome and rows - stopping after the first that fails. PostgreSQL
    scripts are split as psql splits them: at semicolons outside strings,
    `E'...'` escape strings, quoted names, dollar-quoted bodies, comments and
    SQL-standard `BEGIN ATOMIC` function bodies.
  - `UltraDb_CancelQuery(connection)` stops the statement running on a
    connection from any thread (`sqlite3_interrupt`, `PQcancel`); the
    running call returns the new `UltraDbResultCode::Cancelled`.
  - The PostgreSQL `?` placeholder rewriter now recognises `E'...'` strings,
    in which `\'` is a quote: a `?` after one inside the string was taken for
    a placeholder.
  - Driver interface: `IUltraDbConnection` gains `ExecuteBatch`, `Cancel`
    and `OpenCursor`, with defaults, and there is a new `IUltraDbCursor`.
