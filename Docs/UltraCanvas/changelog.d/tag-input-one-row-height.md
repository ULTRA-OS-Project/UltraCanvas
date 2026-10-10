- **A tag field made without a height starts as tall as one row of chips.**
  `CreateTagInput` gave it 36 px, but one row needs 40 at the default style
  (the 28 px chip and 6 px of padding above and below), so every such field
  grew by 4 px on its first frame - a visible jump of whatever sat below it
  on a settings page. With no height given (the default now, `h = -1`) the
  field starts at `OneRowHeight()`, in the layout's `size.height` too, and
  `Tests/TagInputGrowTest.cpp` checks that the first frame keeps it. A
  height passed explicitly is used as before.
