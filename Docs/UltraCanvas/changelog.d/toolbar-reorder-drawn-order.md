- **A toolbar item dragged into another place stays there.** `MoveItem` puts
  the item at its new index in the bar's list of children at once, while every
  item keeps the rectangle it was last drawn in until the next layout - and the
  hit test that picked the drag's next target walked the list while reading
  those rectangles, so it answered with the index of whichever item used to be
  under the pointer and moved the dragged item straight back. Two drag steps
  inside one frame therefore undid each other, and a drag that ended on such a
  pair put the item down where it had started and reported nothing: UltraMail's
  account tiles could not be rearranged at all, and any drag whose steps outran
  the frames jittered. The bar now answers the two questions separately - which
  item is drawn under a point, and which place that point is over, counted in
  the order the items are drawn - and a drag asks for the place, which is where
  the item will be once the layout has caught up. `ToolbarItemReorderTest`
  covers it headless.
