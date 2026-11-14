- **`UltraCanvasKanbanBoardElement::onCardPointerMoved(cardId)`** runs on
  every pointer move over a card (not during a drag), so a host can open a
  card's preview when the pointer comes to rest instead of after a fixed
  delay from entering the card.
