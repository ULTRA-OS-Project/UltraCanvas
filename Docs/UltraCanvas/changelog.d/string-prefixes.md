- **A string's prefix is coloured with the string.** Python's `r"…"`,
  `rb"…"`, `f"…"` and C / C++'s `L"…"`, `u8"…"` showed the prefix as a separate
  name in front of the string. A per-language list, `stringPrefixes`, now joins
  a listed prefix written directly against the quote to its string, in any
  case. Where the string ends is unchanged, and rightly: Python still lets a
  backslash keep a quote from closing a raw string (`r"\""` is one string).
- **A variable named `e`, `f`, `L` or `u` is no longer coloured as a number.**
  The number test accepted a word made only of a number suffix (`f`, `L`, `u`)
  or the exponent letter (`e`, and so `e1`), in every language. A number now
  has to start with a digit or a point and hold a digit; `1e5`, `.5` and
  `1.5f` are unchanged.
- `SyntaxTokenizationRules::hasRawStrings` and `rawStringPrefix` are removed:
  nothing read them, and the string switches that do the work replaced them.
