- **Typing in a search field hung the text area on a large document.**
  `UltraCanvasTextArea::HighlightMatches` collected every match by calling
  `utf8_find` in a loop, and each call walked the text from its start to the
  previous match — and, for a case-insensitive search, made a lowercased copy
  of the whole document first. That is quadratic in the number of matches:
  one letter typed into UltraTexter's search bar with the ~940 KB framework
  changelog open meant some 88,000 matches, each copying the full megabyte,
  and the UI thread did not come back (the first 30 KB alone took 1.4 s). The
  new `utf8_find_all` (`UltraCanvasUtilsUtf8.h`) lowercases once and returns
  every non-overlapping match in one pass — the same positions the loop
  produced, in 17 ms for the whole file. `HighlightMatches`, `CountMatches`,
  `GetCurrentMatchIndex` and UltraTexter's background match counter use it.
- **Replace All hung the text area on a large document.**
  `UltraCanvasTextArea::ReplaceText(..., all = true)` replaced one match at a
  time in place: a fresh `utf8_find` from the start of the text, then a splice
  that shifts everything after it - 6 s for the first 100 KB of the framework
  changelog with one letter replaced, and no end in sight for the whole file.
  The new `utf8_replace_all` finds the matches once and builds the result in a
  single pass (27 ms for the whole ~940 KB), with the same output as before.
