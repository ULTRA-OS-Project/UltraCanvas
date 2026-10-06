- **Mobipocket and Kindle books show their cover in the file display too.**
  `.mobi`, `.prc`, `.azw` and `.azw3` tiles were still purple type sheets
  after EPUB got its covers in 0.9.173. `UltraCanvasFilerWidget` now shows
  their cover the same way - thumbnail grids, the Details / List icon column,
  folder previews, cached on disk - and `GetPreviewableFormats()` reports all
  four as thumbnail-supported in every build. A PalmDOC `.prc` with no
  pictures keeps its glyph.
  - **Reading a cover does not read the book.** The new
    `MOBIEngine::ReadCoverImageFromFile(path)` reads the record list, record 0
    and the cover record - the first bytes of the image records in between at
    most - and gives the picture `GetCoverImage()` gives once the book is
    loaded: the EXTH 201 image, else the first image. It decompresses and
    decrypts nothing, so a **DRM-protected** `.azw` and a HUFF/CDIC-compressed
    book, whose text the engine refuses, show their cover as well (Mobipocket
    DRM encrypts the text records only). The path is opened as UTF-8 through
    `OpenFileUtf8`, and a cover past 64 MB is refused before it is read.
  - `MOBIEngine::ParseRecord0` now reads the PalmDOC, MOBI and EXTH headers
    before it refuses a DRM or HUFF/CDIC text, so the cover reader shares it;
    what `LoadFromMemory` accepts, and the errors it reports, are unchanged.
    `MOBIEngine` 1.3.0.
