#### 2026-09-01 *1.16.0*
- **Drives connected while UltraFiler is running now appear in the tree.** The
  drive rows were enumerated exactly once, at start-up, and there was no path
  back to that code: the *Computer* node is marked as already scanned, so
  collapsing and re-expanding it does nothing, and the toolbar's *Refresh*
  re-lists only the file pane. A USB stick, a card, an optical disc, a network
  share or a disk image plugged in afterwards was therefore invisible until
  UltraFiler was restarted — while the path strip's *Computer* dropdown, which
  re-reads the volume list every time it opens, showed it. `RefreshDriveNodes()`
  brings the rows back in line with what is mounted, driven by the framework's
  new `UltraCanvasVolumeMonitor`, so the operating system says when (no
  background polling). *Refresh drives* in the tree's context menu runs the same
  pass by hand.
- **A volume that is removed no longer leaves a dead row and a stuck tab.** Its
  row went on sitting in the tree, painted as a drive and leading into a folder
  that was gone; a tab inside it just showed an empty listing, and could not be
  left by going up, because its parent had gone too. The row and everything the
  tree remembered about it are dropped, any tab inside the volume moves back to
  the home folder, and the status bar names the volume that disconnected.
  Plugging the same stick back in gives a freshly scanned tree rather than the
  one the window last saw.
- **`/run/media` and `/Volumes` were never scanned.** The tree looked only at
  `/media` and `/mnt`, so on Fedora, RHEL, Arch and openSUSE — where udisks2
  mounts under `/run/media` — a stick was missing from the tree even after a
  restart, and on macOS, where every removable volume lands in `/Volumes`, no
  removable volume ever appeared at all. Both are covered now, together with the
  per-user directory level (`/media/bob/USB STICK`) for each.
- **The Cloud Storage lookup is repeated when a volume appears**, so a Google
  Drive that mounts as its own drive letter reaches the section instead of
  needing a restart. It ran exactly once, at tree build.
- Requires framework 0.3.90 (`UltraCanvasVolumeMonitor`, the folder watcher's
  failure callback, and the `UltraCanvasTreeView::RemoveNode()` fix that
  removing a populated drive row depends on).

#### 2026-08-31 *1.15.0*
- **UltraFiler keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the file manager (`Apps/UltraFiler`) is described here and
  carries this file's version, and UltraFiler no longer moves when the
  framework releases.
- A framework change UltraFiler needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
