- **UltraDatabase 0.4.0: statement observers.** `UltraDb_AddStatementObserver`
  reports every statement that runs through the public calls, on any
  connection, once it has ended: its kind (query, exec, transaction, one
  statement of a script, a cursor, introspection), its text, how many values
  were bound - never the values themselves - the rows it returned, whether it
  ran in a transaction, its time and its outcome. It is called on the thread
  that ran the statement, outside every lock of the module, so an observer may
  run statements of its own. With no observer added, nothing is timed or
  copied. `UltraDb_RemoveStatementObserver` stops it. UltraDBAdmin's SQL log is
  the first observer. Tested in `Tests/UltraDatabase/test_observer.cpp`.
