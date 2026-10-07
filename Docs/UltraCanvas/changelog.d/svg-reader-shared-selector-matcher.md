- **The SVG reader matches `<style>` selectors through the HTMLReader's
  matcher.** 0.9.183 moved selector matching into `CSSStyleSheet.h` so that
  the Vector plugin's SVG reader would stop carrying its own copy, but the
  reader still matched with that copy (`SelectorMatches`, `CompoundMatches`,
  `PseudoMatches`, `AttributeMatches` in `UltraCanvasSVGConverter.cpp`). It
  now supplies a traits type for tinyxml2 elements to `HTML::MatchingRules`,
  and the copy is gone. `SVGConverterTest` covers what the traits decide:
  `:first-child`, `:nth-of-type`, an attribute selector on a camelCase
  attribute (`[pathLength]`), `^=`, and `:root`.
- `scripts/check_html_reuse.py` also reports a selector matcher written
  outside the module (`SelectorMatches`, `CompoundMatches`, `MatchSelector`),
  so a copy like that one is caught.
- `UltraCanvasHTMLReader.md`: the style-sheet example named a variable
  `inline`, a C++ keyword, so it did not compile; it is `declarations` now,
  and the matcher listing shows its namespace. Every example on the page
  compiles against the headers (`scripts/check_doc_examples.py`).
