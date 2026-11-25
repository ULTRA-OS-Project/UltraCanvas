- **UltraMessage: a bus under a long runtime folder is reached (Linux).** A
  Unix socket's address holds a path of 107 bytes, and the bus lies at
  `$XDG_RUNTIME_DIR/ultramessage/bus.sock`. With a runtime folder long enough
  to pass that - a session's own, a sandbox's, a test's - no broker started
  and no endpoint connected (`bus path too long for a Unix socket`), and the
  only trace was in the debug log. What a person saw: a second start of a
  single-instance application could not ask the running copy to come forward,
  waited, and said "already running" instead; notifications and everything
  else on the bus were silently off too. Such a socket is now bound and
  connected through its folder - the folder is opened, which takes a path of
  any length, and the socket named as `/proc/self/fd/<n>/bus.sock`
  (`UltraCanvasUnixSocketAddress.h`). The bus stays exactly where its path says;
  nothing is moved to a shorter place. A client that finds the folder not
  there yet reads it as "no broker", as on a short path.
  - macOS and the BSDs have no such way in, and still refuse a path past
    their 103 bytes with the same message; `Docs/Modules/UltraMessage/README.md`
    says so.
  - `Tests/UltraMessage/test_ultramessage.cpp` hosts a broker on a bus path of
    some 150 bytes and has one endpoint find and ask another across it.
