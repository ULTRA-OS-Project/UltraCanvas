- **A checkbox, radio button or switch can wrap its label.**
  `SetLabelWrap(true)` on any `UltraCanvasLabeledToggleBase` breaks a label
  longer than the toggle is wide into lines at its width: the toggle grows a
  line at a time, the box, dot or track sits level with the first line, and
  its min-content width is its longest word. Off by default, so every toggle
  in the framework and the applications stays one line as before. It is what
  keeps a translation that runs longer than the English from being cut off
  in a fixed-width settings page.
- **The bitmap editor's undo steps are named in the person's language.**
  `UCRasterDocument` names its steps through `Tr` - Duplicate, Delete, Move,
  Show, Hide, Lock, Unlock and Rename Layer, Layer Opacity and Blend Mode,
  Merge Down, Flatten Image, Rasterize Layer, Scale Image, Canvas Size,
  Crop, the flips and turns, Delete, Fill, Add Layer and Selection - so an
  application's Undo / Redo tooltip reads in that language. The turns are
  now "Rotate 90° Clockwise" / "Counter-clockwise", as the menus say them.
  `AddLayer` and `CommitSelectionChange` take an empty label for their
  default ("Add Layer", "Selection") instead of a default English one.
- **The colour picker speaks the person's language.** The four design names
  in its selector, the "Channels" heading of its sliders and the tooltips of
  its models and channels go through `Tr`; the channel letters (H, S, V, R,
  G, B, A) and the model names (HSV, HSL, RGB) stay as they are.
- All of these texts are in the standard set, translated in every language.
- **The bitmap document's layer names and errors are translated.** A new
  image's "Background", "Layer N", the flattened layer and a layer whose
  project names none take the person's language when they are made (a layer
  keeps the name it was saved with). Every load and save error
  `UCRasterDocument` reports - a missing or corrupt project, a damaged
  UltraCanvas document, a file name without a type, a build without libvips -
  is a whole sentence through `Tr` / `TrF`. The reason
  `FileWriteFailureReason` adds (`UltraCanvasFileError.h`) is still English:
  that header is dependency-free by design.
