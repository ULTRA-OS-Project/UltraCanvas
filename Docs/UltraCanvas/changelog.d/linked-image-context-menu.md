- **A right-click on a linked picture no longer opens the link.**
  `UltraCanvasImageElement` sent every mouse button to `onClick`, so a
  right-click on `<a><img></a>` in an HTML view opened the link. A right-click
  now goes to the new `onContextMenu` (with the event) and never to `onClick`.
- **`HTML::BuildOptions::onLinkedImageContextMenu`**: a right-click on a linked
  picture, with its raw href and the event, for the host's menu (Copy link).
  Text links are reached as before, through the selection's `onContextMenu`
  and `UltraCanvasLabel::LinkIndexAtPoint`. Documented in
  `UltraCanvasHTMLReader.md`.
