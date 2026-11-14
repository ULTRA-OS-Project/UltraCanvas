- **A click on a tree view's bottom border no longer picks the row that would
  have been drawn there.** `GetNodeAtY` worked from the scroll offset alone and
  named a row for any y, including the padding and the border below the rows,
  where `Render` draws nothing - so a click or a hover a pixel or two under the
  last visible row selected or highlighted a row the user could not see. It
  now stops at the foot of the rows' viewport, as the rows themselves do.
- **`ScrollTo` leaves a row that is not on show where it is, instead of
  jumping to the top.** Asked to reveal a row under a collapsed parent, hidden
  by the application or filtered out, it took the 0 that the row walk returns
  for a row it never reaches as that row's position and scrolled to the first
  row. Revealing such a row is what `ScrollToSelection()` is for: it opens the
  parents on the way first.
