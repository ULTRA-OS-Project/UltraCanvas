- **HTML reader: `display: flex` and `display: grid` are laid out as flex and
  grid**, on the CSSLayout engines. Before, they were read as `display: block`
  and stacked every item in one column. Mail, eBooks and anything else
  rendered through `HTML::ElementBuilder` now get rows, columns and card grids.
  - The resolver reads `flex-direction` / `-wrap` / `-flow`, `flex` and its
    longhands, `order`, `gap` / `row-gap` / `column-gap`, `justify-*`,
    `align-*` and `place-*`, `grid-template-columns` / `-rows` / `-areas`,
    `grid-template`, `grid` (rows / columns), `grid-column` / `-row` /
    `-area` and their longhands, and `grid-auto-flow`. Tracks can be px, %,
    `fr`, `auto`, `min-content`, `max-content`, `fit-content()`, `minmax()`,
    `repeat(N, ...)` and `repeat(auto-fill | auto-fit, ...)`. Grid lines can
    be negative, `span N`, or area names. `inline-flex` and `inline-grid`
    are inline boxes. New `ComputedStyle` fields: `layoutMode` and the flex /
    grid container and item fields (`HTMLStyleResolver.h`).
  - The builder makes every child element an item at its own size, with real
    margins, and every run of text between them an anonymous item. A
    `repeat(auto-fill, ...)` is counted against the grid's width, estimated
    from the viewport down through its ancestors. Track counts, line numbers,
    spans and `order` are capped at `HTML::kMaxGridLines` (1000).
  - `Tests/HTMLFlexGridLayoutTest.cpp` lays out flex and grid pages and checks
    the positions. `HTMLReaderTest` covers the parsing.
- **A deeply nested message crashed the reader.** Ten thousand nested
  `<div>`s, a few kilobytes of hostile mail, overflowed the stack. The style
  resolver, the builder, the layout and the DOM's own destructor all recurse
  once per level. `HTML::Parser` now nests elements at most
  `HTML::kMaxTreeDepth` (128) deep. An element that would open deeper is kept
  as a leaf, and what follows it lands beside it, as browsers flatten past
  their own limit.
- **CSSLayout: nested flex boxes took time exponential in their depth.** An
  element cached one measurement, and a flex parent measures a child several
  ways, again each time it is measured itself. Twelve nested flex boxes took
  0.7 s, twenty about a minute. `Element::measureCache` keeps the last eight
  measurements under other constraints, so 128 nested levels now lay out in
  0.08 s. Code that drops a cached size by hand calls
  `ForgetMeasurements()` instead of `measured.valid = false`.
- **CSSLayout grid: wrapped text overflowed its row.** Rows were sized from
  each item's one-line (max-content) height before the columns' widths were
  known. Columns are now sized first, and rows measure each item at the width
  of the columns it spans (CSS Grid §12.1).
- **CSSLayout grid: an auto-placed item spanning more columns than the grid
  has hung auto-placement.** The cursor looked forever for a row the item
  fitted in. It now adds implicit columns, as CSS does.
- **Fuzzing for the HTML reader** (`Tests/Fuzz`, see its README). There are
  libFuzzer targets for the parser and for the CSS, with AddressSanitizer and
  UndefinedBehaviorSanitizer, plus a builder-and-layout target, a seed corpus
  and dictionaries. Every `BUILD_TESTS` build runs all three as deterministic
  ctests (`HTMLParserFuzzSmoke`, `CSSFuzzSmoke`, `HTMLBuilderFuzzSmoke`) under
  any compiler. The new `html-fuzz.yml` workflow fuzzes for four minutes per
  target whenever the reader changes, and for forty on Sundays. The source
  list of the reader's framework-independent part is now one file,
  `Tests/HTMLReaderCoreSources.cmake`, shared by the tests and the fuzzers.
