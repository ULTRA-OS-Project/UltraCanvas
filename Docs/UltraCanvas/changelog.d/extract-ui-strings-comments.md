- **`scripts/extract_ui_strings.py` reads past a `"/*"` in a string.** The
  extraction stripped comments with a regular expression, so a `/*` inside
  a string literal (Texter's list of comment markers) swallowed the code up
  to the next `*/` - 1,800 lines of Texter's editor, with 17 `Tr` sites and
  its crash-recovery plural - and a `•` in a literal stopped it. The
  comments are now stripped by a walker that steps over string and
  character literals, and a C++ escape is decoded as the compiler emits it.
