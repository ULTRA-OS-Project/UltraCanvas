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
