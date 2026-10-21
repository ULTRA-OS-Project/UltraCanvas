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
