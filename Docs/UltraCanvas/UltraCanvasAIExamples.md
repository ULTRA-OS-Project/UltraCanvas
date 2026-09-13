# Adobe Illustrator (`.ai`) Artwork

## Overview

An `.ai` file needs no reader of its own. Since **Illustrator 9** the format
*is* PDF: the page content is ordinary PDF, and Illustrator's own editing data
rides along in an extra stream (`AIPrivateData`) that every other PDF consumer
ignores. Both samples shipped under `media/vector/AI/` are exactly that —
`%PDF-1.5`, one page, `AIPrivateData` present.

So UltraCanvas reads `.ai` through the **MuPDF-backed PDF engine**, the same
path the PDF Documents page uses, and writes it through the Vector plugin's
`VectorConverter::AIConverter`, which is **export-only**.

| Direction | Component | Notes |
|---|---|---|
| Read | `UltraCanvasPDFView` / `PDFEngineFactory` (`ULTRACANVAS_PLUGIN_PDF`) | Modern PDF-based `.ai`; paths, clips, transparency groups, gradients, embedded fonts and images render as in any PDF |
| Write | `VectorConverter::AIConverter` (Vector plugin) | Emits the plugin's PDF output under the `.ai` extension — valid for Illustrator and every PDF consumer |

Legacy (v8 and earlier) `.ai` files are EPS/PostScript-based. `AIConverter::ValidateData()`
recognizes them (`%!PS` as well as `%PDF-`), but the writer does not produce them.

**Demo source:** `Apps/DemoApp/UltraCanvasAIExamples.cpp`
**Demo page:** Vector Elements → *AI Artwork*
**Namespace:** `UltraCanvas`

## Why the `.ai` extension needs no special case

`UltraCanvasPDFView::LoadFromPath()` reads a document that fits in memory
(anything up to `GetMaxInMemoryBytes()`, 256 MB by default) into a buffer and
hands it to MuPDF as `"application/pdf"` — the format is stated outright rather
than sniffed from the file name. A `.ai` file therefore opens with no extension
mapping and no conversion step:

```cpp
auto view = CreatePDFView("AIView", 0, 0, 0, 0);
view->onError = [](const std::string& msg) { /* report */ };

const std::string path = NormalizePath(GetResourcesDir() + "media/vector/AI/turtle.ai");
if (view->LoadFromPath(path)) {
    view->ZoomToFit();
}
```

The streaming fallback (used for a document too large to hold in memory) goes
through MuPDF's own format detection, which recognizes the `%PDF-` header
regardless of the extension — so both load paths handle `.ai`. Verified against
both shipped samples: each opens with one page through `OpenInMemory()` and
through `Open()`.

## Example: the demo page

The page is a flex column — title, format note, toolbar, viewer, status row,
notes panel — with a dropdown that switches between the shipped samples.

```cpp
auto root = std::make_shared<UltraCanvasContainer>("AIExamples", 0, 0, 1000, 780);
root->layout.SetFlexColumn().SetFlexGap(8)
            .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
root->SetPadding(10, 12, 10, 12);

auto view = CreatePDFView("AIView", 0, 0, 0, 0);
view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
std::weak_ptr<UltraCanvasPDFView> viewWeak = view;

// Page counter and zoom read-out, wired before the first load so the
// initial document is reflected too.
view->onPageChanged = [pageLabel](int cur, int total) {
    pageLabel->SetText("Page " + std::to_string(cur) + " / " + std::to_string(total));
};
view->onZoomChanged = [zoomLabel](float percent) {
    zoomLabel->SetText(std::to_string(static_cast<int>(std::lround(percent))) + "%");
};

auto sampleDropdown = CreateDropdown("AISample", 0, 0, 220, 30);
sampleDropdown->AddItem("turtle.ai");
sampleDropdown->AddItem("mandalorian-star-wars.ai");
sampleDropdown->SetSelectedIndex(0, false);
sampleDropdown->onSelectionChanged = [viewWeak](int index, const DropdownItem& item) {
    if (auto v = viewWeak.lock()) {
        v->LoadFromPath(NormalizePath(GetResourcesDir() + "media/vector/AI/" + item.text));
        v->ZoomToFit();
    }
};
```

Navigation and zoom are the `UltraCanvasPDFView` methods —
`GoToPrevPage()` / `GoToNextPage()`, `ZoomIn()` / `ZoomOut()`,
`ZoomToFit()` / `ZoomToWidth()` / `ZoomActualSize()` — so text selection,
search and image extraction are available on `.ai` artwork exactly as they are
on a `.pdf`. See
[`UltraCanvasPDFExamples.md`](UltraCanvasPDFExamples.md) for the full viewer API.

## Writing `.ai`

```cpp
// Any VectorStorage::VectorDocument can be saved as .ai; the bytes are the
// Vector plugin's PDF output.
UltraCanvasVectorFormatsPlugin::SaveVectorDocument(document, "artwork.ai");
```

`UltraCanvasVectorFormatsPlugin::CreateConverterForExtension("ai")` returns an
`AIConverter`. Its `CanImport()` is `false` — reading is the PDF engine's job,
as above — so `LoadVectorDocument("artwork.ai")` returns null by design.

## Shipped samples

`media/vector/AI/`:

| File | Notes |
|---|---|
| `turtle.ai` | 319 KB, `%PDF-1.5`, one page, `AIPrivateData` present |
| `mandalorian-star-wars.ai` | 74 KB, `%PDF-1.5`, one page, `AIPrivateData` present |

## See also

- [`UltraCanvasPDFExamples.md`](UltraCanvasPDFExamples.md) — the viewer this page is built on
- [`UltraCanvasVectorConverters.md`](UltraCanvasVectorConverters.md) — the converter matrix `AIConverter` belongs to
- [`UltraCanvasEPSExamples.md`](UltraCanvasEPSExamples.md) — the PostScript interpreter, which is what a legacy `.ai` would need
