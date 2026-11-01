- **A settings window in one kit.** `UltraCanvasSettingsPageKit`
  (`UltraCanvasSettingsPageKit.h`) holds the look of an application's
  settings window once: `UltraCanvasSettingsWindow` - the tree of pages on
  the left, the page on the right, the page's *Restore defaults* and *Close*
  at the foot - and a page's skeleton (title, caption, controls, the notes
  block) with the controls in the window's one type scale (radios,
  checkboxes, sliders with their value, buttons, colour boxes, rows).
  UltraFiler's settings window set the style and is built on the kit now,
  with nothing changed on screen; UltraSnip's is the second; the next
  application's gets the same look without a copy.
- **`UltraCanvasAppSettings`: a note on older files and on renaming a key.**
  A key an older file does not have reads as the fallback and `Set` makes
  the objects on the way, so a rename is read-old, set-new, remove-old,
  save; the header and the doc show it.
