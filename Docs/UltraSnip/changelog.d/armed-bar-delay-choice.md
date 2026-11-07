- **UltraSnip opens over the screen already dimmed, its bar at the top
  centre.** Started without a shape (from the menu, or a second start), it
  takes the screen first and lays the dimmed picture over it, then puts its
  bar centred across the top of the primary screen: the shape chosen on the
  bar can be dragged out, or a window clicked, at once - no Start needed.
  Changing the shape on the bar changes the selection on the spot. Escape or
  a right click puts the dim away and leaves the bar; Start (with its delay)
  and Settings put it away first and go on as before. The hint line moves
  down below the bar.
- **The delay is a segmented choice like the shape and the kind**: 0, 3, 5
  and 10 seconds, every segment as wide as the widest caption in the
  language shown, each with its own tooltip. It replaces the dropdown.
- **Each segment of the bar says its own name.** The shape, kind and delay
  tooltips all showed the first segment's text (the tooltip was read once,
  when the pointer came into the control); they now follow the segment under
  the pointer - see the framework's `SetSegmentTooltip`.
