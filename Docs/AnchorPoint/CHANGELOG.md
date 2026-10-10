#### 2026-10-10 *0.2.3*
- **Windows: no empty console window beside AnchorPoint's.** The GUI was a
  console program, so a double-click opened a console window that nothing
  was ever written to. A Release build is now a GUI-subsystem program
  (`cmake/UltraCanvasWindowsGuiApp.cmake`); the headless `anchorpoint` stays
  a console program.

#### 2026-09-28 *0.2.2*
- **The version is in the window title** — `AnchorPoint 0.2.2` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-09-27 *0.2.1*
- **A received file stays in the save folder.** The receiver joined the file
  name from the peer's Offer to the save folder as it came, so a peer that
  sent `../../.bashrc`, `/etc/…` or `..\..\x.dll` had the file written outside
  it - and, because an existing file is taken as a partial download to resume,
  appended to whatever was already there. The sender only ever sends a base
  name, so this took a hostile or modified peer, not a normal one; since a
  receiver listens for anyone who can reach its port, that is enough.
  - The name is now reduced once in the protocol (`SafeFileName`, `net/Protocol.cpp`),
    before the CLI or the window is asked where to save: only its last
    component is kept, split on `/` and `\` alike, so `../../.bashrc` arrives
    as `.bashrc` in the save folder.
  - A name that leaves nothing usable - empty, ending in a separator, `.` or
    `..`, containing NUL, or containing `:` (a drive such as `C:name`, or an
    NTFS stream such as `name:stream`) - is refused. The sender reports
    "peer rejected: unsafe file name".
  - New `anchorpoint_tests` (run by `ctest`) replays hostile Offers against
    the receiver: the traversal names, the refused names, and that a file
    outside the save folder is left untouched.
- **The core and CLI build on their own again.** UltraCrypt's Base32 decoder
  now calls the framework's shared codec, which the standalone build did not
  compile, so `anchorpoint` failed to link. The build now compiles
  `UltraCanvasBase32.cpp` alongside UltraCrypt.
- **CI builds AnchorPoint now.** It is its own CMake project, outside the
  framework build, so CI never built it and the broken link above went
  unnoticed. The Linux jobs now build the core and CLI on their own, as the
  README describes, and run `anchorpoint_tests`.
- The README no longer says the core has no external dependencies: it needs
  UltraCrypt and libsodium, and has since the SHA-256 moved to UltraCrypt.

#### 2026-08-31 *0.2.0*
- **AnchorPoint keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to peer-to-peer file sharing with no server-side file storage
  (`Apps/AnchorPoint`) is described here and carries this file's version, and
  AnchorPoint no longer moves when the framework releases.
- A framework change AnchorPoint needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
