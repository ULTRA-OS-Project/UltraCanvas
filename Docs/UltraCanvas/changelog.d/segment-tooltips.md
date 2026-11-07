- **`UltraCanvasSegmentedControl::SetSegmentTooltip`: each segment its own
  tooltip.** The tooltip shows while the pointer is over that segment and
  switches, where it is up or about to be, as the pointer moves to the next;
  a segment without one shows the control's own (`SetTooltip`). Changing the
  control's tooltip from `onSegmentHover` did not do this - the manager read
  the text once, as the pointer came in, so every segment showed the first
  one's.
