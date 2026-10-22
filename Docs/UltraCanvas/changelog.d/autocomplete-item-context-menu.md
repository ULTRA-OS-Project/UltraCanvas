- **A menu on an autocomplete suggestion.** `UltraCanvasAutoComplete` has
  `onItemContextMenu(index, item, event)`: a right press on a row of the
  suggestion popup hands that row's item to the handler, which opens a popup
  menu acting on it, instead of picking it. Unset, a right press still picks
  the row as before. UltraMail's recipient fields use it for *Edit contact*
  and *Delete address*.
