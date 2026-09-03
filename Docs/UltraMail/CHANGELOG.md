#### 2026-09-03 *0.2.0*
- **First run shows a start page, nothing else.** Until the first email account
  exists the main window holds only the UltraMail logo, the app title and an
  "Add email account" button (`UltraMailStartPage`). The info-tile bar, the
  Toolbox grid, the Write / Read mail / Contacts buttons and the old "Welcome to
  UltraMail. Add an account to begin." hint are gone from that state; they
  appear once an account is added and the start page goes away. The button is a
  primary-style `UltraCanvasButton` with the envelope icon; the page is a
  centred flex column that follows the window size.
- **Account view header no longer overlaps the info-tile bar.** The bar starts
  below the header row (title + action buttons) instead of at the window top,
  where the "UltraMail" title was drawn over the first account's tile.
- **UltraMail has an app icon** (`media/appicon/UltraMail.svg`): the envelope on
  the selection blue. It is the start-page logo and the window icon.

#### 2026-08-31 *0.1.0*
- **UltraMail keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the mail client (`Apps/UltraMail`) is described here and
  carries this file's version, and UltraMail no longer moves when the framework
  releases.
- A framework change UltraMail needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
