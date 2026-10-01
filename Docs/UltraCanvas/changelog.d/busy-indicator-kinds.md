- **`UltraCanvasBusyIndicator` comes in five kinds.** `BusyIndicatorStyle::kind`
  (`BusyIndicatorKind`) picks `Ring` (the turning arc, still the default),
  `DualRing` (two concentric arcs turning in opposite directions), `Dots` (a
  row of dots swelling one after another), `Bar` (a segment sliding along a
  track) or `Pulse` (a circle breathing in and out). New style fields
  `dotCount` and `barFraction`; `thickness` is the bar height for `Bar`, and
  `revolutionsPerSecond` is cycles per second for every kind. A second
  `CreateBusyIndicator(id, x, y, w, h, kind)` overload builds one of a given
  kind.
