- **One running copy per user: `UltraCanvasInstanceLock`.** An application
  takes `InstanceLock::Acquire(<data folder>/instance.lock)` at start; a second
  start gets `HeldElsewhere` and knows a copy is already running. The lock is
  the operating system's (`flock` on Linux, the BSDs and macOS, `LockFileEx`
  on Windows), so it goes away with the process however it ends - a crash
  never leaves a stale lock behind. Android and WebAssembly answer
  `Unsupported`. UltraMail is the first user (UltraMail changelog 0.10.51).
  Tested by `Tests/InstanceLockTest.cpp` (ctest: `InstanceLockTest`), on a
  Thai-and-emoji folder name and across a process that exits without
  releasing. See `Docs/UltraCanvas/UltraCanvasInstanceLock.md`.
