- **The clipboard history's rows in the person's language.** Everything
  `UltraCanvasClipboardHistoryView` paints goes through `Tr`: the section
  headers (*PINNED*, *TODAY* ...), the meta line (`DescribeClipboardEntry`:
  the kind, "2 lines", "29 characters", "4 files", *cut*), sizes
  (`FormatClipboardSize`), ages (`FormatClipboardAge`: "just now", "5 min
  ago", "today 09:41", the date with the standard set's short month names),
  the action tooltips and the *Copied* pill, which now grows to fit its
  word. Titles are stored in the history in English and searched that way,
  so they are translated where they are shown: `ClipboardEntryDisplayTitle`
  makes an image's "Image" and copied files' "a, b and 2 more" again, and
  `ClipboardEntryKindLabel` is the kind's name to show (`ClipboardEntryKindName`
  stays the English one search matches). UltraDesktop's quick panel and
  UltraClipboard both draw with it. View 1.3.0.
- **`UltraCanvasFileLoader::ExtractText`'s messages are translated** -
  `WhyNoTextExtraction`'s reasons and every error (FileLoaderText 1.1.0).
- **The standard set gains 38 entries, drafted in all 29 languages.** For
  the five that UltraFiler, UltraSnip or UltraTexter had already translated
  (*Save as*, "{n} line", "and {n} more", "{0} bytes", *The picture could not
  be read.*) the standard set takes the application's translation, and the
  application's copy goes; the Text base set's *Code* likewise. The rest are
  machine drafts awaiting review, as the files' `@translated-by` already say.
- **OCR: the language data is read by its UTF-8 name.** Tesseract opened
  `<dataPath>/<lang>.traineddata` with `fopen`, which takes the name in the
  ANSI code page on Windows, so a data folder under a Thai or emoji user
  name or install path failed to load. With Tesseract 5 the engine now hands
  `Init` a `FileReader` that opens the files through `OpenFileUtf8`
  (Tesseract 4, which no Windows build uses, keeps its own reader).
  `FileLoaderExtractTextTest` recognises with the data copied into a folder
  with a Thai name.
