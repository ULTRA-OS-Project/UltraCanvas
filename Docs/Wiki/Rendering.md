# Rendering

Everything on screen is drawn through one interface, **`IRenderContext`**
(`UltraCanvas/include/UltraCanvasRenderContext.h`). The same calls render to
a window, an off-screen surface, a bitmap, a PDF page or a printer, on every
platform - the backend is Cairo with Pango text layout. Application code
mostly never touches it: elements draw themselves. You use it when you write
your **own element** or draw into a canvas.

In depth: [`UltraCanvasRenderContext.md`](../UltraCanvas/UltraCanvasRenderContext.md)
(blend modes, groups and masks, hit testing, paint sources, antialiasing, text
wrapping and outlines, surfaces) and
[`UltraCanvasTextRenderingExamples.md`](../UltraCanvas/UltraCanvasTextRenderingExamples.md).

## What the context can do

| Area | Calls |
|---|---|
| State | `PushState`, `PopState`, `ResetState` |
| Transforms | `Translate`, `Rotate`, `Scale`, `SetTransform`, `Transform`, `ResetTransform`, `GetTransform`, `UserToDevice`, `DeviceToUser` |
| Clipping | `ClipRect`, `ClipRoundedRectangle`, `ClipPath`, `ClearClipRect`, `GetClipRect` |
| Shapes | `DrawLine`, `DrawRectangle` / `FillRectangle`, `DrawRoundedRectangle` / `FillRoundedRectangle`, `DrawCircle` / `FillCircle`, `DrawEllipse` / `FillEllipse`, `DrawArc` / `FillArc`, `DrawBezierCurve`, `DrawLinePath` / `FillLinePath` |
| Paths | `MoveTo`, `LineTo`, `QuadraticCurveTo`, `BezierCurveTo`, `Arc`, `ArcTo`, `Circle`, `Ellipse`, `Rect`, `RoundedRect`, `ClosePath`, `ClearPath`, `Fill`, `Stroke`, `FillPathPreserve`, `StrokePathPreserve`, `SetFillRule`, `GetPathExtents` |
| Hit testing | `IsPointInFill`, `IsPointInStroke`, `GetStrokeExtents` |
| Paint | `SetFillPaint`, `SetStrokePaint`, `SetTextPaint` (a colour or a pattern), `SetAlpha` |
| Patterns | `CreateLinearGradientPattern`, `CreateRadialGradientPattern`, `CreateEllipticalGradientPattern`, `CreateConicGradientPattern`, `CreateMeshGradientPattern`, `CreateImagePattern`, `CreatePixmapPattern` |
| Strokes | `SetStrokeWidth`, `SetLineCap`, `SetLineJoin`, `SetMiterLimit`, `SetLineDash` |
| Compositing | `SetBlendMode`, `BeginGroup` / `EndGroup(opacity)`, `EndGroupAsPattern`, `EndGroupMasked`, `PaintPattern`, `SetAntialias` |
| Text | `SetFontFace`, `SetFontFamily`, `SetFontSize`, `SetFontWeight`, `SetFontSlant`, `SetTextAlignment`, `SetTextVerticalAlignment`, `SetTextWrap`, `SetTextLineHeight`, `SetTextIsMarkup`, `DrawText`, `DrawTextInRect`, `FillText`, `StrokeText`, `GetTextDimensions`, `GetTextIndexForXY`, `AppendTextPath` |
| Text layouts | `MakeTextLayout`, `GetOrCreateTextLayout`, `DrawTextLayout` (measure once, draw many times; caret and selection geometry) |
| Images | `DrawPixmap(pixmap, rect, fitMode)`, `DrawPartOfPixmap`, `DrawMask`, `SetImageSmoothing` |
| Surfaces | `CreateSurface`, `ResizeSurface`, `FlushToSurface`, `CompositeToSurface`, `GetDeviceScale` |

Coordinates are `double` (`Point2Dd`, `Rect2Dd`); colours are `Color(r, g, b, a)`
with named ones in `Colors::` (`Colors::White`, `Colors::TextDefault`,
`Colors::Transparent` ...). Prefer theme roles to fixed colours - see
[Styling and themes](Styling-And-Themes.md).

## A custom element

A new element derives from `UltraCanvasUIElement`, draws in **its own local
coordinates** (`GetLocalBounds()` starts at 0, 0), reports the size it wants
to the layout engine, and handles its events:

```cpp
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTheme.h"

using namespace UltraCanvas;

class TrafficLight : public UltraCanvasUIElement {
public:
    std::function<void(int)> onStateChanged;      // a callback, like every element

    explicit TrafficLight(const std::string& identifier) : UltraCanvasUIElement(identifier) {}

    void SetState(int s) { state = s % 3; RequestRedraw(); if (onStateChanged) onStateChanged(state); }

    void Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) override {
        Rect2Df b = GetLocalBounds();
        const ThemeRoles& roles = UltraCanvasTheme::Current().roles;
        ctx->SetFillPaint(roles.panel);
        ctx->FillRoundedRectangle(Rect2Dd(b.x, b.y, b.width, b.height), 8);
        const Color lamps[3] = {Color(220, 40, 40), Color(240, 190, 30), Color(40, 180, 70)};
        double r = b.width / 2 - 6;
        for (int i = 0; i < 3; ++i) {
            Color c = lamps[i];
            if (i != state) c.a = 60;                  // the others dimmed
            ctx->SetFillPaint(c);
            ctx->FillCircle(Point2Dd(b.width / 2, 6 + r + i * (2 * r + 6)), r);
        }
    }

    Size2Df MeasureOwnContent(std::optional<float>, const CSSLayout::LayoutContext&) override {
        return Size2Df(40, 112);                      // the size it wants; the layout may give more
    }

    bool OnEvent(const UCEvent& event) override {
        if (UltraCanvasUIElement::OnEvent(event)) return true;   // eventCallback first
        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            SetState(state + 1);
            return true;
        }
        return false;
    }

private:
    int state = 0;
};
```

Rules for a custom element:

- **Draw in local coordinates** and only inside your bounds; the framework
  translates and clips for you.
- **Report a content size** (`MeasureOwnContent`) or the element measures
  0 x 0 and is never drawn.
- **Call `RequestRedraw()`** when what you draw changes, `InvalidateLayout()`
  when the size you want changes.
- **Call the base `OnEvent` first** so `eventCallback` keeps working, and
  return `false` for what you do not handle so it bubbles.
- **Read colours from the theme** and implement `ApplyTheme` if you keep a
  style struct.
- **Add a callback** for every change a caller may want to react to, named
  `on...`.
- **Do not paint other elements' jobs.** A text field, a button or a picker
  inside your view is a child element placed with `PlaceChildAt`.
- **Accessibility:** override `GetAccessibleRole` / `GetAccessibleName` so a
  screen reader can describe it.

An element added to the framework also gets a doc in `Docs/UltraCanvas/` and
a row in the catalogue, and then appears in this wiki automatically.

## Images

`UCImage` (`UltraCanvasImage.h`) decodes once and caches; `UCPixmap` is the
decoded pixels. To *show* a picture use
[`UltraCanvasImageElement`](Elements/UltraCanvasImageElement.md) rather than
drawing it every frame. Supported: PNG, JPEG, BMP, TIFF, GIF, WebP, AVIF,
HEIC, JPEG XL, QOI, icons, SVG and animated formats; image processing is in
PixelFX.

## Other surfaces

- **OpenGL**: [`UltraCanvasGLSurface`](Elements/UltraCanvasGLSurface.md).
- **PDF / printing**: `UltraCanvasPdfSurface.h`, and the print paths of the
  rich text, spreadsheet and presentation models.
- **Bitmaps of vector drawings**: `UltraCanvasFileLoader::SaveVectorDocument`
  with a size (see [Files, text and localisation](Files-Text-And-Localisation.md)).

Next: [Files, text and localisation](Files-Text-And-Localisation.md)
