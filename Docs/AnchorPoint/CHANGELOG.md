#### 2026-10-21 *0.2.5*
- **Stop stops a transfer.** It used to cancel only a Receive still waiting
  for a peer: once a file was moving, Stop did nothing and the transfer ran to
  the end - and closing the window waited for it too. Stop now ends a send or a
  receive at any point, including one stuck waiting on a peer that stopped
  answering, and so does closing the window.
  - The peer is told: a stopped sender's receiver reports *"sender:
    cancelled"*. A stopped receiver's sender reports that the connection was
    lost while sending.
  - A stopped receive keeps the partial file, so receiving the same file into
    the same folder again resumes it from where it stopped.
  - `SendFile` / `ReceiveFile` (`net/Protocol.h`) take an optional cancel flag,
    checked between chunks, and report `cancelled` in their result; a call
    blocked on the network is woken with the new `IConnection::Interrupt()`.
    The headless CLI passes no flag and is unchanged.
  - Stop does not interrupt the SHA-256 the sender takes of the whole file
    before it connects to send; a large file still finishes hashing first.
- **A send to a peer that has gone no longer ends the program.** On Linux and
  macOS, writing to a connection the receiver had closed raised SIGPIPE, which
  quit the sending AnchorPoint (or its CLI) with no message. It is now an
  ordinary failed send.
- **The progress bar no longer shows the last transfer next to a new
  message.** *"Choose a file first."*, a bad peer address or a bad listen port
  appeared beside the previous transfer's full bar, and choosing a file left
  the old prompt standing. Those messages now empty the bar, and choosing a
  file resets the status to *Ready.*
- `anchorpoint_tests` covers stopping on either side, a stop that wakes a
  blocked read, and sending to a closed socket.
- **A Browse… button for the save folder.** It used to be typed in, starting
  from `.` - the folder AnchorPoint happened to be started in. *Browse…* opens
  a folder dialog there.
- **Choose File… and Browse… use the framework's file dialogs**
  (`UltraCanvasFileLoader::OpenFileDialog` / `SelectFolderDialog`), like
  every other UltraCanvas application, instead of calling the platform dialog
  directly. A file picked to send is not added to the recent-files list.
  The received file's path is joined with `PathFromUtf8` / `PathToUtf8`, so
  a folder or file name in any script works on Windows too.
- **The command line is `anchorpoint-cli` now, not `anchorpoint`.** On macOS
  and Windows, whose file systems ignore case, `anchorpoint` and the window's
  `AnchorPoint` are the same file, so building both in one folder kept only
  one of them. Scripts that ran `anchorpoint send ...` or `anchorpoint recv
  ...` need the new name; the arguments are unchanged.
- **CI builds the window, on Linux, macOS and Windows.** The framework's build
  now includes AnchorPoint (`BUILD_ANCHORPOINT`), so every CI job builds the
  window and the CLI; until now CI only built the core and CLI on their own,
  on Linux.
- The README no longer says the window's build is blocked by unfinished
  UltraNet sources; it builds.

#### 2026-10-14 *0.2.4*
- **One AnchorPoint at a time.** Starting AnchorPoint while it is already
  running brings the open window forward (restored if minimised; an open
  dialog comes forward with it) instead of starting a second one - the GUI
  opens no files from its command line, so there is nothing to hand over.
  Two starts at the same moment cannot both stay, and a copy that does not
  answer is reported with *"AnchorPoint is already running"*. The headless
  `anchorpoint` is unaffected and runs as many times as it is started.
  Framework: `UltraCanvasSingleInstance` (framework changelog:
  `instance-lock`).

#### 2026-10-10 *0.2.3*
- **Windows: no empty console window beside AnchorPoint's.** The GUI was a
  console program, so a double-click opened a console window that nothing
  was ever written to. A Release build is now a GUI-subsystem program
  (`cmake/UltraCanvasWindowsGuiApp.cmake`); the headless `anchorpoint` stays
  a console program.
- **The build reports the right version.** `Apps/AnchorPoint/CMakeLists.txt`
  set `project(... VERSION 0.2.1)` by hand, so the configure line still said
  0.2.1 two releases later. It takes the number from this changelog's first
  line now, as the window title already did.

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
