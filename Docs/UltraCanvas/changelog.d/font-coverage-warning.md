- **The WYSIWYG editor now names the fonts a document needs that the computer
  lacks.** `UltraCanvasRichTextEdit` shows a notice along its top that lists
  two kinds of font:
  - **Scripts no font covers.** Before, their text came out as rows of hex
    boxes that looked like a bug in the editor. Each script is now named with
    a free font that covers it, for example "Chinese text needs Noto Sans
    SC".
  - **Families the document names that are not installed.** This text is
    drawn in a substitute.

  Each font in the notice is a link to the page it is downloaded from, and
  the notice's x hides it. `GetFontReport()` and `onFontReportChanged`
  report the same list to the host, `onFontLinkClicked` lets the host handle
  a link itself, and `SetShowFontNotice(false)` keeps the report but hides
  the notice. The report is worked out when a document is set, a second
  after an edit, and when the fonts change. That includes Windows' system
  fonts arriving after a cold start. The demo app's *WYSIWYG — Chinese,
  Arabic & Myanmar* page shows the notice on a computer that has no font for
  Chinese or Arabic.
- **New `UltraCanvasFontCoverage.h`: is there a font for this text?** It has
  four calls:
  - `CountCharactersWithoutFont` counts the characters of a text that no
    installed, bundled or registered font covers.
  - `FindScriptsWithoutFont` groups those characters by script. Each script
    comes with a free Noto font that covers it and that font's download
    page.
  - `FontDownloadUrl` gives the address of a search for a font family on
    Google Fonts.
  - `IsFontFamilyAvailable` says whether a family is installed, bundled or
    registered.

  The new headless `FontCoverageTest` covers them, and
  `RichTextEditElementTest` covers the report and the notice.
