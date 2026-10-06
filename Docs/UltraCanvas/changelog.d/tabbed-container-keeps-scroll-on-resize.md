- **Resizing a window keeps the scroll position of the page in a tabbed
  container.** `UltraCanvasTabbedContainer::Arrange` ran the ordinary block
  layout over its tab pages before placing the active one, and in that
  throwaway pass every scroll view on the page saw a viewport as tall as its
  content and was clamped to the top - the same fault the split pane had
  (0.9.154). The tabbed container now takes its box with `ArrangeOwnBox` and
  places only the active page and the overflow button, as it already did
  after that pass. A hidden page is laid out when it is switched to, as
  before. New test: `ScrollKeepsPositionOnResizeTest` scrolls a tall view in
  a split pane and in a tab, resizes the page, and checks the position.
