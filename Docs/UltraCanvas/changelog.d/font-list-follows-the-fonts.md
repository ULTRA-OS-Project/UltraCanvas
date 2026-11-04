- **The text bar's Font list follows the fonts.** It was made once, with the
  window. On Windows, after a start whose system fonts were still being
  scanned (a cold font cache), it then held the bundled fonts alone for the
  whole session - ArtCreator, UltraPaint and UltraVideo offered Sans, Serif,
  Monospace and Ubuntu. `GetFontSetGeneration()` counts each change of the
  fonts the text engine knows (the system fonts switched to, a font file
  registered), and the bar makes its list again when the window lays it out
  after one.
