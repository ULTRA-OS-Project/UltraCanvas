#### 2026-10-23 *0.8*
- **Grids: columns as wide as their text.** The browse grid, the SQL results
  and the import preview sized their columns from a character count, so a
  column of narrow glyphs was wider than it needed to be and a wide one
  still cut its text. Each column is now measured (the list view's
  `SetColumnFitting`): its widest value and its title, at most 360 px. Drag
  a border and the widths are yours.

#### 2026-10-23 *0.7*
- **Browse: a wide table scrolls sideways.** A table with more columns than
  fit the window showed only the leftmost ones, with no way to see the rest.
  The grid now has a horizontal scrollbar (the list view's, see the framework
  changelog): drag it, Shift+wheel, or press Left / Right. Those two keys used
  to switch between Browse, Structure and SQL while the grid had the focus;
  they move the grid now. A cell edited in place is scrolled fully into view
  first.

#### 2026-10-22 *0.6*
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

#### 2026-10-22 *0.5*
- **A cleaner look, and buttons that say what they do without "…"**. Every
  button label is now the action itself - **Add connection**, **Edit row**,
  **Preview SQL**, **Import**, **Export**, **Choose** - where most ended in
  "…"; the tooltip still says it in full. The window is white cards on a
  grey ground (the header with the connection buttons, the tree, the tabs)
  in a theme whose accent is the blue of the app icon. Each button carries
  an icon from the app's own set, and its look says what it does: the
  action a bar is for (**Commit**, **Apply**, **Run**, a dialog's
  **Save**) is filled in the accent, one that removes something (**Remove**,
  **Delete row**, **Drop column**, **Drop index**) is red. Browse pages with
  arrow buttons beside the row range. The tabs carry icons and an accent
  underline; the lists have a quieter header, alternating rows and a soft
  selection whose text stays readable. The tree shows a connection
  (red for production), a schema, a table, a view, a materialized view, a
  foreign table and a sequence each by its icon, without the "(view)"
  suffix, opens with an arrow instead of a +/- box, and shows a node being
  read by a thin bar along its row instead of a "Loading…" line. Error
  messages, in the window and in every dialog, take the theme's red.

#### 2026-10-22 *0.4*
- **Exporting: a table, or the rows of a query, to CSV, TSV, JSON, SQL or a
  workbook** (`Docs/Research/UltraDBAdminDesignProposal.md`). Browse's
  **Export…** writes every row of the table or view shown, read through a
  cursor a page at a time, so a table of millions of rows is never held at
  once; the SQL tab's **Export rows…** writes the rows shown. The Save
  dialog's types are FileLoader's spreadsheet writers - ODS, XLSX, CSV, TSV -
  and JSON and SQL. CSV and TSV go through the framework's Text Export dialog
  (separator, quote, quoting, line ending, charset, BOM) and its writer's
  quoting; JSON is an object per row; SQL is INSERT statements in the
  engine's own dialect between BEGIN and COMMIT, after a SQLite table's own
  CREATE TABLE and indexes, so the file rebuilds the table; a workbook keeps
  numbers as numbers - PostgreSQL's exact numerics too - and never turns
  text into a formula. A file is written beside its name and put in place
  when complete: an export that fails, or the window closing mid-way, never
  leaves half a file where one was.
- **Importing: rows from CSV files and workbooks, and SQL files** (stage 2 of
  `Docs/Research/UltraDBAdminDesignProposal.md`). Browse's **Import…** puts
  the rows of a file into the table shown: the file types are the
  framework's spreadsheet formats (CSV, TSV, ODS, XLSX, XLS); delimited text
  goes through the framework's Text Import dialog (encoding, separators,
  decimal mark, preview), a workbook through its spreadsheet reader, with a
  choice of sheet. A form gives each column of the table a column of the
  file - matched by name when the first row names them, else in order,
  leaving a numbered key to the database - says whether an empty cell is
  NULL and shows the first rows as they will be inserted. Every row goes in
  with bound values in one transaction, or, when one fails, none, and the
  status names the file row that failed. A number column gets the number
  ("1.234,50" from a German export is 1234.50), a text column the text as
  written ("01067" stays "01067"); dates from a workbook are ISO 8601. The
  SQL tab's **Import SQL…** reads a `.sql` or `.sql.gz` file through
  FileLoader, says how many statements it holds and which of them DROP,
  DELETE, TRUNCATE, ALTER or UPDATE, and runs it on the console's session -
  as one transaction unless the file begins and commits its own (a SQLite
  `.dump`), so a failure or Stop leaves nothing of it. A file only psql can
  run (pg_dump's `COPY ... FROM stdin`, backslash commands) is refused with
  the reason.

#### 2026-10-22 *0.3*
- **Editing a table's structure, staged** (stage 2 of
  `Docs/Research/UltraDBAdminDesignProposal.md`). In Structure, **Add
  column…**, **Edit column…** (or a double-click: name, type, NOT NULL,
  default), **Drop column**, **Add index…** (the key in the order the
  columns are ticked, UNIQUE or not), **Drop index** and **Rename table…**
  stage a change, which the lists mark and the bar counts; **Preview SQL…**
  shows the DDL and **Apply** runs it in one transaction, while **Revert**
  drops it. PostgreSQL gets `ALTER TABLE` for everything, a type change
  converting with `USING CAST` and its default set again after it. SQLite gets its own `ALTER TABLE` where it
  has one, and for a change of a column's type, NOT NULL or default the
  documented rebuild: the new table is the old `CREATE TABLE` with only that
  column rewritten - so CHECK constraints, foreign keys, COLLATE,
  AUTOINCREMENT and WITHOUT ROWID stay as written - and the rows, rowids,
  indexes, triggers and AUTOINCREMENT counter come across, with foreign keys
  off around the transaction and checked before it commits. A drop, a
  rebuild and anything on a production connection ask first; a table that
  changed since it was read is not touched. Views, sequences and read-only
  connections are not editable here, and say why.

#### 2026-10-21 *0.2*
- **Editing rows in Browse, staged** (the start of stage 2 of
  `Docs/Research/UltraDBAdminDesignProposal.md`). **Add row…**, **Edit
  row…** (or a double-click) and **Delete row** stage a change, which the
  grid marks and the bar counts; **Preview SQL…** shows the statements and
  **Commit** runs them in one transaction - every UPDATE and DELETE must hit
  exactly one row, by primary key, or nothing is written - while **Revert**
  drops them. Values are bound as parameters, typed as text and converted by
  the database. Views, sequences, tables without a primary key and read-only
  connections are not editable, and say why. Deleting, or committing on a
  production connection, asks first; so does leaving a table or closing the
  window with changes staged.

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
