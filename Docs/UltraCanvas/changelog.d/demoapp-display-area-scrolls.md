- **DemoApp: example pages taller than the window could not be scrolled.**
  0.9.22 made containers scroll only when they ask to, and the demo's display
  area, its one scroll region, was not among the places opted in. Pages built
  at a fixed height were also shrunk to the window, so they never overflowed
  it, and their lower rows (the Colour Picker's variants, most of Slider,
  Split Pane, Tabs, ...) were cut off at the window edge. The display area now
  opts in, and a page with a fixed pixel height or width keeps it rather than
  being squeezed or stretched to the window (stretched, the vertical bar
  would narrow the viewport and fabricate a horizontal overflow). Pages with
  no fixed size are still fitted to the window and scroll their own content.
