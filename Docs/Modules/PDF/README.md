# PDF Support for UltraCanvas — Investigation & Design

**Status:** Research / Proposal
**Branch:** `claude/pdf-support-ultracanvas-qSJzr`
**Last updated:** 2026-05-18

This document is the result of investigating what it would take to add
full-fledged PDF support to UltraCanvas — specifically a Text-plugin-style
component for **UltraTexter** and a reusable PDF engine for the framework.

The target feature set is:

1. Full read **and write** access to PDF files
2. Search text inside a document
3. Extract text
4. Extract images
5. Delete pages
6. Merge documents / pages
7. Edit content (text and annotations)
8. Rearrange pages
9. Room to grow (form filling, signing, OCR, redaction, …)

---

## 1. Current state of PDF in the repository

There is already a *stub* PDF subsystem in the tree, but it is incomplete and
unbuildable as-is. The relevant files are:

| File | Purpose | Status |
|------|---------|--------|
| `UltraCanvas/Plugins/Documents/PDF.h` | `IPDFEngine` interface, `PopplerPDFEngine`, `MuPDFEngine` classes, `UltraCanvasPDFPlugin` | Header exists |
| `UltraCanvas/Plugins/Documents/PDF.cpp` | Poppler implementation (mostly), MuPDF skeleton | Reads only |
| `Apps/PDFExampleApp.cpp` | Demo wiring through `UltraCanvasPDFViewer` and `UltraCanvasMenuBar` | Will not compile |
| Build flag `ULTRACANVAS_PLUGIN_PDF` | Default **OFF** in both root `CMakeLists.txt` and `UltraCanvas/CMakeLists.txt` | Wired |
| `UltraCanvas/CMakeLists.txt` expects `Plugins/Documents/PDF/CMakeLists.txt` | Subdirectory build script | **Missing on disk** |

### Concrete issues with the existing code

1. **Filename / include mismatch.** `PDF.cpp` does `#include "UltraCanvasPDFPlugin.h"`,
   but the file on disk is named `PDF.h`. The example app at `Apps/PDFExampleApp.cpp`
   includes `UltraCanvasPDFPlugin.h` and `UltraCanvasPDFViewer.h`; neither exists.
2. **Dangling classes.** `PDFExampleApp.cpp` references `UltraCanvasPDFViewer`,
   `UltraCanvasFileDialog`, `UltraCanvasMenuBar`, `CreatePDFViewer` and
   `CreateOpenFileDialog` — none of those types exist anywhere in the codebase.
3. **Fake image format.** `PopplerPDFEngine::RenderPageInternal` returns a buffer
   that starts with the literal string `"PNG_DATA"` followed by raw pixel bytes.
   `ConvertPDFPageToImageData` parses that same fake header. It is never a real
   PNG, so any code that hands it to an image decoder will fail.
4. **Read-only by design.** The `IPDFEngine` interface has no `Save`,
   `DeletePage`, `MovePage`, `MergeDocument`, `InsertPage`, or `SetPageText`
   methods. `UltraCanvasPDFPlugin::SaveToFile` deliberately returns `false`.
5. **MuPDF engine is a skeleton.** Only `LoadDocument`, `Close`, `GetDocumentInfo`
   and `GetPageCount` are implemented. Rendering, text extraction, search, and
   everything else are missing.
6. **No build script for the plugin subfolder.** `UltraCanvas/CMakeLists.txt`
   does `add_subdirectory(Plugins/Documents/PDF)` when poppler is found, but the
   sources actually live in `Plugins/Documents/` (not `Plugins/Documents/PDF/`)
   and there is no `CMakeLists.txt` in either location.
7. **No integration with UltraTexter.** In `Apps/Texter/UltraCanvasTextEditor.cpp`
   the file-loader explicitly lists `"pdf"` in `IsBinaryFile`'s binary-extension
   list, so opening a PDF currently drops the user into the hex view.
   See `UltraCanvasTextEditor.cpp:2167`.

**Conclusion:** the existing PDF code is best treated as a design sketch. We
should keep the high-level shape (`IPDFEngine` + plugin) but rewrite the
implementation against a library that can also *write* PDFs.

---

## 2. Library survey

The eight features above split into three capability tiers, and no single
popular library covers them all comfortably. The honest answer is **MuPDF as
the primary engine, with PoDoFo as an optional companion for structural
operations**.

| Library | License | Render | Text search/extract | Image extract | Edit content | Page ops (insert/delete/move/merge) | Save / write | Notes |
|---|---|---|---|---|---|---|---|---|
| **Poppler (poppler-cpp)** | GPL-2.0/3.0 | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | Read-only. Currently sketched in `PDF.cpp`. |
| **MuPDF / mutool** | AGPL-3.0 *or* commercial | ✅ | ✅ | ✅ | ⚠ via `pdf_*` API | ✅ (insert/delete/copy pages, `pdf_save_document`) | ✅ | The only single-library option that touches every requirement. |
| **PoDoFo** | LGPL-2.0 | ❌ (no rasterizer) | ⚠ low-level | ⚠ | ✅ structural editing | ✅ (very granular) | ✅ | Best structural-edit API; pair with a renderer for display. |
| **QPDF** | Apache-2.0 | ❌ | ❌ (object-level only) | ⚠ | ⚠ | ✅ | ✅ | Lossless structural ops, linearization, encryption. |
| **PDFium** | BSD-3-Clause | ✅ | ✅ | ✅ | ⚠ (FPDFEdit) | ✅ | ✅ | Permissive license; build is heavy. |
| **HPDF / libharu** | ZLIB | ❌ | ❌ | ❌ | n/a | n/a | ✅ (create only) | Useful as a *writer* if we ever need to generate PDFs from scratch. |

### Recommendation

* **Primary engine: MuPDF.** Permissively dual-licensed (AGPL or commercial),
  pure C, no GPL contagion if we ship commercial via Artifex's license, and the
  only library that natively supports every requested feature in one place.
* **Optional structural engine: PoDoFo.** Cleaner C++ API for delete/insert/
  rearrange/merge if we hit MuPDF limitations on `pdf_*` mutation.
* **Drop Poppler** from the design, or keep it behind `ULTRACANVAS_POPPLER_SUPPORT`
  as a *read-only fallback* for distributions where MuPDF is unavailable
  (most Linux distros ship both, so this is mostly a Windows packaging concern).

The dual-license clause matters: AGPL-3.0 is incompatible with the rest of the
codebase if UltraCanvas ships as a non-AGPL binary. Anyone shipping
commercially will need to either (a) buy an Artifex commercial license, or
(b) gate MuPDF behind a build flag and ship the Poppler fallback by default.
This needs an explicit decision from the maintainers (see §6).

---

## 3. Proposed architecture

### 3.1. Public C++ surface (rewrite of `IPDFEngine`)

```cpp
namespace UltraCanvas {

struct PDFTextRun {            // result of search / extract
    int     pageNumber;        // 1-based
    Rect2Df bbox;              // in PDF user units
    std::string text;
};

struct PDFImageRef {
    int     pageNumber;
    Rect2Df bbox;
    std::string mimeType;      // "image/jpeg", "image/png", …
    int     widthPx, heightPx;
};

class IPDFDocument {
public:
    virtual ~IPDFDocument() = default;

    // ---------- I/O ----------
    virtual bool Open(const std::string& path,  const std::string& pw = "") = 0;
    virtual bool OpenFromBytes(std::span<const uint8_t>, const std::string& pw = "") = 0;
    virtual bool Save(const std::string& path,  PDFSaveOptions = {}) = 0;
    virtual bool SaveIncremental(const std::string& path) = 0;
    virtual void Close() = 0;
    virtual bool IsDirty() const = 0;

    // ---------- Metadata ----------
    virtual PDFDocumentInfo GetInfo() const = 0;
    virtual int  GetPageCount() const = 0;
    virtual PDFPageInfo GetPageInfo(int pageNumber) const = 0;

    // ---------- Rendering ----------
    virtual std::shared_ptr<IImage> RenderPage(int pageNumber, const PDFRenderSettings&) = 0;
    virtual std::shared_ptr<IImage> RenderThumbnail(int pageNumber, int maxDim) = 0;

    // ---------- Text ----------
    virtual std::string                GetPageText(int pageNumber) = 0;
    virtual std::vector<PDFTextRun>    ExtractTextRuns(int pageNumber) = 0;
    virtual std::vector<PDFTextRun>    Search(const std::string& query, PDFSearchOptions = {}) = 0;

    // ---------- Images ----------
    virtual std::vector<PDFImageRef>   ListImages(int pageNumber) = 0;
    virtual std::vector<uint8_t>       ExtractImageBytes(const PDFImageRef&) = 0;

    // ---------- Page operations (writes) ----------
    virtual bool DeletePage(int pageNumber) = 0;
    virtual bool MovePage(int from, int to) = 0;
    virtual bool InsertBlankPage(int at, float widthPt, float heightPt) = 0;
    virtual bool MergeFrom(const IPDFDocument& other, int srcStart, int srcEnd, int insertAt) = 0;

    // ---------- Content editing (writes) ----------
    virtual bool ReplaceText(const PDFTextRun& target, const std::string& newText) = 0;
    virtual bool AddTextAnnotation(int pageNumber, Rect2Df at, const std::string& text) = 0;
    virtual bool RedactRect(int pageNumber, Rect2Df area) = 0;
};

class IPDFEngineFactory {
public:
    static std::unique_ptr<IPDFDocument> Create(PDFEngineKind = PDFEngineKind::Auto);
    static std::vector<PDFEngineKind>    Available();
};

} // namespace UltraCanvas
```

The key shift from today's stub:

* Rendering returns a real `IImage` (the framework already has `UltraCanvasImage.h`
  and a Cairo-backed image format), not a fake `"PNG_DATA"` byte blob.
* Every write operation has a method — not "Save is not supported".
* Search returns `PDFTextRun`s with bounding boxes (good enough to drive the
  highlight overlay used by the existing search bar).

### 3.2. Plugin registration

Reuse the existing `IGraphicsPlugin` for thumbnail-style display in the file
manager etc., but **PDF is a document, not just an image**, so expose a
separate registration path:

```cpp
UltraCanvasDocumentRegistry::Register<UltraCanvasPDFDocument>({"pdf"});
```

`UltraCanvasDocumentRegistry` is a new, very small class (≈80 LOC) that lives
next to `UltraCanvasGraphicsPluginSystem` and lets UltraTexter ask
"what's the document driver for this extension?" without going through the
image-plugin pipeline.

### 3.3. UltraTexter integration

The text editor needs three changes:

1. **Stop treating PDF as binary.** Remove `"pdf"` from the binary-extension
   list at `UltraCanvasTextEditor.cpp:2167`, or — preferably — route through
   the new document registry *before* the binary check.
2. **New `TextAreaEditingMode::PDF`** (sibling of `MarkdownHybrid` and `Hex`)
   that hosts a `UltraCanvasPDFView` element instead of the raw text area.
3. **Toolbar/menu wiring** for the page-management actions: Delete page,
   Move page (drag-reorder in the thumbnail panel), Merge from… , Save,
   Extract images…, Find. These map 1:1 to the IPDFDocument methods.

### 3.4. New UI element: `UltraCanvasPDFView`

A scrollable container that
* shows a thumbnail strip on the left (drag-to-reorder → `MovePage`),
* renders the current page via `IPDFDocument::RenderPage`,
* overlays search hits (rectangles from `Search()`),
* hosts the markdown-style toolbar for page operations.

This is the only sizable new widget. Estimated ~600 LOC; modeled on
`UltraCanvasMarkdownDisplay` and `UltraCanvasSlideshow`.

---

## 4. File / build layout

```
UltraCanvas/
├── include/Plugins/Documents/
│   ├── UltraCanvasPDF.h                ← new public header (replaces PDF.h)
│   └── UltraCanvasPDFView.h            ← new UI element
├── Plugins/Documents/
│   ├── CMakeLists.txt                  ← new, gates on libs
│   ├── UltraCanvasPDF_MuPDF.cpp        ← primary impl
│   ├── UltraCanvasPDF_Poppler.cpp      ← optional fallback
│   ├── UltraCanvasPDFView.cpp          ← UI element
│   └── (delete: PDF.h, PDF.cpp)
└── CMakeLists.txt                      ← already references the subdir
```

`PDF.h` and `PDF.cpp` are deleted because their public surface is the wrong
shape (read-only) and their include name is broken. The `Apps/PDFExampleApp.cpp`
demo gets either deleted or rewritten against the new API in the same patch
that introduces the new API — leaving it in the tree as-is keeps it
unbuildable.

### CMake gates

```cmake
option(ULTRACANVAS_PLUGIN_PDF      "Build PDF plugin" OFF)
option(ULTRACANVAS_PDF_MUPDF       "Use MuPDF backend (read+write)" ON)
option(ULTRACANVAS_PDF_POPPLER     "Use Poppler backend (read-only fallback)" OFF)
```

Linux deps: `apt install libmupdf-dev` (Debian/Ubuntu),
`dnf install mupdf-devel` (Fedora).
macOS: `brew install mupdf-tools`.
Windows: vcpkg `mupdf` port.

---

## 5. Phased implementation plan

The 8 requested features cleanly split into three milestones. Each milestone is
independently shippable and adds visible value.

### Milestone 1 — Read & navigate (2–3 days)

* Delete dead stub (`PDF.h`, `PDF.cpp`, `Apps/PDFExampleApp.cpp`).
* New `IPDFDocument` interface (read methods only).
* MuPDF implementation of: `Open`, `Close`, `GetInfo`, `GetPageCount`,
  `RenderPage`, `RenderThumbnail`, `GetPageText`, `ExtractTextRuns`, `Search`,
  `ListImages`, `ExtractImageBytes`.
* `UltraCanvasPDFView` widget (page render + scroll + thumbnail strip +
  search-result overlay).
* UltraTexter:
  * Remove `pdf` from binary-extension list.
  * Add `TextAreaEditingMode::PDF` routing.
  * Hook the existing search bar to `IPDFDocument::Search`.

**Delivers: read, search, extract text, extract images.**

### Milestone 2 — Page operations (1–2 days)

* MuPDF implementation of `DeletePage`, `MovePage`, `InsertBlankPage`,
  `MergeFrom`.
* `Save` and `SaveIncremental`.
* PDFView toolbar buttons + drag-and-drop in the thumbnail strip.
* UltraTexter "modified" badge wiring on the PDF tab.

**Delivers: delete pages, rearrange pages, merge documents.**

### Milestone 3 — Content editing (3–4 days, hardest)

* `ReplaceText` — non-trivial because PDF text is positioned glyphs, not a
  stream of characters. Strategy: edit the content stream tokens in place when
  the replacement fits the bounding box; otherwise fall back to a redaction +
  text-overlay annotation.
* `AddTextAnnotation`, `RedactRect`.
* Inline edit handles in `UltraCanvasPDFView` (click a text run, retype).

**Delivers: edit content.**

### Future / "eventually"

* Form filling (AcroForm + XFA) via `pdf_widget` API.
* PDF/A export profile.
* Digital signing (uses `pdf_signature` in MuPDF).
* OCR for image-only PDFs (Tesseract integration; ties into the existing
  Texter encoding pipeline).
* Redaction with content removal (not just visual cover).
* Export-to: SVG (already supported), PNG (Cairo), DOCX (out of scope).

---

## 6. Open decisions for the maintainers

Before starting Milestone 1, please confirm:

1. **License posture.** MuPDF is AGPL-3.0 unless commercially licensed. Are we
   OK shipping a build-time *optional* AGPL dependency, or do we need a
   permissive default? (If permissive-default is required, Milestone 1 ships
   with the Poppler read-only backend, and write features ship under a
   `ULTRACANVAS_PDF_WRITE=ON` opt-in that pulls in MuPDF.)
2. **Where does the PDF live in Texter?** Two options:
   * (a) Inline — a `.pdf` tab opens directly in `UltraCanvasPDFView`, sharing
     the same tab strip and toolbars as text documents. Simpler UX.
   * (b) Side-panel — text view stays, with a PDF preview docked to the right.
     Useful for "PDF as text source" workflows (search across a folder of PDFs).
   Recommendation: (a) for v1, add (b) as an option later.
3. **Default engine on Windows.** Poppler on Windows is painful; PDFium is
   easier to ship but adds a large dependency. MuPDF on vcpkg works but the
   binary is ~6 MB. Pick one default per platform?
4. **Scope of "edit content".** Full content-stream rewriting (hard, brittle)
   or annotation-based editing (easy, lossless, but a literal overlay on the
   original page)? Recommendation: annotation-based for v1 of Milestone 3.

---

## 7. References

* MuPDF API docs — <https://mupdf.readthedocs.io/en/latest/>
* MuPDF `pdf_*` page operations —
  `pdf_delete_page`, `pdf_insert_page`, `pdf_save_document`, `pdf_graft_object`
* PoDoFo API — <https://podofo.sourceforge.net/>
* Poppler-cpp reference — <https://poppler.freedesktop.org/api/cpp/>
* PDFium build guide — <https://pdfium.googlesource.com/pdfium/+/refs/heads/main/README.md>

