- **Demo app: the layout audit runs in CI.** Linux CI runs
  `UltraCanvasDemo --audit-layout` on a virtual display, then again with
  `--pseudo-loc` (every text about 40% longer, as a translation is). A page
  with a label or button narrower than its text, or with content cut off and
  no scrollbar to reach it, fails the build. Both runs find nothing on all 167
  pages. `--pseudo-loc` now leaves text without a letter as it is: a time
  readout ("0:00 / 2:08"), a "+" or "−" button or a "–" between two dates is
  not translated, so it was never going to grow. The Importance-Performance
  chart page uses the shared page look: the chart and its controls are in
  cards, and every text wraps. The badge page's bell tile carries a two-letter
  mark like the other tiles.
- **Charts: the legend restores the font it changes.** While measuring
  itself, the shared chart legend set its own font family (Arial) and size on
  the context and left them there. A chart lays out its legend only on its
  first frame, so on that frame it measured its point labels in the legend's
  face. On every later frame it drew them in its own, wider face, so a name
  ran past its box: "Delivery reliabilit|y" on the Importance-Performance
  chart. The legend now restores the context's font before it returns.
