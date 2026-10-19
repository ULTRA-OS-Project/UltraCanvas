- **Matrix diagram: the House of Quality stayed partly light in the dark
  theme.** `SetDarkTheme(true)` left the competitive-assessment block's
  background and tick lines at their light colours. It also left the
  direction-of-improvement arrows dark grey, so they vanished on the dark
  grid. The theme now covers every colour in `MatrixDiagramStyle`, including
  the roll-up bars and rank text, and `SetDarkTheme(false)` restores them.
  A competitor whose own colour is too dark to read on the block is drawn in
  the label colour instead, as the House sample's near-black "Our bicycle"
  was.
