- **Text in labels can be selected and copied.** `UltraCanvasLabel::SetSelectable(true)`
  lets the reader drag across a label's text, double-click a word (a dot or
  an apostrophe between letters stays in it: `example.com`, `don't`),
  triple-click all of it and Shift+click to extend; Ctrl+C (Ctrl+Insert,
  Cmd+C) copies and Ctrl+A selects all. The highlight is drawn under the text
  in `LabelStyle::selectionColor`; what is copied leaves out inline pictures
  and soft hyphens and turns a no-break space into a space. A link in a
  selectable label opens when the button is released without having dragged,
  so a drag that starts on a link selects. Labels nobody made selectable work
  exactly as before.
  - **One selection across many labels:** `UltraCanvasTextSelection`
    (`UltraCanvasTextSelection.h`), shared by labels in reading order
    (`AddLabel`, `AddLabelsIn`). A drag runs on from one label into the
    next, to the nearest text when the pointer is between them, and scrolls
    the scroll view when it goes past its edge; a copy puts a line break
    between labels above one another and a tab between labels side by side.
    Only the pressed label accepts the keyboard focus, so a page of
    paragraphs is one Tab stop. `onContextMenu` and `onSelectionChanged` let
    the host offer Copy and Select All.
  - **HTML:** `HTML::BuildOptions::selectableText` gives every label of a
    built page one such selection (`HTML::BuildResult::textSelection`).
  - New tests: `LabelSelectionTest` (a window under Xvfb).
