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
