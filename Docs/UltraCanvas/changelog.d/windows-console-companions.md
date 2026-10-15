- **Windows: the applications with command-line modes no longer open a
  console window when the Start menu or Explorer starts them.**
  - These are UltraDesktop, DeviceExplorer, UltraCleaner, UltraNetMonitor,
    UltraCanvasStart, UltraWeb, UltraClaude, UltraFIBU and UltraPress.
  - Their Release builds are GUI programs now, each with a console companion
    beside it: `<App>.com`, built from the new
    `OS/MSWindows/AppEntry/UltraCanvasConsoleCompanion.cpp` by the new
    `ultracanvas_windows_console_companion()`
    (`cmake/UltraCanvasWindowsGuiApp.cmake`).
  - cmd and PowerShell try `.COM` before `.EXE`, so the bare name typed at a
    prompt (`UltraCleaner --scan`) runs the companion. It starts the `.exe`
    with the same command line, waits for it and returns its exit code, so
    prompts and batch files behave as they did with a console program.
  - On a console, the `.exe` attaches to the companion's console, so input
    and Ctrl+C work as before. Redirected, it is handed the companion's
    handles.
  - The companion links nothing of the framework and its C++ runtime
    statically. It is the pattern of Visual Studio's `devenv.com`.
  - UltraFIBU's commands-only build, which has no window, stays a console
    program.
- `package-win.sh` and `scripts/package-ultracanvasstart.sh` ship the `.com`
  files, and their PE headers are checked like the `.exe` files'. The test's
  probe stays out of the package, and the installer CI step fails if any
  test program is installed.
- The new `ConsoleCompanionTest` (Windows CI rows) drives a probe made the
  way the applications are, on real Windows:
  - Redirected: the arguments arrive whole, stdout and stderr stay apart,
    and the exit code comes back.
  - On a console of its own, as typed at a prompt: the program prints into
    the companion's console and reads a line typed there.
  - Ctrl+C reaches the program, and the companion hands back its exit code.
- The Windows CI rows check the installed applications:
  - every installed console-mode application has its companion;
  - each `.exe` is a GUI program and each `.com` a console program;
  - `UltraDesktop --version` typed at a cmd prompt goes through the companion
    and returns 0.
