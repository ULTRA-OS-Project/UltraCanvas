- **ChatCube Direct: renaming a group could report the old name.** A group
  request's reply and the relay's `group.update` pushes arrive on two
  connections (UltraNet's request thread and the socket's), and every group
  request - create, rename, add members, join, describe, make public - wrote
  the relay's reply to the store and then read the conversation back. An
  older push applied in between (the group as it was just made) was read
  back instead, so `RenameGroup` could hand its caller the previous title;
  `DirectProviderTests` failed on it now and then in CI (the check after
  `RenameGroup(..., "Crew")`). `UpsertGroup` now returns the conversation as
  the relay's record has it, and the requests report that. The store was
  never left wrong: the relay pushes the new state as well, in order on the
  socket.
- **ChatCube relay 1.6.0: every group carries a revision, and ChatCube
  Direct drops a record older than the one it holds.** The relay had nothing
  to order a group's records by, so a client could not tell an old one from
  a new one: the group as it was made, pushed late on the socket, put the
  old name back over a rename until the next push. The relay now gives each
  group a `revision` (1 when made, one more with every change, bumped in the
  same database write as the change; schema migration 5, existing groups
  start at 1) in every answer and push that carries a group, and tells a
  member taken out at the revision the removal made. `DirectProvider` keeps
  the newest revision applied per group and drops older records - and, for
  a group it was taken out of, a record from that same change - so a late
  record can neither revert a group nor bring back one it left. The check
  and the store write are one step under their own lock. A relay before
  1.6.0 sends no revision, and records are applied as before.
  `DirectProviderTests` now checks Bob's stored record keeps the new name
  right after a rename; the relay's tests cover every change raising the
  revision and the migration.
- **A merge no longer deploys the ChatCube relay.** The `ChatCube relay`
  workflow put every change to `Server/ChatCubeRelay` that reached `main` on
  the live server - a new version can migrate its database. It now only
  tests on a merge; the operator deploys when they choose, by starting the
  workflow by hand on `main` (Actions > ChatCube relay > Run workflow), as
  before. Relay 1.6.0 goes live that way, not with its merge.
