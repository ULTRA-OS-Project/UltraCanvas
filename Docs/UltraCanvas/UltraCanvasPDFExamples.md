# UltraCanvasPDF Documentation

## Overview

**UltraCanvasPDF** is the PDF subsystem for UltraCanvas. It has two layers:

- **`UltraCanvasPDFView`** — a ready-to-use viewer widget with a thumbnail
  strip, scrollable page render, zoom, and search-hit overlay. This is what the
  *PDF Document Viewer* demo page is built on.
- **`IPDFDocument`** — the headless document engine (MuPDF-backed) for opening,
  rendering, extracting, searching, editing, and saving PDFs without any UI.

The whole subsystem is gated behind the `ULTRACANVAS_PLUGIN_PDF` build option
(requires MuPDF / `libmupdf-dev`).

**Version:** 1.1.0
**Headers:** `include/Plugins/Documents/UltraCanvasPDFView.h`,
`include/Plugins/Documents/UltraCanvasPDF.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement` (the view); `IPDFDocument` (the engine)
**Demo source:** `Apps/DemoApp/UltraCanvasPDFExamples.cpp`

## Features

- **Page rendering** at any DPI/zoom with antialiasing, drop shadow, and a
  white underlay; rendered pages and thumbnails are cached per DPI.
- **Thumbnail strip** with click-to-jump and independent scrolling (toggleable).
- **Navigation**: next/prev/first/last/go-to-page, plus PageUp/Down, arrows,
  Home/End.
- **Zoom modes**: Fit Page, Fit Width, Actual Size (100%), and custom levels;
  Ctrl+wheel to zoom; fit modes re-resolve on resize.
- **Search**: case/whole-word/page-range options, hit overlays, active-hit
  highlight, next/prev stepping (F3 / Shift+F3).
- **Interaction**: mouse-wheel scroll, click-drag panning, on-page page badge.
- **Image extraction**: right-click an image on the page to save it via a
  built-in context menu, preserving the original embedded format
  (JPEG/PNG/JPX/…); only images stored with a PDF-internal filter fall back to
  PNG. Also drivable programmatically.
- **Text selection & export**: switch to select mode, drag to marquee-select
  text lines, then copy (Ctrl+C) or export the selection / whole page to a
  `.txt` file from the right-click menu. Ctrl+A selects all text on the page.
- **Editing** (via the engine): delete / move / insert pages, replace text
  (redact-and-overlay), manage annotations, save.
- **Callbacks** for page, search, active-hit, zoom, error, and document changes.
- **Headless engine**: open from file or memory, extract text/images, search,
  render to pixels, page ops, and save — all without a window.

## Header Include

```cpp
#include "Plugins/Documents/UltraCanvasPDFView.h"   // viewer widget
#include "Plugins/Documents/UltraCanvasPDF.h"       // engine (pulled in by the view)
```

---

## Quick Start: a minimal viewer

```cpp
auto view = UltraCanvas::CreatePDFView("MyPDFView", 0, 0, 800, 600);
view->LoadFromPath("/path/to/document.pdf");   // optional 2nd arg: password
container->AddChild(view);
```

`CreatePDFView(id, x, y, w, h)` returns a `std::shared_ptr<UltraCanvasPDFView>`.
Inside a flex layout, let it grow to fill the available space:

```cpp
view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
```

---

## Class Reference: `UltraCanvasPDFView`

### Document

```cpp
bool LoadFromPath(const std::string& path, const std::string& password = "");
void SetDocument(std::unique_ptr<IPDFDocument> doc);
IPDFDocument* GetDocument() const;
bool HasDocument() const;
```

### Navigation

```cpp
void GoToPage(int page);        // 1-based, clamped
void GoToNextPage();
void GoToPrevPage();
void GoToFirstPage();
void GoToLastPage();
int  GetCurrentPage() const;
int  GetPageCount()   const;
```

### Zoom

```cpp
enum class ZoomMode { FitPage, FitWidth, Custom };

void     SetZoom(float scale);     // absolute scale (1.0 == 100%); switches to Custom
float    GetZoom() const;          // current effective scale
void     SetZoomMode(ZoomMode m);
ZoomMode GetZoomMode() const;
void     ZoomIn();                 // 1.25x step
void     ZoomOut();                // /1.25 step
void     ZoomToFit();              // Fit Page
void     ZoomToWidth();            // Fit Width
void     ZoomActualSize();         // 100%
float    GetZoomPercent() const;   // effective on-screen scale, as a percentage
```

Fit modes recompute their scale from the live viewport every frame, so the page
keeps fitting when the view is resized.

### Search

```cpp
int  SetSearchQuery(const std::string& query);  // returns hit count, jumps to first hit
void ClearSearch();
int  GetSearchHitCount() const;
void NextHit();
void PrevHit();
const std::string& GetSearchQuery() const;
```

### Images

```cpp
std::vector<PDFImageRef> ImagesOnCurrentPage();          // engine order
int  ImageIndexAt(const Point2Di& localPt);             // image under a local point, or -1
bool ExtractImageToFile(int indexOnPage, const std::string& path);
```

Right-clicking an image on the page opens a built-in **Extract Image…** context
menu that prompts for a path (native save dialog) and writes the image. The
**original embedded format is preserved** — JPEG stays JPEG, PNG stays PNG, etc.
(the save dialog defaults to the matching extension). Images stored with a
PDF-internal filter that has no standalone file form (Flate/LZW/CCITT/raw) are
re-encoded to PNG. The format is reported per image as `PDFImageRef::mimeType`,
and the result of a save via `onImageExtracted`.

```cpp
view->onImageExtracted = [](const std::string& path, bool ok) {
    // ok == true → image written to `path`
};

// Headless equivalent (no UI):
auto images = view->ImagesOnCurrentPage();   // each carries .mimeType
view->ExtractImageToFile(0, "/tmp/page-image.jpg");   // bytes are the native format
```

`event.pointer` passed to `ImageIndexAt` is in element-local coordinates (see the
Coordinate System guide). Hit-testing uses the page rect from the last render,
so it reflects the current zoom/scroll.

### Text selection & export

```cpp
enum class MouseMode { Pan, SelectText };
void      SetMouseMode(MouseMode m);     // Pan (default) or SelectText
MouseMode GetMouseMode() const;

bool        HasTextSelection() const;
std::string GetSelectedText();           // selected lines, joined by '\n'
void        ClearTextSelection();
void        SelectAllText();             // all text on the current page
bool        CopySelectionToClipboard();  // copies GetSelectedText()
std::string GetCurrentPageText();
bool        ExportTextToFile(const std::string& path, bool selectionOnly);
```

In `SelectText` mode a left-drag marquee-selects the text **lines** that
intersect the drag (selection is line-granular, since the engine exposes text at
line level). Selected lines are highlighted; `onSelectionChanged(charCount)`
fires as the selection changes. The right-click menu offers **Copy**, **Export
Selected Text…**, **Select All Text**, and **Export Page Text…**; exports go
through a native save dialog and report via `onTextExported`.

```cpp
view->SetMouseMode(UltraCanvasPDFView::MouseMode::SelectText);
view->onSelectionChanged = [](int chars) { /* update a status line */ };
view->onTextExported     = [](const std::string& path, bool ok) { /* … */ };

// Headless: pull text without any UI.
std::string all = view->GetCurrentPageText();        // whole page
view->SelectAllText();
view->ExportTextToFile("/tmp/page.txt", /*selectionOnly=*/true);
```

### Layout & style

```cpp
void SetShowThumbnailStrip(bool show);
bool GetShowThumbnailStrip() const;

void SetStyle(const PDFViewStyle& s);
const PDFViewStyle& GetStyle() const;
```

`PDFViewStyle` exposes colors (background, page, shadow, thumb strip/border,
search-hit fill, scrollbar) and metrics (`thumbStripWidth`, `thumbHeight`,
`thumbSpacing`, `pageMargin`, `pageShadowSize`, `scrollbarWidth`, `defaultDpi`).

### Editing passthroughs

```cpp
bool DeleteCurrentPage();
bool MovePage(int fromPage, int toPage);
bool InsertBlankPageAt(int at, float widthPt, float heightPt);
bool SaveAs(const std::string& path, const PDFSaveOptions& opts = {});

// Content editing (M3)
bool ReplaceTextAt(const Rect2Df& bboxPt, const std::string& newText);
bool ApplyPendingRedactions();
std::vector<PDFAnnotation> ListAnnotationsOnCurrentPage();
bool DeleteAnnotation(int indexOnCurrentPage);
```

### Callbacks

```cpp
std::function<void(int currentPage, int totalPages)> onPageChanged;
std::function<void(int hitCount)>                    onSearchResults;
std::function<void(int activeHit, int totalHits)>     onActiveHitChanged;
std::function<void(float zoomPercent)>                onZoomChanged;
std::function<void(const std::string& path, bool ok)> onImageExtracted;
std::function<void(int selectedChars)>                onSelectionChanged;
std::function<void(const std::string& path, bool ok)> onTextExported;
std::function<void(const std::string&)>               onError;
std::function<void()>                                 onDocumentChanged;
```

Wire callbacks **before** `LoadFromPath` so the initial page/zoom are reflected
immediately.

### Built-in interaction

| Input | Action |
|-------|--------|
| Mouse wheel | Scroll the page (or the thumbnail strip when over it) |
| Ctrl + wheel | Zoom in / out |
| Click-drag on page | Pan |
| Click thumbnail | Jump to that page |
| Drag (in select mode) | Marquee-select text lines |
| Ctrl+C / Ctrl+A | Copy selection / select all text on the page |
| Right-click | Context menu: extract image and/or copy & export text |
| PageUp/Down, ↑/↓ | Prev / next page |
| Home / End | First / last page |
| F3 / Shift+F3 | Next / previous search hit |

---

## Example: the full demo toolbar

This mirrors `Apps/DemoApp/UltraCanvasPDFExamples.cpp` — a viewer with a
navigation / zoom / search toolbar, a page indicator, and a status row showing
the zoom level.

```cpp
auto view = CreatePDFView("DemoPDFView", 0, 0, 0, 0);
view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
std::weak_ptr<UltraCanvasPDFView> viewWeak = view;

// Status read-outs.
auto pageLabel = std::make_shared<UltraCanvasLabel>("PageLabel", 0, 0, 120, 30);
auto zoomLabel = std::make_shared<UltraCanvasLabel>("ZoomLabel", 0, 0, 80, 22);

view->onPageChanged = [pageLabel](int cur, int total) {
    pageLabel->SetText("Page " + std::to_string(cur) + " / " + std::to_string(total));
};
view->onZoomChanged = [zoomLabel](float percent) {
    zoomLabel->SetText(std::to_string((int)std::lround(percent)) + "%");
};
view->onError = [](const std::string& msg) { /* show error */ };

view->LoadFromPath(NormalizePath(GetResourcesDir() + "media/sample.pdf"));

// Navigation + zoom buttons.
auto prev = CreateButton("Prev", 0, 0, 60, 30, "Prev");
prev->onClick = [viewWeak]{ if (auto v = viewWeak.lock()) v->GoToPrevPage(); };
// …Next, Zoom -, Zoom + similarly (v->GoToNextPage / v->ZoomOut / v->ZoomIn)…

// Zoom presets dropdown.
auto zoom = CreateDropdown("ZoomMode", 0, 0, 120, 30);
for (auto s : {"Fit Page","Fit Width","Actual Size","50%","75%","100%","125%","150%","200%"})
    zoom->AddItem(s);
zoom->SetSelectedIndex(0, false);   // Fit Page == the view's default
zoom->onSelectionChanged = [viewWeak](int index, const DropdownItem&) {
    auto v = viewWeak.lock(); if (!v) return;
    switch (index) {
        case 0: v->ZoomToFit();      break;
        case 1: v->ZoomToWidth();    break;
        case 2: v->ZoomActualSize(); break;
        case 3: v->SetZoom(0.50f);   break;
        case 4: v->SetZoom(0.75f);   break;
        case 5: v->SetZoom(1.00f);   break;
        case 6: v->SetZoom(1.25f);   break;
        case 7: v->SetZoom(1.50f);   break;
        case 8: v->SetZoom(2.00f);   break;
    }
};
```

### Search with up/down navigation

```cpp
auto search = CreateTextInput("Search", 0, 0, 180, 30);
search->SetPlaceholder("Search...");
std::weak_ptr<UltraCanvasTextInput> searchWeak = search;

auto doSearch = [viewWeak, searchWeak]{
    auto v = viewWeak.lock(); auto s = searchWeak.lock();
    if (v && s) v->SetSearchQuery(s->GetText());
};
search->onEnterPressed = [doSearch](const std::string&){ doSearch(); return true; };

view->onSearchResults = [](int hits) { /* "N match(es) found" */ };
view->onActiveHitChanged = [](int active, int total) {
    if (total > 0) { /* "Match {active} of {total}" */ }
};

// ▲ / ▼ step through results.
auto prevHit = CreateButton("PrevHit", 0, 0, 36, 30, "\xE2\x96\xB2");  // ▲
prevHit->onClick = [viewWeak]{ if (auto v = viewWeak.lock()) v->PrevHit(); };
auto nextHit = CreateButton("NextHit", 0, 0, 36, 30, "\xE2\x96\xBC");  // ▼
nextHit->onClick = [viewWeak]{ if (auto v = viewWeak.lock()) v->NextHit(); };
```

---

## Class Reference: `IPDFDocument` (headless engine)

Obtain a document via the factory, or directly from the view with
`GetDocument()`.

```cpp
#include "Plugins/Documents/UltraCanvasPDF.h"

auto doc = UltraCanvas::OpenPDF("/path/to/file.pdf");   // nullptr on failure
// or: auto doc = PDFEngineFactory::Create(PDFEngineKind::Auto);
```

### I/O & metadata

```cpp
bool Open(const std::string& path, const std::string& password = "");
bool OpenFromBytes(const std::vector<uint8_t>& data, const std::string& password = "");
bool Save(const std::string& path, const PDFSaveOptions& opts = {});
bool SaveIncremental(const std::string& path);
void Close();
bool IsOpen() const;  bool IsDirty() const;

PDFDocumentInfo GetInfo() const;            // title/author/dates/version/encryption…
int             GetPageCount() const;
PDFPageInfo     GetPageInfo(int pageNumber) const;   // size in pt, rotation, label
```

`PDFSaveOptions`: `linearize` (web-optimize), `garbageCollect`, `deflateStreams`,
`cleanContentStreams`.

### Rendering

```cpp
PDFRenderSettings s; s.dpi = 150.0f; s.colorMode = PDFColorMode::RGBA;
PDFRenderedPage page = doc->RenderPage(1, s);   // page.pixels is premultiplied RGBA
PDFRenderedPage thumb = doc->RenderThumbnail(1, /*maxDim*/ 180);
```

### Text & search

```cpp
std::string             text  = doc->GetPageText(1);
std::vector<PDFTextRun> runs  = doc->ExtractTextRuns(1);   // text + bbox (PDF user units)

PDFSearchOptions opts; opts.caseSensitive = false; opts.maxHits = 5000;
std::vector<PDFTextRun> hits = doc->Search("invoice", opts);
for (const auto& h : hits)
    printf("page %d: \"%s\"\n", h.pageNumber, h.text.c_str());
```

### Images

```cpp
for (const PDFImageRef& img : doc->ListImages(1)) {
    std::vector<uint8_t> bytes = doc->ExtractImageBytes(img);  // img.mimeType tells the format
}
```

### Page operations

```cpp
doc->DeletePage(3);
doc->MovePage(/*from*/ 5, /*to*/ 2);
doc->InsertBlankPage(/*at*/ 1, /*widthPt*/ 595, /*heightPt*/ 842);   // A4
doc->MergeFrom(*other, /*srcStart*/ 1, /*srcEnd*/ 3, /*insertAt*/ 4);
doc->Save("out.pdf");
```

### Content editing & annotations

```cpp
// Replace the text under a run's bbox (lossless redact-and-overlay).
auto runs = doc->ExtractTextRuns(1);
doc->ReplaceText(runs[0], "REPLACED");

doc->AddTextAnnotation(1, Rect2Df(72, 72, 200, 24), "Reviewed");
doc->RedactRect(1, Rect2Df(100, 100, 120, 16));
doc->ApplyPendingRedactions(1);

for (const PDFAnnotation& a : doc->ListAnnotations(1)) { /* a.type, a.contents… */ }
doc->DeleteAnnotation(1, /*indexOnPage*/ 0);
```

The same edits are reachable from the widget via `ReplaceTextAt`,
`ApplyPendingRedactions`, `ListAnnotationsOnCurrentPage`, `DeleteAnnotation`,
`DeleteCurrentPage`, `MovePage`, `InsertBlankPageAt`, and `SaveAs`.

---

## Implementation Notes

- **Build flag.** All PDF code is compiled only when `ULTRACANVAS_PLUGIN_PDF`
  is ON; it links MuPDF (`libmupdf` / `libmupdf-dev`). Guard PDF-specific code
  with `#ifdef ULTRACANVAS_PLUGIN_PDF`.
- **Coordinate units.** Text/image/annotation bounding boxes are in **PDF user
  units** (1pt = 1/72"), origin at the page top-left. Page size from
  `GetPageInfo` is in points; the view scales these by the current zoom/DPI.
- **Pixel format.** `RenderPage`/`RenderThumbnail` return premultiplied RGBA
  (Cairo-compatible) or 8-bit gray, per `PDFColorMode`.
- **Caching.** The view caches rendered pages keyed by DPI and thumbnails by
  page; zoom and resize invalidate the page cache automatically.
- **Threading.** Treat a single `IPDFDocument` as not thread-safe; render/edit
  from the UI thread (or serialize access).
- **Drawing custom overlays.** If you subclass or draw on top of the view,
  remember the framework renders and hit-tests in **element-local** coordinates
  — see the
  [Coordinate System & Positioning Guide](UltraCanvasCoordinateSystemGuide.md).

## See Also

- [Coordinate System & Positioning Guide](UltraCanvasCoordinateSystemGuide.md) -
  local vs. parent vs. window frames (relevant for custom PDF overlays)
- [UltraCanvasToolbarExamples](UltraCanvasToolbarExamples.md) - building the viewer toolbar
- [UltraCanvasDropDownExamples](UltraCanvasDropDownExamples.md) - the zoom presets dropdown
- [UltraCanvasTextInputExamples](UltraCanvasTextInputExamples.md) - the search box
- `Docs/Modules/PDF/README.md` - subsystem design notes and roadmap
</content>
