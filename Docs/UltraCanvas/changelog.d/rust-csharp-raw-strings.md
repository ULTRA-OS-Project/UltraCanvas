- **Rust raw strings and C# verbatim strings end where the compiler says.**
  Their prefix changes where the string ends, and the tokenizer read them as
  ordinary strings with escapes: C#'s `@"C:\"` ran past its closing quote to
  the end of the line, Rust's `r"C:\"` on into the lines after it (Rust's
  ordinary strings may span lines), `r#"say "hi""#` ended at the
  first inner quote, and a C# verbatim string's doubled `""` or its second
  line came out as code. Two switches, `hashRawStrings` (Rust: `r`, `br`, `cr`
  with any number of `#`) and `verbatimStrings` (C#: `@"`, `$@"`, `@$"`), open
  them with no escapes and keep them open to their own end - `"` and as many
  `#`, or a `"` that is not doubled - across lines, through the same end marker
  in the line state that C++ raw strings and Lua long brackets use. Rust's raw
  identifiers (`r#match`) and C#'s (`@class`) stay names.
