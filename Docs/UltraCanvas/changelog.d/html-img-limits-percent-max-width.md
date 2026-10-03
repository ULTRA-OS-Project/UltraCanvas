- **HTML `<img>` honours min / max sizes; `max-width` in percent is kept.**
  - `min-width`, `max-width`, `min-height` and `max-height` on an image are applied as
    CSS applies them to a replaced element (CSS 2.1 §10.4): a size not given follows
    the picture's shape - `max-height: 100px` on a 300x200 picture shows it at
    150x100, `min-width: 96px` on a 24x16 icon at 96x64 - and when both limits bind,
    both win. A given width or height keeps its value. Images used to ignore them.
  - `max-width` in percent (`ComputedStyle::maxWidthPercent`) is kept instead of
    dropped: on an image it replaces the built-in "no wider than the line" limit
    (never above it), and blocks and tables are capped at that share of their line;
    a block with `max-width: 50%; margin: 0 auto` is centred.
  - `UltraCanvasImageElement::SetHeightFollowsWidth(true)`: a width larger than the
    picture's grows its height in proportion too (an `<img width="800">` of a 400x200
    picture is 800x400, not 800x200). Off by default; HTML images turn it on.
