# UltraCanvasBarcodeElement Documentation

## Overview

`UltraCanvasBarcodeElement` is a 1D barcode widget that encodes a string into the chosen symbology and renders the resulting bar pattern via the standard `IRenderContext`. Encoders are pure C++20 with no external dependencies — Cairo (the existing render backend) handles all drawing.

**Version:** 1.0.0
**Header:** `include/Plugins/Barcode/UltraCanvasBarcodeElement.h`
**Implementation:** `Plugins/Barcode/UltraCanvasBarcodeElement.cpp`
**Namespace:** `UltraCanvas`

## Supported Symbologies

| Symbology | Charset | Lengths | Notes |
|---|---|---|---|
| `Code39` | 0-9, A-Z, space, `-.$/+%*` | Variable | Optional mod-43 check |
| `Code39Extended` | Full ASCII | Variable | Shift sequences for control codes & lowercase |
| `Code93` | Code 39 set + 4 control chars | Variable | Mandatory C+K check digits, denser than Code 39 |
| `Code128` | Full ASCII (subsets A/B/C) | Variable | Auto subset selection — most efficient general-purpose 1D |
| `Code128A`, `Code128B`, `Code128C` | Force a specific subset | Variable | Useful when interop demands a fixed subset |
| `GS1_128` | Code 128 + FNC1 | Variable | Use `~F` as the FNC1 sentinel inside data |
| `EAN13` | Digits | 12 or 13 | Auto check digit if 12 supplied |
| `EAN8` | Digits | 7 or 8 | Auto check digit if 7 supplied |
| `UPCA` | Digits | 11 or 12 | Auto check digit |
| `UPCE` | Digits | 6, 7, or 8 | Compressed UPC-A — expand → check → parity |
| `ISBN` | Digits | 12 or 13 with 978/979 prefix | EAN-13 with "ISBN N-NNN-NNNN-N" HRI above |
| `ITF` | Digits | Even length (auto-padded if odd) | Wide:narrow = 2:1 |
| `ITF14` | Digits | 13 or 14 | Bearer bars rendered automatically |
| `Standard25` | Digits | Variable | Industrial 2 of 5 (Code 25) |
| `Codabar` | 0-9, `-$:/.+`, A-D start/stop | Variable | Start + data + stop required (e.g. `A123B`) |
| `MSIPlessey` | Digits | Variable | Mod 10 / Mod 11 / Mod 10+10 / Mod 11+10 |
| `Pharmacode` | Integer 3..131070 | Variable | Laetus binary encoding |

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasBarcodeElement
```

## Quick Start

```cpp
#include "Plugins/Barcode/UltraCanvasBarcodeElement.h"

auto bc = UltraCanvas::CreateBarcode(
    "myBarcode", 20, 20, 300, 120,
    UltraCanvas::BarcodeSymbology::Code128,
    "UltraCanvas-1");

container->AddChild(bc);
```

## Builder Pattern

```cpp
using namespace UltraCanvas;

auto bc = CreateBarcodeBuilder("ean", 50, 50, 240, 100)
    .SetSymbology(BarcodeSymbology::EAN13)
    .SetData("590123412345")          // 12 or 13 digits — check is auto
    .SetModuleWidth(2.0f)
    .SetBarHeight(80.0f)
    .SetTextPosition(BarcodeTextPosition::TextBelow)
    .SetQuietZoneModules(10.0f)
    .SetForegroundColor(Colors::Black)
    .SetBackgroundColor(Colors::White)
    .OnError([](const std::string& msg) { std::cerr << msg << "\n"; })
    .Build();
```

## API

### Data & symbology

```cpp
void SetSymbology(BarcodeSymbology s);
void SetData(const std::string& d);
void Set(BarcodeSymbology s, const std::string& d);   // both at once
void SetEncoderOptions(const BarcodeEncoderOptions& opt);
void Reencode();                                       // normally automatic
bool IsValid() const;
const std::string& GetError() const;
const BarcodePattern& GetPattern() const;
```

### Style

```cpp
void SetStyle(const BarcodeStyle& s);

void SetModuleWidth(float pixels);          // X-dimension
void SetBarHeight(float pixels);
void SetQuietZoneModules(float modules);    // 10x module width is conventional
void SetForegroundColor(const Color& c);
void SetBackgroundColor(const Color& c);
void SetTextPosition(BarcodeTextPosition pos);  // HiddenText / TextBelow / TextAbove / TextBothSides
void SetTextFontSize(float size);
void SetTextFontFamily(const std::string& family);
void SetTextColor(const Color& c);
void SetRotation(BarcodeRotation r);        // Rotate0/90/180/270
void SetAutoFitToBounds(bool autoFit);
```

### Encoder options

```cpp
struct BarcodeEncoderOptions {
    bool          includeChecksum = true;   // Code 39, ITF, Std25, Codabar
    MSICheckDigit msiCheckDigit   = MSICheckDigit::Mod10;
    bool          emitHumanReadable = true;
    bool          codabarUppercaseStartStop = true;
};
```

### Callbacks

```cpp
std::function<void(const std::string& error)> onError;
std::function<void()>                         onEncoded;
std::function<void()>                         onClick;
```

### Style presets

```cpp
BarcodeStyle::DefaultStyle();    // 2px module, 60px bars, black/white
BarcodeStyle::CompactStyle();    // 1.5px, 36px — tight UI cards
BarcodeStyle::PrintStyle();      // 4px, 100px — 300dpi print master
BarcodeStyle::MonochromeStyle(); // Bold HRI font, black on white
```

## Symbology Notes

### Code 128 / GS1-128

The auto encoder picks the shortest subset mix:
- Pure digits (even length ≥ 6): all subset C
- ASCII text: subset B (or A for control chars)
- Digit runs of 6+ inside text: switch to C and back

To insert FNC1 (group separator) mid-string in GS1-128, embed the literal sequence `~F`:

```cpp
bc->Set(BarcodeSymbology::GS1_128, "(01)04012345123456~F(15)241231~F(10)A1234");
```

### EAN-13 / UPC-A / ISBN

The check digit is auto-computed when you supply 12 digits (EAN-13/ISBN) or 11 digits (UPC-A). If you supply the full count, the encoder validates the supplied check digit and reports an error via `onError` if it mismatches.

ISBN-13 additionally shows the formatted prefix (`ISBN 978-X-XXXX-XXXX-X`) above the bars when `textPosition` is `TextAbove` or `TextBothSides`.

### UPC-E

Accepts:
- 6 digits — assumes number system 0
- 7 digits — number system + 5 data digits (check auto)
- 8 digits — full, check digit validated

The encoder expands UPC-E to the equivalent UPC-A to compute the check digit and pick the parity pattern.

### ITF-14

Always uses 14 digits with bearer bars (the horizontal frame top + bottom). 13 digits triggers automatic check-digit computation; 14 digits validates the supplied check.

### Pharmacode

Pure-numeric input in the range 3..131070. There is no human-readable text by default (set `emitHumanReadable = false` to disable).

## Layout & Rotation

Set `autoFitToBounds = true` (default) and the widget scales `moduleWidth`, `barHeight`, quiet zones and HRI proportionally to fit the element bounds. With `autoFitToBounds = false` the widget honors the explicit `moduleWidth` / `barHeight` and may overflow.

`SetRotation(BarcodeRotation::Rotate90)` rotates the entire symbol 90° clockwise around the element center. The rotation is applied to the render transform, so HRI text rotates along with the bars.

## Error Handling

If the encoder rejects the input (wrong length, illegal character for the symbology, bad check digit, etc.), the widget:

1. Stores the message in `pattern.errorMessage`
2. Calls `onError(message)` if registered
3. Renders an error placeholder (diagonal cross + message in the configured error color) instead of bars

```cpp
bc->onError = [](const std::string& msg) {
    std::cerr << "Barcode failed: " << msg << "\n";
};
```

## Demo

See `Apps/DemoApp/UltraCanvasBarcodeExamples.cpp`. Tools → Bar code in the demo app surfaces:

- A live editor (symbology dropdown + text input + HRI/rotation toggles)
- A gallery card for every supported symbology with a canonical sample

## Implementation Files

```
UltraCanvas/include/Plugins/Barcode/
    UltraCanvasBarcodeElement.h        # Widget API
    UltraCanvasBarcodeEncoders.h       # Encoder interface + symbology enum

UltraCanvas/Plugins/Barcode/
    UltraCanvasBarcodeElement.cpp      # Widget impl
    UltraCanvasBarcodeEncoders.cpp     # Encoder factory dispatch
    EncoderUtils.h                     # Internal helpers
    Encoders/
        Code39Encoder.cpp
        Code93Encoder.cpp
        Code128Encoder.cpp
        EANUPCEncoder.cpp
        ITFEncoder.cpp
        Standard25Encoder.cpp
        CodabarEncoder.cpp
        MSIEncoder.cpp
        PharmacodeEncoder.cpp
```

## Roadmap

Planned for v2:
- **QR Code** — full Reed-Solomon, 40 versions, 4 EC levels
- **Data Matrix** — GS1 DataMatrix variant included
- **PDF417** — stacked linear, used in ID cards & boarding passes
- **Aztec Code** — transit tickets
- **Postal codes** — Royal Mail 4-state, USPS IMb
