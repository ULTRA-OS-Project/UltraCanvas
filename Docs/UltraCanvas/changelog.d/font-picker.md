- **A font list in the WYSIWYG style: `UltraCanvasFontPicker`.** A closed box
  that shows the current family in its own font, and a list that opens wider
  than the box (300 px) with the document's fonts (`SetDocumentFonts`; one the
  computer lacks in grey italics, *(not installed)* or *(shown in Arial)*), the
  recent fonts (`SetRecentFonts`, `onRecentFontsChanged`), a divider and *All
  fonts*: every installed family A to Z, each name drawn in itself at one size,
  with an icon for its format - O OpenType, TT TrueType, a Type 1, a pixel A
  bitmap (`media/icons/fonts/`). A symbol or dingbat font (Wingdings, Symbol,
  Zapf Dingbats, an emoji font) is named in the interface's font with a few of
  its own characters. A family with styles bold and italic cannot reach (Light,
  SemiBold, Condensed, Black) has an arrow: resting on it or Right opens its
  styles beside the list, each drawn in itself, and choosing one gives the
  family, weight, slant, width and the face's own family name
  (`FontPickerChoice`); a family of Regular, Bold and the italics alone has none
  (`SetStylesSubmenu` changes the rule). Typing jumps to the first family
  starting with the letters typed; Up, Down, Page Up / Down, Home, End, Return,
  Right, Left and Escape work. The rows are virtual and each name's picture is
  made once, prepared ahead in idle slices. Doc:
  `Docs/UltraCanvas/UltraCanvasFontPicker.md`; tests: `InstalledFontsTest`,
  `FontPickerElementTest` (a window test, run under Xvfb in CI).
- **The installed fonts with their styles and formats:
  `GetInstalledFontCatalogue()`** (`UltraCanvasInstalledFonts.h`). Every family
  the text engine draws - Pango's font map, so the names are the renderer's -
  with each style's name, CSS weight, slant, width, format (the font file
  module's `FontFileFormat`), file and own family name ("Ubuntu Thin", with
  whether the engine reaches the face by it with bold or without), and
  whether it is a symbol font, filled in from fontconfig's records on Linux,
  Windows (which reads the system's and the user's own font folder,
  `%LOCALAPPDATA%\Microsoft\Windows\Fonts`), Android and the browser, and from
  CoreText's descriptors on macOS (`OS/MacOS/UltraCanvasMacOSInstalledFonts.mm`).
  Windows' `@` vertical families, macOS' `.` private ones, LastResort, the
  engine's generic names, synthesized faces and fonts without characters are
  left out. Kept until `GetFontSetGeneration()` moves.
  `GetInstalledFontFamilies()` is now the names of the same enumeration (the
  hidden families left out); `FindInstalledFontFamilyOfName` and
  `IsInstalledFontName` know a face's own family name.
- **The rich text editor's font report names every family the document uses**
  (`FontReport::namedFamilies`), for a font list's top section, and no longer
  reports a face's own family name ("Segoe UI Semibold", "Ubuntu Thin") as a
  missing font.
- **The Styles dialog's font is the font picker**: every installed font, each
  in itself, instead of nine fixed names; a style from its list sets bold and
  italic too.
