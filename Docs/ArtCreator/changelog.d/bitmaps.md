- **Pictures dropped on the page go into the drawing, where they were
  dropped.** A PNG, JPEG or any other picture this build reads is placed at a
  pixel a point (shrunk to the page when larger), centred on the drop point;
  several dropped at once step down and to the right. A drawing dropped still
  opens in a tab of its own. With the Fill tool, a picture dropped on a shape
  fills it instead (Xara's drop onto an object).
- **File > Import... (Ctrl+Shift+I) brings a picture or a drawing into the
  drawing open now,** in the middle of the page - Open still gives a drawing a
  tab of its own. It is in the new **Bitmap** menu too.
- **Bitmap fills.** The Fill tool's *Fill type* has *Bitmap*: *Choose
  picture...* picks the picture (the selection takes it at once), a click on a
  shape fills it with one copy covering the shape, and a drag across a shape
  lays the picture's top edge from the press to the release, its shape kept.
  The selected shape's tile shows as a frame with three handles: the square
  moves the tile, the two circles stretch, turn and skew it (shift keeps the
  picture's shape). *Fit to shape* and *Actual size* lay it again. Selecting a
  bitmap-filled shape makes its picture the one the tool paints with. The fill
  repeats beyond the tile, follows the shape when it is moved, scaled or
  turned, and is kept by XAR and SVG (framework, *Bitmap fills*).
- **The Bitmap menu.** *Fill with Picture...* fills the selected shapes with a
  chosen picture; *Replace Picture...* swaps the selected picture for another
  (same width and place); *Save Picture As...* writes the selected picture's
  own bytes to a file; *Trace Bitmap* and *Trace Bitmap in Black and White*
  turn the selected picture into shapes with the Vectorizer plug-in (VTracer),
  laid exactly over it as a group; *Convert to Bitmap* and *Convert to Bitmap
  at 300 dpi* draw the selection into one picture that takes its place.
- **The status bar gives a selected picture's pixels and resolution** - "(480
  x 480 px, 172 dpi)" - beside its size on the page.
- **Pictures and bitmap fills survive a save as XAR,** ArtCreator's own
  format: they were dropped, and a bitmap fill saved as flat black
  (framework, *XAR writes pictures and bitmap fills*).
