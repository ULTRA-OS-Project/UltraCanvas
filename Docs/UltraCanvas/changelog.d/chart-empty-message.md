- **A chart with no data says what you set.** `SetEmptyMessage(text, detail)`
  and `SetEmptyMessageColor` on `UltraCanvasChartElementBase`: a line and an
  optional smaller one under it, centred - "Nothing is drawing power / All 5
  metered plugs are idle." - in place of the fixed "No data to display". An
  empty text draws the background alone. The empty state is painted on the
  chart's own background colour (it was a fixed grey box whatever the chart's
  colours), and the text is centred (it began 60 px left of the middle, so a
  longer message ran off-centre). The jitter plot uses the same empty state
  instead of a copy of the old one.
- **An icon over the empty-state message.** `SetEmptyIcon(path, iconColor,
  badgeColor)`: 32 px over the text, or 28 px on a 54 px rounded badge when
  `badgeColor` is set; `iconColor` paints a single-colour icon in that
  colour. The icon, message and detail are centred as one block.
