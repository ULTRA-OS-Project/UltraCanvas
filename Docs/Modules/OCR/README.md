# OCR — Investigation & Proposal

**Status:** Proposal · target module: `UltraCanvas/Plugins/Documents/OCR`

OCR (Optical Character Recognition) extracts machine-readable text from
raster images and rasterized document pages. The `Apps/DemoApp` already
exposes an "OCR" tools entry with status `NotImplemented`
(`Apps/DemoApp/UltraCanvasDemo.cpp:1065`) and the sibling `UltraAI`
module reserves OCR as a future `IVisionAnalyzer` capability
(`Docs/Modules/UltraAI/README.md`). This document proposes a concrete
local-first OCR implementation that complements both.

---

## 1. Goals

1. **Local, offline, deterministic OCR** — no network round-trip
   required. Cloud OCR (GPT-4V, Azure, Google Vision) is delegated to
   the `IVisionAnalyzer` adapters in UltraAI and is out of scope here.
2. **Cross-platform** — Linux, Windows, macOS, with the same build
   pattern as the existing PDF / SVG / CDR / XAR plugins (CMake option,
   `pkg_check_modules`, soft-fail when the dependency is missing).
3. **One C++ API surface** for: image → text, image → words+boxes,
   image → layout (lines / paragraphs / tables), and PDF page → text
   (via the existing Poppler plugin).
4. **Composes with libvips/PixelFX** for pre-processing (deskew,
   binarize, denoise, upscale).
5. **Hookable** — UI widgets (`UltraCanvasImageElement`, PDF viewer)
   can request a "Recognize Text" action and receive selectable text
   overlays.

Non-goals: handwriting recognition (HTR), full layout understanding,
table-cell structure extraction. Those are explicit follow-ups.

---

## 2. Engine survey

| Engine | License | Languages | Footprint | Quality | Notes |
|---|---|---|---|---|---|
| **Tesseract 5** (LSTM) | Apache-2.0 | 100+ via `tessdata` | ~30 MB lib + 5–15 MB per language | Good on clean print, mediocre on photos | C/C++ API, packaged on every distro (`libtesseract-dev`), vcpkg, brew |
| **PaddleOCR / PP-OCRv4** | Apache-2.0 | ~80 | 8–15 MB models | State-of-the-art on scene text & multilingual | Runtime needs Paddle Inference or ONNX export; heavier integration |
| **EasyOCR** | Apache-2.0 | 80+ | ~200 MB | Excellent | PyTorch only — not a fit for a C++ runtime |
| **docTR (Mindee)** | Apache-2.0 | EN/FR + a few | ~50 MB | Excellent on documents | Python/TF only |
| **RapidOCR (PaddleOCR-ONNX)** | Apache-2.0 | ~80 | ~12 MB | Near-Paddle quality | Pure ONNX Runtime — best "C++ + modern model" option |
| **Apple Vision** (`VNRecognizeTextRequest`) | system | 30+ | bundled | Excellent | macOS / iOS only — used as native backend |
| **Windows.Media.Ocr** (UWP) | system | ~25 | bundled | Decent | Windows 10+ only — used as native backend |
| **GOCR / CuneiForm / Ocrad** | GPL | few | small | Poor by 2026 standards | Skip |

### Recommendation

**Two-tier backend stack**, mirroring the PDF plugin's
`PopplerPDFEngine` / `MuPDFPDFEngine` split:

* **Tier 1 — `TesseractOCREngine`** (default, hard requirement when
  the OCR plugin is enabled). Mature, packaged everywhere, easy to
  build, predictable. Covers the 80% case.
* **Tier 2 — `OnnxOCREngine`** (optional, gated behind
  `ULTRACANVAS_OCR_ONNX=ON`). Loads PP-OCRv4 detection + recognition
  ONNX models via ONNX Runtime; better on scene text, photos,
  rotated/curved lines. ~12 MB of model files shipped in
  `media/ocr/`.
* **Tier 3 — native OS backends** (`AppleVisionOCREngine`,
  `WindowsMediaOCREngine`) auto-selected when available; zero extra
  binary cost. These are the right default on macOS/iOS.

A user-visible enum (see §4) lets the app pick, or `Auto` picks the
best available at runtime.

---

## 3. Integration shape

OCR is a **document plugin**, not a core dependency. It lives next to
PDF:

```
UltraCanvas/Plugins/Documents/OCR/
├── CMakeLists.txt
├── include/
│   └── UltraCanvasOCRPlugin.h        # public API
├── src/
│   ├── UltraCanvasOCRPlugin.cpp      # facade + engine selection
│   ├── TesseractOCREngine.cpp        # tier 1
│   ├── OnnxOCREngine.cpp             # tier 2 (optional)
│   ├── AppleVisionOCREngine.mm       # macOS only
│   └── WindowsMediaOCREngine.cpp     # Windows only
└── media/                            # bundled tessdata-fast / ONNX models
```

CMake wire-up follows the existing PDF pattern
(`UltraCanvas/CMakeLists.txt:560`):

```cmake
option(ULTRACANVAS_PLUGIN_OCR "Build OCR plugin" OFF)
option(ULTRACANVAS_OCR_ONNX  "Enable ONNX Runtime OCR backend" OFF)

if(ULTRACANVAS_PLUGIN_OCR)
    pkg_check_modules(TESSERACT tesseract)
    pkg_check_modules(LEPTONICA lept)
    if(TESSERACT_FOUND AND LEPTONICA_FOUND)
        add_subdirectory(Plugins/Documents/OCR)
        list(APPEND ULTRACANVAS_PLUGIN_TARGETS ${ULTRACANVAS_OCR_PLUGIN_TARGET})
        message(STATUS "  [✓] OCR Plugin - ENABLED")
    else()
        message(STATUS "  [✗] OCR Plugin - DISABLED (missing tesseract/leptonica)")
    endif()
endif()
```

Image input flows through `PFXImage` (the libvips-backed image type
already in PixelFX), so the OCR plugin gets deskew/binarize/upscale
for free without pulling in OpenCV.

---

## 4. Proposed C++ API

Public header sketch — sits next to `PDF.h`, follows the framework's
PascalCase + `#ifdef ULTRACANVAS_OCR_SUPPORT` convention.

```cpp
// UltraCanvasOCRPlugin.h
#pragma once
#ifdef ULTRACANVAS_OCR_SUPPORT

#include "UltraCanvasCommonTypes.h"
#include "PixelFX/PixelFX.h"
#include <string>
#include <vector>
#include <memory>

namespace UltraCanvas {

enum class OCREngineKind {
    Auto,            // pick best available
    Tesseract,
    ONNX,            // PP-OCRv4 via ONNX Runtime
    AppleVision,     // macOS / iOS
    WindowsMedia     // Windows 10+
};

enum class OCRPageSegmentation {
    Auto,
    SingleBlock,
    SingleLine,
    SingleWord,
    SparseText,      // scattered text on background
    RawLine
};

struct OCRConfig {
    OCREngineKind       engine        = OCREngineKind::Auto;
    OCRPageSegmentation segmentation  = OCRPageSegmentation::Auto;
    std::vector<std::string> languages = {"eng"};  // ISO-639-2 / tessdata codes
    bool                preprocess    = true;  // deskew + binarize + denoise
    int                 dpiHint       = 0;     // 0 = auto from image metadata
    float               minConfidence = 0.0f;  // drop words below this score
    std::string         dataPath;              // tessdata / ONNX model dir; empty = bundled
};

struct OCRWord {
    std::string text;
    Rect2Di     box;          // pixel coords in the source image
    float       confidence;   // 0..1
    int         lineIndex;
    int         paragraphIndex;
    int         blockIndex;
    std::string language;     // when multi-language input is enabled
};

struct OCRLine {
    std::string text;
    Rect2Di     box;
    std::vector<int> wordIndices;
    float       baselineAngleDeg;
};

struct OCRResult {
    std::string             plainText;     // reading-order text
    std::vector<OCRWord>    words;
    std::vector<OCRLine>    lines;
    std::vector<Rect2Di>    paragraphs;
    std::vector<Rect2Di>    blocks;
    double                  elapsedMs = 0.0;
    OCREngineKind           engineUsed = OCREngineKind::Auto;
    std::string             error;         // empty on success
};

class IOCREngine {
public:
    virtual ~IOCREngine() = default;
    virtual bool        Initialize(const OCRConfig& cfg) = 0;
    virtual OCRResult   Recognize(const PFXImage& image) = 0;
    virtual std::vector<std::string> AvailableLanguages() const = 0;
    virtual OCREngineKind Kind() const = 0;
};

// ===== Facade =====
class UltraCanvasOCR {
public:
    explicit UltraCanvasOCR(const OCRConfig& cfg = {});
    ~UltraCanvasOCR();

    OCRResult RecognizeFile(const std::string& path);
    OCRResult RecognizeImage(const PFXImage& img);
    OCRResult RecognizeBuffer(const void* data, size_t bytes,
                              const std::string& formatHint = "");

    // Multi-page convenience (renders pages via the PDF plugin if present)
    std::vector<OCRResult> RecognizePDF(const std::string& path,
                                        int firstPage = 1,
                                        int lastPage  = -1,
                                        int renderDpi = 300);

    void SetConfig(const OCRConfig& cfg);
    const OCRConfig& Config() const;

    static std::vector<OCREngineKind> AvailableEngines();
    static std::string DefaultModelsDir();   // resolves bundled media/ocr/

private:
    std::unique_ptr<IOCREngine> engine;
    OCRConfig config;
};

// ===== hOCR / ALTO / searchable-PDF export =====
std::string OCRResultToHOCR(const OCRResult&, const Size2Di& imageSize);
std::string OCRResultToALTO(const OCRResult&, const Size2Di& imageSize);
bool        OCRWriteSearchablePDF(const std::string& outPath,
                                  const std::vector<PFXImage>& pages,
                                  const std::vector<OCRResult>& perPage);

} // namespace UltraCanvas
#endif // ULTRACANVAS_OCR_SUPPORT
```

### Why these shapes

* `PFXImage` everywhere on the input side — already pulls PNG/JPEG/
  WebP/TIFF/etc. through libvips and gives free preprocessing.
* `OCRResult` carries words **and** layout — required for selectable
  text overlays in the PDF viewer and the planned "select text in any
  image" gesture.
* `Rect2Di` (not floats) matches how the rest of the framework
  stores pixel rectangles.
* `RecognizePDF` reuses the Poppler engine that already exists in
  the PDF plugin — when both plugins are linked, OCR can rasterise
  scanned PDFs without an extra dependency.
* The facade is move-only and engine-pluggable; tests can swap in a
  deterministic `MockOCREngine` exactly like UltraAI does for its
  capability interfaces.

---

## 5. Pre-processing (PixelFX)

A single `OCRPreprocess(PFXImage&, const OCRConfig&)` step runs before
each engine call when `cfg.preprocess` is true. It is a fixed pipeline,
all libvips operations already exposed by PixelFX:

1. **Auto-orient** (EXIF / Tesseract OSD).
2. **Greyscale** (`colourspace → B_W`).
3. **Up-sample** if DPI < 200 (bicubic to ~300 DPI equivalent).
4. **Background normalisation** (`rank` filter, large radius) →
   subtract from source to flatten lighting.
5. **Binarize** (Sauvola threshold; libvips has `hist_local`).
6. **Deskew** via Hough — 0.5° accuracy is enough.
7. **Despeckle** (small connected components removed).

Steps 4–5 are skipped on photos handled by ONNX/Apple Vision engines,
which prefer the original RGB. The pipeline is exposed as a separate
function so apps can call it directly for "Clean up scan" UX.

---

## 6. Bundled data

Tesseract needs `*.traineddata` files. Three tiers, mirroring the
upstream releases:

| Tier | Source | Size (eng) | When |
|---|---|---|---|
| `tessdata_fast` | tesseract-ocr/tessdata_fast | ~2 MB | default bundled — `eng`, `osd` |
| `tessdata` | tesseract-ocr/tessdata | ~5 MB | "balanced", downloadable |
| `tessdata_best` | tesseract-ocr/tessdata_best | ~15 MB | "best quality", downloadable |

Shipped: `eng.traineddata` (fast) + `osd.traineddata` in
`media/ocr/tessdata/`. Additional languages are resolved at runtime
from `OCRConfig::dataPath`, the per-user pack dir, the system
`/usr/share/tesseract-ocr/<ver>/tessdata/` and `TESSDATA_PREFIX`.

**All languages, on demand (implemented).** Rather than bundling every
pack, the plugin exposes the complete upstream catalogue and fetches the
rest on first use:

* `UltraCanvasOCR::SupportedLanguages()` — the full catalogue (~130
  languages: code + English name) regardless of what is installed. This
  is the list from the Tesseract manual's LANGUAGES section.
* `UltraCanvasOCR::InstalledLanguages()` / `IsLanguageInstalled(code)`
  — what is present locally now.
* `UltraCanvasOCR::EnsureLanguages({"deu","fra"}, err, tier)` — seeds
  each requested pack from a local copy when one exists, else downloads
  it via UltraNet, consolidates them into one directory (so Tesseract can
  load them together) and reconfigures the engine.
* `UltraCanvasOCR::DownloadLanguage(code, tier, err)` — fetch a single
  pack. `OCRDataTier` selects the source repo: `Fast`→`tessdata_fast`
  (default), `Standard`→`tessdata`, `Best`→`tessdata_best`.

Downloaded packs are cached under `UltraCanvasOCR::LanguageDataDir()`
(`$XDG_DATA_HOME/UltraCanvas/ocr/tessdata` on Linux, `Application
Support/…` on macOS, `%LOCALAPPDATA%\…` on Windows), so each language is
fetched only once. On a build without network support the same files can
be dropped into that directory manually. The demo OCR screen drives this
through a language dropdown populated from the catalogue.

ONNX models (if `ULTRACANVAS_OCR_ONNX=ON`) come from RapidOCR's
release: detection (~4 MB), recognition (~8 MB), classification
(~600 KB). Apache-2.0, redistributable.

---

## 7. UI integration

Two small additions to the existing widgets, no new top-level
elements:

1. **`UltraCanvasImageElement::RecognizeText()`** — async call,
   returns an `OCRResult`; the element gains an optional overlay layer
   that renders selectable, transparent `<span>`-style text boxes on
   top of the bitmap. Selection and copy go through the existing
   clipboard manager.
2. **PDF viewer "OCR this page" action** — wired into the toolbar
   on pages whose embedded text layer is empty (i.e. scanned PDFs).
   The result can be saved back as a sidecar `.hocr` or merged into a
   new searchable PDF via `OCRWriteSearchablePDF`.

The demo's existing `toolsBuilder.AddItem("ocr", ...)` entry becomes a
small demo screen: drop an image, see boxes + extracted text, toggle
language and engine.

---

## 8. Threading & cancellation

Recognition is CPU-bound and can take seconds per page. The facade
exposes both blocking `Recognize*` calls and a future-style variant:

```cpp
std::future<OCRResult> ocr.RecognizeImageAsync(const PFXImage&,
                                               std::stop_token = {});
```

Engines must honour the stop token between detection and recognition
passes (Tesseract: poll `TessBaseAPI::Recognize`'s `ETEXT_DESC`
monitor; ONNX: between batches). The PDF batch call parallelises
across pages with a worker pool capped at `hardware_concurrency()-1`.

---

## 9. Testing

Mirror UltraAI's mock-adapter approach so the test suite never needs
real tessdata:

* `MockOCREngine` — returns a deterministic `OCRResult` built from a
  hard-coded layout, used by widget tests.
* `tests/ocr/` — six fixtures under `Tests/Fixtures/ocr/`:
  clean print, low-DPI scan, rotated page, photo of a sign,
  multi-column page, mixed-language page. Each pinned to expected
  text via a fuzzy-match harness (CER < 5 %, WER < 10 %).
* Round-trip test for `OCRResultToHOCR` (parse it back, compare).
* Searchable-PDF test: run OCR, write PDF, re-extract text via
  Poppler, compare.

---

## 9a. Troubleshooting

**"OCR failed: Engine not initialised" (commonly on Windows)**

This means `TessBaseAPI::Init` could not load a language, almost always
because it could not find `eng.traineddata`. On Linux, Tesseract's
compiled-in default path (`/usr/share/tesseract-ocr/<ver>/tessdata/`)
usually satisfies this even with no configuration; a portable Windows
build has no such system path, so the data must ship with the app.

The engine auto-discovers the data by probing, in order:

1. `OCRConfig::dataPath`
2. `TESSDATA_PREFIX`
3. `<Resources>/media/ocr/`(`/tessdata/`)
4. `<executable dir>/media/ocr/`, `/ocr/`, and next to the executable

So the fix is to make sure `eng.traineddata` is present in one of those
places — the intended location is `media/ocr/tessdata/` beside the app
(see `media/ocr/tessdata/README.md`). Since 0.3.x the error message lists
every path that was searched, so the log shows exactly where to drop the
file. Setting `TESSDATA_PREFIX` to an existing `tessdata` directory also
works.

---

## 10. Risks & open questions

* **Tesseract memory** — `TessBaseAPI` holds ~80 MB resident after
  init. Mitigation: lazy-init in the facade, destroyed when the
  plugin goes idle for >60 s.
* **Leptonica dependency** — Tesseract pulls in `liblept`. On
  Windows we need vcpkg or a pre-built triplet; on macOS,
  Homebrew. Documented in the same place as Poppler today.
* **`PFXImage` ↔ Tesseract pixel layout** — Tesseract wants
  `Pix*` (Leptonica) or raw `uint8_t*` with known stride. PFX is
  libvips, which can write directly to a memory buffer
  (`write_to_memory`). One small adapter, no copy beyond what
  libvips already does.
* **ONNX Runtime size** — adding ORT pulls ~15 MB. Keep behind
  `ULTRACANVAS_OCR_ONNX=ON`, off by default.
* **Searchable PDF writer** — pure-C++ implementations exist
  (`hpdf`, libharu) but we may want to defer this to Phase 2 and
  ship hOCR + ALTO export first.
* **Cloud OCR overlap with UltraAI** — keep a clean line:
  `UltraCanvas::OCR` is **local**. Cloud OCR (GPT-4V / Azure /
  Google Vision) ships as adapters under
  `UltraAI::IVisionAnalyzer`. A future `IVisionAnalyzer` adapter
  can be backed by `UltraCanvas::OCR` so apps don't have to choose
  the surface up-front.

---

## 11. Phased delivery

| Phase | Scope | Effort |
|---|---|---|
| **P1** | Plugin skeleton + CMake option + Tesseract engine + facade + `RecognizeImage` + demo screen | ~3 days |
| **P2** | hOCR / ALTO export · PDF page rasterization via Poppler · multi-page batch · async + cancellation | ~2 days |
| **P3** | `UltraCanvasImageElement` selectable-text overlay · PDF viewer "OCR page" toolbar action | ~2 days |
| **P4** | Searchable-PDF writer · language-pack downloader (UltraNet) | ~2 days |
| **P5** | ONNX engine (PP-OCRv4) behind `ULTRACANVAS_OCR_ONNX` | ~3 days |
| **P6** | Native backends: `AppleVisionOCREngine` (macOS), `WindowsMediaOCREngine` | ~2 days each |

Each phase is independently shippable behind the master
`ULTRACANVAS_PLUGIN_OCR` flag.

---

## 12. Recommendation

Start at **P1 + P2**:

1. Land `ULTRACANVAS_PLUGIN_OCR` (Tesseract-only) wired into the
   existing plugin system.
2. Implement `UltraCanvasOCR` + `TesseractOCREngine` + the demo
   screen.
3. Ship hOCR export and PDF page rasterization so scanned PDFs
   become searchable.

That is enough to flip the demo entry from `NotImplemented` to
`FullyImplemented`, give the PDF viewer real value on scanned docs,
and give UltraAI a concrete local backend to wrap.
