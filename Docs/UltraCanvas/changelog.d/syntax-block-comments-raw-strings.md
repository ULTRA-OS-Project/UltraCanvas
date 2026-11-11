- **Lua's `--[[ … ]]` and MATLAB's `%{ … %}` block comments are coloured.**
  The tokenizer looked for line comments before block comments, so `--`
  claimed `--[[` and the rest of its line, the block never opened, and the
  lines inside it were coloured as code; MATLAB's `%{` went the same way under
  `%`. Block comments are now matched first, in the line-by-line and the
  whole-text tokenizer.
- **C++ raw strings are coloured to their end, across lines.**
  `R"(…)"`, `R"delim(…)delim"` and the `u8R` / `uR` / `UR` / `LR` forms were
  read as a name and an ordinary string, which ended at the first `"` inside
  the raw text and at the end of the line. A fourth string switch,
  `delimitedRawStrings` (on for C++), matches the opening, takes its delimiter,
  and keeps the string open, without escapes, until its own `)delim"` -
  carried from line to line in `SyntaxLineState::rawStringEnd`.
