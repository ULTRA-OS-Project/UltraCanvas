- **A vertical toolbar from the builder is as wide as it was asked to be
  again.** `UltraCanvasToolbarBuilder` creates every toolbar at 800 x 48
  before it knows the shape. Since toolbars started treating their constructed
  thickness as a floor (0.8.44), turning one
  vertical kept that 800 px width as its minimum, and `SetDimensions()` never
  replaced it: at the origin it only set the bounds, which layout overwrites,
  and away from the origin it set a size the old floor still beat. Texter's
  markdown side toolbar asked for 40 px and took half the window. The same
  stale floor held horizontal builder toolbars to at least 48 px, so a 24 px
  status bar could not be 24 px. The builder now sets the toolbar's thickness
  again, from the width or height it was given (or 48 when it was given none),
  after both `SetOrientation()` and `SetDimensions()`, in either order.
  `UltraCanvasToolbar::SetThickness()` is the new public call for this.
  `Tests/ToolbarThicknessTest` pins both cases.
