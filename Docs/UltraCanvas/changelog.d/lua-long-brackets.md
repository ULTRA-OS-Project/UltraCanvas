- **Lua long brackets are coloured at every level.** Lua's long strings and
  block comments take any number of `=` between the brackets - `[==[ … ]==]`,
  `--[=[ … ]=]` - and end only at the bracket with the same number. The
  tokenizer knew only the level-0 `[[ ]]` and `--[[ ]]`, so a levelled comment
  was read as a line comment and the lines after it as code, and a `]]` inside
  a `[==[` string closed it early. A new switch, `levelledLongBrackets` (on
  for Lua), matches the opening at any level and keeps the comment or string
  open, without escapes, until its own closing bracket; the line state carries
  that marker from line to line (`SyntaxLineState::blockCommentEnd`, and
  `rawStringEnd` as C++ raw strings use).
