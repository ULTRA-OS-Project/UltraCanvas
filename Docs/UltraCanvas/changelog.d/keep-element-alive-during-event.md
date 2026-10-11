- **A handler that rebuilds the list it sits in no longer crashes.** A
  button whose handler rebuilds the container it is in - UltraPaint's layer
  rows, whose button selects the layer and rebuilds the list - dropped the
  last reference to itself from inside its own `OnEvent`, and went on to
  request a redraw on the freed object: double-clicking a layer's name
  crashed UltraPaint in `UltraCanvasUIElement::RequestRedraw`. The
  application's dispatch (`DispatchEventToElement`) now holds the element
  until its `OnEvent` has returned, whatever widget it is; one the handler
  removed finishes detached, where a redraw request does nothing, and is
  destroyed after the event. `MouseClickBubblingTest` rebuilds a toggle
  button's list from its own handler, on a press and on a double-click (it
  crashed before the fix).
- **A container scrolled to its end stays there when it is laid out
  again.** `UltraCanvasContainer::UpdateScrollability` set each scroll range
  first for a viewport measured with both scrollbars hidden - a track taller
  or wider than the real one - and setting a range clamps the position to
  it, so a container with both scrollbars, scrolled to the end, lost a
  scrollbar's thickness of scroll at every re-layout (UltraPaint's side
  panel jumped 16 px when a layer was clicked). The provisional viewports
  now only decide which scrollbars show; the ranges are set once, for the
  final one. `ScrollKeepsPositionOnResizeTest` lays out a view scrolled to
  its end again and finds it at the end (16 px short before).
