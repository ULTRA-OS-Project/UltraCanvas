- **The Filer widget can mark favorites.**
  `UltraCanvasFilerWidget::SetFavoriteMarkProvider` takes a function that
  says whether an entry is one of the host's favorites; each one it answers
  `true` for is drawn with a small red heart at its outermost left,
  vertically centred - in a 14 px gutter left of the icon in the Details,
  List and Size bars views (reserved for every row while a provider is set,
  so icons stay aligned), at the tile's left edge centred on the icon box in
  the thumbnail views, and inside treemap cells large enough for it. The
  widget keeps no list of its own: the provider is asked while painting, so
  `RequestRedraw()` is all a changed favorite needs. UltraFiler uses it for
  its Favorites view (UltraFiler 1.65.0).
