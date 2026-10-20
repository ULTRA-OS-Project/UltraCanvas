- **UltraDatabase 0.2.0: schema introspection, identifier quoting and
  result-column metadata** - stage 0 of UltraDBAdmin
  (`Docs/Research/UltraDBAdminDesignProposal.md`). New
  `UltraDatabaseSchema.h`: `UltraDb_ListSchemas`, `UltraDb_ListObjects`
  (tables, views, materialized views, foreign tables, sequences) and
  `UltraDb_DescribeTable` (columns with type, nullability, default, identity
  / serial, generated, collation and comment; primary key; foreign keys with
  their actions; indexes, expression indexes included; triggers; a view's
  definition; size and row estimate), for SQLite and PostgreSQL, every name
  bound rather than spliced. `UltraDb_QuoteIdentifier` /
  `UltraDb_QuoteQualifiedName` quote a table or column name by the engine's
  rules and refuse an empty name or a NUL byte - the one sanctioned way a
  name goes into SQL text. `UltraDbResultSet::ColumnInfo()` gives each
  result column its declared type and, for a column read from a table, the
  schema, table and column it came from (PostgreSQL: cached per connection,
  emptied by DDL, looked up under a savepoint inside a transaction; SQLite:
  where the library has column metadata, checked at configure time).
  `UltraDb_ListConnections` lists the registered names. Driver interface:
  `IUltraDbConnection` gains `QuoteIdentifier`, `ListSchemas`,
  `ListObjects` and `DescribeTable`, with defaults, so existing drivers
  still build.
- **PostgreSQL `numeric` is exact.** It came back as a double, so a
  `numeric(20,4)` of `12345678901234.5678` read rounded; it is now `Text`,
  as stored, and `AsDouble` still converts it.
