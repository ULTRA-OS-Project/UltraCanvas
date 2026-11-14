- **The battery icon shows the charge.** Its fill is as long as the battery
  is full, in steps of 20 %: one fifth from the first percent (up to 29 %),
  two from 30 %, ..., all of it from 90 %. A bolt appears while it charges and the icon
  turns green; below 25 % and not charging it turns red (the badge's red
  also starts at 25 % now, not 15 %). Without a battery the bar's usual
  glyph stays.
- **"100" fits on the battery's badge.** The badge, and the keyboard
  layout's ("DEU"), grows inward from the corner instead of running off the
  right edge of the screen (UltraCanvasBadge::SetGrowInward, framework).
