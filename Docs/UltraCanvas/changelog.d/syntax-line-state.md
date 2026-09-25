- **Syntax highlighting carries state from line to line.** The text area
  highlighted every line on its own, so only the first line of a
  `/* block comment */` (or `(* *)`, `{ }`, `<!-- -->`, `--[[ ]]`, … in the
  languages that have them) was drawn as a comment: the lines under it were
  coloured as code. The tokenizer now takes what the line above left open and
  returns what this line leaves open (`SyntaxLineState`, through the new
  `SyntaxTokenizer::TokenizeLine(line, state)`); the text area keeps the
  state with each cached line layout and rebuilds a line whose starting state
  changed, so typing or deleting a `/*` recolours the lines below it at once.
  - A line longer than 8000 characters is split into segments for layout. A
    string or `//` comment cut at a segment boundary now continues into the
    next segment; neither runs past a real line break. This matters most for
    minified CSS, which previously restarted the CSS scanner from a guess at
    every segment — dozens of times in a 294 KB Bootstrap build.
  - CSS keeps its place in the rule structure (selector list, declaration
    block, at-rule prelude, open blocks) across lines, replacing the per-line
    guess the CSS scanner used until now.
- **Type names are drawn in the type style.** `GetStyleForTokenType` had no
  case for `TokenType::Type`, so words a language lists as types (`size_t`,
  `uint8_t`, `std::string`, Pascal's `Integer`, …) fell through to the default
  text colour and `tokenStyles.typeStyle` was ignored. The light and dark
  themes now give types their own colour.
