// OS/MacOS/UltraCanvasMacOSRenderContext.h
// macOS Core Graphics platform support implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_RENDER_CONTEXT_H
#define ULTRACANVAS_MACOS_RENDER_CONTEXT_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasRenderContext.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
    #import <CoreGraphics/CoreGraphics.h>
    #import <CoreText/CoreText.h>
    #import <QuartzCore/QuartzCore.h>
    #import <Foundation/Foundation.h>
#else
    // Forward declarations for C++ only files
    typedef struct CGContext* CGContextRef;
    typedef struct CGLayer* CGLayerRef;
    typedef struct CGPath* CGPathRef;
    typedef struct CGMutablePath* CGMutablePathRef;
    typedef struct CTFont* CTFontRef;
    typedef struct CTLine* CTLineRef;
    typedef struct CGColor* CGColorRef;
    typedef struct CGColorSpace* CGColorSpaceRef;
    typedef struct CGImage* CGImageRef;
    typedef double CGFloat;
    typedef struct CGPoint CGPoint;
    typedef struct CGSize CGSize;
    typedef struct CGRect CGRect;
#endif

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <cstring>
#include <condition_variable>
#include <string>
#include <stack>

namespace UltraCanvas {

// ===== CORE GRAPHICS DOUBLE BUFFER =====
class MacOSCoreGraphicsDoubleBuffer : public IDoubleBuffer {
private:
    mutable std::mutex bufferMutex;

    // Window context (target)
    CGContextRef windowContext;
    CGLayerRef windowLayer;

    // Staging context (back buffer)  
    CGLayerRef stagingLayer;
    CGContextRef stagingContext;

    int bufferWidth;
    int bufferHeight;
    bool isValid;
    
    // Color space for optimal performance
    CGColorSpaceRef colorSpace;

public:
    MacOSCoreGraphicsDoubleBuffer();
    virtual ~MacOSCoreGraphicsDoubleBuffer();

    bool Initialize(int width, int height, void* windowContext) override;
    bool Resize(int newWidth, int newHeight) override;
    void* GetStagingContext() override { return stagingContext; }
    void* GetStagingSurface() override { return stagingLayer; }
    void SwapBuffers() override;
    void Cleanup() override;

    int GetWidth() const override { return bufferWidth; }
    int GetHeight() const override { return bufferHeight; }
    bool IsValid() const override { return isValid; }

private:
    bool CreateStagingLayer();
    void DestroyStagingLayer();
    void CreateOptimalColorSpace();
};

// ===== CORE GRAPHICS PIXEL BUFFER =====
class MacOSPixelBuffer : public IPixelBuffer {
private:
    std::vector<uint32_t> pixelData;
    CGImageRef cgImage;
    CGDataProviderRef dataProvider;
    CGColorSpaceRef colorSpace;
    int width;
    int height;
    bool needsUpdate;

public:
    MacOSPixelBuffer();
    MacOSPixelBuffer(int w, int h);
    virtual ~MacOSPixelBuffer();

    bool Initialize(int w, int h);
    void Clear() override;
    bool IsValid() const override;
    size_t GetSizeInBytes() const override;
    uint32_t* GetPixelData() override;
    int GetWidth() const override { return width; }
    int GetHeight() const override { return height; }
    
    void SetPixel(int x, int y, uint32_t pixel) override;
    uint32_t GetPixel(int x, int y) const override;
    
    // macOS-specific methods
    CGImageRef GetCGImage();
    void MarkAsUpdated() { needsUpdate = true; }
    
private:
    void UpdateCGImage();
    void CreateCGImage();
    void ReleaseCGImage();
};

// ===== MACOS RENDER CONTEXT =====
class MacOSRenderContext : public IRenderContext {
private:
    // ===== CORE GRAPHICS CONTEXT =====
    CGContextRef cgContext;
    CGLayerRef backBuffer;
    bool ownsContext;
    
    // ===== DOUBLE BUFFER SYSTEM =====
    std::unique_ptr<MacOSCoreGraphicsDoubleBuffer> doubleBuffer;
    bool doubleBufferingEnabled;

    // ===== STATE MANAGEMENT =====
    std::stack<CGContextRef> contextStack;
    DrawingStyle currentDrawingStyle;
    TextStyle currentTextStyle;
    float globalAlpha;

    // ===== GRAPHICS RESOURCES =====
    CGColorSpaceRef colorSpace;
    CGMutablePathRef currentPath;
    std::unordered_map<std::string, CTFontRef> fontCache;
    
    // ===== VIEWPORT =====
    int viewportWidth;
    int viewportHeight;
    CGRect clipRect;
    bool hasClipRect;

    // ===== THREAD SAFETY =====
    mutable std::mutex cgMutex;
    std::thread::id owningThread;

    // ===== PERFORMANCE OPTIMIZATION =====
    CGColorRef cachedFillColor;
    CGColorRef cachedStrokeColor;
    CTFontRef cachedFont;
    std::string cachedFontName;
    float cachedFontSize;

public:
    MacOSRenderContext(CGContextRef context, int width, int height, bool enableDoubleBuffering = true);
    MacOSRenderContext(int width, int height, bool enableDoubleBuffering = true);
    virtual ~MacOSRenderContext();

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
    float GetAlpha() const override { return globalAlpha; }
    const DrawingStyle& GetDrawingStyle() const override { return currentDrawingStyle; }
    const TextStyle& GetTextStyle() const override { return currentTextStyle; }

    // ===== BASIC SHAPES =====
    void DrawLine(float x1, float y1, float x2, float y2) override;
    void DrawRectangle(float x, float y, float width, float height) override;
    void FillRectangle(float x, float y, float width, float height) override;
    void DrawCircle(float centerX, float centerY, float radius) override;
    void FillCircle(float centerX, float centerY, float radius) override;
    void DrawEllipse(float x, float y, float width, float height) override;
    void FillEllipse(float x, float y, float width, float height) override;

    // ===== PATH OPERATIONS =====
    void BeginPath() override;
    void ClosePath() override;
    void MoveTo(float x, float y) override;
    void LineTo(float x, float y) override;
    void CurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override;
    void ArcTo(float x1, float y1, float x2, float y2, float radius) override;
    void Arc(float centerX, float centerY, float radius, float startAngle, float endAngle, bool counterclockwise) override;
    void StrokePath() override;
    void FillLinePath() override;

    // ===== TEXT RENDERING =====
    void DrawText(const std::string& text, float x, float y) override;
    void DrawTextInRect(const std::string& text, float x, float y, float width, float height, TextAlign align) override;
    Rect2D MeasureText(const std::string& text) override;
    float GetTextHeight() const override;

    // ===== IMAGE OPERATIONS =====
    void DrawImage(const std::string& imagePath, float x, float y) override;
    void DrawImage(const std::string& imagePath, float x, float y, float width, float height) override;
    void DrawImageFromBuffer(IPixelBuffer* buffer, float x, float y) override;
    void DrawImageFromBuffer(IPixelBuffer* buffer, float x, float y, float width, float height) override;

    // ===== BUFFER OPERATIONS =====
    void Clear() override;
    void Clear(const Color& color) override;
    std::unique_ptr<IPixelBuffer> CaptureBuffer() override;
    std::unique_ptr<IPixelBuffer> CaptureBuffer(float x, float y, float width, float height) override;
    void Flush() override;

    // ===== ADVANCED OPERATIONS =====
    void SetBlendMode(BlendMode mode) override;
    void DrawGradient(const std::vector<Color>& colors, const std::vector<float>& stops, 
                     float startX, float startY, float endX, float endY) override;

    // ===== MACOS-SPECIFIC METHODS =====
    CGContextRef GetCGContext() const { return cgContext; }
    bool IsDoubleBufferingEnabled() const { return doubleBufferingEnabled; }
    void EnableDoubleBuffering(bool enable);
    void SetCGContext(CGContextRef context);

    // ===== THREAD SAFETY =====
    template<typename Func>
    auto SafeExecute(Func&& func) -> decltype(func());
    void SetOwningThread();

private:
    // ===== INITIALIZATION =====
    bool InitializeCoreGraphics(int width, int height);
    void SetupDefaultState();
    void CreateColorSpace();

    // ===== COLOR MANAGEMENT =====
    CGColorRef CreateCGColor(const Color& color);
    void UpdateFillColor(const Color& color);
    void UpdateStrokeColor(const Color& color);
    void ReleaseCachedColors();

    // ===== FONT MANAGEMENT =====
    CTFontRef GetOrCreateFont(const std::string& fontName, float fontSize);
    void ReleaseFontCache();

    // ===== PATH HELPERS =====
    void CreatePathIfNeeded();
    void ApplyCurrentPath();
    
    // ===== COORDINATE CONVERSION =====
    CGPoint UCPointToCGPoint(const Point2D& point);
    CGRect UCRectToCGRect(const Rect2D& rect);
    Point2D CGPointToUCPoint(CGPoint point);
    Rect2D CGRectToUCRect(CGRect rect);

    // ===== BLEND MODE CONVERSION =====
    CGBlendMode UCBlendModeToCGBlendMode(BlendMode mode);

    // ===== CLEANUP =====
    void Cleanup();
};

// ===== THREAD-SAFE EXECUTION TEMPLATE =====
template<typename Func>
auto MacOSRenderContext::SafeExecute(Func&& func) -> decltype(func()) {
    std::lock_guard<std::mutex> lock(cgMutex);
    
    if (owningThread == std::thread::id() || owningThread == std::this_thread::get_id()) {
        return func();
    }
    
    // Execute on main thread for thread safety
    __block decltype(func()) result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = func();
    });
    
    return result;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_RENDER_CONTEXT_H