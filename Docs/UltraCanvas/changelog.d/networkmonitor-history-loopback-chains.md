- **The activity store keeps loopback chains.** A recorded flow carries
  the chain its sightings decoded - `RecordedFlow::loopbackRole`,
  `localPeer` and `forProcesses`, the same as on `NetworkConnection` - so
  "what did the mail client fetch on Tuesday" has an answer although the
  mail server only ever saw the antivirus proxy. A sighting with a chain
  replaces the recorded one; a sighting without (the mirror socket
  already gone) keeps it. The daily totals keep the last `for` their
  flows carried (`DailyProcessTotal::forProcesses`), the text filter
  matches the peer and the `for` list on both, and the flows CSV gains
  `loopback_role`, `local_peer` and `for`. Schema version 4, migrated in
  place; a file from an earlier version reads back with no chain, as
  before. NetworkMonitor 0.8.
