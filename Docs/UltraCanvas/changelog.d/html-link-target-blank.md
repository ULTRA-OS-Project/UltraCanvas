- **Links with `target="_blank"` can open in a new window.**
  `UltraCanvasWebView::onOpenInNewWindow` (WebView 1.4.0) gets the absolute
  address of a link that asks for another window: `target="_blank"`, a
  target naming a window, or no target under `<base target="_blank">`.
  `_self`, `_parent` and `_top` stay in the view. Such a link opens in the
  view as before while the callback is unset, and `mailto:` still goes to
  `onExternalLink`. Underneath, `HTML::BuildOptions::onLinkActivatedNewWindow`
  (builder 1.34.0) takes those clicks, on text links and linked pictures
  alike. `WebViewTest` and `HTMLImageAlignTest` cover it.
