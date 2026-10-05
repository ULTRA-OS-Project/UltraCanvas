- **Tooltips sit above and to the right of the pointer.** A tooltip's top-left
  corner was put 10 px right of and 10 px below the pointer's tip: the body
  covered the lower half of the line the pointer was on, its soft shadow (10 px
  blur) reached over the rest, and the lines below went under it - the text
  being read was hidden by its own explanation, and the tooltip sat on the
  pointer's arrow. Now its body ends 20 px above the pointer and starts 12 px
  to its right (`TooltipStyle::offsetY` / `offsetX`, now the gap above and
  right of the pointer), so the line under the pointer and the arrow stay in
  view, and it follows the pointer as before. With no room above - near the
  top of the window - it goes below the pointer's arrow instead, and with no
  room on the right, to the left of the pointer
  (`UltraCanvasTooltipManager::UpdateTooltipPosition`). `UltraCanvasGroupBox`'s
  help tooltip, anchored at a spot rather than the pointer, now passes the
  info icon's top edge, so it opens above the icon instead of over it.
