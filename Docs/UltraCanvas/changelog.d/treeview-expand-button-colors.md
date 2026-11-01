- **UltraCanvasTreeView: the +/- box takes the theme's colours.**
  `SetExpandButtonColors(fill, border, symbol)` sets the expand button's
  fill, its 1 px border and the `+` / `-` together. The border was always
  gray and the glyph always black, so on a dark theme the box could only be
  a light square with a black mark in it; ChatCube's Settings window is the
  first to use it, in its dark look. `SetExpandButtonColor` (the fill
  alone) is unchanged.
