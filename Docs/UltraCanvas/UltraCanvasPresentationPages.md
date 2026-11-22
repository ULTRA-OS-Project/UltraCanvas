# UltraCanvasPresentationPages — a deck's slides as a document's pages

`UltraCanvasPresentationPages` is an `IPDFDocument` (`UltraCanvasPDF.h`)
whose pages are a presentation's slides. Whatever shows a PDF through that
interface shows a deck the same way: `UltraCanvasPDFView` draws each slide at
its zoom, lists them in its thumbnail strip, moves through them with its page
keys and searches their text, each hit framed on its shape. UltraViewer
(MediaViewer) opens decks this way.

Header: `UltraCanvasPresentationPages.h`. It reads a PowerPoint deck (`.pptx`
and kin, [UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md), with
its pictures) or an OpenDocument presentation (`.odp`, `.otp`, `.fodp`,
[UltraCanvasPresentationOdp](UltraCanvasPresentationOdp.md), with its
pictures and the look its styles give it); each slide is drawn by
`RenderPptxSlide`.

## Showing one

<!-- doc-check: std::string path; std::string password; std::shared_ptr<UltraCanvas::UltraCanvasPDFView> view; void AskForPassword(); void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationPages.h"
#include "Plugins/Documents/UltraCanvasPDFView.h"

using namespace UltraCanvas;

auto pages = std::make_unique<UltraCanvasPresentationPages>();
if (pages->Open(path, password)) {
    view->SetDocument(std::move(pages));
    view->SetZoomMode(UltraCanvasPDFView::ZoomMode::FitPage);
} else if (pages->GetLastOpenError() == PDFOpenError::NeedsPassword ||
           pages->GetLastOpenError() == PDFOpenError::WrongPassword) {
    AskForPassword();                        // protected: ask (again)
} else {
    ShowError(pages->GetLastErrorText());    // damaged, or no deck
}
```

| Member | Gives |
|---|---|
| `Open(path, password)` / `OpenInMemory` | reads the deck; no file handle is kept. `GetLastOpenError` is `NeedsPassword` for a protected deck opened without its password, `WrongPassword` with a wrong one, `Failed` otherwise, `GetLastErrorText` the reader's sentence |
| `OpenFromBytes(package)` | a `.pptx` package already in memory |
| `GetPageCount` / `GetPageInfo(n)` | the slides; every page the deck's slide size in points, its label the slide's number |
| `RenderPage(n, settings)` / `RenderThumbnail(n, maxDim)` | the slide drawn at the settings' DPI and zoom, or fitted into `maxDim`, as plain RGBA |
| `GetPageText(n)` | the slide's text in reading order - paragraphs, table cells, a chart's title - then its speaker notes after a blank line |
| `ExtractTextRuns(n)` / `Search(query, options)` | one run per paragraph or cell, framed on its shape (points); hits found case-insensitively, or as whole words, in a page range |
| `ListImages(n)` / `ExtractImageBytes(ref)` | the slide's pictures, framed on their shapes, and their bytes |
| `GetPresentation()` / `GetNotes(n)` | the deck as read, and a slide's speaker notes as text |

Every write (`Save`, `DeletePage`, `AddTextAnnotation` ...) answers false,
and `ExtractTextChars` gives nothing: a slide's text is selected by searching,
not by dragging across it. Hidden slides are pages like the others.

## In UltraViewer

MediaViewer shows a deck as its slides in its document view, the info bar
naming the slide shown and its speaker notes ("slide 2 / 5 · Notes: ..."). The
O key shows the deck's outline in the text view instead, and back
(`SetPresentationOutline`). F5 starts the slide show from the first slide,
Shift+F5 from the one shown (`StartPresenting`): the window fills the screen,
the bars and the thumbnail strip are put away, each slide fills it; the
arrows, Space, Return, Page Up / Down, N and P move, Home and End go to the
first and last, and Escape (`StopPresenting`) brings everything back. A
protected deck is locked with the Unlock button, as a PDF is. See
[UltraCanvasMediaViewer](UltraCanvasMediaViewer.md).

## Test

`Tests/MediaViewerToolsTest.cpp` opens `.pptx`, `.odp` and `.fodp` decks as
slides - the page count, the slides' text, the info bar's slide and notes, a
search - starts and stops the slide show, shows the outline instead and back,
and unlocks protected decks.
