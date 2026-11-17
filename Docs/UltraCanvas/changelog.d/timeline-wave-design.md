- **Timeline diagram: the Wave design.** `TimelineDesign::Wave` threads one
  thick half-ring per item on a straight axis, each bending the other way from
  the one before, so together they make one smooth wave - the infographic
  where a year sits inside every ring and the item's text beside it. The node
  sits where two rings meet and a plain dot closes the path. The axis runs at
  any angle (`waveAxisAngle`: 45 falls diagonally by default, 0 is horizontal,
  90 top-down, -45 rises) or at the angle that gives the largest rings
  (`waveAutoAngle`); the rings are made as large as the frame allows while
  every item's text keeps room of its own - beside the rings on a steep axis,
  above and below them on a flat one - and no text block covers a ring or
  another block. Each caption is drawn as large as fits inside its ring,
  clear of the band, the nodes and every text block, reaching a little past
  the chord as in the printed designs. Side policy, reversed order, placement by date (rings of different
  sizes), the gradient along the path, the pending rings after
  `SetCurrentIndex`, shadows, cards and stems all apply. `GetItemArc` returns
  an item's ring, so an application can set its own icon beside it. The
  demo's Timeline Diagram page has a Wave tab and a Wave Axis control, and
  `Tests/TimelineDiagramWaveTest.cpp` holds the layout to its promises at six
  axis angles, 1 to 12 items, reversed, by date and all on one side.
- **Demo: the "Waves info graphic" placeholder is gone** from Info Graphics.
  It was a Planned entry that only said it was not ready; the wave
  infographic is now Timeline Diagram's Wave variant.
