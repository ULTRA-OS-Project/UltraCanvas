- **HTML borders per side.** Every border property set all four sides alike, so a
  mail's rule under its header (`border-bottom: 1px solid #eee`) or a quote's left
  accent bar became a full box. Each side now has its own width, style and colour.
  - `border`, `border-top` / `-right` / `-bottom` / `-left`, `border-width` /
    `border-style` / `border-color` with 1-4 values, and the per-side longhands
    (`border-left-color`, ...). `ComputedStyle` has `borderTop` ... `borderLeft`
    (`BorderSide`) in place of `borderWidth` / `borderColor`.
  - As in CSS, a border without a style draws nothing (`border: 1px #ccc`), a style
    alone is `medium` (3px), and a border without a colour takes the element's final
    text colour (`currentColor`), even when `color` comes after it.
  - `dashed` and `dotted` are drawn dashed and dotted; the other styles solid.
  - `<hr>` is its border box, as in a browser: by default a 1px inset rule (darker
    above, lighter below); `border: none; border-top: 1px solid #ddd` gives the
    author's line, `height` with a `background` a bar.
  - Fixed (Cairo): with borders that differ per side, a dashed side was stroked in the
    previous side's colour and passed its dash on to the sides drawn after it.
