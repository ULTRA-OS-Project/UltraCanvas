- **Extras > Open location** opens the folders a user is sent to and cannot
  easily find, in the active tab. The *UltraCanvas* block lists where the
  framework writes: **Crash dumps** (`%LOCALAPPDATA%\UltraCanvas\CrashDumps`,
  or `ULTRACANVAS_CRASH_DUMP_DIR`; Windows only), **Settings**, **UltraFiler
  settings** and **Cache**. The *System* block lists the operating system's
  places: application data, temporary files, crash reports, logs, startup
  programs, fonts and `/etc` / `drivers\etc`, chosen per platform:
  - Windows: Windows Error Reporting's dumps and report archive, the Start
    menu and Startup folders, Program Files.
  - macOS: `~/Library/Logs/DiagnosticReports`, Preferences, LaunchAgents.
  - Linux: the XDG folders, `/var/crash`, the systemd core dumps.

  A folder that does not exist yet is listed greyed out, so "there are no
  crash dumps" reads at a glance. Hovering an entry shows the full path it
  opens, a greyed-out one included.
- **Enter location…** (also **Ctrl+L**, wherever the focus is) opens a
  typed folder or file the way the system writes it. It expands `%VAR%` on
  Windows (`%LOCALAPPDATA%\UltraCanvas\CrashDumps`), `$VAR` / `${VAR}`
  elsewhere, and a leading `~`. It strips the quotes "Copy as path" adds, and
  reads a relative path from the folder on screen. A file opens its folder
  with the file selected.
- A location that does not exist is reported with the path it expanded to,
  so an unset variable shows. The last ten locations that opened are kept,
  as typed, in the submenu's *Recent* block, with **Clear recent locations**.
