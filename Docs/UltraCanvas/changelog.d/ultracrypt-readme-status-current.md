- **The UltraCrypt README says where its consumers stand now.** Its rollout
  table still listed the UCD v2 writer and reader as to do, and its table of
  consumers still called UCD v2 unimplementable for want of primitives,
  UltraVault a design document, UltraDatabase's at-rest encryption unstarted
  and UltraAuthenticator blocked - the state before the module existed, in a
  column headed "State today". All four are built on it: the UCD v2
  container with password protection (`UltraCanvasUCDContainer.h`),
  UltraVault's file backend, SQLite files encrypted at rest
  (`UltraDatabaseEncryption.h`) and UltraAuthenticator. The tables say so,
  the problem statement around them is in the past tense, the status line
  names the stages that are done (1, 2 and 5), and stage 3 is down to one
  item: UCD v2's SuperVault key source, which the container refuses to open
  so far. The same two stale rows in
  `Docs/UltraAuthenticator/UltraAuthenticator-Investigation.md` are corrected.
