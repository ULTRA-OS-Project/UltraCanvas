- **The date picker's hint is the one the application set.**
  `UltraCanvasDatePicker::SetPlaceholder` now reaches the field after it
  was built; until now the field kept "Select a date" whatever was set
  later, which is what UltraFIBU's start window showed for its
  *TT.MM.JJJJ*. `GetPlaceholder` reads it back.
- **A button's text can lose its middle instead of its end:
  `ButtonStyle::ellipsize`, `SetEllipsize`.** `EllipsizeMiddle` keeps the
  first and the last part of a path readable, `EllipsizeStart` the end;
  the default is unchanged; the layout the text is shaped with is kept
  from draw to draw, as the label keeps its own, and rebuilt only when the
  text, the width, the font, the alignment or the mode changes. The start
  page's recent entries use it, so a long folder keeps its drive and its
  name on the button.
