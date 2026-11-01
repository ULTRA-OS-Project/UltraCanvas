- **Kanban cards can show a picture.** `UltraCanvasKanbanBoardElement::cardImageProvider`
  hands over a picture for a card - a file's thumbnail, a photo - drawn above
  its title in a band `KanbanBoardStyle::cardImageHeight` high. The board
  never loads anything itself: the provider answers with what is ready, and
  the caller invalidates the layout when a thumbnail made elsewhere arrives.
