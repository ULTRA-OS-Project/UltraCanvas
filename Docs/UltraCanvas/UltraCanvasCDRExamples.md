# UltraCanvasCDRElement Documentation

## Overview

The `UltraCanvasCDRElement` is a UI element that loads and renders **CorelDRAW vector graphics** (`.cdr`, `.cmx`, `.ccx`, `.cdt`) inside an UltraCanvas window. It is part of the `UltraCanvasCDRPlugin`, which uses **libcdr** + **librevenge** to parse CDR documents into an intermediate `CDRDocument` of pages, draw commands, gradients, styles, and embedded images, then plays those commands back through the standard `IRenderContext`. Supported features include multi-page documents, vector paths, text with character / paragraph styles, gradients (linear / radial / conical), groups & layers, and configurable fit modes plus interactive zoom & pan.

**Namespace:** `UltraCanvas`  
**Header:** `Plugins/Vector/CDR/UltraCanvasCDRPlugin.h`  
**Implementation Header:** `Plugins/Vector/CDR/UltraCanvasCDRPluginImpl.h`  
**Plugin Interface:** `IGraphicsPlugin`  
**UI Element Base:** `UltraCanvasUIElement`  
**Version:** 1.1.0  
**Last Modified:** 2025-12-15  
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasCDRElement

IGraphicsPlugin
    └── UltraCanvasCDRPlugin
```

The element delegates parsing & rendering to an internal `UltraCanvasCDRRenderer`, which owns the parsed `CDRDocument`.

## Build-Time Guard: `ULTRACANVAS_HAS_CDR_PLUGIN`

The CDR plugin requires `libcdr` and `librevenge` (CorelDRAW format parsing libraries) at build time. When the plugin's CMakeLists.txt detects these libraries it defines:

```cmake
# UltraCanvas/Plugins/Vector/CDR/CMakeLists.txt
ULTRACANVAS_HAS_CDR_PLUGIN=1
```

Application code should gate CDR usage behind this preprocessor symbol so the project still builds on platforms where libcdr is unavailable. The demo wires the CDR examples into the menu like this:

```cpp
// Apps/DemoApp/UltraCanvasDemo.cpp
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
    vectorBuilder.AddItem("cdrimages", "CDR Images",
                          "CDR (CorelDraw) images display and manipulation",
                          ImplementationStatus::FullyImplemented,
                          [this]() { return CreateCDRVectorExamples(); });
#endif
```

```cpp
// Apps/DemoApp/UltraCanvasDemo.h
#ifdef ULTRACANVAS_HAS_CDR_PLUGIN
    // forward declaration of CreateCDRVectorExamples()
#endif
```

Always wrap both the menu entry and the `CreateCDRVectorExamples()` body declaration in the guard.

## Enumerations & Structs

### CDRFitMode

Controls how the parsed page is fit into the element's viewport.

```cpp
enum class CDRFitMode {
    FitNone,    // 1:1 with zoom + offset honoured
    FitWidth,   // Scale so page width fills the viewport
    FitHeight,  // Scale so page height fills the viewport
    FitPage     // Fit the entire page (default)
};
```

### CDRGradientType

```cpp
enum class CDRGradientType { Linear, Radial, Conical };
```

### CDRGradient

```cpp
struct CDRGradient {
    CDRGradientType type = CDRGradientType::Linear;
    std::vector<GradientStop> stops;

    // Linear endpoints
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 1.0f, y2 = 0.0f;

    // Radial centre / focal / radius
    float cx = 0.5f, cy = 0.5f;
    float fx = 0.5f, fy = 0.5f;
    float radius = 0.5f;

    float angle = 0.0f;            // degrees (linear / conical)
    bool useObjectBounds = true;   // true = objectBoundingBox, false = userSpaceOnUse
};
```

### CDRPage / CDRDocument

```cpp
struct CDRPage {
    float width = 0;
    float height = 0;
    std::vector<std::function<void(IRenderContext*)>> drawCommands;
};

class CDRDocument {
public:
    std::vector<CDRPage> pages;
    std::map<std::string, CDRGradient> gradients;
    std::map<std::string, std::vector<uint8_t>> images;

    float documentWidth = 0;
    float documentHeight = 0;
    std::string title;
    std::string author;

    bool IsValid() const;
    int GetPageCount() const;
};
```

### CDRParagraphStyle / CDRCharacterStyle / CDRStyleState

These structs hold the typography and drawing state recovered from the CDR file (text alignment, line height, margins, font family / size / weight / slant, fill / stroke colours, opacities, line caps & joins, dash patterns, named style references). They are populated by `UltraCanvasCDRPainterImpl` (the librevenge drawing interface) while parsing the document.

## UltraCanvasCDRRenderer

Internal renderer used by `UltraCanvasCDRElement`. Application code does not normally instantiate this directly, but the public API is useful when integrating with a custom UI element.

```cpp
class UltraCanvasCDRRenderer {
public:
    bool LoadFromFile(const std::string& filePath);
    bool LoadFromMemory(const std::vector<uint8_t>& data);

    void SetFitMode(CDRFitMode mode);
    CDRFitMode GetFitMode() const;
    bool IsLoaded() const;
    int GetPageCount() const;
    void SetViewport(float w, float h);
    void SetScale(float scale);
    float GetScale() const;
    void SetOffset(float x, float y);
    Point2Df GetOffset() const;

    void RenderPage(IRenderContext* ctx, int pageIndex);
};
```

## UltraCanvasCDRElement

### Constructor

```cpp
UltraCanvasCDRElement(const std::string& identifier,
                      int x, int y, int width, int height);
```

### Loading

```cpp
bool LoadFromFile(const std::string& filePath);
bool LoadFromMemory(const std::vector<uint8_t>& data);

bool IsLoaded() const;
int  GetPageCount() const;
```

### Page Navigation

```cpp
int  GetCurrentPage() const;
void SetCurrentPage(int page);
```

### Zoom / Pan / Fit

```cpp
void SetZoom(float zoom);
float GetZoom() const;
void SetOffset(float x, float y);
Point2Df GetOffset() const;
void SetFitMode(CDRFitMode mode);
CDRFitMode GetFitMode() const;
```

Switching to `CDRFitMode::FitNone` disables automatic fit so subsequent `SetZoom()` / `SetOffset()` calls take effect.

### Callbacks

```cpp
std::function<void(int)>             onPageChanged;
std::function<void(const std::string&)> onLoadError;
std::function<void()>                onLoadComplete;
```

### Rendering Override

```cpp
void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
```

## UltraCanvasCDRPlugin

Registers as an `IGraphicsPlugin` so the framework's graphics plugin registry can auto-detect and load CDR files.

```cpp
class UltraCanvasCDRPlugin : public IGraphicsPlugin {
public:
    std::string GetPluginName() const override;       // "UltraCanvas CDR Plugin"
    std::string GetPluginVersion() const override;    // "1.2.0"
    std::vector<std::string> GetSupportedExtensions() const override;
        // returns {"cdr", "cmx", "ccx", "cdt"}

    bool CanHandle(const std::string& filePath) const override;
    bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

    std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
    std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
    std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                         GraphicsFormatType type) override;

    GraphicsManipulation GetSupportedManipulations() const override;
    GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
    bool ValidateFile(const std::string& filePath) override;

    static bool IsFileSupported(const std::string& filePath);
    static std::shared_ptr<CDRDocument> ParseCDRFile(const std::string& filePath);
    static std::shared_ptr<CDRDocument> ParseCDRMemory(const std::vector<uint8_t>& data);

    // Export ("save as") — see below
    static CDRExportResult ExportToSVG(const std::string& cdrPath,
                                       const std::string& svgPath,
                                       int pageIndex = -1);
    static CDRExportResult ExportToXAR(const std::string& cdrPath,
                                       const std::string& xarPath);
};
```

### Export ("Save As")

`ExportToSVG` converts a CDR/CMX file to SVG by re-parsing the source through
librevenge's `RVNGSVGDrawingGenerator` — the generator receives the same
drawing callbacks as the on-screen painter, so everything libcdr understands
(paths, shapes, gradients, text, embedded bitmaps) is preserved. No extra
dependency: the generator ships in core librevenge.

```cpp
struct CDRExportResult {
    bool success;                          // false => see error
    std::string error;
    std::vector<std::string> writtenFiles; // every file created
};

// pageIndex is 0-based and selects one page; pass -1 to export every page.
// SVG has no multi-page form, so with -1 page N >= 2 goes to "<stem>-p<N>.svg".
auto r = UltraCanvasCDRPlugin::ExportToSVG("brochure.cdr", "out/brochure.svg", -1);
if (r.success)
    for (const auto& f : r.writtenFiles) std::cout << "wrote " << f << "\n";
else
    std::cerr << r.error << "\n";
```

Notes:

- The output directory must exist; the exporter does not create it.
- A page that embeds a very large bitmap produces a large base64 `xlink:href`
  attribute. The file is valid SVG (browsers open it), but renderers built on
  libxml2's default limits (e.g. librsvg) refuse attributes over 10 MB.

`ExportToXAR` is **not implemented yet**: the XAR writer (`XARConverter`,
`Plugins/Vector`) exports only from the shared `VectorStorage` document model,
and no CDR importer into that model exists. It always fails with an error
saying so — offer the format in UI, surface the message, and route users to
SVG meanwhile.

### Plugin Registration

```cpp
inline void RegisterCDRPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(
        std::make_shared<UltraCanvasCDRPlugin>());
}
```

Call `RegisterCDRPlugin()` once during application startup (typically inside an `#ifdef ULTRACANVAS_HAS_CDR_PLUGIN` block) to wire CDR support into the framework's file-loading pipeline.

## Convenience Factories

```cpp
std::shared_ptr<UltraCanvasCDRElement> CreateCDRElement(
        const std::string& identifier,
        int x, int y, int width, int height);

std::shared_ptr<UltraCanvasCDRElement> LoadCDRFromFile(
        const std::string& identifier,
        int x, int y, int width, int height,
        const std::string& filePath);   // returns nullptr on failure
```

## Usage Examples

All examples below are drawn from `Apps/DemoApp/UltraCanvasCDRExamples.cpp`.

### Example 1: Loading and Displaying a CDR File

```cpp
// Create a bordered container to host the CDR view
auto cdrContainer1 = std::make_shared<UltraCanvasContainer>(
    "CDRContainer1", 20, 100, 300, 280);
cdrContainer1->SetBackgroundColor(Colors::White);
cdrContainer1->SetBorders(2, Color(180, 180, 180, 255));

// Create the CDR element and fit the whole page into its bounds
auto cdrElement1 = std::make_shared<UltraCanvasCDRElement>(
    "CDR1", 10, 10, 280, 220);
cdrElement1->SetFitMode(CDRFitMode::FitPage);

// Resolve the demo asset path and load
std::string cdrFile1 = NormalizePath(GetResourcesDir() + "media/cdr/demo.cdr");
if (cdrElement1->LoadFromFile(cdrFile1)) {
    statusLabel->SetText("Loaded: " + cdrFile1 + " (" +
                         std::to_string(cdrElement1->GetPageCount()) + " pages)");
}

auto cdrLabel1 = std::make_shared<UltraCanvasLabel>("CDRLabel1", 10, 240, 280, 30);
cdrLabel1->SetText("demo.cdr");
cdrLabel1->SetAlignment(TextAlignment::Center);
cdrLabel1->SetFontSize(11);
cdrContainer1->AddChild(cdrLabel1);

cdrContainer1->AddChild(cdrElement1);
container->AddChild(cdrContainer1);
```

### Example 2: Click-to-Fullscreen Handler

The demo defines a small handler class that opens any clicked CDR file in a dedicated fullscreen window. Note the use of `onPageChanged` for live page-counter updates and `SetEventCallback()` for hover / click handling.

```cpp
class CDRDemoHandler {
private:
    std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
    std::string cdrFilePath;

public:
    CDRDemoHandler(const std::string& filePath) : cdrFilePath(filePath) {}

    void OnCDRClick() {
        if (!fullscreenWindow) {
            CreateFullscreenWindow();
        }
    }

    void CreateFullscreenWindow() {
        int screenWidth  = 1920;
        int screenHeight = 1080;

        WindowConfig config;
        config.title     = "CDR Fullscreen Viewer";
        config.width     = screenWidth;
        config.height    = screenHeight;
        config.x         = 0;
        config.y         = 0;
        config.type      = WindowType::Fullscreen;
        config.resizable = false;

        fullscreenWindow = CreateWindow(config);
        fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

        // Fullscreen CDR viewer
        auto fullscreenCDR = std::make_shared<UltraCanvasCDRElement>(
                "FullscreenCDR", 0, 50, screenWidth, screenHeight - 100);
        fullscreenCDR->SetFitMode(CDRFitMode::FitPage);
        if (!cdrFilePath.empty()) {
            fullscreenCDR->LoadFromFile(cdrFilePath);
        }
        fullscreenWindow->AddChild(fullscreenCDR);

        // Page navigation
        auto btnPrev = std::make_shared<UltraCanvasButton>("BtnPrev", 10, 10, 80, 30);
        btnPrev->SetText("Prev");
        btnPrev->onClick = [fullscreenCDR]() {
            if (fullscreenCDR->IsLoaded()) {
                int current = fullscreenCDR->GetCurrentPage();
                if (current > 0) {
                    fullscreenCDR->SetCurrentPage(current - 1);
                }
            }
        };
        fullscreenWindow->AddChild(btnPrev);

        auto btnNext = std::make_shared<UltraCanvasButton>("BtnNext", 100, 10, 80, 30);
        btnNext->SetText("Next");
        btnNext->onClick = [fullscreenCDR]() {
            if (fullscreenCDR->IsLoaded()) {
                int current = fullscreenCDR->GetCurrentPage();
                if (current < fullscreenCDR->GetPageCount() - 1) {
                    fullscreenCDR->SetCurrentPage(current + 1);
                }
            }
        };
        fullscreenWindow->AddChild(btnNext);

        // Live page label fed by the onPageChanged callback
        auto pageLabel = std::make_shared<UltraCanvasLabel>("PageLabel", 200, 10, 150, 30);
        pageLabel->SetTextColor(Colors::White);
        if (fullscreenCDR->IsLoaded()) {
            pageLabel->SetText("Page 1/" +
                std::to_string(fullscreenCDR->GetPageCount()));
        }
        fullscreenWindow->AddChild(pageLabel);

        fullscreenCDR->onPageChanged = [pageLabel, fullscreenCDR](int page) {
            pageLabel->SetText("Page " + std::to_string(page + 1) + "/" +
                               std::to_string(fullscreenCDR->GetPageCount()));
        };

        // Zoom controls — switch out of FitPage first
        auto btnZoomOut = std::make_shared<UltraCanvasButton>("BtnZoomOut", 400, 10, 40, 30);
        btnZoomOut->SetText("-");
        btnZoomOut->onClick = [fullscreenCDR]() {
            fullscreenCDR->SetFitMode(CDRFitMode::FitNone);
            fullscreenCDR->SetZoom(fullscreenCDR->GetZoom() / 1.25f);
        };
        fullscreenWindow->AddChild(btnZoomOut);

        auto btnZoomIn = std::make_shared<UltraCanvasButton>("BtnZoomIn", 450, 10, 40, 30);
        btnZoomIn->SetText("+");
        btnZoomIn->onClick = [fullscreenCDR]() {
            fullscreenCDR->SetFitMode(CDRFitMode::FitNone);
            fullscreenCDR->SetZoom(fullscreenCDR->GetZoom() * 1.25f);
        };
        fullscreenWindow->AddChild(btnZoomIn);

        auto btnFitPage = std::make_shared<UltraCanvasButton>("BtnFit", 500, 10, 80, 30);
        btnFitPage->SetText("Fit Page");
        btnFitPage->onClick = [fullscreenCDR]() {
            fullscreenCDR->SetFitMode(CDRFitMode::FitPage);
        };
        fullscreenWindow->AddChild(btnFitPage);

        // ESC closes the fullscreen window
        fullscreenWindow->SetEventCallback([this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp &&
                event.virtualKey == UCKeys::Escape) {
                if (fullscreenWindow) {
                    fullscreenWindow->Close();
                    fullscreenWindow.reset();
                }
                return true;
            }
            return false;
        });

        fullscreenWindow->Show();
    }
};
```

### Example 3: Hover Highlight + Click-to-Open

Per-element `SetEventCallback()` wired to a `CDRDemoHandler`. Mouse-enter / leave swap the container's border colour for visual feedback; mouse-up triggers the fullscreen viewer.

```cpp
auto demoHandler1 = std::make_shared<CDRDemoHandler>(cdrFile1);

cdrElement1->SetEventCallback(
    [demoHandler1, cdrContainer1, statusLabel, cdrFile1](const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseUp:
                demoHandler1->OnCDRClick();
                statusLabel->SetText("Opened fullscreen: " + cdrFile1);
                return true;
            case UCEventType::MouseEnter:
                cdrContainer1->SetBordersColor(Color(0, 122, 204, 255));
                return true;
            case UCEventType::MouseLeave:
                cdrContainer1->SetBordersColor(Color(180, 180, 180, 255));
                return true;
            default:
                return false;
        }
    });
```

### Example 4: Inline Multi-Page Navigation

Inline (non-fullscreen) prev / next buttons driving the `SetCurrentPage()` API and a label that follows `onPageChanged`.

```cpp
auto cdrElement4 = std::make_shared<UltraCanvasCDRElement>("CDR4", 10, 10, 280, 220);
cdrElement4->SetFitMode(CDRFitMode::FitPage);
std::string cdrFile4 = NormalizePath(GetResourcesDir() + "media/cdr/logo.cdr");
cdrElement4->LoadFromFile(cdrFile4);

auto prevBtn4 = std::make_shared<UltraCanvasButton>("Prev4", 10, 240, 60, 25);
prevBtn4->SetText("<");
prevBtn4->onClick = [cdrElement4]() {
    if (cdrElement4->IsLoaded() && cdrElement4->GetCurrentPage() > 0) {
        cdrElement4->SetCurrentPage(cdrElement4->GetCurrentPage() - 1);
    }
};

auto pageLabel4 = std::make_shared<UltraCanvasLabel>("PageLabel4", 80, 240, 140, 25);
pageLabel4->SetText("brochure.cdr");
pageLabel4->SetAlignment(TextAlignment::Center);
pageLabel4->SetFontSize(10);

cdrElement4->onPageChanged = [pageLabel4, cdrElement4](int page) {
    pageLabel4->SetText("Page " + std::to_string(page + 1) + "/" +
                        std::to_string(cdrElement4->GetPageCount()));
};

auto nextBtn4 = std::make_shared<UltraCanvasButton>("Next4", 230, 240, 60, 25);
nextBtn4->SetText(">");
nextBtn4->onClick = [cdrElement4]() {
    if (cdrElement4->IsLoaded() &&
        cdrElement4->GetCurrentPage() < cdrElement4->GetPageCount() - 1) {
        cdrElement4->SetCurrentPage(cdrElement4->GetCurrentPage() + 1);
    }
};
```

## Supported Features (Plugin Summary)

The info panel in the demo lists the headline capabilities exposed by the CDR plugin:

- CorelDRAW CDR format
- Corel Presentation Exchange CMX (and `ccx`, `cdt`)
- Multi-page document support
- Vector paths and shapes
- Text with paragraph + character styling
- Transformations (rotate, scale)
- Groups and layers
- Stroke and fill styles (incl. linear / radial / conical gradients)
- Zoom and pan controls
- Fit modes (FitPage, FitWidth, FitHeight, FitNone)
- Save as SVG via `ExportToSVG` (XAR export planned; see Export section)

Parsing is implemented on top of **libcdr** via `librevenge::RVNGDrawingInterface` (see `UltraCanvasCDRPainterImpl` in `UltraCanvasCDRPluginImpl.h`).

## Integration Checklist

1. **Build:** Ensure the CDR plugin's `CMakeLists.txt` finds `libcdr` and `librevenge` — this defines `ULTRACANVAS_HAS_CDR_PLUGIN=1`.
2. **Guard your code:** Wrap every reference to `UltraCanvasCDRElement`, `RegisterCDRPlugin()`, and any CDR-loading utility behind `#ifdef ULTRACANVAS_HAS_CDR_PLUGIN`.
3. **Register the plugin** during application startup so file-extension dispatch via `UltraCanvasGraphicsPluginRegistry` finds CDR files:
   ```cpp
   #ifdef ULTRACANVAS_HAS_CDR_PLUGIN
   RegisterCDRPlugin();
   #endif
   ```
4. **Load files** either through the plugin (extension-based dispatch) or directly via `UltraCanvasCDRElement::LoadFromFile()` / `LoadCDRFromFile()`.
5. **Pick a fit mode** — `CDRFitMode::FitPage` is the right default for thumbnails; switch to `CDRFitMode::FitNone` before applying user zoom/offset.

## Writing CDR files (`UltraCanvasCDRConverter.h`)

The Vector plugin's `UltraCanvas::VectorConverter::CDRConverter` writes a `VectorStorage::VectorDocument` as a version-7 RIFF CDR file (`Export` / `ExportToString` / `ExportToStream`). It is export-only — `CanImport()` is false; reading stays with this plugin.

CorelDRAW's format has no public specification, so the writer targets the record layouts consumed by libcdr — the reference open-source reader and the engine underneath this plugin — and the round-trip through that parser is the correctness contract (`Tests/CDRWriterTest.cpp`). What the writer emits:

- The RIFF structure `RIFF…CDR7` / `vrsn` / `LIST doc` / `mcfg` (page size) / `fild`+`outl` style definitions / `LIST page` of `LIST obj` objects — written topmost-first, because CDR draws objects in reverse file order.
- All geometry as line-and-curve `loda` chunks (point list + per-point type bytes) with 32-bit coordinates in 1/254000 inch, page-centred with Y up; every `PathCommandType` is normalised through the shared `PathOps` helpers, and transforms are baked into the points (each object carries an identity `trfd`).
- Solid fills (RGB colour model) and outline styles: width, caps, joins, and dash patterns (stored in line-width multiples, so dash lengths round to those units); fill opacity from style opacity and fill alpha.
- Flattened with a warning: gradients (blend of the end stops), pattern fills. Skipped with a warning: text and bitmap objects.

```cpp
using namespace UltraCanvas::VectorConverter;
CDRConverter cdr;
cdr.Export(*document, "drawing.cdr");          // document is a VectorStorage::VectorDocument
```

## See Also

- [UltraCanvasSVGExamples](UltraCanvasSVGExamples.md) — SVG vector graphics plugin
- [UltraCanvasBitmapExamples](UltraCanvasBitmapExamples.md) — Raster image display
- [UltraCanvasPieChartExamples](UltraCanvasPieChartExamples.md) — Pie / donut / 3D chart
- [UltraCanvasPopulationChartExamples](UltraCanvasPopulationChartExamples.md) — Population pyramid charts
- [UltraCanvasJitterPlotExamples](UltraCanvasJitterPlotExamples.md) — Jitter / strip plot charts
