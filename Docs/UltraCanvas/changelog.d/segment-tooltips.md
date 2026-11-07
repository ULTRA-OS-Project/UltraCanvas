- **`UltraCanvasSegmentedControl::SetSegmentTooltip`: each segment its own
  tooltip.** The tooltip shows while the pointer is over that segment and
  switches, where it is up or about to be, as the pointer moves to the next;
  a segment without one shows the control's own (`SetTooltip`). Changing the
  control's tooltip from `onSegmentHover` did not do this - the manager read
  the text once, as the pointer came in, so every segment showed the first
  one's.
- **Linux: `RaiseAndFocus` raises the window itself as well (`XRaiseWindow`).**
  It only asked the window manager (`_NET_ACTIVE_WINDOW`), so with none - a
  kiosk, a test display - nothing was raised, and a window stayed under every
  window created after it however often it was shown or raised.
