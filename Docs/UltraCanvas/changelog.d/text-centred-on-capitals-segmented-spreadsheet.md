- **Segmented controls and the spreadsheet centre their text on its capitals.**
  Both centred a line of text by its line box (ascent plus descent), which
  sits wherever the font's metrics put it, so the Windows demo drew every
  segmented control's captions a couple of pixels high and every spreadsheet
  cell low enough to cut the descenders off. They now centre the middle of a
  capital letter on the middle of the segment or row, as buttons and labels
  already do:
  - `UltraCanvasSegmentedControl` gives each caption's layout the segment's
    height and its font as the layout's own (`SetFontStyle`) rather than as
    font attributes, which the layout's cap-height measurement ignored.
  - `UltraCanvasSpreadsheet` places cells, the row and column headers, the
    formula bar, the sheet tabs and the add-sheet button with
    `TextTopCentredOnCaps`.
  - `TextCentredOnCapsTest` draws both offscreen in the bundled
    Ubuntu font, and in FreeSans and Carlito where installed (line boxes that
    put the capitals 2.5 px high and 1.5 px low), and checks the capitals'
    middle is within a pixel of the middle.
