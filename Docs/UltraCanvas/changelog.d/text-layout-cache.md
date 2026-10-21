- **One way to keep a text layout between draws:
  `UltraCanvasTextLayoutCache`.** The inputs a layout is built from - the
  text, the font, the wrap, the alignment, the shortening, the width - are a
  key (`TextLayoutKey`), and `Get(ctx, key)` hands back the layout held
  while the key is the one it was built with, else a new one with the key
  applied. The label keeps its layout this way now, so no setter of its
  resets it (only the inline images, which are not in the key), and the
  button's middle-shortened text does too; the next element copies the
  pattern instead of choosing one. `IRenderContext::CreateTextLayout` counts
  the layouts it made (`TextLayoutsCreated`; the backend implements
  `MakeTextLayout`), and `TextLayoutCacheTest` draws a label and a button
  ten times each and finds one layout.
- **`scripts/check_version_history.py` lists the headers under
  `UltraCanvas/include` that carry no `Version:` line**, so the history a
  reader expects in a header's head is added where it is missing;
  `--strict` makes it a check.
