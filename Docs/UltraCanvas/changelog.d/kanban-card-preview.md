- **Kanban board: a card's picture beside its text, a context menu, hover and
  a larger preview.** `KanbanBoardStyle::cardImagePlacement`
  (`KanbanCardImagePlacement::Top` / `Left`) puts a card's picture in a band
  above the title or in a square at its left with the text beside it;
  `centerCardTitle` centres the title. `onContextMenu(cardId, columnId,
  windowPos)` runs on a right click (the card is selected first),
  `onCardHoverChanged(cardId)` names the card under the pointer, and
  `ShowCardPreview(cardId, picture, size)` / `HideCardPreview()` draw a
  larger picture of a card beside it over the board, gone by itself when the
  pointer leaves the card, a button is pressed, the board scrolls or a drag
  starts. `GetCardBounds(cardId)` gives a card's laid-out rectangle.
  `KanbanFileDropTest` covers all of it.
