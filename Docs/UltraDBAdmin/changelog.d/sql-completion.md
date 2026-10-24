- **Completion in the SQL tab.** As a name is typed, or on Ctrl+Space, a
  list under the caret offers what fits there: the tables and views after
  `FROM` and `JOIN`, the columns of the statement's tables after `SELECT` and
  `WHERE`, an alias's or a table's columns after `alias.`, a schema's tables
  after `schema.`, and the keywords. Names are offered as they must be
  written - quoted where the engine needs it - and nothing is offered inside
  a string or a comment.
