# UltraCanvasFilePreviews

What a file looks like, from whoever reads its format: a registry of **file
preview providers**. A format registers, under its extensions, a picture of a
file (a thumbnail) and/or the start of its text (a preview document); a caller
asks the registry and never names the format.

- Header: `include/UltraCanvasFilePreviews.h`
- Registry: `core/UltraCanvasFilePreviews.cpp`
- Built-in providers: `core/UltraCanvasFilePreviewsBuiltin.cpp`
- Tests: `Tests/FilePreviewsTest.cpp` (`ctest -R FilePreviewsTest`)

**Version:** 1.0.0
**Last Modified:** 2026-11-10
**Author:** UltraCanvas Framework
**Namespace:** `UltraCanvas`

## Contents

- [Why it exists](#why-it-exists)
- [Asking for a preview](#asking-for-a-preview)
- [Registering a provider](#registering-a-provider)
- [The built-in providers](#the-built-in-providers)
- [Threads](#threads)
- [Related](#related)

## Why it exists

`UltraCanvasFilerWidget` used to call every format it previews by name: the PDF
engine for a first page, the e-book engines for a cover, the video backend for
a poster frame, the Word and PowerPoint readers for a page of text. The file
display depended on each of them, and a format could not preview without the
Filer knowing it. Now a format registers a provider and the Filer asks the
registry, so a format can move into a runtime module and take its previews
with it.

`UltraCanvasVectorPreview.h` and `UltraCanvasModelPreview.h` do the same for
one family each, with a single provider; this registry holds any number.

## Asking for a preview

```cpp
#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasRichDocument.h"

FileThumbnailRequest request;
request.width = 96;              // logical pixels
request.height = 72;
request.fit = ImageFitMode::Contain;
request.scale = 2.0f;            // device pixels per logical pixel

if (CanRenderFileThumbnail("/home/user/Report.pdf")) {
    std::shared_ptr<UCPixmap> page = RenderFileThumbnailFromProvider("/home/user/Report.pdf", request);
}

// A file whose name hides its format: an Illustrator .ai that is a PDF.
auto drawing = RenderFileThumbnailAs("/home/user/Logo.ai", "pdf", request);

UCRichDocument document;
if (ReadPreviewDocument("/home/user/Letter.docx", document)) {
    // document.blocks: the paragraphs and tables, laid out as the caller likes
}

std::vector<std::string> pictures = FileThumbnailExtensions();   // "epub", "pdf", ...
std::vector<std::string> texts = PreviewDocumentExtensions();    // "docx", "pptx", ...
```

An extension may be given with or without its dot and in any case, or as a
whole path, whose last extension is used. A thumbnail request of no size draws
nothing. Every query returns null or false when no provider answers, the
provider is not available in this process (`Available()`), or it cannot read
the file - a damaged or password-protected one - so the caller shows the
type's icon.

## Registering a provider

```cpp
FilePreviewProvider provider;
provider.name = "my-format";                   // a later registration of this name replaces it
provider.thumbnailExtensions = {"myf"};
provider.documentExtensions = {"myf"};
provider.Available = [] { return true; };      // optional: a backend present at run time
provider.RenderThumbnail = [](const std::string& path, const FileThumbnailRequest& r) {
    std::shared_ptr<UCPixmap> pixmap;          // draw the file at r.width x r.height, r.scale
    (void)path; (void)r;
    return pixmap;                             // null: no picture of this file
};
provider.ReadDocument = [](const std::string& path, UCRichDocument& document) {
    (void)path; (void)document;                // fill document.blocks
    return false;                              // false: cannot read this file
};
RegisterFilePreviewProvider(std::move(provider));
```

- Extensions are matched lowercase and without a leading dot; a two-part one
  such as `fb2.zip` keeps both parts.
- Of two providers listing one extension, the later registered answers; one
  not available now is passed over for the next.
- A provider with no name, or with neither a `RenderThumbnail` and thumbnail
  extensions nor a `ReadDocument` and document extensions, is ignored.
- A provider that throws is contained: the query answers null or false.
- A plugin registers from its init; the framework's own formats register
  themselves (below), so an application never has to call anything first.

## The built-in providers

`RegisterBuiltinFilePreviewProviders()` registers the formats compiled into the
framework, once, on the first query:

| Provider | Answers | Extensions |
|---|---|---|
| `pdf` | the first page (PDF plugin builds, with an engine) | `pdf` |
| `ebook-cover` | the cover the book declares | `epub`, `mobi`, `prc`, `azw`, `azw3` |
| `video-poster` | a frame a short way into the clip | the containers the media codec registry decodes |
| `word-processing` | the document, the first page's header and footer inline | `odt`, `docx`, `doc`, `tex` |
| `powerpoint` | each slide's number and title, bullets, tables and notes | `pptx`, `pptm`, `ppsx`, `ppsm`, `potx`, `potm` |

The Filer draws its own styling over these: an outline round a PDF page, a
shaded edge on a book cover.

## Threads

Every function is safe to call from several threads at once, including while
another thread registers; the Filer's thumbnail workers do. A provider is
called outside the registry's lock, so it must itself be safe to run
concurrently - each call opening its own document, as the built-in ones do.

## Related

- `UltraCanvasFilerWidget` (`Docs/UltraCanvas/UltraCanvasFilerWidget.md`) -
  the tiles, the preview pane and `RenderFileThumbnail`
- `UltraCanvasVectorPreview.h`, `UltraCanvasModelPreview.h` - the
  single-provider seams for vector drawings and 3D models
- `UltraCanvasMediaCodecRegistry.h` - which video containers have a poster frame
