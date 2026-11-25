#### 2026-11-25 *0.1.3*
- **The machine starts in a folder of any length.** A Unix socket's address
  holds a path of 107 bytes, and the machine has two sockets in its folder
  (`UltraWinConfig::vmDirectory`): QEMU's control socket `qmp.sock` and
  virtiofsd's `vfs.sock` for the home share. In a folder long enough to pass
  that, QEMU refused the control socket and ended at once -
  `UltraWin_VmStart` answered "QEMU did not answer on QMP" after its whole
  timeout - virtiofsd refused its own, and UltraWin's QMP client refused the
  path as well ("socket path too long").
  - QEMU and virtiofsd make their sockets themselves, so in such a folder
    both are now started **in** it and given the sockets by name
    (`qmp.sock`, `vfs.sock`), which they resolve against it; the disk, the
    ISOs and the answer-file folder are handed to QEMU as absolute paths
    there, whatever the caller gave. In a folder that fits, nothing changes.
  - The QMP client reaches the control socket through its folder
    (`UltraCanvasUnixSocketAddress.h`, the framework's), on every later
    command too: suspend, resume, stop, state.
  - `Tests/UltraWin/test_sharedhome.cpp` boots a real QEMU with the home
    share in a folder of some 150 bytes and drives it through suspend and
    resume; it fails on the code before.
- **The QMP client closes what it opened.** `QmpClient::Connect` left the
  socket of a connection that failed open until the client was destroyed,
  and opened a new one over it when asked again: on one client, each further
  try lost a descriptor. Every caller makes a fresh client per command, so
  nothing leaked in practice - it was a trap for the next caller. `Connect`
  now closes an earlier session first and leaves nothing open when it answers
  false; `Disconnect()` ends a session by hand; and a command on a client
  that is not connected says "QMP not connected" at once instead of waiting
  out its timeout. `Tests/UltraWin/test_vm.cpp` counts the open descriptors
  over failed connects and has a stand-in server see the first session end
  when the client connects again.

#### 2026-10-12 *0.1.2*
- **One UltraWin Manager at a time.** Starting UltraWin Manager
  (`Apps/UltraWinManager`) while it is already running brings the open window
  forward (restored if minimised; an open dialog comes forward with it)
  instead of opening a second copy. Two starts at the same moment cannot both
  stay, and a copy that does not answer is reported with *"UltraWin Manager is
  already running"*. `--help` and `--version` are unaffected; UltraWinSetup is
  unchanged. Framework: `UltraCanvasSingleInstance` (framework changelog:
  `instance-lock`).

#### 2026-09-28 *0.1.1*
- **The version is in the window title** — `UltraWin Manager 0.1.1` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-08-31 *0.1.0*
- **UltraWin keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../../UltraCanvas/CHANGELOG.md) —
  nothing was rewritten or moved, so that history stays where it was published.
  From now on a change to the Windows-compatibility tier and its two front-ends
  (`Apps/UltraWinManager`, `Apps/UltraWinSetup`) is described here and carries
  this file's version, and UltraWin no longer moves when the framework
  releases.
- A framework change UltraWin needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
