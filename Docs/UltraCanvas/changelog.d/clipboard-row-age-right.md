- **The clipboard history's rows (UltraClipboard, UltraDesktop's quick
  panel) are calmer.** A text's meta line no longer counts its lines and
  characters or gives its size - "Formatted text · 1 line · 230 bytes" is
  "Formatted text" - and the age ("5 min ago") stands at the right end of the
  line instead of at the end of the text. `DescribeClipboardEntry` takes
  `withAge` (default true) for a caller that places the age itself.
- **`ClipboardHistoryRowDelegate::SetCoveredFrom(x, entryId)`**: when something
  covers the rows' right part, the selected row's actions and those of
  `entryId` stand just left of it instead of under it.
