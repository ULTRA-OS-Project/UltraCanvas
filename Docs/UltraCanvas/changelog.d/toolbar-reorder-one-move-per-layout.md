- **A toolbar item dragged into another place stays there.** `MoveItem`
  reorders the bar's children and asks for a layout, but their rectangles stay
  where they were until that layout has run - and the hit test that picks the
  next target read those rectangles while walking the new order, so it found
  the dragged item's index on its old neighbour's rectangle and moved the item
  straight back. Two drag steps inside one frame therefore undid each other,
  and a drag that ended on such a pair put the item down where it had started
  and reported nothing: UltraMail's account tiles could not be rearranged at
  all, and any fast drag of a toolbar item jittered. An item now moves at most
  one place per layout, so every step reads rectangles that mean what they
  say. `ToolbarItemReorderTest` covers it headless.
