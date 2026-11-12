- **A document view resized at its end showed the desk below the last page.**
  `UltraCanvasRichTextEdit` keeps its scroll offset in document pixels, so a
  taller view - or a zoom-out - leaves more of the document on screen and the
  end of the scroll range moves up. The horizontal offset was clamped wherever
  the visible area was recomputed; the vertical one only at the end of a layout
  pass, and a size change dirties no layout in page view, because the wrap
  width is the page's and not the view's. A window grown taller while the view
  sat at the end of a long document therefore stayed where it was - in the test
  case, 540 px past the last page, with nothing on screen to say why. Both
  offsets are now clamped in the same place.
  - A zoom was the suspected way in and is not one: `SetZoom` asks for a fresh
    layout, so the pass that recomputes the content height clamps the offset,
    and any scroll before it does the same. `RichTextEditElementTest` covers
    all four paths - 50%, 25%, fit-whole-page and a taller element - so the
    next one that stops dirtying the layout is caught.
