- **macOS: `UltraCanvasDesktopShell::ListApplications` lists the
  installed applications.** It read freedesktop desktop entries there,
  which macOS does not have, so UltraDesktop's Apps window was empty. It now
  reads the application bundles in `~/Applications`, `/Applications`,
  `/System/Applications` (their `Utilities` too) and one level of subfolders
  below them: named as Finder names them, with the icon Finder shows (a PNG
  in the disk cache's `desktop-icons` folder) and the bundle's
  `LSApplicationCategoryType` as its category; agents and background-only
  bundles are left out. `LaunchApplication` starts such an entry through
  `NSWorkspace`, as Finder does. New file
  `OS/MacOS/UltraCanvasMacOSApplications.mm`; the rest of the macOS desktop
  shell is unchanged. Doc: `Docs/UltraCanvas/UltraCanvasDesktopShell.md`.
