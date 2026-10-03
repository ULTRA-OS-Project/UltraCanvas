- **HTML `width` / `height` size a block's content, as in CSS.** Every HTML box counted
  its padding and border inside the `width` / `height` it was given, so a 30px-high box
  with 10px borders and 4px padding kept 2px for its text. Now (CSS's initial
  `box-sizing: content-box`) they size the content and padding and border go around
  them: `width: 120px; padding: 4px; border: 10px solid` is 148px wide.
  - `box-sizing: border-box` is read (`ComputedStyle::borderBoxSizing`) and keeps the
    given size for the whole box, `max-width` included.
  - A percentage width is the content's share of the line, padding and border added
    (`width: 50%; padding: 0 10px` on a 400px line is 220px plus its border).
  - `max-width` limits the content (the box with `border-box`).
  - Tables and their cells keep sizing the box as a whole, as browsers size them;
    images already sized their picture. `ApplyBoxStyle` takes `borderBoxSizes` for
    such callers.
