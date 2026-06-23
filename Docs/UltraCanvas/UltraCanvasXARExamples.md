# UltraCanvasXAR Documentation

## Overview

The `UltraCanvasXARElement` is a UI element that loads and renders **Xara vector graphics** (`.xar`, `.web`, `.wix`) inside an UltraCanvas window. It is part of the `UltraCanvasXARPlugin`, which parses the binary Xara record stream (per the Xara Format Specification, Appendix A) into an in-memory tree of `XARNode` objects — spreads, layers, groups, paths, regular shapes (rectangles, ellipses, polygons), text stories, bitmaps and effect nodes — and then plays that tree back through the standard `IRenderContext`. Coordinates are stored internally in millipoints (1/72000 inch) and converted to pixels at render time.

XAR support is **partially implemented**. Parsing of the document structure and the core renderable nodes (paths, rectangles, ellipses, polygons, groups, layers, text strings, bitmaps) is in place, along with flat / gradient / bitmap fills, line attributes, transparency and the attribute stack. Higher-level effect nodes such as `XARBlendNode`, `XARMouldNode`, `XARBevelNode`, `XARContourNode`, `XARFeatherNode` and `XARLiveEffectNode` are parsed into the tree but have limited or no visual rendering. Unlike the sibling CDR element, the XAR element exposes **no multi-page navigation API** (no `GetPageCount` / `SetCurrentPage`) and **no `CDRFitMode`-style fit modes**; viewport handling is done with a single uniform `scale` plus an aspect-ratio flag. The commented-out blocks in the demo source (`SetFitMode`, `SetZoom`, `GetCurrentPage`, `onPageChanged`, …) reference an API that does **not** exist on this element — only the symbols documented below are real.

**Version:** 1.0.0  
**Header:** `Plugins/Vector/XAR/UltraCanvasXARPlugin.h`  
**Namespace:** `UltraCanvas`  
**Base Class:** `UltraCanvasUIElement` (the plugin class derives from `IGraphicsPlugin`)

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasXARElement

IGraphicsPlugin
    └── UltraCanvasXARPlugin

XARNode
    ├── XARGroupNode
    ├── XARPathNode
    ├── XARRectangleNode
    ├── XAREllipseNode
    ├── XARPolygonNode
    ├── XARTextStoryNode / XARTextLineNode / XARTextStringNode
    ├── XARLayerNode / XARSpreadNode / XARChapterNode / XARPageNode
    ├── XARBitmapNode
    └── XARShadowNode / XARBevelNode / XARContourNode / XARBlendNode /
        XARMouldNode / XARClipViewNode / XARFeatherNode / XARLiveEffectNode / XARBrushNode
```

The element owns an `XARDocument`, which parses the file into the `XARNode` tree (rooted at `XARDocument::GetRoot()`) and renders it via `XARDocument::Render()`.

## Features

- Xara `.xar` format parsing (the plugin also advertises `web` and `wix` extensions)
- Binary record stream parsing per the official Xara spec (Appendix A tag values), including zlib-compressed sections
- Vector paths (filled / stroked / filled-stroked, absolute and relative encodings)
- Regular shapes: rectangles (simple/rounded/stellated variants), ellipses, polygons
- Document structure: spreads, chapters, layers (visibility / guide handling), pages
- Fills: flat, linear / circular / elliptical / conical gradients, diamond, three-colour, four-colour, bitmap, contone bitmap, fractal and noise (`XARFillType`), including multistage gradient stops
- Transparency attributes with composition modes (`XARTransparencyMix`)
- Line attributes: width, colour, caps, joins, mitre limit, dash patterns, arrowheads
- Text stories, lines and strings with font / size / style / justification attributes (`XARTextAttribute`)
- Embedded bitmaps (JPEG / PNG / BMP / GIF / 8bpp / XPE) and named colour, font, arrow and dash definitions
- Uniform scaling and aspect-ratio-preserving display through the UI element
- Registers with `UltraCanvasGraphicsPluginRegistry` for extension-based file dispatch

> Effect nodes (blend, mould, bevel, contour, feather, live effect, brush) are captured during parsing but are not yet fully rendered — treat rendering of these as work in progress.

## Header Include

```cpp
#include "Plugins/Vector/XAR/UltraCanvasXARPlugin.h"
```

## Class Reference

### Format Constants

```cpp
namespace XARConstants {
    constexpr uint32_t MAGIC_XARA = 0x41524158;        // "XARA" little-endian
    constexpr uint32_t MAGIC_SIGNATURE = 0x0A0DA3A3;
    constexpr float MILLIPOINTS_PER_INCH = 72000.0f;
    constexpr float MILLIPOINTS_TO_PIXELS = 72.0f / 72000.0f;
}
```

### Coordinate Helper

```cpp
// Convert a millipoint coordinate to pixels at the given scale.
inline Point2Dd MillipointsToPixels(const Point2Di& mp, float scale = 1.0f);
```

### Key Enumerations

```cpp
enum class XARPathVerb : uint8_t { MoveTo = 6, LineTo = 2, BezierTo = 4, ClosePath = 1 };

enum class XARFillType {
    NoneFill, Flat, LinearGradient, CircularGradient, EllipticalGradient,
    ConicalGradient, Diamond, ThreeColour, FourColour,
    Bitmap, ContoneBitmap, Fractal, Noise
};

enum class XARFillRepeat { NonRepeating, Repeating, RepeatingInverted, RepeatingExtra };
enum class XARFillEffect { Fade, Rainbow, AltRainbow };

enum class XARTransparencyType {
    NoTrans, Flat, LinearGradient, CircularGradient, EllipticalGradient,
    ConicalGradient, Diamond, ThreeColour, FourColour, Bitmap, Fractal, Noise
};

enum class XARTransparencyMix {
    NoMix = 0, Mix = 1, Stained = 2, Bleach = 3, Contrast = 4,
    Saturation = 5, Darken = 6, Lighten = 7, Brightness = 8,
    Luminosity = 9, Hue = 10
};

enum class XARWindingRule { NonZero = 0, EvenOdd = 2 };

enum class XARNodeType {
    Document, Chapter, Spread, Layer, Page,
    Group, Path, Rectangle, Ellipse, Polygon,
    Text, TextStory, TextLine, TextString,
    Bitmap, ContonedBitmap,
    Blend, Mould, Bevel, Contour, Shadow,
    ClipView, Feather, LiveEffect, Brush, Unknown
};
```

> `XARTag` (in the same header) and `VectorConverter::XARTags` (in `UltraCanvasXARConverter.h`) enumerate the full set of Xara record tag IDs from Appendix A; you rarely touch these directly unless extending the parser.

### XARMatrix

A 2x3 affine transform stored with `double` components; translation (`e`, `f`) is in millipoints.

```cpp
struct XARMatrix {
    double a = 1.0, b = 0.0;
    double c = 0.0, d = 1.0;
    double e = 0.0, f = 0.0;   // translation in millipoints

    XARMatrix();
    XARMatrix(double a_, double b_, double c_, double d_, double e_, double f_);

    bool IsIdentity() const;
    void ApplyToContext(IRenderContext* ctx) const;
    Point2Di Transform(const Point2Di& coord) const;
};
```

### XARDocument

Owns the parsed node tree and the reusable definition tables (colours, bitmaps, fonts, arrows, dashes). The `UltraCanvasXARElement` constructs and drives this for you; you only need it directly via `GetDocument()` for inspection.

```cpp
class XARDocument {
public:
    XARDocument();
    ~XARDocument();

    bool LoadFromFile(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* data, size_t size);

    float GetWidth() const;
    float GetHeight() const;
    Rect2Dd GetViewBox() const;          // Rect2Dd(0, 0, width, height)

    XARNodePtr GetRoot() const;

    XARColorDefinition*  GetColor(int32_t ref);
    XARBitmapDefinition* GetBitmap(int32_t ref);
    XARFontDefinition*   GetFont(int32_t ref);
    XARArrowDefinition*  GetArrow(int32_t ref);
    XARDashDefinition*   GetDash(int32_t ref);
    Color ResolveColorRef(int32_t ref);

    void Render(IRenderContext* ctx, float scale = 1.0f);

    const std::string& GetProducer() const;
    const std::string& GetProducerVersion() const;
    const std::string& GetProducerBuild() const;
    const std::string& GetFileType() const;
};
```

### XARNode

Base node in the parsed tree. Each node captures a snapshot of the fill / line / transparency / text attributes that were active when it was parsed, plus its children.

```cpp
using XARNodePtr = std::shared_ptr<XARNode>;

class XARNode {
public:
    XARNodeType type = XARNodeType::Unknown;
    std::vector<XARNodePtr> children;
    std::weak_ptr<XARNode> parent;

    XARFillAttribute fill;
    XARTransparencyAttribute transparency;
    XARLineAttribute line;
    XARWindingRule windingRule = XARWindingRule::NonZero;
    XARTextAttribute textAttr;
    bool hasFill = false;
    bool hasLine = false;
    bool hasTransparency = false;

    Rect2Dd bounds;
    bool boundsCached = false;

    virtual void Render(IRenderContext* ctx, float scale = 1.0f);
    void AddChild(XARNodePtr child);
    Rect2Dd CalculateBounds() const;
};
```

### UltraCanvasXARElement

#### Constructor

```cpp
UltraCanvasXARElement(const std::string& identifier,
                      float x, float y, float w, float h);
```

#### Loading

```cpp
bool LoadFromFile(const std::string& filepath);
bool LoadFromMemory(const uint8_t* data, size_t size);

// Reason for the most recent failed load (locked / missing / not a valid
// XAR file). Empty after a successful load.
const std::string& GetLastError() const;
```

#### Display (Scale & Aspect Ratio)

```cpp
void  SetScale(float s);
float GetScale() const;
void  SetPreserveAspectRatio(bool preserve);   // default true
bool  GetPreserveAspectRatio() const;
```

> There is no `SetFitMode` / `SetZoom` / page-navigation API on this element. Zoom is performed by multiplying / dividing `GetScale()` and calling `SetScale()` (see Example 2). Aspect-ratio-preserving "fit page" behaviour is toggled with `SetPreserveAspectRatio(true)`.

#### Inspection & Rendering

```cpp
const XARDocument* GetDocument() const;   // nullptr until a file is loaded
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
```

### UltraCanvasXARPlugin

Registers as an `IGraphicsPlugin` so the framework's graphics plugin registry can auto-detect and load Xara files.

```cpp
class UltraCanvasXARPlugin : public IGraphicsPlugin {
public:
    std::string GetPluginName() const override;       // "UltraCanvas XAR Plugin"
    std::string GetPluginVersion() const override;    // "2.0.0"
    std::vector<std::string> GetSupportedExtensions() const override;
        // returns {"xar", "web", "wix"}

    bool CanHandle(const std::string& filePath) const override;
    bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

    std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
    std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
    std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                         GraphicsFormatType type) override;

    GraphicsManipulation GetSupportedManipulations() const override;
        // Move | Rotate | Scale | Flip | Transform
    GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
    bool ValidateFile(const std::string& filePath) override;
};
```

#### Plugin Registration

```cpp
inline std::shared_ptr<UltraCanvasXARPlugin> CreateXARPlugin();

inline void RegisterXARPlugin() {
    UltraCanvasGraphicsPluginRegistry::RegisterPlugin(CreateXARPlugin());
}
```

Call `RegisterXARPlugin()` once during application startup to wire XAR support into the framework's file-loading pipeline.

### XARElementBuilder

A fluent builder that constructs, configures and (optionally) loads an element in one expression.

```cpp
class XARElementBuilder {
public:
    XARElementBuilder& SetIdentifier(const std::string& s);
    XARElementBuilder& SetPosition(long px, long py);
    XARElementBuilder& SetSize(long ww, long hh);
    XARElementBuilder& SetFilePath(const std::string& p);
    XARElementBuilder& SetScale(float s);
    XARElementBuilder& SetPreserveAspectRatio(bool p);

    std::shared_ptr<UltraCanvasXARElement> Build();
};
```

## XAR Converter (`UltraCanvasXARConverter.h`)

A separate, lower-level converter lives in the `UltraCanvas::VectorConverter` namespace. It implements the generic `IVectorFormatConverter` interface (import/export to the framework's `VectorStorage::VectorDocument`), independent of the UI element above. Use it when you need format conversion rather than on-screen display.

```cpp
namespace UltraCanvas { namespace VectorConverter {

class XARConverter : public IVectorFormatConverter {
public:
    XARConverter();
    ~XARConverter() override;

    VectorFormat GetFormat() const override;              // VectorFormat::XAR
    std::string  GetFormatName() const override;          // "Xara Format"
    std::string  GetFormatVersion() const override;       // "2.0"
    std::vector<std::string> GetFileExtensions() const override;  // {".xar", ".xar", ".web"}
    std::string  GetMimeType() const override;            // "application/x-xara"

    bool CanImport() const override;                      // true
    bool CanExport() const override;                      // true

    std::shared_ptr<VectorStorage::VectorDocument> Import(
        const std::string& filename,
        const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromString(...);
    std::shared_ptr<VectorStorage::VectorDocument> ImportFromStream(...);

    bool Export(const VectorStorage::VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    std::string ExportToString(...);
    bool ExportToStream(...);

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::string& data) const override;

    void SetXAROptions(const XARConversionOptions& opts);
    const XARConversionOptions& GetXAROptions() const;
};

}} // namespace UltraCanvas::VectorConverter
```

`XARConversionOptions` controls compression, progressive rendering, layer/effect preservation, strict mode, a feather-fallback opacity, and optional warning / progress callbacks. The header also provides `XARCoordUtils` and `XARColourUtils` helpers for millipoint / 16.16-fixed-point / colour conversions.

## Examples

All examples below are drawn from or based on `Apps/DemoApp/UltraCanvasXARExamples.cpp`.

### Example 1: Loading and Displaying a XAR File

```cpp
// Create a bordered container to host the XAR view
auto xarContainer1 = std::make_shared<UltraCanvasContainer>(
    "XARContainer1", 20, 100, 300, 280);
xarContainer1->SetBackgroundColor(Colors::White);
xarContainer1->SetBorders(2, Color(180, 180, 180, 255));

// Create the XAR element
auto xarElement1 = std::make_shared<UltraCanvasXARElement>(
    "XAR1", 10, 10, 280, 220);

// Resolve the demo asset path and load
std::string xarFile1 = NormalizePath(GetResourcesDir() + "media/xar/demo.xar");
if (xarElement1->LoadFromFile(xarFile1)) {
    statusLabel->SetText("Loaded: " + xarFile1);
} else {
    statusLabel->SetText("Failed: " + xarElement1->GetLastError());
}

auto xarLabel1 = std::make_shared<UltraCanvasLabel>("XARLabel1", 10, 240, 280, 30);
xarLabel1->SetText("demo.xar");
xarLabel1->SetAlignment(TextAlignment::Center);
xarLabel1->SetFontSize(11);
xarContainer1->AddChild(xarLabel1);

xarContainer1->AddChild(xarElement1);
container->AddChild(xarContainer1);
```

### Example 2: Click-to-Fullscreen Handler with Zoom

The demo defines a small handler class that opens any clicked XAR file in a dedicated fullscreen window. Zoom is implemented purely through `SetScale()` / `GetScale()`, and "Fit Page" via `SetPreserveAspectRatio(true)`.

```cpp
class XARDemoHandler {
private:
    std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
    std::string xarFilePath;

public:
    XARDemoHandler(const std::string& filePath) : xarFilePath(filePath) {}

    void OnXARClick() {
        if (!fullscreenWindow) {
            CreateFullscreenWindow();
        }
    }

    void CreateFullscreenWindow() {
        int screenWidth  = 1920;
        int screenHeight = 1080;

        WindowConfig config;
        config.title     = "XAR Fullscreen Viewer";
        config.width     = screenWidth;
        config.height    = screenHeight;
        config.x         = 0;
        config.y         = 0;
        config.type      = WindowType::Fullscreen;
        config.resizable = false;

        fullscreenWindow = CreateWindow(config);
        fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

        // Fullscreen XAR viewer
        auto fullscreenXAR = std::make_shared<UltraCanvasXARElement>(
                "FullscreenXAR", 0, 50, screenWidth, screenHeight - 100);
        if (!xarFilePath.empty()) {
            fullscreenXAR->LoadFromFile(xarFilePath);
        }
        fullscreenWindow->AddChild(fullscreenXAR);

        // Zoom controls — scale up / down by 1.25x each click
        auto btnZoomOut = std::make_shared<UltraCanvasButton>("BtnZoomOut", 400, 10, 40, 30);
        btnZoomOut->SetText("-");
        btnZoomOut->onClick = [fullscreenXAR]() {
            fullscreenXAR->SetScale(fullscreenXAR->GetScale() / 1.25f);
        };
        fullscreenWindow->AddChild(btnZoomOut);

        auto btnZoomIn = std::make_shared<UltraCanvasButton>("BtnZoomIn", 450, 10, 40, 30);
        btnZoomIn->SetText("+");
        btnZoomIn->onClick = [fullscreenXAR]() {
            fullscreenXAR->SetScale(fullscreenXAR->GetScale() * 1.25f);
        };
        fullscreenWindow->AddChild(btnZoomIn);

        auto btnFitPage = std::make_shared<UltraCanvasButton>("BtnFit", 500, 10, 80, 30);
        btnFitPage->SetText("Fit Page");
        btnFitPage->onClick = [fullscreenXAR]() {
            fullscreenXAR->SetPreserveAspectRatio(true);
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

Per-element `SetEventCallback()` wired to an `XARDemoHandler`. Mouse-enter / leave swap the container's border colour for visual feedback; mouse-up triggers the fullscreen viewer.

```cpp
auto demoHandler1 = std::make_shared<XARDemoHandler>(xarFile1);

xarElement1->SetEventCallback(
    [demoHandler1, xarContainer1, statusLabel, xarFile1](const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseUp:
                demoHandler1->OnXARClick();
                statusLabel->SetText("Opened fullscreen: " + xarFile1);
                return true;
            case UCEventType::MouseEnter:
                xarContainer1->SetBordersColor(Color(0, 122, 204, 255));
                return true;
            case UCEventType::MouseLeave:
                xarContainer1->SetBordersColor(Color(180, 180, 180, 255));
                return true;
            default:
                return false;
        }
    });
```

### Example 4: Building an Element with `XARElementBuilder`

```cpp
auto xarElement = XARElementBuilder()
    .SetIdentifier("Logo")
    .SetPosition(10, 10)
    .SetSize(280, 220)
    .SetFilePath(NormalizePath(GetResourcesDir() + "media/xar/logo.xar"))
    .SetPreserveAspectRatio(true)
    .SetScale(1.0f)
    .Build();   // file is loaded inside Build()

container->AddChild(xarElement);
```

### Example 5: Registering the Plugin and Inspecting the Document

```cpp
// During application startup, wire XAR into the file-loading pipeline:
RegisterXARPlugin();

// Later, load and inspect parsed document metadata:
auto element = std::make_shared<UltraCanvasXARElement>("Inspect", 0, 0, 400, 400);
if (element->LoadFromFile(path)) {
    if (const XARDocument* doc = element->GetDocument()) {
        std::cout << "Producer: "    << doc->GetProducer()        << "\n"
                  << "Version: "      << doc->GetProducerVersion() << "\n"
                  << "Size (px): "    << doc->GetWidth() << " x "  << doc->GetHeight() << "\n";
    }
} else {
    std::cerr << "Load failed: " << element->GetLastError() << "\n";
}
```

## Best Practices

- **Check the return value of `LoadFromFile()`** and surface `GetLastError()` on failure — it distinguishes a locked file, a missing file, and an invalid (non-XAR) file.
- **Register the plugin once** with `RegisterXARPlugin()` at startup if you want extension-based dispatch through `UltraCanvasGraphicsPluginRegistry`; otherwise instantiate `UltraCanvasXARElement` directly.
- **Use `SetScale()` for zoom.** There is no fit-mode or page API here — multiply/divide `GetScale()` for zoom in/out, and use `SetPreserveAspectRatio(true)` for aspect-correct "fit" display.
- **Prefer `XARElementBuilder`** when constructing-and-loading in one place; it applies scale / aspect-ratio settings and loads the file inside `Build()`.
- **Treat effect nodes as best-effort.** Blend, mould, bevel, contour, feather, live-effect and brush nodes are parsed but not yet fully rendered; do not rely on their visual output.
- **Don't follow the commented-out demo blocks verbatim.** The `SetFitMode` / `SetZoom` / `GetCurrentPage` / `onPageChanged` calls in `UltraCanvasXARExamples.cpp` are disabled because that API does not exist on this element — only the methods documented above are real.
- **Use `XARConverter` (not the UI element) for format conversion.** When you need to import a XAR into a `VectorStorage::VectorDocument` or export one, reach for `VectorConverter::XARConverter` and its `XARConversionOptions`.
- **Remember the coordinate system.** Internal geometry is in millipoints (1/72000 inch); use `MillipointsToPixels()` / `XARConstants::MILLIPOINTS_TO_PIXELS` when working with raw node coordinates.

## See Also

- `Apps/DemoApp/UltraCanvasXARExamples.cpp` — the demo source these examples are based on
- `Plugins/Vector/XAR/UltraCanvasXARPlugin.h` — element, plugin, node tree, attribute and builder declarations
- `Plugins/Vector/UltraCanvasXARConverter.h` — the `VectorConverter::XARConverter` import/export converter, tag tables and conversion utilities
- [UltraCanvasCDRExamples](UltraCanvasCDRExamples.md) — sibling CorelDRAW vector plugin
- [UltraCanvasSVGExamples](UltraCanvasSVGExamples.md) — SVG vector graphics plugin
- [UltraCanvasBitmapExamples](UltraCanvasBitmapExamples.md) — Raster image display
