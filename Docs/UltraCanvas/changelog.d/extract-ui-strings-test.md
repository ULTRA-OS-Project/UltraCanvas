- **The extractor's reading rules are tested.** `scripts/tests/test_extract_ui_strings.py`
  holds what `extract_ui_strings.py` reads the sources by - a literal's
  escapes (a `\xHH` or octal escape a byte of the UTF-8 text, `\uXXXX` a
  code point), comments stripped without a `"/*"` or a URL's `//` inside a
  string swallowing the code after it, a call's literal arguments, the sites
  a file yields - after two escape bugs in two days; `locale-catalogues.yml`
  runs it before the catalogue check.
