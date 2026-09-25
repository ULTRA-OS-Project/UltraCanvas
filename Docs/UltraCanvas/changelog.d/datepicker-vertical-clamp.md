- **The date picker's calendar is kept inside the window vertically, as it
  already was horizontally** (`UltraCanvasDatePicker::CalculatePopupPosition`).
  A calendar that fit neither below nor above its field was placed at a
  negative y, and what was cut off was its header - the month name and the
  arrows to change it. No other month could be reached. It is now pinned to
  the window's top edge in that case, so the navigation stays usable even
  where the grid covers the field. Found in UltraFIBU's start window, whose
  date field sat too low in a 440-pixel window; seen and checked under Xvfb
  before and after.
  - Not changed, and noted for a separate change: the calendar's "Today",
    "Clear", month and weekday names are fixed English strings
    (`UltraCanvasDatePicker.cpp`), with no way for an application to
    translate them. The placeholder and the first day of the week already are
    settable.
