- **Hovered links are reported; size limits apply to the box their box-sizing
  names.**
  - `UltraCanvasLabel::onLinkHovered(href)`: fired as the pointer moves onto a
    text link (its href) and off it (`""`). `UltraCanvasImageElement` gains
    `onHoverEnter` / `onHoverLeave`. The HTML reader passes both on through
    `BuildOptions::onLinkHovered`, for text links and linked pictures alike - a
    mail reader shows the real target in its status line before the click.
  - CSSLayout: the block path applies `boxConstraints` (min / max width and
    height) to the box the element's box-sizing names - the whole box for a
    border-box element (every widget), the content for a content-box one - as
    the flex path and the documentation already did. Before, the block path
    limited a border-box element's content, so the same limit gave a box a
    padding's width wider in a block than in a flex column.
  - The HTML reader passes limits as the whole box's (a content-box limit gains
    the padding and border around the content), and table cells size their
    content from `width` / `height`, as browsers do.
