- **UltraDatabase: a connection closed at exit wrote into freed memory.**
  The manager's table of retired connections was a function-local static,
  first built by the first `UltraDb_CloseConnection`. UltraMessage's atexit
  handler - registered earlier - closes its journal after that table was
  already destroyed, and the process aborted in `free()` as it exited
  (`malloc_consolidate(): unaligned fastbin chunk`). The table is now never
  destroyed, like the handle tables. `UltraDatabaseExitClose`, a test
  program of its own, closes a connection from such a handler under glibc's
  strict heap checks.
