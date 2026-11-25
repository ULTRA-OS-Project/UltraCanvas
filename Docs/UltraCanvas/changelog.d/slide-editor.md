- **A presentation could be shown, not edited.** New:
  - `UCSlideDeckEditor` (`UltraCanvasSlideDeckEditor.h`), the slide
    editor's core without a window: the slide being edited, the selection,
    and every change as a command with undo and redo - slides inserted from
    a layout, duplicated, moved, hidden and deleted; shapes moved, nudged (a
    run is one step), resized, turned, reordered, duplicated, deleted,
    copied and pasted, inserted (text box, drawn shape, picture), their text
    and the notes set - with hit testing of turned shapes, the handles and
    snapping to the slide and to other shapes.
  - `UltraCanvasSlideEdit` (`UltraCanvasSlideEdit.h`), its element: the
    slide strip (drag to reorder) and the slide, prompt text and outlines on
    empty placeholders, selection by click and band, dragging with guides,
    resize (Shift keeps the proportions, Alt from the centre, a turned shape
    in its own frame), turning, the keys, and a simple full-screen show.
    Documented in `Docs/UltraCanvas/UltraCanvasSlideEdit.md`.
- **The slide painter decodes a picture once**, not on every paint.

  Covered by `SlideDeckEditorTest`, `SlideEditElementTest` (the element
  driven by pointer and key events) and `PresentationPrintTest` (the
  drawing).
