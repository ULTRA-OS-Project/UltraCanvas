- **A JMAP sync keeps its connection.** UltraMail asks UltraNet to pool
  HTTP connections per thread (`UltraNetConfig::reuseHttpConnections`), so
  a mailbox sync reuses one connection to the server instead of a fresh
  TCP and TLS handshake for every call - what "Fix email dates in the JMAP"
  set out to do, now switched on by UltraMail alone rather than for every
  program.
