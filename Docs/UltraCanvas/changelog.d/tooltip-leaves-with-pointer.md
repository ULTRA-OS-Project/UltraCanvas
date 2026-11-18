- **A tooltip no longer floats over another program's window.** Since the
  tooltip got a window of its own above every other (so UltraDesktop's bar
  tooltips are not covered), nothing took it away when the pointer left the
  window it belongs to: a tooltip of UltraClipboard stayed standing over the
  window that had come over UltraClipboard - and one still waiting for its
  delay appeared there afterwards. Drawn inside its window it used to be
  covered with it. The pointer leaving that window (`MouseLeave`), or the
  window losing the focus (`WindowBlur`), now hides its tooltip at once and
  cancels one still waiting (`UltraCanvasTooltipManager::PointerLeftWindow`),
  and the element the pointer was over is left, so coming back to it shows
  its tooltip again; an element holding the pointer (a drag) keeps its hover.
