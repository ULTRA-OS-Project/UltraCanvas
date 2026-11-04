- **`scripts/extract_ui_strings.py` reads a `\xHH` escape as a byte again.**
  The escape decoder of 0.9.483 took `\xc3\x97` as two code points, so an
  UltraSnip catalogue with a multiplication sign written that way came out
  as mojibake and `check_locale_catalogues --strict` was red on `main`. A
  hex or octal escape is a byte of the UTF-8 text, as the compiler emits
  it; `\uXXXX` stays a code point.
