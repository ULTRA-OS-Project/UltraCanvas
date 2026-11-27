- **The text bar's Font list is the font picker.** `UltraCanvasTextFormatBar`
  shows an `UltraCanvasFontPicker` in place of its plain dropdown, in the same
  210 pixels (down to 90 in a narrow place): every family drawn in itself with
  its format's icon after the generic Sans, Serif and Monospace; *In this
  document* from the host (`SetDocumentFonts` in `onFontListOpening`;
  `AddTextFormatFonts` gathers a text's families); *Recent* kept by the host
  in its settings (`SetRecentFonts`, `onRecentFontsChanged`); and the styles
  list beside a family with styles bold and italic cannot reach. The bar has
  Bold and Italic but no weight, so a style chosen there sets the family - the
  face's own name where the engine reaches it by that ("Ubuntu Light") - and
  Bold and Italic, reported as a Font change followed by Bold and Italic where
  they changed. The keyboard stays with the text while the list is open
  (`HasKeyboardFocus` says so). `GetFontPicker()` reaches the list. Doc:
  `Docs/UltraCanvas/UltraCanvasTextFormatBar.md`; test:
  `FontPickerElementTest`.
