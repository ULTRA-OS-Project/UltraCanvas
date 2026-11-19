- **Markdown tables use the width they have.** A text area without word wrap
  (the media viewer's text view, which shows a deck's outline, Texter's
  Markdown tabs with wrap off) built its line layouts before it had measured
  its visible area, so every table was sized to the 100 px floor and broke
  its header words over two lines ("Quar-ter", "Reven-ue") with the whole view
  to spare - and nothing laid it out again. The area now remembers the width
  its tables were sized to and lays the lines out once more when the visible
  width turns out different (the first frame, a resize, a scrollbar coming or
  going). `TextAreaDetachedTest` checks a table is sized to its area's width.
