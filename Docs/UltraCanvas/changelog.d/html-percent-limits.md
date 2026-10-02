- **Percentage size limits everywhere, and percentage minus pixels.**
  - HTML `min-width`, `min-height` and `max-height` in percent are kept
    (`ComputedStyle::minWidthPercent` / `minHeightPercent` / `maxHeightPercent`), on
    blocks and images: `min-width: 75%` of a 400px line is 300px. A height percentage
    resolves against the container's set height, and limits nothing when it has none,
    as in CSS.
  - A percentage limit under `box-sizing: border-box` covers the whole box: with 10px
    padding and a 2px border, `max-width: 50%` of a 400px line is now 200px wide, not
    224px.
  - CSSLayout: `Dimension::offsetPx` (`Dimension::PctPlus(pct, px)`) adds pixels to a
    value as it resolves - `calc(50% - 20px)`.
  - CSSLayout: `Element::percentHeightBase` - a block parent records its set height on
    each child, so a child's percentage `minHeight` / `maxHeight` has a base; block
    children are measured with unbounded height and had none.
