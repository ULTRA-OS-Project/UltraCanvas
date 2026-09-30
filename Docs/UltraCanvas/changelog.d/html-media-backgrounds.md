- **HTML mail: `@media` queries, background pictures, rounded borderless buttons,
  centred boxes.** Anthropic's sign-in mail showed a square-cornered button, a
  blank gap where its waving hand should be, and its footer columns stacked.
  - `@media` blocks (and `<style media="...">`) apply when their query holds for
    `HTML::BuildOptions::viewportWidth` (default 800px): `min-width`, `max-width`,
    `width` (px / em), `screen` / `all` / `print` / `not` / `only`; an unknown
    feature does not match. `StyleSheet::MediaMatches` answers a query list. The
    `<!-- ... -->` some mail wraps its style sheet in is skipped.
  - `background` / `background-image` pictures are drawn under the box's content,
    fitted by `background-size` (`contain` / `cover`, else unscaled). Of several
    `url()` layers the first that loads is shown (an animated GIF over its poster
    falls back to the poster). The background colour is found anywhere in the
    shorthand's last layer. Repeat and position are not honoured yet.
  - `UltraCanvasUIElement::SetBorderRadius`: rounded corners without a border - the
    background is filled rounded. The HTML reader uses it for `border-radius` on a
    borderless box.
  - `max-width` (px) caps a box and a table; `margin: 0 auto` (or `margin-left:
    auto`) centres (or right-aligns) a block or table that has a width or max-width.
