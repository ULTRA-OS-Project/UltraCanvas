# UltraCanvasEPS Documentation

## Overview

The `UltraCanvasEPSElement` is a UI element that loads and renders **Encapsulated PostScript** (`.eps`, `.epsf`, `.ps`) drawings inside an UltraCanvas window. It is part of the `UltraCanvasEPSPlugin`.

EPS files are PostScript *programs*: real-world files (Illustrator, CorelDRAW, ghostscript, cairo, …) define their own procedures in a prolog and draw through them, so a fixed operator table cannot render them. The plugin therefore embeds a **PostScript-subset interpreter** — scanner, operand / dictionary / execution stacks, procedures, control flow, and the graphics, path, text and image operators — and plays the program back through the standard `IRenderContext`. There is no external dependency beyond zlib (for `FlateDecode` image data).

EPS support is **partially implemented**: the interpreter covers the level-1/2 core that drawing programs actually emit; see *Known gaps* below for the approximations. Renderings of the shipped samples agree with ghostscript to within antialiasing differences.

**Version:** 1.0.0
**Header:** `Plugins/Vector/EPS/UltraCanvasEPSPlugin.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement` (the plugin class derives from `IGraphicsPlugin`)

## Features

- PostScript scanner: numbers (radix forms included), `(strings)` with escapes, `<hex>` strings, literal and executable names, procedures, `<< >>` dictionaries, DSC comments
- Execution model: operand / dictionary stacks, `def`/`bind`/`load`/`store`/`where`, `if`/`ifelse`/`for`/`repeat`/`loop`/`forall`/`exit`/`stopped`, arrays, strings and dictionary operators, PostScript single-precision `for` accumulation
- Paths: `moveto`/`lineto`/`curveto` (absolute and relative), `arc`/`arcn`, `closepath`, `rectfill`/`rectstroke`/`rectclip`, `pathbbox`, `currentpoint`
- Painting: `fill`, `eofill`, `stroke`, `clip`, `eoclip` — the even-odd rule renders through the new `IRenderContext::SetFillRule`
- Full transform algebra: `translate`/`scale`/`rotate`/`concat`, matrix objects with `transform`/`itransform`/`dtransform`/`idtransform`/`invertmatrix`/`concatmatrix`
- Color: gray, RGB, HSB, CMYK, `setcolorspace`/`setcolor` for the device spaces
- Line attributes: width, caps, joins, miter limit, dash patterns (all CTM-scaled)
- Sampled images: level-1 operand and level-2 dictionary forms, `image`/`imagemask`/`colorimage`, hex, `ASCII85Decode` and `FlateDecode` data sources; very large fallback rasters are sampled down
- Text: `findfont`/`scalefont`/`makefont`/`setfont`/`selectfont`, `show` family (`ashow`, `widthshow`, `awidthshow`, `kshow`, `xshow`/`yshow`/`xyshow`), `stringwidth`; standard PS font names map to system families (Helvetica → Liberation Sans, Times → serif, Courier → monospace)
- DOS EPS binary preview headers (`C5 D0 D3 C6`) are unwrapped automatically
- `%%BoundingBox` (or `%%HiResBoundingBox`) sets the page; `%%Title` / `%%Creator` are exposed
- Registers with `UltraCanvasGraphicsPluginRegistry` for extension-based file dispatch

## Known gaps (diagnosed, approximated)

- Embedded Type 1 fonts (`eexec`) are skipped; text renders in the mapped system font
- `charpath` advances the caret but produces no outlines
- Shading patterns (`shfill`, `makepattern`/`setpattern`) are not painted
- `save`/`restore` only restore graphics state, not VM state
- `initclip` cannot widen an active clip; `strokepath` leaves the path unchanged
- Rotated/sheared *text* renders unrotated at the transformed size (paths transform fully)

`EPSDocument::GetDiagnostics()` reports every operator the interpreter did not recognize (with use counts) and each approximation taken — the first thing to check when a file renders wrong. `Tests/EPSProbeTest` prints that triage from the command line and, with `--render <dir>`, rasterizes files to PNG for comparison against a ghostscript rendering.

## Header Include

```cpp
#include "Plugins/Vector/EPS/UltraCanvasEPSPlugin.h"
```

## Class Reference

### EPSDocument

```cpp
class EPSDocument {
public:
    bool LoadFromFile(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* data, size_t size);

    float GetWidth() const;    // BoundingBox size in points
    float GetHeight() const;

    // Interpret the program against the context; the BoundingBox's
    // lower-left corner maps to the page's bottom-left.
    void Render(IRenderContext* ctx, float scale = 1.0f);

    const EPSParseDiagnostics& GetDiagnostics() const;
    const std::string& GetTitle() const;
    const std::string& GetCreator() const;
};
```

`EPSParseDiagnostics` carries `tokenCount`, `unknownOperators` (name → use count) and `warnings`.

### UltraCanvasEPSElement

```cpp
class UltraCanvasEPSElement : public UltraCanvasUIElement {
public:
    UltraCanvasEPSElement(const std::string& identifier,
                          float x, float y, float w, float h);

    bool LoadFromFile(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* data, size_t size);
    bool IsLoaded() const;
    const std::string& GetLastError() const;   // reason of the last failed load

    void SetScale(float s);
    float GetScale() const;
    void SetPreserveAspectRatio(bool preserve);
    bool GetPreserveAspectRatio() const;

    const EPSDocument* GetDocument() const;
};
```

The element letterboxes the BoundingBox into its bounds (aspect preserved by default), paints a white page behind the drawing, and re-interprets the program on each render.

### UltraCanvasEPSPlugin

Implements `IGraphicsPlugin` for the extensions `eps`, `epsf`, `ps`; `ValidateFile` accepts `%!` signatures and DOS EPS binary headers. Register it at application startup:

```cpp
#ifdef ULTRACANVAS_HAS_EPS_PLUGIN
    RegisterEPSPlugin();
#endif
```

With registration in place the File Loader lists the extensions and `LoadGraphicsFile()` dispatches EPS files to this plugin.

## Usage Example

```cpp
auto eps = std::make_shared<UltraCanvasEPSElement>("Logo", 20, 20, 300, 280);
if (!eps->LoadFromFile("media/eps/demo.eps")) {
    std::cerr << eps->GetLastError() << std::endl;
}
window->AddChild(eps);
```

The demo application's **EPS Images** page (`Apps/DemoApp/UltraCanvasEPSExamples.cpp`) shows the shipped samples from `media/eps/` with a fullscreen viewer and zoom controls.
