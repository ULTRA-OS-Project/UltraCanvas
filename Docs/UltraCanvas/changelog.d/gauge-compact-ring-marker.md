- **A round gauge can be a small marker.** `UltraCanvasGaugeDiagramElement`
  in `CircularRing` mode kept its card layout at every size - 30 px of padding
  and a 20 px minimum radius - and always drew its value in the middle, so
  below about 60 px it drew a corner of a ring and a number. Two options,
  off by default so every gauge looks as before:
  - `SetRingCompact(true)`: the ring fills the element - no title, no
    padding, no minimum radius - for a ring of 12 to 40 px in a row of text
    or buttons; nothing is drawn outside the element.
  - `SetRingValueText(false)`: the ring without the number; a centre label
    or icon, when set, is centred on its own.
  `GaugeLinearModesTest` draws a compact ring at 12, 16, 26 and 40 px.
