- **A badge can grow inward instead of past its anchor's edge.**
  `UltraCanvasBadge::SetGrowInward(true)`: an anchored pill wider than it is
  tall keeps the outer edge a round badge would have on its corner and
  grows towards the anchor's middle. A badge is still centred on its corner
  by default. UltraDesktop uses it for the battery's "100" and the keyboard
  layout, which ran off the right edge of the screen.
