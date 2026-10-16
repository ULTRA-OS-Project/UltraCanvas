- **A menu bar's first item keeps its label.** The menu measured its first
  item before it set its own font, in whatever font the window's context was
  last left in, and ellipsized the label into that width: in UltraPaint and
  ArtCreator, once the colour picker had laid itself out in its smaller font
  (choosing a design, opening its channels), *File* read "...". Every item is
  measured in its own font now, so the bar's items keep their width and
  place whatever was drawn or measured before them
  (`UltraCanvasMenu::CalculateItemWidth`; `MenuAndTabBehaviourTest`).
