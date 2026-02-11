// libspecific/Direct2D/RenderContextDirect2D.h
// Direct2D render context stub for Windows platform
// TODO: Implement full Direct2D rendering backend
#pragma once

#ifndef RENDERCONTEXT_DIRECT2D_H
#define RENDERCONTEXT_DIRECT2D_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

// Undefine Windows macros that conflict with our method names
#ifdef DrawText
#undef DrawText
#endif
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef CreateDialog
#undef CreateDialog
#endif
#ifdef RGB
#undef RGB
#endif

#include "../../include/UltraCanvasRenderContext.h"

namespace UltraCanvas {

class RenderContextDirect2D : public IRenderContext {
private:
    ID2D1RenderTarget* renderTarget = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    TextStyle textStyle;

public:
    RenderContextDirect2D() = default;
    RenderContextDirect2D(HWND, int, int, ID2D1Factory*, IDWriteFactory*, IWICImagingFactory*) {}
    ~RenderContextDirect2D() override = default;

    void SetRenderTarget(ID2D1RenderTarget* rt) { renderTarget = rt; }
    void SetDWriteFactory(IDWriteFactory* factory) { dwriteFactory = factory; }
    ID2D1RenderTarget* GetRenderTarget() const { return renderTarget; }

    // ===== LIFECYCLE (used by UltraCanvasWindowsWindow) =====
    void Resize(int, int) {}
    void BeginDraw() {}
    void EndDraw() {}
    void SwapBuffers() {}

    // ===== STATE MANAGEMENT =====
    void PushState() override {}
    void PopState() override {}
    void ResetState() override {}

    // ===== TRANSFORMATION =====
    void Translate(float, float) override {}
    void Rotate(float) override {}
    void Scale(float, float) override {}
    void SetTransform(float, float, float, float, float, float) override {}
    void Transform(float, float, float, float, float, float) override {}
    void ResetTransform() override {}

    // ===== CLIPPING =====
    void ClearClipRect() override {}
    void ClipRect(float, float, float, float) override {}
    void ClipPath() override {}
    void ClipRoundedRectangle(float, float, float, float, float, float, float, float) override {}

    // ===== BASIC SHAPES =====
    void DrawLine(float, float, float, float) override {}
    void DrawRectangle(float, float, float, float) override {}
    void FillRectangle(float, float, float, float) override {}
    void DrawRoundedRectangle(float, float, float, float, float) override {}
    void FillRoundedRectangle(float, float, float, float, float) override {}
    void DrawRoundedRectangleWidthBorders(float, float, float, float,
                                          bool,
                                          float, float, float, float,
                                          const Color&, const Color&,
                                          const Color&, const Color&,
                                          float, float, float, float,
                                          const UCDashPattern&, const UCDashPattern&,
                                          const UCDashPattern&, const UCDashPattern&) override {}
    void DrawCircle(float, float, float) override {}
    void FillCircle(float, float, float) override {}
    void DrawEllipse(float, float, float, float) override {}
    void FillEllipse(float, float, float, float) override {}
    void DrawArc(float, float, float, float, float) override {}
    void FillArc(float, float, float, float, float) override {}
    void DrawBezierCurve(const Point2Df&, const Point2Df&, const Point2Df&, const Point2Df&) override {}
    void DrawLinePath(const std::vector<Point2Df>&, bool) override {}
    void FillLinePath(const std::vector<Point2Df>&) override {}

    // ===== PATH =====
    void ClearPath() override {}
    void ClosePath() override {}
    void MoveTo(float, float) override {}
    void RelMoveTo(float, float) override {}
    void LineTo(float, float) override {}
    void RelLineTo(float, float) override {}
    void QuadraticCurveTo(float, float, float, float) override {}
    void BezierCurveTo(float, float, float, float, float, float) override {}
    void RelBezierCurveTo(float, float, float, float, float, float) override {}
    void Arc(float, float, float, float, float) override {}
    void ArcTo(float, float, float, float, float) override {}
    void Circle(float, float, float) override {}
    void Ellipse(float, float, float, float, float) override {}
    void Rect(float, float, float, float) override {}
    void RoundedRect(float, float, float, float, float) override {}
    void FillPathPreserve() override {}
    void StrokePathPreserve() override {}
    void GetPathExtents(float&, float&, float&, float&) override {}

    // ===== GRADIENT =====
    std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(float, float, float, float,
                                                               const std::vector<GradientStop>&) override { return nullptr; }
    std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(float, float, float,
                                                               float, float, float,
                                                               const std::vector<GradientStop>&) override { return nullptr; }
    void SetFillPaint(std::shared_ptr<IPaintPattern>) override {}
    void SetFillPaint(const Color&) override {}
    void SetStrokePaint(std::shared_ptr<IPaintPattern>) override {}
    void SetStrokePaint(const Color&) override {}
    void SetTextPaint(std::shared_ptr<IPaintPattern>) override {}
    void SetTextPaint(const Color&) override {}
    void Fill() override {}
    void Stroke() override {}

    // ===== STYLE =====
    void SetAlpha(float) override {}
    float GetAlpha() const override { return 1.0f; }
    void SetStrokeWidth(float) override {}
    void SetLineCap(LineCap) override {}
    void SetLineJoin(LineJoin) override {}
    void SetMiterLimit(float) override {}
    void SetLineDash(const UCDashPattern&) override {}

    // ===== TEXT =====
    void SetFontFace(const std::string&, FontWeight, FontSlant) override {}
    void SetFontFamily(const std::string&) override {}
    void SetFontSize(float) override {}
    void SetFontWeight(FontWeight) override {}
    void SetFontSlant(FontSlant) override {}
    void SetTextLineHeight(float) override {}
    void SetTextWrap(TextWrap) override {}
    const TextStyle& GetTextStyle() const override { return textStyle; }
    void SetTextStyle(const TextStyle&) override {}
    void SetTextAlignment(TextAlignment) override {}
    void SetTextVerticalAlignment(TextVerticalAlignement) override {}
    void SetTextIsMarkup(bool) override {}
    void FillText(const std::string&, float, float) override {}
    void StrokeText(const std::string&, float, float) override {}
    void DrawText(const std::string&, float, float) override {}
    void DrawTextInRect(const std::string&, float, float, float, float) override {}
    bool GetTextLineDimensions(const std::string&, int&, int&) override { return false; }
    bool GetTextDimensions(const std::string&, int, int, int&, int&) override { return false; }
    int GetTextIndexForXY(const std::string&, int, int, int, int) override { return 0; }

    // ===== IMAGE =====
    void DrawPartOfPixmap(UCPixmap&, const Rect2Df&, const Rect2Df&) override {}
    void DrawPixmap(UCPixmap&, float, float, float, float, ImageFitMode) override {}

    // ===== PIXEL =====
    void Clear(const Color&) override {}

    // ===== UTILITY =====
    void* GetNativeContext() override { return renderTarget; }
};

} // namespace UltraCanvas

#endif // RENDERCONTEXT_DIRECT2D_H
