- **The rest of the controls centre their text on its capitals too.** After
  the segmented control and the spreadsheet, the last controls that centred a
  line of text by its line box - which sits high or low with the font - now
  centre the middle of a capital letter, as buttons and labels do:
  checkbox and radio labels (`UltraCanvasLabeledToggleBase`), the switch's
  state and side labels, a split button's two captions, tab titles and the
  dragged tab's ghost, the slideshow's counter, the password strength
  meter's percentage and the timecode field's digits.
  - `IRenderContext::GetFontStyle()`: the font `DrawText` draws in now, and
    `TextTopCentredOnCaps(row)` centring in it, for code that only sets a
    size or a weight.
  - `ITextLayout::GetCapHeight` measures the font the text starts in, font
    attributes included. A layout sized by a `ChangeAttribute` font size was
    measured in its 12 pt base font, so a larger caption centred with
    `VerticalAlignment::Middle` sat high.
- **Segmented control captions stay on one line.** A caption too wide for
  its segment is shortened with an ellipsis instead of wrapping onto a
  second line that ran out of the segment; the demo's disabled-segments
  control is wide enough for "Also Disabled".
- **A spreadsheet cell set to the Top vertical alignment draws at the top of
  its row.** Middle and Bottom (every cell's default, as in .xlsx and .ods)
  stay centred.
- `TextCentredOnCapsTest` also checks a checkbox's label and the cap height
  of a layout sized by a font attribute.
- The Mekko chart's segment labels are centred on their capitals too.
