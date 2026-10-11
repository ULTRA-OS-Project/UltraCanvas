- **`UltraCanvasNewDocumentDialog` draws again, with its text in place.**
  Under the CSS layout it showed only its fields and buttons, stacked at its
  top-left corner. The container's own background was painted over the
  title, list and labels the dialog draws, and the layout ignored the bounds
  the fields were given. The fields are now pinned at their places
  (`SetElementAbsolutePosition` + `SetElementSize`), and the dialog paints
  its own background. Text sits on the rows and fields it belongs to:
  - a list row's name and category, which were 18 px low and across the
    next row;
  - the title, in its band;
  - "Document type:", clear of the filter field's border;
  - "File name:", which was drawn at the dialog's own offset in its window,
    over the title;
  - the "Will create:" preview, which was drawn below the dialog;
  - the count line, clear of the file name field.
  The selection tick no longer covers the category. Tested in the new
  `NewDocumentDialogLayoutTest`.
