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
