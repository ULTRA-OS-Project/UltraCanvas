- **UltraFiler crashed while making thumbnails of folders with HTML or SVG
  files.** The HTML parser put inline `<svg>` / `<math>` names back in their
  vocabulary's case (`viewBox`, `linearGradient`) from lookup maps it filled
  on first use, unguarded; UltraFiler's thumbnail workers extract text
  previews on several threads at once, and two of them meeting foreign
  content together corrupted the hash table - an access violation in
  `AdjustedForeignName` (`ExtractTextPreview` → `HTML::ExtractPlainText` →
  `Parser::ParseAttributes`). The maps are now built in one static
  initializer, which C++ runs exactly once however many threads parse.
  `HTMLReaderTest` parses SVG and MathML on eight threads; ThreadSanitizer
  reported 51 races in the old parser there and none now.
