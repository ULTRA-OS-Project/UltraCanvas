- **`UltraCanvasFilePreviews`: a registry of file preview providers.** A format
  registers, under its extensions, a thumbnail (`RenderThumbnail`) and/or a
  preview document (`ReadDocument`, the start of its text). Callers ask
  `RenderFileThumbnailFromProvider`, `RenderFileThumbnailAs` and
  `ReadPreviewDocument`. Any number of providers can be registered, and the
  latest registration of an extension wins. The registry is thread-safe for
  the Filer's thumbnail workers. The formats compiled in register themselves
  on the first query: the PDF first page, EPUB and Mobipocket covers, video
  poster frames, Word-processing documents and PowerPoint decks.
  `UltraCanvasFilerWidget` no longer includes the PDF, Word, PowerPoint,
  e-book or video-thumbnail code; it asks the registry. Its tiles, text
  previews and `RenderFileThumbnail` are unchanged: identical lines and
  pixels for every test file, before and after.
