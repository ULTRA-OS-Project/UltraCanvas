// OS/WASM/UltraCanvasWASMRenderContext.h
// WebAssembly Canvas 2D rendering implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework
#pragma once

#include "../../include/UltraCanvasRenderContext.h"
#include <emscripten.h>
#include <string>
#include <stack>

namespace UltraCanvas {

class UltraCanvasWASMRenderContext : public IRenderContext {
private:
    // Canvas identification
    std::string canvasId;
    
    // Viewport
    int viewportWidth;
    int viewportHeight;
    
    // State stack for push/pop
    int stateDepth;
    
    // Current drawing state
    DrawingStyle currentDrawingStyle;
    TextStyle currentTextStyle;
    float globalAlpha;
    
    // Clipping
    bool hasClipRect;
    Rect2D clipRect;
    
    // Frame state
    bool inFrame;
    
    // JavaScript context access helpers
    void ExecuteJS(const std::string& code);
    
    // Style application
    void ApplyDrawingStyle();
    void ApplyTextStyle();
    void ApplyFillColor(const Color& color);
    void ApplyStrokeColor(const Color& color);
    
public:
    UltraCanvasWASMRenderContext(const std::string& canvasId);
    virtual ~UltraCanvasWASMRenderContext();
    
    // ===== INITIALIZATION =====
    bool Initialize();
    void SetViewportSize(int width, int height);
    
    // ===== FRAME CONTROL =====
    void BeginFrame();
    void EndFrame();
    void Clear(const Color& color);
    void Flush() override;
    
    // ===== STATE MANAGEMENT =====
    void PushState() override;
    void PopState() override;
    void ResetState() override;
    
    // ===== TRANSFORMATION =====
    void Translate(float x, float y) override;
    void Rotate(float angle) override;
    void Scale(float sx, float sy) override;
    void SetTransform(float a, float b, float c, float d, float e, float f) override;
    void ResetTransform() override;
    
    // ===== CLIPPING =====
    void SetClipRect(float x, float y, float w, float h) override;
    void ClearClipRect() override;
    void ClipRect(float x, float y, float w, float h) override;
    
    // ===== STYLE MANAGEMENT =====
    void SetDrawingStyle(const DrawingStyle& style) override;
    void SetTextStyle(const TextStyle& style) override;
    void SetAlpha(float alpha) override;
    float GetAlpha() const override;
    const DrawingStyle& GetDrawingStyle() const override;
    const TextStyle& GetTextStyle() const override;
    
    // ===== BASIC SHAPES =====
    void DrawLine(float x1, float y1, float x2, float y2) override;
    void DrawRectangle(float x, float y, float width, float height) override;
    void DrawFilledRectangle(float x, float y, float width, float height) override;
    void DrawFilledRectangle(const Rect2D& rect, const Color& fillColor, 
                           float borderWidth, const Color& borderColor) override;
    void DrawRoundedRectangle(float x, float y, float width, float height, float radius) override;
    void DrawFilledRoundedRectangle(float x, float y, float width, float height, float radius) override;
    void DrawCircle(float cx, float cy, float radius) override;
    void DrawFilledCircle(float cx, float cy, float radius) override;
    void DrawEllipse(float cx, float cy, float rx, float ry) override;
    void DrawFilledEllipse(float cx, float cy, float rx, float ry) override;
    void DrawArc(float cx, float cy, float radius, float startAngle, float endAngle) override;
    void DrawFilledArc(float cx, float cy, float radius, float startAngle, float endAngle) override;
    
    // ===== PATHS =====
    void BeginPath() override;
    void ClosePath() override;
    void MoveTo(float x, float y) override;
    void LineTo(float x, float y) override;
    void BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override;
    void QuadraticCurveTo(float cpx, float cpy, float x, float y) override;
    void ArcTo(float x1, float y1, float x2, float y2, float radius) override;
    void StrokePath() override;
    void FillPath() override;
    
    // ===== TEXT RENDERING =====
    void DrawText(const std::string& text, float x, float y) override;
    void DrawText(const std::string& text, const Point2D& position) override;
    Point2D MeasureText(const std::string& text) override;
    float GetTextWidth(const std::string& text) override;
    float GetTextHeight(const std::string& text) override;
    
    // ===== IMAGE RENDERING =====
    void DrawImage(const ImageData& image, float x, float y) override;
    void DrawImage(const ImageData& image, float x, float y, float width, float height) override;
    void DrawImage(const ImageData& image, const Rect2D& sourceRect, const Rect2D& destRect) override;
    
    // ===== GRADIENTS =====
    void SetLinearGradient(float x0, float y0, float x1, float y1, 
                          const Color& color1, const Color& color2) override;
    void SetRadialGradient(float x0, float y0, float r0, float x1, float y1, float r1,
                          const Color& color1, const Color& color2) override;
    
    // ===== EFFECTS =====
    void SetShadow(float offsetX, float offsetY, float blur, const Color& color) override;
    void ClearShadow() override;
    
    // ===== CANVAS ACCESS =====
    const std::string& GetCanvasId() const { return canvasId; }
};

} // namespace UltraCanvas
