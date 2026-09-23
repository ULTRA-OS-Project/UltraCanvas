- **Connection events carry the loopback chain.** `NetworkConnectionEvent`
  gains `loopbackRole`, `localPeer` and `forProcesses`, as on
  `NetworkConnection`: the registry fills them from the socket table it
  already attributes from (decoded by `NetworkMonitor_ListConnections`),
  remembers them with the process so a Closed carries what its Opened
  had, and the snapshot differ fills them from its own table and keeps
  a closing connection's chain from the read that still saw the peer's
  socket owned (`chainDecoded` says a source did). A tuple the table
  lacks - a connection younger than the table, as often as not - makes
  the registry read the table again, at most every 20 ms, which also
  attributes conntrack's NEW events better. The store records them with each
  event (schema version 5, migrated in place), its text filter matches
  the peer and the `for` list, and the events CSV gains `loopback_role`,
  `local_peer` and `for`. NetworkMonitor 0.9.
