- **UltraDatabase 0.3.1: re-registering or closing a connection no longer
  frees it under a running call.** `UltraDb_RegisterConnection` and
  `UltraDb_CloseConnection` dropped the registry's entry, and with it the
  physical connection. Every query, script, schema and quoting call held
  only a raw pointer to it, so the call running at that moment on another
  thread ran on freed memory. AddressSanitizer reports a heap-use-after-free
  in the SQLite driver; without a sanitizer the call hung, or failed when it
  next touched the connection. A PostgreSQL call lost its socket.
  - Each call now holds the connection until it returns. The old connection
    closes after its last call and its last open handle; calls that start
    later use the new registration.
  - Handles get the same treatment. A prepared statement or cursor closed
    with `UltraDb_Finalize` / `UltraDb_CloseCursor` while another thread
    runs it now finishes that call first. A transaction keeps its connection
    for the call even when `UltraDb_Commit` runs on another thread.
  - `Tests/UltraDatabase/test_lifetime.cpp`: six tests (SQLite, and
    PostgreSQL where a server is configured) that replace or close a name
    or a handle while a call on it runs. On the old code they report the
    use-after-free under ASan and hang without it.
