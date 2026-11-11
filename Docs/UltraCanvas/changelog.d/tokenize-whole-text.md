- **`SyntaxTokenizer::Tokenize(text)` gives the tokens the text area colours.**
  The whole-text tokenizer was a separate scanner that had fallen behind the
  line-by-line one: it knew only level-0 Lua brackets, none of the strings that
  span lines, no C++ raw strings, and it dropped every line break - it built a
  `Newline` token for each and never added it to the result. It now runs
  `TokenizeLine` over each line with the line state carried between them, with
  one `Newline` token per `\n` or `\r\n`, so the two can no longer disagree.
  Seven private helpers only the old scanner used (`ParseString`,
  `ParseComment`, `ParseWord`, `ParseCharacter`, `ParsePreprocessor`,
  `ParseStringInLine`, `IsIdentifier`) were removed.
