- **A container sized to its content grows with a button grown to its
  text.** The first layout read a fixed-width button's given width before
  the button measured its text; the button grew afterwards, but its row
  kept the width it had measured, so a group of header buttons came out as
  wide as the English labels and the next item was laid over the last one
  (UltraStore's German "Updates" tab under its search field, "Clear
  finished" off the Downloads panel). A button grown during a layout pass
  now asks for one more pass, and `UltraCanvasWindowBase::UpdateAndRender`
  runs it at the next frame: a layout an element invalidated while being
  drawn is laid out and drawn again, and the event loop is woken for it
  instead of waiting for the next mouse move. `ButtonGrowToFitTest` covers a
  group of grown buttons in a header row.
