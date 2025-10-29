// OS/WASM/UltraCanvasWASMRenderContext.cpp
// WebAssembly Canvas 2D rendering implementation  
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "UltraCanvasWASMRenderContext.h"
#include <iostream>
#include <sstream>
#include <cmath>

namespace UltraCanvas {

// ===== CONSTRUCTOR / DESTRUCTOR =====

UltraCanvasWASMRenderContext::UltraCanvasWASMRenderContext(const std::string& canvasId)
    : canvasId(canvasId)
    , viewportWidth(800)
    , viewportHeight(600)
    , stateDepth(0)
    , globalAlpha(1.0f)
    , hasClipRect(false)
    , inFrame(false)
{
    std::cout << "[WASM RenderContext] Created for canvas: " << canvasId << std::endl;
}

UltraCanvasWASMRenderContext::~UltraCanvasWASMRenderContext() {
    std::cout << "[WASM RenderContext] Destroyed" << std::endl;
}

// ===== INITIALIZATION =====

bool UltraCanvasWASMRenderContext::Initialize() {
    std::cout << "[WASM RenderContext] Initializing Canvas 2D context..." << std::endl;
    
    // Get 2D rendering context from canvas
    bool success = EM_ASM_INT({
        const canvas = document.getElementById(UTF8ToString($0));
        if (!canvas) {
            console.error('Canvas not found:', UTF8ToString($0));
            return 0;
        }
        
        const ctx = canvas.getContext('2d');
        if (!ctx) {
            console.error('Could not get 2D context');
            return 0;
        }
        
        // Store context globally for easy access
        if (!Module.canvasContexts) {
            Module.canvasContexts = {};
        }
        Module.canvasContexts[UTF8ToString($0)] = ctx;
        
        console.log('Canvas 2D context initialized for:', UTF8ToString($0));
        return 1;
    }, canvasId.c_str());
    
    if (!success) {
        std::cerr << "[WASM RenderContext] ERROR: Failed to initialize Canvas 2D context" << std::endl;
        return false;
    }
    
    // Reset to default state
    ResetState();
    
    std::cout << "[WASM RenderContext] Initialization complete" << std::endl;
    return true;
}

void UltraCanvasWASMRenderContext::SetViewportSize(int width, int height) {
    viewportWidth = width;
    viewportHeight = height;
    
    std::cout << "[WASM RenderContext] Viewport size: " << width << "x" << height << std::endl;
}

// ===== FRAME CONTROL =====

void UltraCanvasWASMRenderContext::BeginFrame() {
    if (inFrame) {
        std::cerr << "[WASM RenderContext] WARNING: BeginFrame called while already in frame" << std::endl;
        return;
    }
    
    inFrame = true;
    
    // Save initial state
    PushState();
}

void UltraCanvasWASMRenderContext::EndFrame() {
    if (!inFrame) {
        std::cerr << "[WASM RenderContext] WARNING: EndFrame called without BeginFrame" << std::endl;
        return;
    }
    
    // Restore initial state
    PopState();
    
    inFrame = false;
}

void UltraCanvasWASMRenderContext::Clear(const Color& color) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fillStyle = `rgba(${$1}, ${$2}, ${$3}, ${$4})`;
            ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        }
    }, canvasId.c_str(), color.r, color.g, color.b, color.a / 255.0f);
}

void UltraCanvasWASMRenderContext::Flush() {
    // Canvas 2D auto-flushes, but we can trigger a composite
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx && ctx.canvas) {
            // Force browser to composite
            ctx.canvas.style.transform = ctx.canvas.style.transform;
        }
    }, canvasId.c_str());
}

// ===== STATE MANAGEMENT =====

void UltraCanvasWASMRenderContext::PushState() {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.save();
        }
    }, canvasId.c_str());
    
    stateDepth++;
}

void UltraCanvasWASMRenderContext::PopState() {
    if (stateDepth == 0) {
        std::cerr << "[WASM RenderContext] WARNING: PopState called without matching PushState" << std::endl;
        return;
    }
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.restore();
        }
    }, canvasId.c_str());
    
    stateDepth--;
}

void UltraCanvasWASMRenderContext::ResetState() {
    // Restore to base state
    while (stateDepth > 0) {
        PopState();
    }
    
    // Reset to defaults
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.resetTransform();
            ctx.globalAlpha = 1.0;
            ctx.lineWidth = 1.0;
            ctx.lineCap = 'butt';
            ctx.lineJoin = 'miter';
            ctx.fillStyle = 'black';
            ctx.strokeStyle = 'black';
            ctx.font = '12px sans-serif';
            ctx.textAlign = 'left';
            ctx.textBaseline = 'alphabetic';
            ctx.shadowBlur = 0;
            ctx.shadowOffsetX = 0;
            ctx.shadowOffsetY = 0;
        }
    }, canvasId.c_str());
    
    globalAlpha = 1.0f;
    hasClipRect = false;
}

// ===== TRANSFORMATION =====

void UltraCanvasWASMRenderContext::Translate(float x, float y) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.translate($1, $2);
        }
    }, canvasId.c_str(), x, y);
}

void UltraCanvasWASMRenderContext::Rotate(float angle) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.rotate($1);
        }
    }, canvasId.c_str(), angle);
}

void UltraCanvasWASMRenderContext::Scale(float sx, float sy) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.scale($1, $2);
        }
    }, canvasId.c_str(), sx, sy);
}

void UltraCanvasWASMRenderContext::SetTransform(float a, float b, float c, float d, float e, float f) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.setTransform($1, $2, $3, $4, $5, $6);
        }
    }, canvasId.c_str(), a, b, c, d, e, f);
}

void UltraCanvasWASMRenderContext::ResetTransform() {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.resetTransform();
        }
    }, canvasId.c_str());
}

// ===== CLIPPING =====

void UltraCanvasWASMRenderContext::SetClipRect(float x, float y, float w, float h) {
    hasClipRect = true;
    clipRect = Rect2D(x, y, w, h);
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.save();
            ctx.beginPath();
            ctx.rect($1, $2, $3, $4);
            ctx.clip();
        }
    }, canvasId.c_str(), x, y, w, h);
}

void UltraCanvasWASMRenderContext::ClearClipRect() {
    if (hasClipRect) {
        EM_ASM({
            const ctx = Module.canvasContexts[UTF8ToString($0)];
            if (ctx) {
                ctx.restore();
            }
        }, canvasId.c_str());
        
        hasClipRect = false;
    }
}

void UltraCanvasWASMRenderContext::ClipRect(float x, float y, float w, float h) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.rect($1, $2, $3, $4);
            ctx.clip();
        }
    }, canvasId.c_str(), x, y, w, h);
}

// ===== STYLE MANAGEMENT =====

void UltraCanvasWASMRenderContext::SetDrawingStyle(const DrawingStyle& style) {
    currentDrawingStyle = style;
    ApplyDrawingStyle();
}

void UltraCanvasWASMRenderContext::SetTextStyle(const TextStyle& style) {
    currentTextStyle = style;
    ApplyTextStyle();
}

void UltraCanvasWASMRenderContext::SetAlpha(float alpha) {
    globalAlpha = alpha;
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.globalAlpha = $1;
        }
    }, canvasId.c_str(), alpha);
}

float UltraCanvasWASMRenderContext::GetAlpha() const {
    return globalAlpha;
}

const DrawingStyle& UltraCanvasWASMRenderContext::GetDrawingStyle() const {
    return currentDrawingStyle;
}

const TextStyle& UltraCanvasWASMRenderContext::GetTextStyle() const {
    return currentTextStyle;
}

void UltraCanvasWASMRenderContext::ApplyDrawingStyle() {
    ApplyFillColor(currentDrawingStyle.fillColor);
    ApplyStrokeColor(currentDrawingStyle.strokeColor);
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.lineWidth = $1;
        }
    }, canvasId.c_str(), currentDrawingStyle.lineWidth);
}

void UltraCanvasWASMRenderContext::ApplyTextStyle() {
    // Build font string
    std::stringstream fontStr;
    
    // Font weight
    switch (currentTextStyle.fontWeight) {
        case FontWeight::Thin: fontStr << "100 "; break;
        case FontWeight::Light: fontStr << "300 "; break;
        case FontWeight::Normal: fontStr << "400 "; break;
        case FontWeight::Medium: fontStr << "500 "; break;
        case FontWeight::SemiBold: fontStr << "600 "; break;
        case FontWeight::Bold: fontStr << "700 "; break;
        case FontWeight::ExtraBold: fontStr << "800 "; break;
        case FontWeight::Black: fontStr << "900 "; break;
    }
    
    // Font style
    if (currentTextStyle.italic) {
        fontStr << "italic ";
    }
    
    // Font size and family
    fontStr << currentTextStyle.fontSize << "px ";
    fontStr << currentTextStyle.fontFamily;
    
    // Apply to canvas
    std::string font = fontStr.str();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.font = UTF8ToString($1);
            
            // Text alignment
            const alignments = ['left', 'center', 'right'];
            ctx.textAlign = alignments[$2] || 'left';
            
            ctx.textBaseline = 'top';
        }
    }, canvasId.c_str(), font.c_str(), static_cast<int>(currentTextStyle.alignment));
    
    ApplyFillColor(currentTextStyle.textColor);
}

void UltraCanvasWASMRenderContext::ApplyFillColor(const Color& color) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fillStyle = `rgba(${$1}, ${$2}, ${$3}, ${$4})`;
        }
    }, canvasId.c_str(), color.r, color.g, color.b, color.a / 255.0f);
}

void UltraCanvasWASMRenderContext::ApplyStrokeColor(const Color& color) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.strokeStyle = `rgba(${$1}, ${$2}, ${$3}, ${$4})`;
        }
    }, canvasId.c_str(), color.r, color.g, color.b, color.a / 255.0f);
}

// ===== BASIC SHAPES =====

void UltraCanvasWASMRenderContext::DrawLine(float x1, float y1, float x2, float y2) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.moveTo($1, $2);
            ctx.lineTo($3, $4);
            ctx.stroke();
        }
    }, canvasId.c_str(), x1, y1, x2, y2);
}

void UltraCanvasWASMRenderContext::DrawRectangle(float x, float y, float width, float height) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.strokeRect($1, $2, $3, $4);
        }
    }, canvasId.c_str(), x, y, width, height);
}

void UltraCanvasWASMRenderContext::DrawFilledRectangle(float x, float y, float width, float height) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fillRect($1, $2, $3, $4);
        }
    }, canvasId.c_str(), x, y, width, height);
}

void UltraCanvasWASMRenderContext::DrawFilledRectangle(const Rect2D& rect, const Color& fillColor, 
                                                      float borderWidth, const Color& borderColor) {
    // Fill
    ApplyFillColor(fillColor);
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fillRect($1, $2, $3, $4);
        }
    }, canvasId.c_str(), rect.x, rect.y, rect.width, rect.height);
    
    // Border
    if (borderWidth > 0) {
        ApplyStrokeColor(borderColor);
        EM_ASM({
            const ctx = Module.canvasContexts[UTF8ToString($0)];
            if (ctx) {
                ctx.lineWidth = $5;
                ctx.strokeRect($1, $2, $3, $4);
            }
        }, canvasId.c_str(), rect.x, rect.y, rect.width, rect.height, borderWidth);
    }
}

void UltraCanvasWASMRenderContext::DrawRoundedRectangle(float x, float y, float width, float height, float radius) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const r = Math.min($5, $3 / 2, $4 / 2);
            ctx.beginPath();
            ctx.moveTo($1 + r, $2);
            ctx.lineTo($1 + $3 - r, $2);
            ctx.arcTo($1 + $3, $2, $1 + $3, $2 + r, r);
            ctx.lineTo($1 + $3, $2 + $4 - r);
            ctx.arcTo($1 + $3, $2 + $4, $1 + $3 - r, $2 + $4, r);
            ctx.lineTo($1 + r, $2 + $4);
            ctx.arcTo($1, $2 + $4, $1, $2 + $4 - r, r);
            ctx.lineTo($1, $2 + r);
            ctx.arcTo($1, $2, $1 + r, $2, r);
            ctx.closePath();
            ctx.stroke();
        }
    }, canvasId.c_str(), x, y, width, height, radius);
}

void UltraCanvasWASMRenderContext::DrawFilledRoundedRectangle(float x, float y, float width, float height, float radius) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const r = Math.min($5, $3 / 2, $4 / 2);
            ctx.beginPath();
            ctx.moveTo($1 + r, $2);
            ctx.lineTo($1 + $3 - r, $2);
            ctx.arcTo($1 + $3, $2, $1 + $3, $2 + r, r);
            ctx.lineTo($1 + $3, $2 + $4 - r);
            ctx.arcTo($1 + $3, $2 + $4, $1 + $3 - r, $2 + $4, r);
            ctx.lineTo($1 + r, $2 + $4);
            ctx.arcTo($1, $2 + $4, $1, $2 + $4 - r, r);
            ctx.lineTo($1, $2 + r);
            ctx.arcTo($1, $2, $1 + r, $2, r);
            ctx.closePath();
            ctx.fill();
        }
    }, canvasId.c_str(), x, y, width, height, radius);
}

void UltraCanvasWASMRenderContext::DrawCircle(float cx, float cy, float radius) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.arc($1, $2, $3, 0, Math.PI * 2);
            ctx.stroke();
        }
    }, canvasId.c_str(), cx, cy, radius);
}

void UltraCanvasWASMRenderContext::DrawFilledCircle(float cx, float cy, float radius) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.arc($1, $2, $3, 0, Math.PI * 2);
            ctx.fill();
        }
    }, canvasId.c_str(), cx, cy, radius);
}

void UltraCanvasWASMRenderContext::DrawEllipse(float cx, float cy, float rx, float ry) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.ellipse($1, $2, $3, $4, 0, 0, Math.PI * 2);
            ctx.stroke();
        }
    }, canvasId.c_str(), cx, cy, rx, ry);
}

void UltraCanvasWASMRenderContext::DrawFilledEllipse(float cx, float cy, float rx, float ry) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.ellipse($1, $2, $3, $4, 0, 0, Math.PI * 2);
            ctx.fill();
        }
    }, canvasId.c_str(), cx, cy, rx, ry);
}

void UltraCanvasWASMRenderContext::DrawArc(float cx, float cy, float radius, float startAngle, float endAngle) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.arc($1, $2, $3, $4, $5);
            ctx.stroke();
        }
    }, canvasId.c_str(), cx, cy, radius, startAngle, endAngle);
}

void UltraCanvasWASMRenderContext::DrawFilledArc(float cx, float cy, float radius, float startAngle, float endAngle) {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
            ctx.moveTo($1, $2);
            ctx.arc($1, $2, $3, $4, $5);
            ctx.closePath();
            ctx.fill();
        }
    }, canvasId.c_str(), cx, cy, radius, startAngle, endAngle);
}

// ===== PATHS =====

void UltraCanvasWASMRenderContext::BeginPath() {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.beginPath();
        }
    }, canvasId.c_str());
}

void UltraCanvasWASMRenderContext::ClosePath() {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.closePath();
        }
    }, canvasId.c_str());
}

void UltraCanvasWASMRenderContext::MoveTo(float x, float y) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.moveTo($1, $2);
        }
    }, canvasId.c_str(), x, y);
}

void UltraCanvasWASMRenderContext::LineTo(float x, float y) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.lineTo($1, $2);
        }
    }, canvasId.c_str(), x, y);
}

void UltraCanvasWASMRenderContext::BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.bezierCurveTo($1, $2, $3, $4, $5, $6);
        }
    }, canvasId.c_str(), cp1x, cp1y, cp2x, cp2y, x, y);
}

void UltraCanvasWASMRenderContext::QuadraticCurveTo(float cpx, float cpy, float x, float y) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.quadraticCurveTo($1, $2, $3, $4);
        }
    }, canvasId.c_str(), cpx, cpy, x, y);
}

void UltraCanvasWASMRenderContext::ArcTo(float x1, float y1, float x2, float y2, float radius) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.arcTo($1, $2, $3, $4, $5);
        }
    }, canvasId.c_str(), x1, y1, x2, y2, radius);
}

void UltraCanvasWASMRenderContext::StrokePath() {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.stroke();
        }
    }, canvasId.c_str());
}

void UltraCanvasWASMRenderContext::FillPath() {
    ApplyDrawingStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fill();
        }
    }, canvasId.c_str());
}

// ===== TEXT RENDERING =====

void UltraCanvasWASMRenderContext::DrawText(const std::string& text, float x, float y) {
    ApplyTextStyle();
    
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.fillText(UTF8ToString($1), $2, $3);
        }
    }, canvasId.c_str(), text.c_str(), x, y);
}

void UltraCanvasWASMRenderContext::DrawText(const std::string& text, const Point2D& position) {
    DrawText(text, position.x, position.y);
}

Point2D UltraCanvasWASMRenderContext::MeasureText(const std::string& text) {
    ApplyTextStyle();
    
    float width = EM_ASM_DOUBLE({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const metrics = ctx.measureText(UTF8ToString($1));
            return metrics.width;
        }
        return 0;
    }, canvasId.c_str(), text.c_str());
    
    // Height is approximately fontSize
    float height = currentTextStyle.fontSize;
    
    return Point2D(width, height);
}

float UltraCanvasWASMRenderContext::GetTextWidth(const std::string& text) {
    ApplyTextStyle();
    
    return EM_ASM_DOUBLE({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const metrics = ctx.measureText(UTF8ToString($1));
            return metrics.width;
        }
        return 0;
    }, canvasId.c_str(), text.c_str());
}

float UltraCanvasWASMRenderContext::GetTextHeight(const std::string& text) {
    return currentTextStyle.fontSize;
}

// ===== IMAGE RENDERING =====

void UltraCanvasWASMRenderContext::DrawImage(const ImageData& image, float x, float y) {
    // TODO: Implement image drawing
    // This requires creating an Image object in JavaScript and drawing it
    std::cerr << "[WASM RenderContext] DrawImage not yet implemented" << std::endl;
}

void UltraCanvasWASMRenderContext::DrawImage(const ImageData& image, float x, float y, float width, float height) {
    // TODO: Implement image drawing with scaling
    std::cerr << "[WASM RenderContext] DrawImage (scaled) not yet implemented" << std::endl;
}

void UltraCanvasWASMRenderContext::DrawImage(const ImageData& image, const Rect2D& sourceRect, const Rect2D& destRect) {
    // TODO: Implement image drawing with source and destination rects
    std::cerr << "[WASM RenderContext] DrawImage (cropped) not yet implemented" << std::endl;
}

// ===== GRADIENTS =====

void UltraCanvasWASMRenderContext::SetLinearGradient(float x0, float y0, float x1, float y1,
                                                     const Color& color1, const Color& color2) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const gradient = ctx.createLinearGradient($1, $2, $3, $4);
            gradient.addColorStop(0, `rgba(${$5}, ${$6}, ${$7}, ${$8})`);
            gradient.addColorStop(1, `rgba(${$9}, ${$10}, ${$11}, ${$12})`);
            ctx.fillStyle = gradient;
        }
    }, canvasId.c_str(), x0, y0, x1, y1,
       color1.r, color1.g, color1.b, color1.a / 255.0f,
       color2.r, color2.g, color2.b, color2.a / 255.0f);
}

void UltraCanvasWASMRenderContext::SetRadialGradient(float x0, float y0, float r0, 
                                                     float x1, float y1, float r1,
                                                     const Color& color1, const Color& color2) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            const gradient = ctx.createRadialGradient($1, $2, $3, $4, $5, $6);
            gradient.addColorStop(0, `rgba(${$7}, ${$8}, ${$9}, ${$10})`);
            gradient.addColorStop(1, `rgba(${$11}, ${$12}, ${$13}, ${$14})`);
            ctx.fillStyle = gradient;
        }
    }, canvasId.c_str(), x0, y0, r0, x1, y1, r1,
       color1.r, color1.g, color1.b, color1.a / 255.0f,
       color2.r, color2.g, color2.b, color2.a / 255.0f);
}

// ===== EFFECTS =====

void UltraCanvasWASMRenderContext::SetShadow(float offsetX, float offsetY, float blur, const Color& color) {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.shadowOffsetX = $1;
            ctx.shadowOffsetY = $2;
            ctx.shadowBlur = $3;
            ctx.shadowColor = `rgba(${$4}, ${$5}, ${$6}, ${$7})`;
        }
    }, canvasId.c_str(), offsetX, offsetY, blur, color.r, color.g, color.b, color.a / 255.0f);
}

void UltraCanvasWASMRenderContext::ClearShadow() {
    EM_ASM({
        const ctx = Module.canvasContexts[UTF8ToString($0)];
        if (ctx) {
            ctx.shadowOffsetX = 0;
            ctx.shadowOffsetY = 0;
            ctx.shadowBlur = 0;
        }
    }, canvasId.c_str());
}

} // namespace UltraCanvas
