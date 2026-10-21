- **One way to keep a text layout between draws:
  `UltraCanvasTextLayoutCache`.** The inputs a layout is built from - the
  text, the font, the wrap, the alignment, the shortening, the width - are a
  key (`TextLayoutKey`), and `Get(ctx, key)` hands back the layout held
  while the key is the one it was built with, else a new one with the key
  applied. The label keeps its layout this way now, so no setter of its
  resets it (only the inline images, which are not in the key), and the
  button's middle-shortened text does too, and so do the text input (its
  text and its placeholder), the dropdown (the closed button's text and
  each row of its list) and the menu (each entry's label and header),
  which shaped their text on every draw before; the next element copies
  the pattern instead of choosing one. `IRenderContext::CreateTextLayout` counts
  the layouts it made (`TextLayoutsCreated`; the backend implements
  `MakeTextLayout`), and `TextLayoutCacheTest` draws a label, a button, a
  text input, a dropdown and a menu bar ten times each and finds one
  layout per text.
- **`scripts/check_version_history.py` lists the headers under
  `UltraCanvas/include` that carry no `Version:` line**, so the history a
  reader expects in a header's head is added where it is missing;
  `--strict` makes it a check.
- **A menu entry's width is measured once.** `CalculateItemWidth` keeps
  each entry's width with what it was measured from (the label, the
  shortcut, the font, the icon, the submenu, the orientation, the style's
  spacings) and measures again only when one changed; every layout pass
  asked for every width before, and each ask shaped the label. A menu
  drawn without a window (a test's offscreen surface) estimates its widths
  instead of dereferencing a context it does not have. The kept layout and
  the kept width are one slot per entry, moved with the entries by
  `InsertItem` and `RemoveItem`, so the entries that only shifted rebuild
  nothing.
- **The render context doc explains the context's own text cache**: what
  its key holds (the text, the size, the font, the text style, the
  resolution) and leaves out (the device scale, the font options), and how
  an element's own layout differs from it.
- **The dropdown, the tree view and the timer headers carry a version
  history**, reconstructed from the framework changelog.
