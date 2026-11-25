- **Filer widget: a favorite can stand out by more than its heart.**
  `SetFavoriteHighlight(look, colour)` gives every entry the favorite provider
  names a solid background (the whole tile or row, its text switched to white
  or dark, whichever reads on the colour), a border around it, or a shade -
  the colour at the top, fading out to the bottom; `NoHighlight` (the default)
  keeps only the heart. `SetFavoriteHeartShown(false)` leaves the heart out
  and gives its gutter back. A selected favorite keeps the selection colour,
  and the border is drawn over it.
