- **TextArea: every search match is highlighted, not only the current one.**
  `HighlightMatches` collected all matches, but only the hex view drew them;
  the plain-text, code and markdown views now paint each match visible on
  screen in the new `searchHighlightColor` style colour (amber, a darker
  amber in the dark theme), with the current match - the selection - shown
  over it in the selection colour. The marks follow edits: they are found
  again on every text change until `ClearHighlights`, and only the lines
  whose marks changed are re-laid out. New overload
  `HighlightMatches(text, caseSensitive)`, so a caller no longer has to set
  the case through `SetTextToFind` first.
