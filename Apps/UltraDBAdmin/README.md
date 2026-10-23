# UltraDBAdmin

Browse and query the databases UltraDatabase reaches - SQLite files and
PostgreSQL servers - in the spirit of phpMyAdmin, as a desktop application.
The design, the roadmap and the decisions behind it are in
[`Docs/Research/UltraDBAdminDesignProposal.md`](../../Docs/Research/UltraDBAdminDesignProposal.md).

This is **stage 1, a browser with a SQL console, and stage 2: editing rows
in Browse and the table's structure in Structure, importing rows and SQL
files, and exporting tables and query results.** The console runs
whatever SQL it is given, so a connection marked *read only* (the database
refuses every change, and neither Browse nor Structure offers editing) is
the way to make it safe.

| | |
|---|---|
| **Connections** | SQLite files and PostgreSQL servers (host or socket directory, port, database, user, password, TLS posture), added and edited in a dialog, kept in `connections.json` in the data folder. A password is stored in the app's own encrypted vault (`vault/`, UltraVault's device-key vault) and never written to the list; the connection hands UltraDatabase only the vault key. *Production* draws a connection in red; *read only* makes the database refuse changes. |
| **Tree** | Each connection's schemas, and in each its tables, views, materialized views, foreign tables and sequences (`UltraDb_ListSchemas`, `UltraDb_ListObjects`), read when the node is expanded. A server that cannot be reached says why in the tree; collapsing and expanding tries again, **Refresh** reconnects. |
| **Look** | White cards on a grey ground - the header with the connection buttons, the tree, the tabs - in the framework theme the app sets at start-up, its accent the blue of the app icon (`ui/DbAdminTheme`). Every button carries an icon from the app's own set (`media/icons/dbadmin`) and says by its look what it does: the one action a bar is for (**Commit**, **Apply**, **Run**, **Add connection**, a dialog's OK) in the accent, one that removes something in red. A label is the action itself, never followed by `…`; the tooltip says it in full. The tree shows a connection (red for production), a schema, a table, a view, a materialized view, a foreign table and a sequence each by its icon, an arrow to open a node (`TreeExpanderStyle::Chevron`), and a node being read by a bar along its foot. Errors, in the window and the dialogs, are the theme's red. |
| **Browse** | A table's rows 200 at a time, never the whole table: one bound `LIMIT ? OFFSET ?` query per page, in primary-key order or sorted on the server by a click on a column header (then the key), with the exact row count beside the previous and next page buttons. Every name in it is quoted by `UltraDb_QuoteIdentifier`. A table without a key says its order is not stable. A sequence shows its one row of state. **Filters** under the grid narrow the rows: a column, a comparison (contains, starts with, =, ≠, <, ≤, >, ≥, is NULL, is not NULL) and a value, one filter per column, all of them true - each a bound parameter, the value converted by the database to the column's type, so `100` compares as a number and `2026-01-31` as a date; "contains" matches the column's text in any case (`LIKE`, PostgreSQL's `ILIKE`, `%` and `_` in the value matched literally). The count, the pages and an export of the table follow them (`engine/DbAdminTablePager`). |
| **Editing** | In Browse, a double-click edits a cell in place - a field over the cell: Enter stages the value, Escape leaves it, a click elsewhere stages it too; the right-click menu sets a cell to NULL. **Add row**, **Edit row** (the row form) and **Delete row** stage a change; nothing is written yet. The grid marks it - an edited cell reads its new value followed by `*`, a row to be deleted starts with `DEL` - and the bar counts it. **Preview SQL** shows the statements, **Commit** runs them all in one transaction (every `UPDATE` and `DELETE` must hit exactly one row, or nothing is written), **Revert** drops them. A row is addressed by its primary key as read, so a view, a sequence and a table without a key are not editable here, and say why. The row form has a field per column with **NULL** and, for a new row, **Default**; values are typed as text and the database converts them to the column's type. A commit that deletes, and any commit on a *production* connection, asks first; opening another table or closing the window with changes staged asks before dropping them (`engine/DbAdminEditBuffer`). |
| **Structure** | The table's columns (type, nullability, default, key position, identity / generated, collation, comment); its primary key, unique constraints, indexes, foreign keys and triggers; a view's definition (`UltraDb_DescribeTable`). |
| **Structure editing** | **Add column**, **Edit column** (or a double-click: name, type, NOT NULL, default), **Drop column**, **Add index**, **Drop index** and **Rename table** stage a change; the lists mark it (`NEW`, `DROP`, a changed value followed by `*`) and the bar counts it. **Preview SQL** shows the DDL, **Apply** runs it all in one transaction, **Revert** drops it. The statements run in a fixed order - drop indexes, change definitions under the old names, rename columns, drop columns, add columns, create indexes, rename the table - so none needs a name another has not made yet. PostgreSQL: `ALTER TABLE` throughout, a type change converting with `USING CAST(column AS type)` and the column's default dropped before it and set again after (one written for the old type, `'1'::text`, then fails with PostgreSQL's own advice and nothing is changed). SQLite: its own `ALTER TABLE` where it has one (rename, add and drop a column, rename the table); a change of a column's type, NOT NULL or default is SQLite's documented rebuild - a new table from the old `CREATE TABLE` with only that column's definition rewritten (CHECK, foreign keys, COLLATE, AUTOINCREMENT and WITHOUT ROWID kept as written), the rows copied with their rowids, the old table dropped, the new one renamed, its indexes, triggers and AUTOINCREMENT counter put back, with `foreign_keys` off around the transaction and `foreign_key_check` before it commits. SQLite's refusals are said when a change is staged, not at Apply (a NOT NULL column added without a default, dropping a key column or one an index uses). A type and a default are SQL, checked to be one fragment - no `;`, no comment, closed quotes and parentheses - before they are written in. A drop, a rebuild and anything on a *production* connection ask first; a SQLite table that changed since it was read is not touched; staged rows in Browse must be committed or reverted first (`engine/DbAdminStructureEdit`, `engine/DbAdminSqliteTable`). |
| **Import rows** | Browse's **Import** puts the rows of a file into the table shown. The file types are FileLoader's spreadsheet formats (`AddLoadTypes` with `Spreadsheet`: CSV, TSV, ODS, XLSX, XLS). Delimited text goes through the framework's Text Import dialog (`ShowCSVImportDialog`: encoding, separators, quote, decimal mark, preview) and is read with `UltraCanvasFileLoader::LoadFile` and the CSV engine; a workbook is read by the framework's spreadsheet, one grid per sheet. A form then gives each column of the table a column of the file, or leaves it to its default: by name when the first row names them (guessed when one of its cells is a column's name), else in order - a file one column short of the table leaves the numbered key out. It says whether an empty cell is NULL or empty text, refuses what cannot work (a NOT NULL column without a default left out, a computed column given a value) and shows the first 50 rows as they will be inserted. Every row is an `INSERT` with bound values, all in one transaction: when one fails, nothing stays, and the status names the file row (`engine/DbAdminImport`). A column of numbers - by value type, or by declared type, as PostgreSQL's `numeric` comes back as text - gets a cell's number (`1.234,50` from a German export is `1234.50`, grouping checked in threes, so `1,5` is not 15); a text column gets the text as written (`01067` stays `01067`). A workbook's dates and times are ISO 8601, TRUE / FALSE are 1 / 0 for a number column. A production connection asks first. |
| **Import SQL** | The SQL tab's **Import SQL** reads a `.sql` file - or `.sql.gz`, unpacked by `UltraCanvasFileLoader::LoadFile` - and says first what is in it: how many statements (split as SQLite and psql split them: not at a `;` in a quote, a comment, a PostgreSQL dollar quote or a trigger's body), and which of them DROP, DELETE, TRUNCATE, ALTER or UPDATE (`engine/DbAdminSqlScript`). It runs on the console's own session, so the statement list shows what each did and Stop cancels it - by default as one transaction, so a failure or Stop leaves nothing of the file; a file that begins and commits its own transactions (a SQLite `.dump`) runs as written. A file only psql can run - pg_dump's `COPY ... FROM stdin`, backslash commands - is refused with the reason. The file is not put in the history. |
| **Blobs** | A blob cell - double-clicked, or **View** in the right-click menu, in Browse and in the console's rows - opens a viewer that shows what the bytes are (read from their signature: PNG, JPEG, GIF, BMP, WebP, TIFF, SVG, PDF, ZIP, gzip, SQLite, text): a picture as a picture, text as text, anything else as a hex dump (`engine/DbAdminBlob`). **Save to file** writes the bytes as they are, under an extension that fits them, through `UltraCanvasFileLoader::SaveFile`; **Load from file** (Browse, where the row can be edited) stages a file's bytes for the cell, bound as a blob - SQLite's BLOB, PostgreSQL's bytea - and Commit writes them. A staged blob reads as its size followed by `*`. |
| **Copy rows** | Rows selected in Browse or in the console's result (Ctrl+click, Shift+click) are copied from the right-click menu as CSV (a header line, quotes where needed), JSON (an array of objects) or SQL `INSERT` statements - into Browse's table, or into the table the console's `SELECT` read; Ctrl+C copies CSV (`RowsText` in `engine/DbAdminExport`). |
| **Export** | Browse's **Export** writes every row of the table or view shown - every row its filters let through, in its order (`SELECT *`, every name quoted, the filters' values bound), read through a cursor 1 000 rows at a time (`UltraDb_OpenCursor`), so a table of millions of rows is never held; the status counts them as they go. The SQL tab's **Export rows** writes the rows shown below the console. The Save dialog offers FileLoader's spreadsheet writers (`AddSaveTypes` with `Spreadsheet`: ODS, XLSX, CSV, TSV) and the two this app writes itself, JSON and SQL; the extension chosen picks the format (`engine/DbAdminExport`). **CSV / TSV**: the framework's Text Export dialog (`ShowCSVExportDialog`: separator, quote, quoting, line ending, charset, BOM) previewed on the rows at hand, and its writer's quoting (`CSVFormatRecord`, `CSVEncodeFromUtf8`); NULL is an empty field, a blob `\x` hex. **JSON**: an array of objects, one per row and line; numbers as numbers, NULL as null. **SQL**: `INSERT` statements in the engine's dialect (SQLite `X'..'`, PostgreSQL `decode('..', 'hex')` for a blob) between `BEGIN` and `COMMIT`, after a SQLite table's own `CREATE TABLE` and `CREATE INDEX` statements, so running the file rebuilds the table; a PostgreSQL table's definition is pg_dump's to write, and the file says so. **Workbook**: written by the framework's spreadsheet - column names in bold, numbers as numbers (an exact numeric too, by its declared type; `01067` stays text), TRUE / FALSE, everything else as text and never as a formula - up to a sheet's 1 048 575 rows, and the status says when a table had more. A file is written beside its name and moved there when complete, so an export that fails, or is stopped by the window closing, leaves the file that was there. |
| **SQL** | A console that runs the selection, or the whole script (**Run** or Ctrl+Enter), on the connection picked in the tree, and lists what every statement did (`UltraDb_ExecBatch`); a click on one shows its rows, sortable. **Stop** cancels the running statement (`UltraDb_CancelQuery`). The history dropdown puts an earlier script back. |

## Threads and sessions

No query runs on the UI thread. The tree, Browse and Structure queue theirs on
one worker (`engine/DbAdminJobQueue`), one after another on the connection's
own session; the SQL console runs on a thread of its own (`SqlRunner`) on a
second session to the same database (`ConsoleName`), so a script that runs
for minutes holds up neither the tree nor a page, and Stop cancels exactly the
console's statement. Results come back with `PostToUIThread`; one that arrives
after another table was picked is recognised and dropped.

Editing or refreshing a connection re-registers its sessions straight away,
even while a page loads or a script runs: UltraDatabase keeps a connection
until the last call on it returns, so what is running finishes on the old
settings and the next query uses the new.

Editing in the grid cell itself, the ER view and server administration are
the later stages of the proposal.

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
│   ├── DbAdminBlob               what a blob is, by its bytes; its hex dump
│   ├── DbAdminConnections        the connection list and its vault
│   ├── DbAdminEditBuffer         staged row edits, their SQL, the commit
│   ├── DbAdminExport             rows to CSV, JSON, SQL through a cursor; a workbook's cells
│   ├── DbAdminFormat             a value as one line of a grid cell
│   ├── DbAdminImport             a file's rows mapped onto a table, inserted all or none
│   ├── DbAdminJobQueue           the worker the window's queries run on
│   ├── DbAdminPaths              the data folder
│   ├── DbAdminSqlRunner          a script on a worker thread, Stop, history, one transaction
│   ├── DbAdminSqlScript          a SQL file read before it runs: statements, risks, psql-only parts
│   ├── DbAdminSqliteTable        a SQLite CREATE TABLE read and rewritten, for the rebuild
│   ├── DbAdminStructureEdit      staged structure edits, their DDL per engine, Apply
│   └── DbAdminTablePager         the Browse view's pages and filters
└── ui/
    ├── DbAdminWindow             tree, tabs, status line
    ├── DbAdminTheme              the theme, the icons, the buttons, cards and bars every page uses
    ├── DbAdminModels             list models over result sets
    ├── DbAdminRowDialog          the row form, the SQL preview
    ├── DbAdminStructureDialog    the column and index forms
    ├── DbAdminImportDialog       the SQL import, the row import's mapping, workbook reading and writing
    ├── DbAdminBlobDialog         a blob: the picture, the text or a hex dump; save, load
    └── DbAdminConnectionDialog   add / edit a connection
```

## Tests

`Tests/UltraDBAdmin` (`-DULTRACANVAS_BUILD_ULTRADBADMIN_TESTS=ON`, target
`UltraDBAdminEngineTests`): the connection store and that its file holds no
password, the console's own session, cell formatting, paging and sorting with
quoted names, the SQL runner including Stop, the job queue's order and shutdown, and the
staged row edits (the SQL they become, composite keys and quoted names, a
commit that rolls everything back when one row has gone), and the staged
structure edits: the CREATE TABLE reader and rewriter, the order of the DDL,
SQLite's rebuild keeping rows, rowids, CHECK constraints, indexes,
triggers, foreign keys and the AUTOINCREMENT counter, a rebuild the data
refuses leaving the table exactly as it was - on SQLite in temporary
folders, and on PostgreSQL (type changes with USING, a failed cast rolling
back a rename with it) where `ULTRAFIBU_TEST_PG_*` name a server, as in CI -
and the imports: numbers read in a file's own format, a CSV file's grid and
the line each row came from, header guessing and mapping by name or order,
the preview, an import that fails on one row leaving nothing, a SQL file's
statements counted past quotes, triggers and dollar quotes, its own
transactions and psql-only parts found, and a file run as one transaction
rolled back on failure - and the exports: literals per engine, CSV, TSV and
JSON read back byte for byte, a SQL file that rebuilds its SQLite table and
refills a PostgreSQL one exactly (numeric, boolean, time, quotes, bytea), a
stopped export leaving the file that was there, and a workbook's cells -
and Browse's filters (counts on SQLite and PostgreSQL, `%` and `_` matched
literally, ILIKE, a number and a date compared by type), a blob staged,
written and read back on both engines, rows copied as CSV, JSON and
INSERTs, a blob's kind and hex dump, and the table a SELECT reads.
