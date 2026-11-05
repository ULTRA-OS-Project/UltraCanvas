- **`UltraCanvasFileLoader::ExtractText`: the words in a file as plain
  text.** A word processor document or a deck (through `LoadTextDocument`), a
  PDF (every page's text; a scanned PDF without any is rendered at 300 DPI and
  read by text recognition), a spreadsheet (every sheet, tab-separated, under
  its name when there are several), a text, Markdown, CSV or code file (as it
  is, also of a type the inventory does not know when it reads as UTF-8), a
  picture (text recognition, on white where it is transparent) and a sound or
  a video (speech to text). `GetTextExtractionMethod`, `CanExtractText` and
  `WhyNoTextExtraction` say how a file would be read, whether this build can,
  and why not in a sentence for the user. Text recognition and speech to text
  are hooks the core calls on the caller's thread:
  `SetImageTextRecognizer` / `SetSpeechTranscriber` (and `Has...`). New
  source `core/UltraCanvasFileLoaderText.cpp`; FileLoader 1.11.0.
- **OCR plugin 0.2.0: Tesseract as FileLoader's text recognizer.**
  `RegisterOCRTextRecognition(languages)` installs it - every language
  installed when none are given, English when none is, the first alone when
  the languages sit in different folders - one engine shared by every call,
  one at a time. `UltraCanvasOCR::RecognizeRaw` reads pixels already in
  memory (1, 3 or 4 bytes a pixel). UltraClipboard's *Extract text* is the
  first user. `Docs/Modules/FileLoader/README.md`, *Text From Any File*.
