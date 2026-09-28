#### 2026-09-28 *0.1.1*
- **The version is in the window title** — `UltraWin Manager 0.1.1` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-08-31 *0.1.0*
- **UltraWin keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../../UltraCanvas/CHANGELOG.md) —
  nothing was rewritten or moved, so that history stays where it was published.
  From now on a change to the Windows-compatibility tier and its two front-ends
  (`Apps/UltraWinManager`, `Apps/UltraWinSetup`) is described here and carries
  this file's version, and UltraWin no longer moves when the framework
  releases.
- A framework change UltraWin needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
