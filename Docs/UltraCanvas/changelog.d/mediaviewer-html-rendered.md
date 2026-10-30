- **The media viewer shows an HTML page rendered, not its source.** An
  `.html`, `.htm`, `.xhtml`, `.xht` or `.shtml` file - and an `.xml` whose
  content is XHTML (an `<html>` root or an HTML DOCTYPE) - is now shown as a
  browser shows it, through `UltraCanvasWebView`, in UltraFiler's preview
  pane, its View window and every other host of `UltraCanvasMediaViewer`.
  Before, it opened in the read-only text view as highlighted markup, which
  is the Texter's job. The preview reads only local files: the page's style
  sheets and pictures come from the disk beside it, a link to another local
  page opens in place, and a web or `mailto:` link opens in the system's
  browser or mail client; forms send nothing and media shows its fallback.
  XML that is not a page (data, configuration, feeds) stays in the text view.
  `UltraCanvasMediaViewer::IsHtmlFile` says which files are pages; the info
  bar calls them `HTML`.
- **UltraCanvasWebView 1.6.0: `SetOpensWebLinks`, `ScrollBy`, `Clear`.** A
  viewer of local files can send `http` / `https` links to `onExternalLink`
  instead of following them, scroll the page from its own keys, and let a
  page, its players and the history go when it moves on.
