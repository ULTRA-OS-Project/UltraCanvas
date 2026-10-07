- **Transforms are written dot-decimal whatever the desktop's locale.**
  `SerializeTransform` (`UltraCanvasVectorStorage.cpp`) formatted with
  `std::to_string(double)`, which renders through `LC_NUMERIC` - and the
  Linux backend sets that from the environment for XIM. On a comma-decimal
  desktop (German, French, Russian, Brazilian Portuguese, ...) every
  transform the SVG writer saved, on groups, gradients and patterns, came
  out as `matrix(0,866025,0,500000,...)`, whose commas are also the
  separators, so a saved drawing reopened with its rotated and moved groups
  somewhere else. It now formats through `FormatFloatClassic` at twelve
  significant digits, which keeps everything `"%f"` kept below a million (a
  CAD drawing's 250000.5 stays 250000.5) without the trailing zeros.
  `scripts/check_locale_numbers.py` did not flag it because it cannot see
  that the matrix entries are doubles. `Tests/SVGLocaleTest.cpp` now checks
  transforms, and a rotated group saved and reopened, under "C" and a
  comma-decimal locale.
