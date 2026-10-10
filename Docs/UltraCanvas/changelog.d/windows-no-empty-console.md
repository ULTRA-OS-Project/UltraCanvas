- **Windows: apps no longer open an empty console window beside their own.**
  Only the demo, Texter, UltraFiler, UltraViewer, UltraPaint, ArtCreator,
  UCFileManager and UltraMail were built as GUI-subsystem programs; every
  other app opened a console window when started, and nothing was ever
  written to it. In a Release build:
  - **EmailCleaner, UltraSocial, UOS-Settings, UltraClipboard,
    UltraAuthenticator, UltraPassword and the UltraAI dashboard are now
    GUI-subsystem programs too**, so no console window opens for them at all. The list is now one `foreach` in the root
    `CMakeLists.txt` (*Hide console window on Windows Release builds*).
  - **The apps with command-line modes stay console programs** (DeviceExplorer,
    UltraCleaner, UltraNetMonitor, UltraCanvasStart, UltraWeb, UltraClaude,
    UltraDesktop, UltraFIBU), because a GUI-subsystem program cannot make the
    prompt wait for it or see its exit code, so `DeviceExplorer --list` or
    `UltraCleaner --scan` would stop working in a script. Started by a
    double-click, they now close the console Windows opened for them as their
    window comes up: the new `ReleaseOwnConsole()`
    (`OS/MSWindows/UltraCanvasWindowsDiagnostics.h`), called by
    `UltraCanvasWindowsApplication::InitializeNative()`, frees a console that
    no other process shares. Started from a prompt, the console is the
    prompt's and is kept, and so is the console of a Debug build or of any
    run with `ULTRACANVAS_DEBUG_LOG` set, where the log is written to it.
  - In those GUI-subsystem builds, `--help` and `--version` typed at a prompt
    print nothing, as already for Texter and UltraFiler: they write before
    the framework attaches to the prompt's console, which it does as the
    window opens.
