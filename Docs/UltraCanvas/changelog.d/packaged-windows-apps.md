- **The applications now work when Windows starts them from the MSIX
  package.** A packaged application starts with System32 as its working
  folder, and its registry writes stay inside the package. Four things
  assumed otherwise:
  - **UltraNet's plug-in folder** defaulted to the relative path
    `Plugins/UltraNet`, which is relative to the working folder. Unless an
    application set its own folder, as UltraMail and EmailCleaner do, no
    plug-in was found when it started anywhere but its own folder. That
    covered DeviceExplorer's network printer and scanner discovery (the mDNS
    plug-in) from the package, a shortcut or another program. The default is
    now `Plugins/UltraNet` beside the executable, or one or two folders above
    it, whichever holds a plug-in (`UltraNetPlugins.cpp`).
  - **The file dialog**, with no folder remembered, opened in the working
    folder. That is System32 for a packaged application, and `/` for a macOS
    application started from Finder. It now opens in the home folder when the
    working folder is the Windows folder or the root of the drive. It treats a
    relative folder (UltraMail passes `.` when it finds no home) the same way.
  - **UltraDesktop's desktop switch** writes `HKEY_CURRENT_USER` values, which
    sign-in never sees from a package. The switch reported success and did
    nothing. On Windows, `CanReplaceSystemDesktop()` is now false when running
    from a package. The new
    `UltraCanvasDesktopShell::GetSystemDesktopSwitchUnavailableReason()` says
    why, for UltraDesktop and UOS-Settings to show. `ReplaceSystemDesktop` and
    `RestoreSystemDesktop` refuse with that sentence.
  - **Texter** embeds the UTF-8 code-page manifest, so a file with a Thai or
    CJK name opened through the package's file types arrives intact. The
    package's minimum Windows is now 10 1903 (`--min-version 10.0.18362.0`),
    the first that honours that manifest.

  With the switch explaining itself, the MSIX package gives UltraDesktop a
  Start menu entry again, as the installer does. Texter 1.70, UltraDesktop
  0.4.2 and UOS-Settings 0.7.1 carry the application side.
