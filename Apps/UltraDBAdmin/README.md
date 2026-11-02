# UltraDBAdmin

Browse and query the databases UltraDatabase reaches - SQLite files,
PostgreSQL servers and MySQL / MariaDB servers - in the spirit of
phpMyAdmin, as a desktop application - and, compiled to WebAssembly, in a
browser.
The design, the roadmap and the decisions behind it are in
[`Docs/Research/UltraDBAdminDesignProposal.md`](../../Docs/Research/UltraDBAdminDesignProposal.md).

This is **stage 1, a browser with a SQL console, and stage 2: editing rows
in Browse and the table's structure in Structure, importing rows and SQL
files, exporting tables, query results and whole schemas, and the
operations on a whole object - create, copy, rename, empty, drop - with
SQLite's maintenance; and from stage 4, a schema's relations as an ER
diagram, its triggers and routines, and the server's sessions, settings and
sizes; and stage 3, all of it on MySQL and MariaDB too; and stage 6, the
same application in a browser, through a relay on the server (below).** The console runs
whatever SQL it is given, so a connection marked *read only* (the database
refuses every change, and neither Browse nor Structure offers editing) is
the way to make it safe.

| | |
|---|---|
| **Connections** | SQLite files, PostgreSQL servers and MySQL / MariaDB servers (host or socket - a directory for PostgreSQL, the socket file for MySQL - port, 5432 or 3306 when empty, database, user, password, TLS posture) - directly or through an **SSH tunnel** (below), added and edited in a dialog, kept in `connections.json` in the data folder. A password is stored in the app's own encrypted vault (`vault/`, UltraVault's device-key vault) and never written to the list; the connection hands UltraDatabase only the vault key. *Production* draws a connection in red; *read only* makes the database refuse changes. |
| **Tabs** | Each table or view picked in the tree opens in a tab of its own, in a row above Browse, Structure, SQL, Search, Relations, Routines and Server; a click shows it again as it was left - its page, its order, its filters and the changes staged to it, which a tab marks with `*` and keeps until they are committed, reverted or the tab is closed (which asks first). Up to ten are open; the eleventh takes the place of the oldest with nothing staged. |
| **Tree menu** | A right-click on a connection, a schema or an object offers what can be done to it. A table: open it, its structure, export its rows, copy its name (quoted), **Copy table** (a new name, with or without its rows: SQLite from the table's own CREATE TABLE and its indexes, renamed; PostgreSQL `LIKE ... INCLUDING ALL`, the identity moved past the rows), **Rename**, **Empty table** (DELETE or TRUNCATE, the counter reset if asked), **Drop**, and its maintenance. A schema - and a SQLite connection, whose schema is `main` - : **New table** (a line per column: name, type, NOT NULL, key, numbered, default - checked as one SQL fragment each), **Search every table**, **Export as SQL**, **Relations**, **Routines**, and maintenance: SQLite's *Check integrity*, *Quick check*, *Analyze*, *Optimize*, *Vacuum* and journal mode; PostgreSQL's *Analyze* and *Vacuum* on a connection or a table. Each operation shows its SQL before it runs (`engine/DbAdminOperations`); none writes CASCADE; a read-only connection offers only what reads; the ones that remove something are red and ask. **Compare data…** on a table sets its rows beside another table's on any connection - another engine too - matched by the first one's primary key, the columns both have compared (a number by its value, so 10.50 is 10.5): how many are the same, the rows only here, only there and changed - a changed value shown as `here -> there` - and **Make the other the same…**, the INSERTs, UPDATEs and DELETEs that make the other table hold these rows, shown first and run as one transaction (`engine/DbAdminCompare`; the other table up to 500,000 rows). |
| **Tree** | Each connection's schemas, and in each its tables, views, materialized views, foreign tables and sequences (`UltraDb_ListSchemas`, `UltraDb_ListObjects`), read when the node is expanded. A server that cannot be reached says why in the tree; collapsing and expanding tries again, **Refresh** reconnects. |
| **Look** | White cards on a grey ground - the header with the connection buttons, the tree, the tabs - in the framework theme the app sets at start-up, its accent the blue of the app icon (`ui/DbAdminTheme`). Every button carries an icon from the app's own set (`media/icons/dbadmin`) and says by its look what it does: the one action a bar is for (**Commit**, **Apply**, **Run**, **Add connection**, a dialog's OK) in the accent, one that removes something in red. A label is the action itself, never followed by `…`; the tooltip says it in full. The tree shows a connection (red for production), a schema, a table, a view, a materialized view, a foreign table and a sequence each by its icon, an arrow to open a node (`TreeExpanderStyle::Chevron`), and a node being read by a bar along its foot. Errors, in the window and the dialogs, are the theme's red. |
| **Browse** | A table's rows 200 at a time, never the whole table: one bound `LIMIT ? OFFSET ?` query per page, in primary-key order or sorted on the server by a click on a column header (then the key), with the exact row count beside the previous and next page buttons. Every name in it is quoted by `UltraDb_QuoteIdentifier`. A table without a key says its order is not stable. A sequence shows its one row of state. **Filters** under the grid narrow the rows: a column, a comparison (contains, starts with, =, ≠, <, ≤, >, ≥, is NULL, is not NULL) and a value, one filter per column, all of them true - each a bound parameter, the value converted by the database to the column's type, so `100` compares as a number and `2026-01-31` as a date; "contains" matches the column's text in any case (`LIKE`, PostgreSQL's `ILIKE`, `%` and `_` in the value matched literally). The count, the pages and an export of the table follow them (`engine/DbAdminTablePager`). A table wider than the window scrolls sideways - the grid's horizontal scrollbar, Shift+wheel, or the Left / Right keys, which move the grid rather than the tabs while it has the focus. |
| **Editing** | In Browse, a double-click edits a cell in place - a field over the cell: Enter stages the value, Escape leaves it, a click elsewhere stages it too; the right-click menu sets a cell to NULL. **Add row**, **Edit row** (the row form) and **Delete row** stage a change; nothing is written yet. The grid marks it - an edited cell reads its new value followed by `*`, a row to be deleted starts with `DEL`, a new row follows the rows read with its first cell starting `NEW` (a column left to its default reads `(default)`; double-clicked, its form opens again; **Delete row** drops it) - and the bar counts it. **Preview SQL** shows the statements, **Commit** runs them all in one transaction (every `UPDATE` and `DELETE` must hit exactly one row, or nothing is written), **Revert** drops them. A row is addressed by its primary key as read, so a view, a sequence and a table without a key are not editable here, and say why. The row form has a field per column with **NULL** and, for a new row, **Default**; values are typed as text and the database converts them to the column's type. A commit that deletes, and any commit on a *production* connection, asks first; opening another table or closing the window with changes staged asks before dropping them (`engine/DbAdminEditBuffer`). |
| **Structure** | The table's columns (type, nullability, default, key position, identity / generated, collation, comment); its primary key, unique constraints, indexes, foreign keys and triggers; a view's definition (`UltraDb_DescribeTable`). |
| **Structure editing** | **Add column**, **Edit column** (or a double-click: name, type, NOT NULL, default), **Drop column**, **Add index**, **Drop index** and **Rename table** stage a change; the lists mark it (`NEW`, `DROP`, a changed value followed by `*`) and the bar counts it. **Preview SQL** shows the DDL, **Apply** runs it all in one transaction, **Revert** drops it. The statements run in a fixed order - drop indexes, change definitions under the old names, rename columns, drop columns, add columns, create indexes, rename the table - so none needs a name another has not made yet. PostgreSQL: `ALTER TABLE` throughout, a type change converting with `USING CAST(column AS type)` and the column's default dropped before it and set again after (one written for the old type, `'1'::text`, then fails with PostgreSQL's own advice and nothing is changed). SQLite: its own `ALTER TABLE` where it has one (rename, add and drop a column, rename the table); a change of a column's type, NOT NULL or default is SQLite's documented rebuild - a new table from the old `CREATE TABLE` with only that column's definition rewritten (CHECK, foreign keys, COLLATE, AUTOINCREMENT and WITHOUT ROWID kept as written), the rows copied with their rowids, the old table dropped, the new one renamed, its indexes, triggers and AUTOINCREMENT counter put back, with `foreign_keys` off around the transaction and `foreign_key_check` before it commits. SQLite's refusals are said when a change is staged, not at Apply (a NOT NULL column added without a default, dropping a key column or one an index uses). A type and a default are SQL, checked to be one fragment - no `;`, no comment, closed quotes and parentheses - before they are written in. A drop, a rebuild and anything on a *production* connection ask first; a SQLite table that changed since it was read is not touched; staged rows in Browse must be committed or reverted first (`engine/DbAdminStructureEdit`, `engine/DbAdminSqliteTable`). |
| **Import rows** | Browse's **Import** puts the rows of a file into the table shown. The file types are FileLoader's spreadsheet formats (`AddLoadTypes` with `Spreadsheet`: CSV, TSV, ODS, XLSX, XLS). Delimited text goes through the framework's Text Import dialog (`ShowCSVImportDialog`: encoding, separators, quote, decimal mark, preview) and is read with `UltraCanvasFileLoader::LoadFile` and the CSV engine; a workbook is read by the framework's spreadsheet, one grid per sheet. A form then gives each column of the table a column of the file, or leaves it to its default: by name when the first row names them (guessed when one of its cells is a column's name), else in order - a file one column short of the table leaves the numbered key out. It says whether an empty cell is NULL or empty text, refuses what cannot work (a NOT NULL column without a default left out, a computed column given a value) and shows the first 50 rows as they will be inserted. Every row is an `INSERT` with bound values, all in one transaction: when one fails, nothing stays, and the status names the file row (`engine/DbAdminImport`). A column of numbers - by value type, or by declared type, as PostgreSQL's `numeric` comes back as text - gets a cell's number (`1.234,50` from a German export is `1234.50`, grouping checked in threes, so `1,5` is not 15); a text column gets the text as written (`01067` stays `01067`). A workbook's dates and times are ISO 8601, TRUE / FALSE are 1 / 0 for a number column. A production connection asks first. |
| **Import SQL** | The SQL tab's **Import SQL** reads a `.sql` file - or `.sql.gz`, unpacked by `UltraCanvasFileLoader::LoadFile` - and says first what is in it: how many statements (split as SQLite and psql split them: not at a `;` in a quote, a comment, a PostgreSQL dollar quote or a trigger's body), and which of them DROP, DELETE, TRUNCATE, ALTER or UPDATE (`engine/DbAdminSqlScript`). It runs on the console's own session, so the statement list shows what each did and Stop cancels it - by default as one transaction, so a failure or Stop leaves nothing of the file; a file that begins and commits its own transactions (a SQLite `.dump`) runs as written. A file only psql can run - pg_dump's `COPY ... FROM stdin`, backslash commands - is refused with the reason. The file is not put in the history. |
| **Blobs** | A blob cell - double-clicked, or **View** in the right-click menu, in Browse and in the console's rows - opens a viewer that shows what the bytes are (read from their signature: PNG, JPEG, GIF, BMP, WebP, TIFF, SVG, PDF, ZIP, gzip, SQLite, text): a picture as a picture, text as text, anything else as a hex dump (`engine/DbAdminBlob`). **Save to file** writes the bytes as they are, under an extension that fits them, through `UltraCanvasFileLoader::SaveFile`; **Load from file** (Browse, where the row can be edited) stages a file's bytes for the cell, bound as a blob - SQLite's BLOB, PostgreSQL's bytea - and Commit writes them. A staged blob reads as its size followed by `*`. |
| **Copy rows** | Rows selected in Browse or in the console's result (Ctrl+click, Shift+click) are copied from the right-click menu as CSV (a header line, quotes where needed), JSON (an array of objects) or SQL `INSERT` statements - into Browse's table, or into the table the console's `SELECT` read; Ctrl+C copies CSV (`RowsText` in `engine/DbAdminExport`). |
| **Export** | Browse's **Export** writes every row of the table or view shown - every row its filters let through, in its order (`SELECT *`, every name quoted, the filters' values bound), read through a cursor 1 000 rows at a time (`UltraDb_OpenCursor`), so a table of millions of rows is never held; the status counts them as they go. The SQL tab's **Export rows** writes the rows shown below the console. The Save dialog offers FileLoader's spreadsheet writers (`AddSaveTypes` with `Spreadsheet`: ODS, XLSX, CSV, TSV) and the two this app writes itself, JSON and SQL; the extension chosen picks the format (`engine/DbAdminExport`). **CSV / TSV**: the framework's Text Export dialog (`ShowCSVExportDialog`: separator, quote, quoting, line ending, charset, BOM) previewed on the rows at hand, and its writer's quoting (`CSVFormatRecord`, `CSVEncodeFromUtf8`); NULL is an empty field, a blob `\x` hex. **JSON**: an array of objects, one per row and line; numbers as numbers, NULL as null. **SQL**: `INSERT` statements in the engine's dialect (SQLite `X'..'`, PostgreSQL `decode('..', 'hex')` for a blob) between `BEGIN` and `COMMIT`, after a SQLite table's own `CREATE TABLE` and `CREATE INDEX` statements, so running the file rebuilds the table; a PostgreSQL table's definition is pg_dump's to write, and the file says so. **Workbook**: written by the framework's spreadsheet - column names in bold, numbers as numbers (an exact numeric too, by its declared type; `01067` stays text), TRUE / FALSE, everything else as text and never as a formula - up to a sheet's 1 048 575 rows, and the status says when a table had more. A file is written beside its name and moved there when complete, so an export that fails, or is stopped by the window closing, leaves the file that was there. |
| **SQL** | A console that runs the selection, or the whole script (**Run** or Ctrl+Enter), or the statement under the caret alone (the button beside it, or Ctrl+Shift+Enter: it is selected as it runs), on the connection picked in the tree, and lists what every statement did (`UltraDb_ExecBatch`); a click on one shows its rows, sortable. **Explain** shows how the database would run the selection or the statement under the caret - `EXPLAIN QUERY PLAN` on SQLite, `EXPLAIN` on PostgreSQL, never `EXPLAIN ANALYZE`, which runs it. **Stop** cancels the running statement (`UltraDb_CancelQuery`). The history dropdown puts an earlier script back; **Snippets** keeps the editor's SQL under a name (`snippets.json` in the data folder) and puts one back. **Completion**: as a name is typed (from its second letter, or just after a `.`), or on Ctrl+Space, a list under the caret offers what fits there - after `FROM`, `JOIN`, `INTO` or `UPDATE` the tables and views of the schema picked in the tree (the engine's default one otherwise) and the other schemas; after `SELECT`, `WHERE`, `ON`, `SET` and the like the columns of the tables the statement names, and their aliases; after `alias.` or `table.` its columns, after `schema.` its tables - and the keywords, in the case typed; Up / Down and Enter or Tab take one, Escape closes it (`engine/DbAdminCompletion`). A name is offered as it must be written, quoted where the engine needs it. The names are read once per connection and schema, and again after Refresh or a script. |
| **Search** | The Search tab looks for a text in every table and view of the schema picked in the tree: *contains* (anywhere in a value, in any case: `LIKE`, PostgreSQL's `ILIKE`, `%` and `_` matched as themselves) or *is exactly*. One query per table counts the rows that hold it in each column, the text bound once per column; blobs are not searched. The tables are searched one by one on the window's worker, so **Stop** acts between tables and a Browse page waits for one table at most (`engine/DbAdminSearch`). A hit, double-clicked, opens the table filtered on that column. |
| **Relations** | The Relations tab draws the tables of the schema picked in the tree and the foreign keys between them, read through the schema introspection (`UltraDb_ListObjects`, `UltraDb_DescribeTable`; `engine/DbAdminRelations`) and drawn by the framework's `UltraCanvasERDiagram`. **Crow's foot**: each table a box of its columns with their types (**Column types** hides them) and `PK`, `FK` and `UK` badges, `*` for NOT NULL; a link from the table holding the key to the one it refers to, named by its columns - exactly one at the parent's end, or none-or-one when a key column may be NULL; many at the child's, or none-or-one when the key is unique in the child (its whole primary key, or a unique index); a table that refers to itself a loop. The tables are laid out in layers, each below or to the right of the tables it refers to - across a wide tab, down a tall one - and the tables with no link in a last layer of their own. **Chen**: the same model with each link a diamond and each column an oval, placed by the diagram's force layout. A key to a table in another schema is counted, not drawn; 120 tables at most are read. A table double-clicked opens in Browse. Wheel zooms, a drag pans, **Fit** shows it all; Delete removes nothing. |
| **Routines** | The Routines tab lists the schema's triggers (both engines) and on PostgreSQL its functions, procedures, aggregates and window functions - not those an extension installed - with the statement that makes each as the database reports it (`sqlite_master`, `pg_get_triggerdef`, `pg_get_functiondef`; `engine/DbAdminRoutines`). **Edit in SQL** (or a double-click) puts the statement in the SQL tab to change and run: PostgreSQL's `CREATE OR REPLACE` as it is, a trigger's `CREATE TRIGGER` made `CREATE OR REPLACE TRIGGER`; a SQLite trigger, which cannot be replaced, behind its `DROP TRIGGER` in one transaction. **New** starts a trigger (on the table picked in the tree, if one is), a function or a procedure in the SQL tab from a template that runs as it is. **Drop** shows its `DROP` first - a function named with its arguments, so one overload goes and the other stays; never CASCADE, so a function a trigger calls is refused while the trigger is there. The tab reads them again each time it is shown. |
| **Server** | The Server tab shows the connection picked in the tree (`engine/DbAdminServer`). PostgreSQL: **Sessions** - every client session (`pg_stat_activity`): its process id, user, database, application, client, state, how long its query has run, what it waits on and the query - with **Stop query** (`pg_cancel_backend`) and **End session** (`pg_terminate_backend`) for the one selected, each shown first and refused for the tab's own session; **Settings** (`pg_settings`: value, unit, category, description, source, a restart pending), narrowed by a text in the name or description, in any case, the text bound and `%` and `_` in it matched as themselves; **Sizes** - each database, then each table of the picked schema with its data and its indexes; **Monitor** - line charts of the server, read every two seconds while they are shown (`engine/DbAdminMonitor`): sessions connected and running a statement, transactions, rows read and written a second, and the share of page reads the buffer cache served; on MySQL / MariaDB also statements a second and the traffic received and sent. Four minutes of each are kept; **Refresh** starts them again, and the same server's charts carry on where they were when the tab is shown again. SQLite: the file - page size, pages, free pages, size, path, journal mode, encoding, auto-vacuum, `user_version`, `application_id`, whether foreign keys are checked, and the library's version. A read-only connection stops no session. |
| **SQL log** | Under the tabs, every statement the app ran - a Browse page, a count, a commit, an import, an export's cursor, an operation, the console's statements - with its time, its connection (the console's marked *SQL*), how long it took, its rows or what it changed, and its error. Filled by UltraDatabase's statement observer (`UltraDb_AddStatementObserver`, `engine/DbAdminSqlLog`); bound values are never logged, only how many there were. *Schema reads* adds the tree's and Structure's catalog reads. **Copy** gives the selected statements - or all - as SQL, each under a comment line; a double-click puts one in the SQL tab. The newest 2 000 are kept. |
| **Auto-commit** | A connection can be set to write a row change as soon as it is made, as phpMyAdmin does, instead of staging it for **Commit**. A deletion, and any change on a production connection, still ask first. |

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

## SSH tunnels

A server behind a bastion is reached through an SSH tunnel: **Through an SSH
tunnel** in the connection dialog, then the SSH server and its port (22 when
empty), the SSH user and how to sign in there - the SSH agent and the default
keys, a password, or a private key file, read through
`UltraCanvasFileLoader` into the vault with its passphrase. Host and port are
then the database's as the bastion reaches it (an empty host: the bastion
itself). The password or key and the passphrase are kept in the app's vault
beside the database password, never in `connections.json`.

On the first connection the bastion's host key is not known: the app shows
its fingerprint (`SHA256:...`) and asks to trust it - as ssh does - for the
user to compare with the one the bastion's administrator gave them; trusted,
it is kept with the connection, and a bastion that presents another key is
refused. **Forget the trusted host key** (a reinstalled server) and a new
bastion host clear it. Through the tunnel MySQL can check no certificate
against the database's name, so *Verify* is refused there with that reason;
PostgreSQL still checks it against the database's host. Every session -
Browse's, the SQL console's - goes through the connection's tunnel, which
closes with it. The tunnel is UltraDatabase's (`UltraDatabaseTunnel.h`),
opened through UltraNet's SSH plug-in (`UltraDb_UseUltraNetSshTunnels`).

## The browser build and its relay

The same application runs in a browser, compiled to WebAssembly
(`UltraDBAdmin.html`), with every database call going to
**ultradbadmin-relay** on the server - a small headless program beside the
databases (proposal §8.1). The page holds no password and names no host: the
relay offers the connections configured on its side, by name, to the users
its tokens name, and the browser reaches only those.

```
browser ── UltraDBAdmin.wasm ──WebSocket──► TLS proxy ──► ultradbadmin-relay ──► PostgreSQL / MySQL / SQLite
```

In the app a relay connection is a connection like the others - every tab
works through it, Stop and the server's sessions too - made by **Relay**
in the header: the relay's address (`wss://db.example.com/relay`) and the
user's token - or **Sign in**, where the relay offers its sign-in (below) -
then one connection per name the relay offers that user, shown as
`name on wss://...`. The token is kept where a password goes - the
app's vault on a desktop, the page's memory in a browser, so a page that is
reloaded asks for it again - and a connection the relay offers *read* only
is read only in the app, and in the database. The desktop app reaches a
relay too, through UltraNet's WebSocket (a libcurl built with WebSocket
support, 7.86 or later).

The browser build opens its relay at start-up - `?relay=wss://...` in the
page's address, else `wss://` the page's own host `/relay` - and has no
**Add connection**: no SQLite file from the visitor's disk, no server of its
own, no SSH tunnel (the relay is already on the inside).

### Running the relay

```
ultradbadmin-relay --hash-token < token.txt          # its token_sha256 for the file
ultradbadmin-relay --config relay.ini --set-secret crm   # a password from stdin into the relay's vault
ultradbadmin-relay --config relay.ini --check        # what it offers, to whom
ultradbadmin-relay --config relay.ini                # serve
```

`relay.ini` (`relay/RelayConfig.h` has every key):

```ini
listen = 127.0.0.1:8765               ; loopback: TLS is the proxy's
path = /relay
vault = /var/lib/ultradbadmin-relay/vault
audit = /var/log/ultradbadmin-relay/audit.log
origins = https://db.example.com      ; the pages allowed to connect

[connection crm]
driver = postgresql                   ; postgresql, mysql or sqlite
host = db.internal
database = crm
user = crm_app
password_secret = crm                 ; --set-secret crm; a password here is refused
tls = verify-full

[user anna]
token_sha256 = 3f2a...                ; --hash-token
crm = write                           ; read, write; a connection not named: none

[limits]
rows_per_page = 1000
requests_per_minute = 1200
statement_timeout = 300               ; seconds a request may run; 0: no limit
```

A token is at least 24 characters - a long random one, given to its user
the way a password would be; the file keeps only its SHA-256. *read* opens
the connection in the driver's read-only mode, so the database refuses
every change whatever the page sends. The limits cap the rows of a page and
of a result, a user's requests a minute and sessions, and how long a
request may run: a query, a script or a cursor's page still running after
`statement_timeout` seconds (300 unless set) is stopped on the database and
answered *stopped after 300 s*; the audit log has a
line per statement - time, user, address, connection, whether it worked,
the SQL, with the visitor's address (the last one the proxy in front puts
in `X-Forwarded-For`). The relay runs where it reaches its databases: it
opens no SSH tunnel, and an `ssh_` option is refused.

### Signing in with an account

Instead of handing out tokens, the relay can sign its users in with an
OpenID Connect provider - the company's Keycloak or Entra ID, Google, any
provider that publishes a discovery document - and the page and the desktop
app then show **Sign in** beside the token:

```ini
public_url = https://db.example.com/relay   ; how browsers reach the relay

[signin]
label = Example SSO                   ; "Sign in with Example SSO"
issuer = https://sso.example.com/realms/staff
client_id = ultradbadmin
client_secret_secret = sso            ; --set-secret sso; leave out for a public client
scopes = openid email                 ; the default
ticket_hours = 8                      ; the default

[user anna]
email = anna@example.com              ; the provider's verified email...
crm = write

[user ben]
subject = 248289761001                ; ...or its subject (sub), for a provider that verifies no email
crm = read
```

Register the relay at the provider as a web application whose redirect URI
is `<public_url>/callback` (`https://db.example.com/relay/callback`). The
relay is the provider's client, so no token of the provider ever reaches the
browser: the page goes to `<public_url>/signin`, the relay sends it on to
the provider with PKCE and a nonce, takes the code back at `/callback`,
exchanges it, checks the ID token - its signature against the provider's
published keys (RS256 or ES256), its issuer, audience, expiry and nonce -
and finds the user by subject, or by email when the provider verified it.
The page then comes back with a ticket in its address's fragment, which no
server and no log sees and which the page takes off the address at once; a
ticket is good for `ticket_hours`, and kept by the relay as its SHA-256 only.
The desktop app does the same through the system browser and a loopback
address of its own. A sign-in may only return to `public_url`'s own origin,
the `origins` configured, or this computer. The audit log has a line for each
sign-in, made or refused, and why. The provider is read at start-up
(`--check` shows it), and its keys again when it rolls them.

### Behind a proxy

The relay speaks plain WebSocket and serves `GET /health`. It listens on
loopback and leaves TLS to the web server in front - the one that serves
the page, so page and relay share an origin:

```nginx
location /relay {
    proxy_pass http://127.0.0.1:8765;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_read_timeout 1h;
}
```

A `listen` address other than loopback is refused unless `plain_ok = true`
says the network is trusted. The page itself is `UltraDBAdmin.html` with
its `.js`, `.wasm` and `.data`, served with the cross-origin isolation
headers every threaded UltraCanvas page needs
(`Cross-Origin-Opener-Policy: same-origin`,
`Cross-Origin-Embedder-Policy: require-corp`;
`UltraCanvas/OS/WASM/README.md`).

### Building the page

As the other WebAssembly pages (`UltraCanvas/OS/WASM/README.md`: emsdk and
the wasm sysroot), with the app switched on:

```
emcmake cmake -B build-wasm -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_ULTRADBADMIN=ON
cmake --build build-wasm --target UltraDBAdmin
```

The page carries UltraDatabase with its SQLite core only (Emscripten's
port, which the module is built with), and opens no database itself;
libpq and libmariadb are in the relay alone.

## MySQL and MariaDB

A MySQL or MariaDB connection goes through UltraDatabase's driver over
MariaDB Connector/C, which speaks to both; a MySQL database is a schema in
the tree. Every name is quoted with backticks (`UltraDb_QuoteIdentifier`),
and where the engines differ, each tab says it MySQL's way:

- **Browse and Search**: "contains" is `CAST(column AS CHAR) LIKE ? ESCAPE '!'`
  - `!` rather than a backslash, which a server in `NO_BACKSLASH_ESCAPES`
  mode reads differently - in any case as the column's collation compares;
  Search's "is exactly" compares with `BINARY`, so case counts.
- **Structure**: a column whose type, NOT NULL or default changes is
  restated with `MODIFY COLUMN`, saying its `AUTO_INCREMENT`, collation and
  comment again (an `ON UPDATE` clause is not kept, and the preview says
  so); an index is dropped with `DROP INDEX ... ON` its table. MySQL commits
  before and after every `ALTER TABLE`, so Apply runs the steps one by one,
  and a failure leaves the steps before it done - the preview says that too.
- **Operations**: **Empty table** is `TRUNCATE` (the counter reset) or
  `DELETE`; **Rename** is `RENAME TABLE`, for a view too; **Copy table** is
  `CREATE TABLE ... LIKE` and `INSERT ... SELECT` without the generated
  columns (no foreign keys come with it, and the plan says so). The
  maintenance - *Check integrity* (`CHECK TABLE`), *Quick check*, *Analyze*,
  *Optimize* - is on a table, as each statement names one. DDL runs outside
  a transaction, which MySQL would commit anyway.
- **SQL**: a script is split as the mysql client splits it -
  `DELIMITER` lines, a `BEGIN ... END` body, `#` and `-- ` comments,
  backslash escapes and backticks - by the same splitter UltraDb_ExecBatch
  runs it with (`UltraDb_MysqlScriptStatements`), so Import SQL's count and
  the statement under the caret are the ones that run. **Explain** is
  `EXPLAIN`.
- **Export and the dump**: strings escaped as mysqldump escapes them, a blob
  as `X'..'`; a table's export writes its `SHOW CREATE TABLE` first. **Export
  as SQL** on a database writes, as mysqldump does, `SET FOREIGN_KEY_CHECKS = 0`,
  each table's `SHOW CREATE TABLE` and rows, its triggers between
  `DELIMITER ;;` lines, then the views - every name without the database and
  no `DEFINER` clause, so the file loads into whichever database is in use,
  as any user. Procedures, functions and events are mysqldump's to write.
- **Routines**: triggers, functions and procedures with their parameters.
  **Edit in SQL** gives `DROP ... IF EXISTS` and the definition between
  `DELIMITER $$` lines, named with its database and without its `DEFINER`, as
  MySQL replaces none in place; the templates do the same, a function
  `DETERMINISTIC` (binary logging refuses one that says nothing).
- **Server**: **Sessions** from the process list (without the `PROCESS`
  privilege, the user's own), **Stop query** `KILL QUERY` and **End
  session** `KILL`; **Settings** - MariaDB's system variables with their
  descriptions, MySQL's with where each value came from; **Sizes** from
  `information_schema.TABLES`. Every tab reads the server through
  UltraDatabase's server administration (`UltraDb_ServerSessions`,
  `UltraDb_ServerSettings`, `UltraDb_ServerSizes`, `UltraDb_StopSession`,
  and `UltraDb_ServerStatus` for the **Monitor**), PostgreSQL's too.
- **Row form**: `UTC_TIMESTAMP()` and `UUID()` among the value functions.

## Data folder

`$XDG_DATA_HOME/UltraDBAdmin` (`~/.local/share/UltraDBAdmin`),
`~/Library/Application Support/UltraDBAdmin` or `%LOCALAPPDATA%\UltraDBAdmin`,
readable by its owner alone:

- `connections.json` - the connection list, no secrets;
- `snippets.json` - the console's saved snippets;
- `vault/` - the passwords, encrypted (`dbadmin.vault`, unlocked by the
  device key beside it, as UltraMail's and UltraPress's vaults are).

## Layout

```
Apps/UltraDBAdmin/
├── main.cpp                      the application, the window, the event loop
├── engine/                       no UI - compiled headless by Tests/UltraDBAdmin
│   ├── DbAdminBlob               what a blob is, by its bytes; its hex dump
│   ├── DbAdminCompare            two tables' rows side by side, by key; the SQL that makes the second the same
│   ├── DbAdminCompletion         the SQL tab's completion: a connection's names, and what fits at the caret
│   ├── DbAdminConnections        the connection list and its vault
│   ├── DbAdminDump               a whole schema or database as one SQL file; a PostgreSQL table's definition
│   ├── DbAdminEditBuffer         staged row edits, their SQL, the commit
│   ├── DbAdminEngine             the SQL dialect a connection speaks
│   ├── DbAdminExport             rows to CSV, JSON, SQL through a cursor; a workbook's cells
│   ├── DbAdminFormat             a value as one line of a grid cell
│   ├── DbAdminImport             a file's rows mapped onto a table, inserted all or none
│   ├── DbAdminJobQueue           the worker the window's queries run on
│   ├── DbAdminOperations         create, copy, rename, empty, drop; maintenance; the journal mode
│   ├── DbAdminPaths              the data folder
│   ├── DbAdminRelations          a schema's tables and the links between them, in layers
│   ├── DbAdminRoutines           triggers, functions, procedures: listed, edited, dropped, templates
│   ├── DbAdminSearch             a text in every table: a count per column
│   ├── DbAdminServer             a server's sessions, settings and sizes, as the grid shows them; SQLite's file
│   ├── DbAdminMonitor            the Server tab's charts: a server's status read again and again, as rates
│   ├── DbAdminSnippets           the saved snippets
│   ├── DbAdminSqlLog             every statement the app ran
│   ├── DbAdminSqlRunner          a script on a worker thread, Stop, history, one transaction
│   ├── DbAdminSqlScript          a SQL file read before it runs: statements, risks, psql-only parts; MySQL's split
│   ├── DbAdminSqliteTable        a SQLite CREATE TABLE read and rewritten, for the rebuild
│   ├── DbAdminStructureEdit      staged structure edits, their DDL per engine, Apply
│   ├── DbAdminTablePager         the Browse view's pages and filters
│   └── DbAdminValueTypes         what a column takes; the value functions it can be set to
├── relay/                        ultradbadmin-relay, the browser build's server half
│   ├── main.cpp                  the program: --config, --check, --hash-token, --set-secret, the audit log
│   ├── RelayConfig               its INI file
│   ├── RelaySignIn               signing in with an OpenID Connect provider: the redirects, the ID token's checks (its RS256 / ES256 signature through UltraCrypt), tickets
│   └── RelayWebSocket            its WebSocket listener (RFC 6455), /health, the sign-in's pages
├── web/                          the browser build only
│   ├── DbAdminRelayWeb           every relay connection through the browser's WebSocket; the relay's address
│   └── shell.html                the page around the canvas
└── ui/
    ├── DbAdminWindow             tree, tabs, status line, Browse, Structure, SQL
    ├── DbAdminWindowObjects      the tabs of open objects, the tree's menu and operations, Search, the SQL log
    ├── DbAdminWindowSchema       the Relations (an ER diagram), Routines and Server tabs
    ├── DbAdminWindowParts        what the window's three sources share: sizes, layout helpers
    ├── DbAdminOperationDialogs   an operation's SQL first; the copy, rename, empty and dump forms; a new table; the compare and the relay sign-in
    ├── DbAdminTheme              the theme, the icons, the buttons, cards and bars every page uses
    ├── DbAdminModels             list models over result sets
    ├── DbAdminRowDialog          the row form (a field per type, NULL, default, a function), the SQL preview
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
INSERTs, a blob's kind and hex dump, and the table a SELECT reads. On
MySQL / MariaDB, where `ULTRADB_TEST_MYSQL_*` name a server (CI runs MySQL
8; the suites also pass on MariaDB 10.11): Browse's filters (`%`, `_`, `!` and
a backslash matched literally, a number searched as text) and a blob with a
backslash and a quote in it written and read back, and the structure edits
(`MODIFY COLUMN` keeping `AUTO_INCREMENT` and the comment, `DROP INDEX ...
ON`, each step on its own, a conversion the data refuses stopping before the
rename), and a MySQL script's statements counted past `DELIMITER`, `#`
comments, escapes and backticks.

A second program, `UltraDBAdminOperationsTests`, has the rest: the SQL log
(every statement, the app's connections only, no bound value in it), the
operations on SQLite and PostgreSQL (a table created, copied with its indexes,
generated columns and rows and its counter going on, renamed, emptied with
its counter reset, a referenced table neither truncated nor dropped, dropped;
the integrity check, analyze, optimize, vacuum, the journal mode set to WAL;
a failing plan leaving nothing), the search (counts per column, `%` and `_`
literal, a blob left out, ILIKE, a date and a boolean read as text), both
schema dumps read back into an empty database exactly - rows with quotes,
`;`, `--`, line breaks and blobs, counters, CHECK, UNIQUE, foreign keys,
identity, serial and generated columns, jsonb, bytea, comments, views,
indexes, triggers - and again with DROP first, structure only, data only, a
stopped dump leaving the file that was there; the snippets, the statement
under the caret, EXPLAIN, the row form's checks and value functions, and
auto-commit kept with a connection. On MySQL / MariaDB: the operations (an
`AUTO_INCREMENT` table created outside a transaction, copied with its index
and generated column, truncated, renamed, its four maintenance reports, a
referenced row neither deleted nor its table dropped), the search (any case,
`BINARY` for exactly, `%` and `_` literal, a blob left out) and the dump of a
database read back into an empty one (a backslash, a newline, quotes and a
`;` in a value, a blob, a generated column, the counter, the foreign key, the
trigger, the view, the table's comment), and again with DROP first.

A third, `UltraDBAdminSchemaTests`, has stage 4's: the relations of a
schema on both engines (a required one-to-many, a one-to-one by a primary
key, by a unique column and by a two-column key, a table that refers to
itself, a key to a table not read or in another schema counted, the limit)
and their layers; the routines (a SQLite trigger edited, run and dropped;
PostgreSQL's trigger, overloaded functions, procedure and aggregate listed,
edited and run again, one overload dropped and the other kept, a function a
trigger calls refused, new ones from their templates, an extension's
functions left out); and the server views (SQLite's file in WAL mode with
its `user_version`; PostgreSQL's sessions with this one among them, a
running `pg_sleep` stopped from another session, the settings filter in any
case and with `%` typed, every setting, the sizes) - and all three again on
MySQL / MariaDB: the relations of a database written with `FOREIGN KEY`
clauses (MySQL 8 ignores a column's `REFERENCES`), the routines (a trigger
edited between `DELIMITER` lines and the edit the one that fires, a function
edited where it lives and dropped by its name, a procedure, the templates),
and the server (the sessions, `max_connections` found in any case, a
`SLEEP` stopped with `KILL QUERY`, a session not there not stopped).

A fourth, `UltraDBAdminTunnelTests` (built where UltraNet's SSH plug-in is),
connects through a real SSH tunnel: the app's own connection store with the
bastion's key in the vault, a PostgreSQL and a MySQL / MariaDB server reached
through a local sshd over TCP - the unknown host key refused with its
fingerprint, trusted, a query, a cursor's 2000 rows, the SQL console's own
session, a changed key refused, MySQL's *Verify* not attempted. CI runs it
against an sshd of its own (`ULTRANET_TEST_SSH_*`, `ULTRADB_TEST_TUNNEL_*`).
The engine suite checks the store's side: the bastion and host key in the
list, the key and passphrase in the vault only, and what UltraDatabase is
handed.

A fifth, `UltraDBAdminRelayTests`, runs the relay on this computer: its
configuration file read and what it refuses (a password in the file, a
network address without TLS, a token hash that is not one, a grant of a
connection not configured, an SSH option), the audit log's address behind a
proxy, its WebSocket listener on a free port (`/health`, another path, a
plain GET, an origin not allowed), and the app's own engine on a relay
connection - the token kept where a password goes, a Browse page and its
count, the completion catalog, a console script - against a SQLite file the
relay serves, and on PostgreSQL where `ULTRAFIBU_TEST_PG_*` names a server:
a read-only grant refusing a change, the server's status, a query stopped
through the relay. Where the relay has its sign-in (UltraCrypt with its
OpenSSL 3 backend, which checks the token's signature): the ID
token's checks one by one - RS256 and ES256 signatures, a changed token,
another issuer, another application, several audiences, an expired token,
another sign-in's nonce, `none` and HS256, a key the provider does not
publish - who a token is, where a sign-in may return, and the whole sign-in
against a provider of the test's own on 127.0.0.1: PKCE, the ticket in the
fragment or a desktop's loopback query, the ticket then opening the user's
connections, a replayed callback, a stranger, a refused sign-in and a return
elsewhere turned away, each in the audit log. UltraDatabase's own
`test_relay` checks the wire format, the statement time-out (a query, a
script and a cursor's page that never end) and a program's own sign-in
without a socket.

The browser build is tested end to end by the WebAssembly workflow's
`dbadmin-browser` job (`Tests/UltraDBAdmin/e2e/run_e2e.sh`): the baseline's
`UltraDBAdmin.html` in Chrome against a relay built natively and a test
OpenID provider (`test_oidc_provider.py`, the Python standard library and
the openssl command) - the page signs in through the relay, browses a table
and runs a query the time-out stops, and the relay's audit log is checked
for each step. It runs the same on a developer's computer:
`run_e2e.sh <ultradbadmin-relay> <folder with the page> <out>`.
