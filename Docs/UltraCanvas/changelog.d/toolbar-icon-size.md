- **Toolbar icons are drawn at the size the toolbar asks for.**
  `ToolbarAppearance::iconSize` is documented as Small 16, Medium 24 (the
  default), Large 32, ExtraLarge 48 and Huge 64 px, but every toolbar button
  got 20 px whatever it said, and a later `SetAppearance()` reset buttons that
  had their own size to 20 again. A line icon is drawn on the pixel grid of
  one size, so the 24 px sets most toolbars use (`media/icons/menu/`,
  `media/icons/texter/`, ArtCreator's, UltraPaint's, the media viewer's) were
  scaled to 5/6 and blurred, and UltraVideo's 16 px set was blown up to 20.
  `UltraCanvasToolbar` now gives its buttons the size the appearance names
  (`ToolbarAppearance::IconPixelSize()`, Custom is `customIconWidth` x
  `customIconHeight`), when they are added and again on `SetAppearance()`.
  Toolbars with the default Medium size - Texter, UltraPaint, ArtCreator, the
  media viewer and UltraViewer, the DemoApp's - draw 24 px icons now, and
  UltraVideo's Small bars 16 px; a toolbar grows to fit them as before.
  `UltraCanvasButton::GetIconSize()` is new. `ToolbarThicknessTest` checks
  each size, for buttons added before and after the appearance changes.
