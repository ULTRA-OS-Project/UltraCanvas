#### 2026-10-21 *0.4*
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

#### 2026-10-21 *0.3*
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
