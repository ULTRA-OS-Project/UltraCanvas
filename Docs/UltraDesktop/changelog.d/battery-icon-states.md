- **The battery icon shows the charge.** Its fill is as long as the battery
  is full, to the pixel: 10 pixels at the usual icon size (8 on a short
  screen), one per 10 %, complete only at 100 % and at least one while any
  charge is left. The level is read every 2 seconds. The icon is drawn on
  its own pixel grid, so the fill stands 1 pixel clear of the outline and no
  edge is blurred. A full battery is green inside the bar's white (in the
  light theme, dark) outline; while charging the battery is green with a
  white bolt over it; below 25 % and not charging it is red (the badge's red
  also starts at 25 % now, not 15 %). Without a battery the bar's usual
  glyph stays.
- **"100" fits on the battery's badge.** The badge, and the keyboard
  layout's ("DEU"), grows inward from the corner instead of running off the
  right edge of the screen (UltraCanvasBadge::SetGrowInward, framework).
