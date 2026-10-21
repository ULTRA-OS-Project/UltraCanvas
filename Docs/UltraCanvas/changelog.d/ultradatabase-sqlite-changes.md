- **UltraDatabase: a SQLite statement that changes no rows reports none.**
  `sqlite3_changes()` and `sqlite3_last_insert_rowid()` keep the last INSERT,
  UPDATE or DELETE's figures until the next one, and the SQLite driver handed
  them on for every statement - so in a script, a `CREATE TRIGGER`, a
  `CREATE VIEW` or a `SELECT` after an INSERT reported the INSERT's "2 rows
  affected" (UltraDBAdmin's SQL console showed it so), and `lastInsertId`
  the INSERT's row id. The driver now reports a statement's counts only when
  it changed rows itself, as the connection's running total shows; an
  INSERT's own count still leaves out what its triggers changed.
  PostgreSQL was right already: each result carries its own count.
