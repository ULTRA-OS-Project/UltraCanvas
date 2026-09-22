#### 2026-09-22 *0.1.1*
- **The account wizard's captions size themselves to the network's wording.**
  The form was a flex container per field with every caption pinned to 120 px,
  and the captions are rewritten each time the network dropdown changes -
  "Access token" becomes "Page access token" becomes "(not used)" - so that one
  width had to be wide enough for the longest wording of all seven networks and
  would cut off the first one to grow past it. The form is a
  `UltraCanvasFormLayout` grid now: the caption column measures its own text and
  follows the wording, every input starts where that column ends, and the grid
  is `flex-shrink: 0` and does not scroll, so a field can no longer be squeezed
  below the input inside it and raise a scrollbar pair across its own caption -
  the defect fixed in UltraCloud's add-account dialog in framework 0.9.20, which
  this wizard was one short dialog away from showing too.

#### 2026-08-31 *0.1.0*
- **UltraSocial keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the social-posting client (`Apps/UltraSocial`) is
  described here and carries this file's version, and UltraSocial no longer
  moves when the framework releases.
- A framework change UltraSocial needs still belongs in the framework
  changelog. Cross-reference it from here when a release depends on it; never
  describe one change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
