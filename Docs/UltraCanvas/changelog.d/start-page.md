- **The start page every application shows with nothing open:
  `UltraCanvasStartPage`.** A container that lays out, top to bottom, a
  title, the reason this page and not the thing itself, the entries opened
  last as buttons (their whole path in a tooltip), the one action that
  opens an existing thing, a section of the application's own fields for
  a new one - laid out row by row by the page (`AddField` puts a caption
  and an input on a row, `AddNote` a line of text, `AddButton` a button)
  - and a status line in the theme's error colour when something could
  not be done; `PreferredHeight()` is what the window should be.
  UltraPress's start window (a site folder) and UltraFIBU's (a bookkeeping
  file) are both this page with their own words and fields
  ([doc](UltraCanvasStartPage.md)).
