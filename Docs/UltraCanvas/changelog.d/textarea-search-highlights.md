- **TextArea: every search match is highlighted, not only the current one.**
  `HighlightMatches` collected all matches, but only the hex view drew them;
  the plain-text, code and markdown views now paint each match visible on
  screen in the new `searchHighlightColor` style colour (amber, a darker
  amber in the dark theme), with the current match - the selection - shown
  over it in the selection colour. The hex view uses the same colour instead
  of its own fixed yellow. New overload
  `HighlightMatches(text, caseSensitive)`, so a caller no longer has to set
  the case through `SetTextToFind` first.
- **TextArea: matches outside the view show on the scrollbar.** Each match
  puts a tick on the vertical track at its line's place in the document
  (wrapped lines included; rows in the hex view), and the current match a
  tick in the selection colour.
- **TextArea: the marks follow edits.** They are found again after every text
  change until `ClearHighlights`, and only the lines whose marks changed are
  re-laid out. In a document over 256 KB the search waits until typing has
  paused for 200 ms instead of making a pass over the whole text on every
  keystroke.
