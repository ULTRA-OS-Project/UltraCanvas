#### 2026-09-22 *0.1.1*
- **UltraAuthenticator has an app icon.** The uploaded artwork — the ULTRA
  wordmark in five code tiles — is `media/appicon/UltraAuthenticator.svg`, and
  `media/appicon/UltraAuthenticator.png` is its 256 px render. The Xara export
  was a page, not an icon: a 2:1 `viewBox` in pt, a Times New Roman
  declaration no glyph in the file uses, an empty `<defs>` and an SVG 1.1 DTD
  reference a parser may try to fetch off the network. The drawing is kept
  verbatim; only the root element was rewritten, onto a square canvas with the
  mark centred — `share/icons/hicolor/256x256/apps` means 256x256, and a
  landscape render is not that.
- **It is drawn everywhere the app is shown.** `main.cpp` hands the PNG to
  `SetDefaultWindowIcon` for the window and the taskbar entry that follows it;
  `UCAPP_ICON_PATH` names the same file as the core's fallback, so a window
  never comes up unbranded; `ultracanvas_embed_app_icon` builds the `.exe`
  icon Explorer and the Windows taskbar read off the binary itself. A
  freedesktop entry (`Apps/UltraAuthenticator/UltraAuthenticator.desktop`)
  puts the authenticator in the application menu, and the install rules place
  the PNG in `share/icons/hicolor/256x256/apps` and the SVG in
  `share/icons/hicolor/scalable/apps` so `Icon=UltraAuthenticator` resolves —
  the route UltraFiler takes to show an application's own icon. The app now
  takes the build's asset copy as a dependency, so the PNG is in its resources
  dir when it runs out of the build tree. The entry claims no `MimeType`: an
  `otpauth://` URI is pasted into the Add-account dialog, and making this the
  system's handler for secrets it cannot vet is not something a desktop file
  should do quietly.

#### 2026-08-31 *0.1.0*
- **UltraAuthenticator keeps its own changelog from here.** Everything up to
  and including this version shipped as part of a framework release and is
  recorded in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) —
  nothing was rewritten or moved, so that history stays where it was published.
  From now on a change to the TOTP/HOTP authenticator
  (`Apps/UltraAuthenticator`) is described here and carries this file's
  version, and UltraAuthenticator no longer moves when the framework releases.
- A framework change UltraAuthenticator needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
