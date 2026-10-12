- **One running copy per user: `UltraCanvasSingleInstance`.** A second start
  of an application no longer opens another window: it asks the copy already
  running to come forward - restored if minimised, an open dialog first - and
  quits. An application opts in with `ultracanvas_single_instance(<target>)`
  in CMake and a few lines in `main()` (`Claim()` before `Initialize`,
  `ListenRaise()` before `Run`). UltraMail, UltraClipboard, DeviceExplorer,
  UOS-Settings, UltraNetMonitor, UltraPassword, UltraAuthenticator,
  UltraCleaner, EmailCleaner, UltraClaude, the UltraAI dashboard, UltraSocial,
  UltraFIBU and UltraWin Manager use it; command-line tools and the
  document applications do not (yet). The request travels over UltraMessage
  (`<appId>.show`); the source is compiled into each application, since the
  core does not link UltraMessage. See
  `Docs/UltraCanvas/UltraCanvasSingleInstance.md`.
- **`UltraCanvasInstanceLock`**, underneath it: `Acquire(<path>)` answers
  `Acquired`, `HeldElsewhere` (another copy is running), `Failed` or
  `Unsupported`. The lock is the operating system's (`flock` on Linux, the
  BSDs and macOS, `LockFileEx` on Windows), so it goes away with the process
  however it ends - a crash never leaves a stale lock behind. Android and
  WebAssembly answer `Unsupported`. Tested by `Tests/InstanceLockTest.cpp`
  (ctest: `InstanceLockTest`), on a Thai-and-emoji folder name and across a
  process that exits without releasing. See
  `Docs/UltraCanvas/UltraCanvasInstanceLock.md`.
