- **Windows: a GUI program typed at a prompt prints its `--help` and
  `--version` there.** A GUI-subsystem program is given no console, and the
  framework attached it to the prompt's only in
  `UltraCanvasWindowsApplication::InitializeNative()`, as the window opens -
  after `main()` had already printed its help, its version or a bad
  argument into nothing. The new `ultracanvas_windows_gui_app()`
  (`cmake/UltraCanvasWindowsGuiApp.cmake`) makes a target a GUI-subsystem
  program in a Release build, as the *Hide console window on Windows Release
  builds* list in the root `CMakeLists.txt` did, and compiles
  `OS/MSWindows/AppEntry/UltraCanvasWindowsAttachConsole.cpp` into it, which
  calls `AttachParentConsole()` before `main()`. The prompt still does not
  wait for a GUI program, so the text appears after it has come back;
  redirected to a file it is complete.
  - The source goes into the executable, not the core: the core is a DLL in
    the shared build, and a DLL's static initialisers run under the loader
    lock. It sits in a folder of its own so the core's `OS/MSWindows/*.cpp`
    glob does not pick it up.
  - The root list, UltraMail, and the standalone UltraTexter and AnchorPoint
    builds use it.
  - **DemoApp's `--help` prints to stdout**, and an unknown argument to
    stderr. It wrote to `debugOutput`, which a Release build keeps off, so it
    printed nothing on any platform. Texter, UltraFiler, UltraViewer and
    UltraPaint had the same fault; see their changelogs.
