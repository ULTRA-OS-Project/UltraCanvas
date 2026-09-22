#### 2026-09-21 *0.1.2*
- **The dashboard app has an icon.** `media/appicon/UltraAI.svg` is the
  uploaded artwork; `media/appicon/UltraAI.png` is its 256 px render and
  what the window and taskbar icon (`main.cpp` hands it to
  `SetDefaultWindowIcon`) and the Windows `.exe` icon are made from, as for
  the other applications. The app now takes the build's asset copy as a
  dependency, so the icon is in its resources dir. A freedesktop entry
  (`Apps/UltraAIApp/UltraAI.desktop`) puts UltraAI in the application menu;
  the install rules place it, the binary and both icon files where the
  desktop looks for them.

#### 2026-08-31 *0.1.1*
- **UltraAI keeps its own changelog from here.** Everything up to and including
  this version shipped as part of a framework release and is recorded in
  [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing was
  rewritten or moved, so that history stays where it was published. From now on
  a change to the UltraAI module and its dashboard app (`UltraAI/`,
  `Apps/UltraAIApp`) is described here and carries this file's version, and
  UltraAI no longer moves when the framework releases.
- A framework change UltraAI needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
