- **The Rename button on a hovered file shows a "T", not a pencil.** The
  pencil looked like "edit the file"; the button renames it, so it now draws
  a text glyph. The strip's tooltips also name each button's keyboard
  shortcut (Ctrl+C, Ctrl+X, F2, Del). UltraCanvasFilerWidget does this
  (framework changelog: `filer-rename-text-glyph`).
- **Cut on a hovered file is greyed out on a drive**, as it is in the
  right-click menu: the hover button had started a cut there that could not
  be completed.
