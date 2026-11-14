- **C# 11 raw strings end at their own run of quotes, with no escapes.**
  `"""…"""` went through the shared `"""` string rule, which applied escapes,
  so `"""C:\"""` ran on past its end, and a raw string opened with four or
  more quotes (`""""…""""`, to hold `"""` inside) closed at the first three.
  A new switch, `quotedRawStrings` (C#), opens one at three or more quotes,
  after any `$` of an interpolated raw string, and keeps it open across lines
  until the same run of quotes, through the line state's end marker. `""` is
  still an empty string, and `@"…"` verbatim strings are unchanged.
- **A preprocessor line keeps its strings and comments.** A line starting with
  `#` was one token from the `#` to the end, so a raw string a `#define`
  opened never registered - the lines after it came out as code - and neither
  did a `/* comment */` opened there, the path in `#include "config.h"` or a
  trailing `// note`. Directive lines are now tokenized like code and drawn in
  the preprocessor colour except for strings, characters and comments, which
  keep their colour and their state across lines. A `#define` continued with a
  trailing `\` stays a directive on the next line, and an indented `#if` is a
  directive too (only column 0 counted before). C, C++, C# and GLSL.
