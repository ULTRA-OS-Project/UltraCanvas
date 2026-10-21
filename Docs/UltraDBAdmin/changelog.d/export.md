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
