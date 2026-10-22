- **Browse: filters, editing in the cell, blobs, and rows copied as text.**
  - **Filters.** A bar under the grid filters on a column: contains, starts
    with, =, ≠, <, ≤, >, ≥, is NULL, is not NULL. There is one filter per
    column and all of them must hold. Each value is a bound parameter, so a
    number compares as a number and a date as a date. "Contains" ignores case
    on PostgreSQL too. The row count, the paging and an export of the table
    all follow the filters.
  - **Editing in the cell.** A double-click opens a field over the cell. Enter
    stages the value, Escape leaves the cell as it was, and clicking
    elsewhere stages it too. The right-click menu also sets a cell to NULL.
    The row form stays on Edit row.
  - **Blobs.** A blob cell opens a viewer, which shows a picture as a picture,
    text as text, and anything else as a hex dump. The kind of file is read
    from its bytes, not guessed from a name. Save to file writes the bytes as
    they are, with an extension that fits their kind. Load from file stages a
    file's bytes for the cell, and Commit writes them (SQLite BLOB,
    PostgreSQL bytea). The console's result rows offer View and Save as well.
  - **Copy rows.** Select rows (Ctrl+click, Shift+click) and copy them from
    the right-click menu as CSV, JSON or SQL INSERT statements, or press
    Ctrl+C for CSV. This works in Browse and in the console's result. The
    INSERTs go into Browse's table, or into the table the console's SELECT
    read.
