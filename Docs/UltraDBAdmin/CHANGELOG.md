#### 2026-10-28 *0.13*
- **The relay's sign-in checks the ID token through UltraCrypt.** Its RS256 /
  ES256 signature (`UltraCryptPublicKey.h`) and the sign-in's random state
  and nonce (`UltraCrypt_RandomBytes`) are UltraCrypt's; the relay calls no
  OpenSSL itself and is built with its sign-in where UltraCrypt has its
  OpenSSL 3 backend. The test still signs its tokens with OpenSSL, a signer
  independent of the code it checks.

#### 2026-10-25 *0.12*
- **The browser build.** UltraDBAdmin compiles to WebAssembly
  (`UltraDBAdmin.html`) and reaches its databases through
  **ultradbadmin-relay** on the server: the page holds no password and names
  no host, the relay offers the connections configured on its side to the
  users its tokens name, with read or write access each. The page opens its
  relay at start-up (`?relay=` in its address, else `/relay` on its own
  host).
- **Relay connections on the desktop too.** **Relay** in the header asks for
  a relay's address and a token and adds a connection for each name the
  relay offers; every tab works through it. The token is kept in the vault,
  where a password goes.
- **ultradbadmin-relay**: an INI configuration (connections, users by token
  hash, limits), passwords in its own vault (`--set-secret`),
  `--hash-token`, `--check`, an audit log of every statement with its user
  and address, and a WebSocket listener on loopback for the TLS proxy in
  front, with `/health`. A plain password in the file, a network address
  without TLS and an SSH option are refused.
- **Data compare.** **Compare data…** in a table's menu sets its rows beside
  another table's on any connection, another engine too, matched by its
  primary key: the rows that are the same, only here, only there or changed,
  a changed value shown as `here -> there`. **Make the other the same…**
  shows the INSERTs, UPDATEs and DELETEs that make the other table hold these
  rows, and runs them as one transaction.
- **Signing in with an account.** The relay can sign its users in with an
  OpenID Connect provider (`[signin]`: Keycloak, Entra ID, Google...): the
  relay is the provider's client - PKCE, the ID token's signature, issuer,
  audience, expiry and nonce checked - finds the user by verified email or
  subject, and hands the page a short-lived ticket. **Sign in** appears
  beside the token in the page and in the desktop app, which signs in
  through the system browser.
- **A statement time-out on the relay** (`statement_timeout`, 300 s unless
  set): a query still running then is stopped on the database and the page
  told why.
- **The relay dialog** is titled "Connect to a DB relay", with the address,
  the token and the buttons first and what a relay is under them; in the
  browser it has a title bar to move it by, as every dialog there now has.
- **Forms first, notes after.** In the row form, the column and index forms,
  New table and Compare the fields and buttons come first and how the form
  works is a quieter note under them; dialogs that say what is about to
  happen (the SQL before an operation, Import SQL's warnings, the import's
  mapping, the integrity check's findings) keep it on top.
- **Monitor charts on the Server tab.** **Monitor**, beside Sessions,
  Settings and Sizes, charts a PostgreSQL or MySQL / MariaDB server read
  every two seconds while it is shown: sessions connected and running a
  statement, transactions, rows read and written a second, the share of
  reads the buffer cache served, and on MySQL / MariaDB statements a second
  and the traffic received and sent. The last four minutes are kept; Refresh
  starts the charts again.
- **Completion in the SQL tab.** As a name is typed, or on Ctrl+Space, a
  list under the caret offers what fits there: the tables and views after
  `FROM` and `JOIN`, the columns of the statement's tables after `SELECT` and
  `WHERE`, an alias's or a table's columns after `alias.`, a schema's tables
  after `schema.`, and the keywords. Names are offered as they must be
  written - quoted where the engine needs it - and nothing is offered inside
  a string or a comment.
- **SSH tunnels.** A PostgreSQL or MySQL / MariaDB server behind a bastion
  is reached through an SSH tunnel: **Through an SSH tunnel** in the
  connection dialog, the SSH server, its user and how to sign in there - the
  SSH agent, a password, or a private key file with its passphrase, all kept
  in the vault. On the first connection the bastion's host key is shown and
  trusted only when you agree; a bastion that presents another key later is
  refused. The tree, Browse and the SQL console all go through the tunnel.

#### 2026-10-24 *0.11*
- **MySQL and MariaDB.** A connection can be a MySQL or MariaDB server:
  host or the socket file, port 3306 by default, database, user, password
  in the vault, TLS. Every tab works on it, in MySQL's own dialect:
  - Names are quoted with backticks. Browse's filters and Search use
    `CAST AS CHAR` and `LIKE ... ESCAPE '!'`; Search's "is exactly"
    minds case (`BINARY`).
  - Structure restates a changed column with `MODIFY COLUMN`, keeping its
    `AUTO_INCREMENT`, collation and comment, and drops an index with
    `DROP INDEX ... ON`. MySQL commits around every `ALTER TABLE`, so the
    steps run one by one, and the preview says a failure leaves the steps
    before it done.
  - The operations: empty with `TRUNCATE` or `DELETE`, rename with
    `RENAME TABLE`, copy with `CREATE TABLE ... LIKE`; *Check integrity*,
    *Quick check*, *Analyze* and *Optimize* on a table.
  - The SQL tab splits a script as the mysql client does (`DELIMITER`,
    `#` comments, backslash escapes), for Import SQL's count and the
    statement under the caret alike.
  - Export as SQL writes a database as mysqldump does: foreign key checks
    off, each table's `SHOW CREATE TABLE` and rows, its triggers between
    `DELIMITER` lines, the views - without the database's name or a
    `DEFINER`, so the file loads anywhere. A table's own export writes its
    `SHOW CREATE TABLE` first.
  - Routines lists triggers, functions and procedures; **Edit in SQL**
    gives `DROP ... IF EXISTS` and the definition between `DELIMITER`
    lines, named with its database.
  - Server shows the sessions (**Stop query** is `KILL QUERY`, **End
    session** `KILL`), the system variables and the sizes.
  - The row form offers `UTC_TIMESTAMP()` and `UUID()`.
- The Server tab reads PostgreSQL through UltraDatabase's new server
  administration too, the same calls MySQL's view uses.

#### 2026-10-24 *0.10*
- **Relations.** A new tab draws the tables of the schema picked in the tree
  and the foreign keys between them as an ER diagram, in crow's foot (each
  table a box of its typed columns with PK, FK and UK badges) or Chen. A
  link is drawn one-to-many, optional where a key column may be NULL, and
  one-to-one where the key is unique in its table; a table that refers to
  itself is a loop. The tables are laid out in layers, each beside or below
  the tables it refers to. A key into another schema is counted, not drawn.
  A table double-clicked opens in Browse.
- **Routines.** A new tab lists a schema's triggers, and on PostgreSQL its
  functions, procedures and aggregates, each with the statement that makes
  it. **Edit in SQL** puts that statement in the SQL tab to change and run
  again (a SQLite trigger is dropped and made again in one transaction);
  **New** starts a trigger, function or procedure from a template; **Drop**
  shows its SQL first, names a function with its arguments, and never
  cascades.
- **Server.** A new tab shows a PostgreSQL server's sessions - with **Stop
  query** and **End session** for the one selected - its settings, filtered
  by name or description, and the sizes of its databases and of a schema's
  tables; for SQLite, the file: its pages, size, journal mode, encoding,
  versions and whether foreign keys are checked.
- The tree's menu leads to them: **Relations** and **Routines** on a schema
  or a SQLite connection, **Server** (**The file**) on a connection.

#### 2026-10-24 *0.9*
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
- **New rows show in Browse before Commit.** A staged new row follows the rows
  read, marked `NEW`, with `(default)` for a column left to the database; the
  grid scrolls to it. A double-click opens its form again with what was
  staged, and Delete row (or the right-click menu) drops it - as edited cells
  show `*` and deleted rows `DEL`.

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
