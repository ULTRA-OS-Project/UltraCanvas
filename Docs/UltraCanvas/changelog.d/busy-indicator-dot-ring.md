- **`UltraCanvasBusyIndicator` gets a sixth kind, `DotRing`, and a two-colour
  `DualRing`.** `DotRing` is a ring of `ringDotCount` dots (default 8) with a
  head running round it; the dots behind the head shrink, and
  `dotRingFade` (`BusyDotRingFade`) picks whether they also fade (`Fade`, the
  default), stay solid (`NoFade`) or fade and come back in a new random colour
  every time (`FadeRandomColor`). `DualRing` draws its inner arc in the new
  `secondArcColor` (orange by default), and `Bar` takes a segment length in
  pixels, `barLength`, which overrides `barFraction` when set.
- **DemoApp: the Busy Indicator page shows the new kind.** A `DotRing` row,
  its three fades side by side at 64 px, the dual ring in two colours (orange
  and purple in the second-colour column), and a green bar with a 40 px
  segment.
