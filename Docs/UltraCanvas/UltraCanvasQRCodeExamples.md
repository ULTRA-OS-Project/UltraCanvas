# UltraCanvasQRCode Documentation

## Overview

`UltraCanvasQRCode` is a 2D QR code widget that encodes a string into a QR matrix and renders it through the standard `IRenderContext`. It supports all four error-correction levels, version 1–40 matrices, fully customizable module/finder shapes, solid colours and gradients, an embedded centre logo with a quiet zone, and export to SVG or raster image formats. Generation is provided by free functions in the `QRCodeUtils` namespace, and (when a decoder backend such as libzbar is available) images can be scanned back into their data.

**Version:** 1.0.0
**Header:** `include/Plugins/QRCode/UltraCanvasQRCode.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **QR generation** from arbitrary text, URLs, and structured content (email, SMS, tel, Wi-Fi, vCard, geo)
- **Four error-correction levels**: Low (~7%), Medium (~15%), Quartile (~25%), High (~30%)
- **Encoding modes**: Auto, Numeric, Alphanumeric, Byte
- **Version control**: clamp the chosen symbol to a `[minVersion, maxVersion]` range (1–40)
- **Module styling**: Square, RoundedSquare, Circle, Diamond, Hexagon, VerticalBars, HorizontalBars, Dots, plus module roundness/scale
- **Finder styling**: Standard, Rounded, Circle, RoundedCircle, Diamond, with an optional dedicated finder colour
- **Colours & gradients**: solid foreground/background, plus Linear, Radial, and Diagonal foreground gradients
- **Embedded logo**: centre logo with several placement styles (Center, CenterWithBorder, CenterRounded, CenterCircle), adjustable scale, border, and corner radius
- **Quiet zone**: configurable margin in modules
- **Export**: SVG and raster (PNG, JPEG, QOI, WebP, AVIF)
- **Decoding**: scan an image file for one or more QR codes (when a decoder backend is built in)

## Header Include

```cpp
#include "Plugins/QRCode/UltraCanvasQRCode.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasQRCode(const std::string& identifier = "QRCode",
                  int x = 0, int y = 0, int w = 200, int h = 200);
```

### Enums

```cpp
enum class QRErrorCorrection { Low = 0, Medium = 1, Quartile = 2, High = 3 };

enum class QREncodingMode { Auto = 0, Numeric, Alphanumeric, Byte };

enum class QRPointStyle {
    Square = 0, RoundedSquare, Circle, Diamond,
    Hexagon, VerticalBars, HorizontalBars, Dots
};

enum class QRFinderStyle { Standard = 0, Rounded, Circle, RoundedCircle, Diamond };

enum class QRLogoStyle { None = 0, Center, CenterWithBorder, CenterRounded, CenterCircle };

enum class QRGradientType { None = 0, Linear, Radial, Diagonal };

enum class QRImageFormat { PNG = 0, JPEG, QOI, WebP, AVIF };
```

### Configuration Structures

```cpp
struct QRStyleConfig {
    QRPointStyle  pointStyle    = QRPointStyle::Square;
    QRFinderStyle finderStyle   = QRFinderStyle::Standard;
    float moduleRoundness = 0.35f;
    float moduleScale     = 0.92f;

    Color foregroundColor = Colors::Black;
    Color backgroundColor = Colors::White;
    Color finderColor     = Color(0, 0, 0, 0);   // transparent => use foreground

    QRGradientType gradientType       = QRGradientType::None;
    Color          gradientStartColor = Colors::Black;
    Color          gradientEndColor   = Color(0, 102, 204);

    QRLogoStyle              logoStyle        = QRLogoStyle::None;
    std::shared_ptr<UCImage> logoImage;
    float                    logoScale        = 0.20f;
    float                    logoBorderWidth  = 4.0f;
    float                    logoCornerRadius = 6.0f;
    Color                    logoBorderColor  = Colors::White;

    int quietZoneModules = 4;
};

struct QRGeneratorConfig {
    QRErrorCorrection errorCorrection = QRErrorCorrection::Medium;
    QREncodingMode    encodingMode    = QREncodingMode::Auto;
    int               minVersion      = 1;
    int               maxVersion      = 40;
    int               mask            = -1;   // -1 => auto-select best mask
    bool              boostEcl        = true;
    QRStyleConfig     style;
};

struct QRCodeData {
    std::string                content;
    QREncodingMode             mode;
    QRErrorCorrection          errorCorrection;
    int                        version;
    int                        size;          // module count per side
    int                        mask;
    bool                       valid;
    std::string                errorMessage;
    std::vector<std::uint8_t>  matrix;

    bool IsDark(int x, int y) const;           // true if module (x,y) is set
};

struct QRScanResult {
    std::string           data;
    std::string           type;               // e.g. "QRCODE"
    std::vector<Point2Df> polygon;            // corner points in the source image
    bool                  valid;
};
```

### Content & Configuration

```cpp
void SetContent(const std::string& text);
const std::string& GetContent() const;

void SetGeneratorConfig(const QRGeneratorConfig& cfg);
const QRGeneratorConfig& GetGeneratorConfig() const;

void SetStyleConfig(const QRStyleConfig& style);
const QRStyleConfig& GetStyleConfig() const;

void SetErrorCorrection(QRErrorCorrection level);
void SetEncodingMode(QREncodingMode mode);
void SetMinMaxVersion(int minVer, int maxVer);
```

### Styling

```cpp
void SetPointStyle(QRPointStyle s);
void SetFinderStyle(QRFinderStyle s);
void SetModuleRoundness(float r);
void SetModuleScale(float s);
void SetQuietZoneModules(int modules);

void SetForegroundColor(const Color& c);
void SetBackgroundColor(const Color& c);
void SetFinderColor(const Color& c);
void SetGradient(QRGradientType type, const Color& start, const Color& end);
void ClearGradient();
```

### Logo

```cpp
void SetLogo(std::shared_ptr<UCImage> logo, float scale = 0.20f);
void SetLogoStyle(QRLogoStyle s);
void ClearLogo();
```

### State & Inspection

```cpp
const QRCodeData& GetQRData() const;
bool IsValid() const;
int  GetVersion() const;
int  GetModuleCount() const;                  // size per side
const std::string& GetErrorMessage() const;
```

### Export

```cpp
bool ExportToSVG(const std::string& filename, int moduleSize = 10);
bool ExportToImage(const std::string& filename,
                   QRImageFormat format,
                   int moduleSize = 10,
                   std::string* errorMessage = nullptr);
```

### Free Functions — `QRCodeUtils`

```cpp
QRCodeData GenerateQRCode(const std::string& text,
                          QRErrorCorrection ecl = QRErrorCorrection::Medium);
QRCodeData GenerateQRCode(const std::string& text,
                          const QRGeneratorConfig& config);

bool ExportToSVG(const QRCodeData& data,
                 const std::string& filename,
                 int moduleSize = 10,
                 const Color& foreground = Colors::Black,
                 const Color& background = Colors::White);

std::vector<QRScanResult> ScanQRCodeFile(const std::string& imagePath,
                                         std::string* errorMessage = nullptr);
bool IsDecoderAvailable();

// Structured-content builders
std::string CreateURLContent(const std::string& url);
std::string CreateEmailContent(const std::string& email,
                               const std::string& subject = "",
                               const std::string& body    = "");
std::string CreateSMSContent(const std::string& phone,
                             const std::string& message = "");
std::string CreateTelContent(const std::string& phone);
std::string CreateWiFiContent(const std::string& ssid,
                              const std::string& password,
                              const std::string& security = "WPA",
                              bool hidden = false);
std::string CreateVCardContent(const std::string& name,
                               const std::string& phone,
                               const std::string& email = "");
std::string CreateGeoContent(double latitude, double longitude);

bool IsNumeric(const std::string& text);
bool IsAlphanumeric(const std::string& text);
```

## Examples

### 1. Basic QR Code from a URL

Create the widget, set its content, and add it to a container. The matrix is regenerated automatically whenever the content or configuration changes.

```cpp
#include "Plugins/QRCode/UltraCanvasQRCode.h"
using namespace UltraCanvas;

auto qr = std::make_shared<UltraCanvasQRCode>("qr-url", 30, 50, 300, 300);
qr->SetContent("https://ultraos.eu");
container->AddChild(qr);
```

### 2. Choosing an Error-Correction Level

Higher error correction adds redundancy (and a larger symbol) — useful when the code is printed small, may be damaged, or carries an embedded logo.

```cpp
auto qr = std::make_shared<UltraCanvasQRCode>("qr-ecl", 30, 50, 300, 300);
qr->SetContent("https://ultraos.eu");
qr->SetErrorCorrection(QRErrorCorrection::High);   // ~30% recovery

container->AddChild(qr);
```

### 3. Custom Colours, Size, and Module Style

Solid foreground/background colours, a rounded-circle finder, circular data modules, and a wider quiet zone.

```cpp
auto qr = std::make_shared<UltraCanvasQRCode>("qr-styled", 30, 50, 360, 360);
qr->SetContent("UltraCanvas QR");

qr->SetForegroundColor(Color(0, 0, 139));     // indigo modules
qr->SetBackgroundColor(Colors::White);
qr->SetPointStyle(QRPointStyle::Circle);
qr->SetFinderStyle(QRFinderStyle::RoundedCircle);
qr->SetFinderColor(Color(139, 0, 0));         // crimson finders
qr->SetModuleScale(0.92f);
qr->SetQuietZoneModules(4);

container->AddChild(qr);
```

### 4. A Foreground Gradient

Apply a diagonal gradient across the data modules. A gradient overrides any solid foreground colour; call `ClearGradient()` to revert.

```cpp
auto qr = std::make_shared<UltraCanvasQRCode>("qr-grad", 30, 50, 360, 360);
qr->SetContent("https://ultraos.eu");
qr->SetGradient(QRGradientType::Diagonal,
                Color(0, 102, 204), Color(102, 204, 255));

container->AddChild(qr);

// Later: drop the gradient and go back to a solid colour.
qr->ClearGradient();
qr->SetForegroundColor(Colors::Black);
```

### 5. Embedding a Centre Logo

Load a logo image, set its scale (fraction of the QR width), and choose a placement style. Pair logos with `QRErrorCorrection::High` so the covered modules can still be recovered.

```cpp
auto qr = std::make_shared<UltraCanvasQRCode>("qr-logo", 30, 50, 420, 420);
qr->SetContent("https://ultraos.eu");
qr->SetErrorCorrection(QRErrorCorrection::High);

auto logoImg = UCImage::Get("media/images/UOS_logo_white.png");
qr->SetLogo(logoImg, 0.25f);                  // 25% of the QR width
qr->SetLogoStyle(QRLogoStyle::CenterRounded);

container->AddChild(qr);

// Remove the logo again:
qr->ClearLogo();
```

### 6. Tuning the Style via `QRStyleConfig`

For fine-grained control (logo border width, corner radius, module roundness) read the current style, mutate the fields you need, and write it back.

```cpp
auto cfg = qr->GetStyleConfig();
cfg.logoScale       = 0.20f;
cfg.logoBorderWidth = 6.0f;
cfg.logoCornerRadius = 8.0f;
cfg.moduleRoundness = 0.35f;
qr->SetStyleConfig(cfg);
```

### 7. Structured Content Helpers

The `QRCodeUtils` builders produce correctly-formatted payloads for common QR use cases. Feed the result straight into `SetContent`.

```cpp
using namespace UltraCanvas::QRCodeUtils;

qr->SetContent(CreateURLContent("https://ultraos.eu"));
qr->SetContent(CreateEmailContent("info@ultraos.org", "Hello", "Sent from UltraCanvas"));
qr->SetContent(CreateSMSContent("+15551234567", "Hi from UltraCanvas"));
qr->SetContent(CreateWiFiContent("UltraOS-Net", "supersecret", "WPA"));
qr->SetContent(CreateVCardContent("Jane Doe", "+15551234567", "jane@example.com"));
qr->SetContent(CreateGeoContent(37.7749, -122.4194));
```

### 8. Exporting to Image and SVG

`ExportToImage` writes a raster file in the chosen format; `ExportToSVG` writes a vector file. `moduleSize` is the pixel size of one module.

```cpp
std::string err;
if (!qr->ExportToImage("ultracanvas_url.png", QRImageFormat::PNG, 12, &err)) {
    std::cerr << "Save failed: " << err << "\n";
}

qr->ExportToSVG("ultracanvas_url.svg", 10);
```

### 9. Generating Without a Widget

`QRCodeUtils::GenerateQRCode` returns a `QRCodeData` matrix directly — handy for headless generation or custom rendering. Inspect `valid`, `version`, `size`, and read individual modules with `IsDark`.

```cpp
using namespace UltraCanvas;

QRGeneratorConfig config;
config.errorCorrection = QRErrorCorrection::Quartile;
config.encodingMode    = QREncodingMode::Auto;

QRCodeData data = QRCodeUtils::GenerateQRCode("https://ultraos.eu", config);
if (data.valid) {
    QRCodeUtils::ExportToSVG(data, "headless.svg", 10);
    bool topLeftDark = data.IsDark(0, 0);
}
```

### 10. Decoding an Image

When a decoder backend is built in, `ScanQRCodeFile` returns every QR code found in an image. Always gate on `IsDecoderAvailable()` first.

```cpp
using namespace UltraCanvas;

if (QRCodeUtils::IsDecoderAvailable()) {
    std::string err;
    auto results = QRCodeUtils::ScanQRCodeFile("photo.png", &err);
    if (results.empty() && !err.empty()) {
        std::cerr << "Scan error: " << err << "\n";
    }
    for (const auto& r : results) {
        std::cout << "[" << r.type << "] " << r.data << "\n";
    }
} else {
    std::cerr << "Decoder not built in — install libzbar-dev and rebuild.\n";
}
```

## Best Practices

- **Match error correction to use case**: use `High` for printed codes, codes embedding a logo, or codes that may be partially obscured; `Low`/`Medium` keep the symbol smaller for clean, on-screen scanning.
- **Keep a logo small**: a logo scale of ~0.20–0.25 with `QRErrorCorrection::High` generally stays scannable. Larger logos may exceed the recoverable area.
- **Preserve the quiet zone**: keep `quietZoneModules` at 4 (the spec minimum) or higher so scanners can locate the symbol; do not place the QR code flush against busy backgrounds.
- **Verify before relying on output**: check `IsValid()` (and `GetErrorMessage()` on failure) after setting content, especially for long payloads that may exceed the chosen version range.
- **Maintain contrast**: keep a dark foreground on a light background. Decorative gradients and styled modules are fine, but extreme stylings (very low module scale, low contrast) reduce scan reliability.
- **Gradients vs solid colours are exclusive**: setting a gradient overrides the foreground colour; call `ClearGradient()` before relying on `SetForegroundColor` again.
- **Guard decoding**: always call `IsDecoderAvailable()` before `ScanQRCodeFile`, since decoding depends on an optional backend (e.g. libzbar).

## See Also

- Demo source: `Apps/DemoApp/UltraCanvasQRCodeExamples.cpp` — a two-tab Encode/Decode panel showing live styling, content presets, logo upload, image/SVG export, and image scanning.
- Header: `UltraCanvas/include/Plugins/QRCode/UltraCanvasQRCode.h`
- Related: `Docs/UltraCanvas/UltraCanvasBarcodeElement.md` — the 1D barcode sibling widget.
