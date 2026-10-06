- **EPUB files show their cover in the file display, as in Finder.** A folder
  of books was a wall of identical purple "EPUB" sheets: the thumbnail workers
  had no picture to read out of an e-book, and epub was on the list of
  containers no reader unpacks. `UltraCanvasFilerWidget` now shows the cover
  the book declares - in the thumbnail grids, the icon column of the Details
  and List rows, and (Display > Folder previews) peeking out of the folder
  that holds the books. It is cached like every other thumbnail, on disk
  included, so a library is read once. Display > Thumbnails > Docs, or the
  epub switch of the per-format list, turns it off; `GetPreviewableFormats()`
  now reports epub as thumbnail-supported in every build. mobi / prc / azw /
  azw3 keep their glyph.
  - **Reading a cover does not read the book.** The new
    `EPUBEngine::ReadCoverImageFromFile(path)` inflates three entries -
    `META-INF/container.xml`, the package document and the cover - and no
    chapter, table of contents or other resource, so a 300 MB illustrated
    book costs what a novel does. `EBookArchive::OpenFromFile` (which nothing
    called) now reads only the ZIP central directory and inflates entries
    from the file as they are asked for, opening the UTF-8 path through
    `OpenFileUtf8`; `EBookArchive::FileSize` lets a caller refuse an entry
    before inflating it, which the cover reader does past 64 MB.
  - **More books are found to have a cover.** The engine took a cover only
    from EPUB 3 `cover-image` or an EPUB 2 `<meta name="cover">` id, and
    otherwise from a manifest image whose id said "cover". It now also takes
    a `<meta name="cover">` that names the image's href instead of its id,
    and the `<guide>` cover reference; a declaration that names a cover
    **page** (the `cover.xhtml`, or the SVG-wrapped `titlepage.xhtml` Calibre
    writes) gives the picture on that page instead of the page's markup; a
    declared cover missing from the archive falls through to the next rule;
    and the last resort also matches an image's file name. The viewer's
    `GetCoverImage()` and `EBookMetadata::hasCover` follow the same rules.
    `EPUBEngine` 1.1.0, `EBookArchive` 1.1.0.
