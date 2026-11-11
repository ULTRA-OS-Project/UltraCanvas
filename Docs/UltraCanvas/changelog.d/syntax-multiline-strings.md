- **Strings that run across lines are coloured on every line, per language.**
  A string ended at the end of its line in every language, so a Python
  docstring, a JavaScript template literal or a C string continued with a
  backslash was coloured on its first line only, and the lines after it as
  code. Three rules in `SyntaxTokenizationRules` now say which strings span
  lines: `multiLineStrings` (`"""` / `'''` in Python and Dart, `"""` in Kotlin,
  Swift, Java, Scala, C# and Elixir, `[[ ]]` in Lua), `multiLineStringDelimiters`
  (the backtick in JavaScript, TypeScript and Go; both quotes in Shell, Ruby,
  PHP and Perl; `"` in Rust) and `stringLineContinuation` (a trailing `\` in C,
  C++, Python and JavaScript). Elsewhere an unclosed string still stops at its
  line, so a missing quote colours one line, not the rest of the file.
- **A Markdown code fence in an unknown language is shown plain.** The fence's
  language was looked up by exact name, then by extension, and when both failed
  the shared code-block tokenizer kept the previous fence's language: a block
  tagged `foo` after a C++ one was coloured as C++. And since names were
  matched case-sensitively, a block tagged `python` only ever worked when the
  block before it was Python. The new `SyntaxTokenizer::SetLanguageByTag`
  matches names in any case, then extensions, then common aliases (`golang`,
  `shell`, `csharp`, `{.rust}`), and clears the language when nothing matches.
