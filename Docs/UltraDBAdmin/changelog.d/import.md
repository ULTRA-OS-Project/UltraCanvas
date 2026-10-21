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
