- **The Microsoft Store copy says why it cannot become the Windows desktop.**
  Both of UltraDesktop's switches are values under `HKEY_CURRENT_USER`. A
  program running from its MSIX package writes into the package's own
  registry, which sign-in never reads, so the switch would have reported
  success and changed nothing. Run from a package, the settings menu now
  offers *Use as the Windows desktop...*, which explains this and points to
  the UltraCanvas installer. It also keeps *Close UltraDesktop*, since a
  packaged copy is always a trial run with no title bar to close.
  `--use-as-desktop` fails with the same explanation. The framework change
  behind it, `UltraCanvasDesktopShell::GetSystemDesktopSwitchUnavailableReason`,
  is in the framework changelog. The
  MSIX package now gives UltraDesktop a Start menu entry, as the installer
  does.
