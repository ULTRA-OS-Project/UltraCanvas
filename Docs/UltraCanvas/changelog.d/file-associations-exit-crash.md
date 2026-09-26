- **A program could crash on exit after using file associations.** The
  association service's worker thread resolves applications and icons in the
  background; the service's destructor joins it, but only once the current
  lookup ends. The statics that lookup used — the desktop icon cache on Linux
  (`FindDesktopIconFile`), the icon cache directory on Windows and macOS, the
  sweep's extension list — were function-local statics first built on that
  thread, so they were destroyed *before* the service and freed under the
  running lookup. `FilerNameEncodingTest` printed `ALL PASSED` and then
  crashed in CI. They are now allocated once and never destroyed. The Linux
  backend's MIME/application index is a namespace-scope global, built before
  the service, and so already outlived it.
- **More statics that background threads use now outlive them at exit.** An
  audit of every library thread that can still be running at exit found the
  same pattern in five more places. Each of these is now allocated once and
  never destroyed:
  - the NetworkMonitor name table and name listeners, used by the name-source
    workers;
  - its connection attribution, event listeners and recent-event ring, used
    by the event-source workers;
  - UltraDatabase's handle, prepared-statement and transaction tables and
    their mutex, which UltraMessage broker sessions journal through; the
    broker is stopped by an `atexit` handler that is registered before these
    tables are first built;
  - `JSONValue::NullValue()`, returned by every missing-key lookup, including
    those on UltraMessage threads;
  - UltraNet's c-ares channels (the default channel and the per-server-list
    map) and the empty server list, used by detached async DNS lookups. The
    per-server-list channels' comment already said "leaked on purpose", but
    the map destroyed them at exit.
