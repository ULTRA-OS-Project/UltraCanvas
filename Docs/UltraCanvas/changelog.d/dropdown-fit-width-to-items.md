- **`UltraCanvasDropdown::SetFitWidthToItems`: a dropdown as wide as its
  widest item.** A dropdown sized for its English entries cut a longer
  translation off - the button clips its text, so UltraFiler's video mode
  read *Automatisch al…* in German. On, the button takes the width its
  widest item needs (icon, text and arrow) and follows the items, the
  placeholder and the font as they change; the width given stays the least
  it takes, so a layout made for the English text does not move where that
  text fits. Off by default. `DropdownFitWidthTest` covers it.
