- **A scrolled view no longer leaves glyph fringes beside and below it.**
  Text drawn at the edge of a scrolling container marks the pixel just
  outside it with its anti-aliased fringe, and a scroll repainted exactly the
  container's box, so that pixel was never painted over: scrolling built up a
  faint dotted column beside the text (yellow, from sub-pixel anti-aliasing)
  and slivers of the line scrolled past the bottom edge. A scroll now repaints
  2px past the container's box.
- **Resizing a window keeps the scroll position of everything in a split
  pane.** `UltraCanvasSplitPane::Arrange` first ran the ordinary block layout,
  which stacked the panes one under the other at their full content height,
  and only then placed them side by side. That throwaway pass clamped every
  scroll view inside a pane to the top, so a resize sent a scrolled message,
  list or document back to its start. The split pane now takes its box with
  the new `UltraCanvasUIElement::ArrangeOwnBox` and places its panes once,
  then runs the container's post-layout steps through the new protected
  `UltraCanvasContainer::FinishArrange`.
