// UltraCanvasX11RenderContext.h
// Linux/X11 platform support implementation for UltraCanvas Framework
// Version: 1.0.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
//

#ifndef INC_3_ULTRACANVASX11RENDERCONTEXT_H
#define INC_3_ULTRACANVASX11RENDERCONTEXT_H

#include "../../include/UltraCanvasRenderInterface.h"

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasRenderInterface.h"

// ===== LINUX PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

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

namespace UltraCanvas {
// ===== LINUX RENDER CONTEXT =====
    struct XImageBuffer {
        XImage* ximage;
        uint32_t* pixels;  // Direct pointer to XImage data
        int width;
        int height;
        size_t size_bytes;
        Display* display;  // Keep display for cleanup

        XImageBuffer() : ximage(nullptr), pixels(nullptr), width(0), height(0), size_bytes(0), display(nullptr) {}

        ~XImageBuffer() {
            if (ximage) {
                XDestroyImage(ximage); // Automatically frees ximage->data
                ximage = nullptr;
                pixels = nullptr;
            }
        }

        // Non-copyable but movable
        XImageBuffer(const XImageBuffer&) = delete;
        XImageBuffer& operator=(const XImageBuffer&) = delete;

        XImageBuffer(XImageBuffer&& other) noexcept {
            ximage = other.ximage;
            pixels = other.pixels;
            width = other.width;
            height = other.height;
            size_bytes = other.size_bytes;
            display = other.display;

            other.ximage = nullptr;
            other.pixels = nullptr;
            other.width = other.height = 0;
            other.size_bytes = 0;
            other.display = nullptr;
        }

        XImageBuffer& operator=(XImageBuffer&& other) noexcept {
            if (this != &other) {
                // Clean up current
                if (ximage) {
                    XDestroyImage(ximage);
                }

                // Take ownership
                ximage = other.ximage;
                pixels = other.pixels;
                width = other.width;
                height = other.height;
                size_bytes = other.size_bytes;
                display = other.display;

                // Clear other
                other.ximage = nullptr;
                other.pixels = nullptr;
                other.width = other.height = 0;
                other.size_bytes = 0;
                other.display = nullptr;
            }
            return *this;
        }

        bool IsValid() const {
            return ximage != nullptr && pixels != nullptr && width > 0 && height > 0;
        }

        // Get pixel at coordinates (for debugging)
        uint32_t GetPixel(int x, int y) const {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                return pixels[y * width + x];
            }
            return 0;
        }

        // Set pixel at coordinates (for direct manipulation)
        void SetPixel(int x, int y, uint32_t pixel) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[y * width + x] = pixel;
            }
        }
    };

    // ===== ENHANCED PIXEL BUFFER WITH XIMAGE SUPPORT =====
    class X11PixelBuffer : public IPixelBuffer {
    public:
        std::vector<uint32_t> traditional_buffer;
        std::unique_ptr<XImageBuffer> ximage_buffer;

        int width;
        int height;
        bool is_ximage_backed;

        X11PixelBuffer() : width(0), height(0), is_ximage_backed(false) {}

        // Traditional buffer constructor
        X11PixelBuffer(int w, int h, bool is_x11 = false) : width(w), height(h), is_ximage_backed(is_x11) {
            traditional_buffer.resize(w * h);
        }

        // XImage buffer constructor (zero-copy)
        X11PixelBuffer(std::unique_ptr<XImageBuffer> ximg)
                : ximage_buffer(std::move(ximg)), is_ximage_backed(true) {
            if (ximage_buffer && ximage_buffer->IsValid()) {
                width = ximage_buffer->width;
                height = ximage_buffer->height;
            } else {
                width = height = 0;
                is_ximage_backed = false;
            }
        }
        virtual ~X11PixelBuffer() { Clear(); }

        bool IsValid() const  override {
            if (is_ximage_backed) {
                return ximage_buffer && ximage_buffer->IsValid();
            } else {
                return width > 0 && height > 0 && !traditional_buffer.empty();
            }
        }

        size_t GetSizeInBytes() const override {
            if (is_ximage_backed && ximage_buffer) {
                return ximage_buffer->size_bytes;
            } else {
                return traditional_buffer.size() * sizeof(uint32_t);
            }
        }

        uint32_t* GetPixelData() override {
            if (is_ximage_backed && ximage_buffer) {
                return ximage_buffer->pixels;
            } else if (!traditional_buffer.empty()) {
                return traditional_buffer.data();
            }
            return nullptr;
        }

        void Clear() {
            traditional_buffer.clear();
            ximage_buffer.reset();
            width = height = 0;
            is_ximage_backed = false;
        }

        // Convert to traditional buffer if needed (for cross-platform compatibility)
        std::vector<uint32_t> ToTraditionalBuffer() override {
            if (is_ximage_backed && ximage_buffer && ximage_buffer->IsValid()) {
                std::vector<uint32_t> result(width * height);
                std::memcpy(result.data(), ximage_buffer->pixels, width * height * sizeof(uint32_t));
                return result;
            } else {
                return traditional_buffer;
            }
        }
    };

    class LinuxRenderContext : public IRenderContext {
    private:
        cairo_t *cairo;
        std::mutex cairoMutex;
        PangoContext *pangoContext;
        PangoFontMap *fontMap;

        // State management
        std::vector<RenderState> stateStack;
        RenderState currentState;

        // Internal helper methods
        void ApplyDrawingStyle(const DrawingStyle &style);

        void ApplyTextStyle(const TextStyle &style);

        void ApplyFillStyle(const DrawingStyle &style);

        void ApplyStrokeStyle(const DrawingStyle &style);

        PangoFontDescription *CreatePangoFont(const TextStyle &style);

        // Add a flag to track if we're being destroyed
        bool destroying = false;
        bool contextValid;

        bool SaveXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                             X11PixelBuffer &buffer);

        bool RestoreXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                                X11PixelBuffer &buffer);

        bool SaveImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                              X11PixelBuffer &buffer);

        bool RestoreImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                                 X11PixelBuffer &buffer);

    public:
        LinuxRenderContext(cairo_t *cairoContext);

        virtual ~LinuxRenderContext();

        // ===== INHERITED FROM IRenderContext =====

        // State management
        virtual void PushState() override;

        virtual void PopState() override;

        virtual void ResetState() override;

        // Transformation
        virtual void Translate(float x, float y) override;

        virtual void Rotate(float angle) override;

        virtual void Scale(float sx, float sy) override;

        virtual void SetTransform(float a, float b, float c, float d, float e, float f) override;

        virtual void ResetTransform() override;

        // Clipping
        virtual void SetClipRect(float x, float y, float w, float h) override;

        virtual void ClearClipRect() override;

        virtual void IntersectClipRect(float x, float y, float w, float h) override;

        // Style management
        virtual void SetDrawingStyle(const DrawingStyle &style) override;

        virtual const DrawingStyle &GetDrawingStyle() const override;

        virtual void SetTextStyle(const TextStyle &style) override;

        virtual const TextStyle &GetTextStyle() const override;

        virtual void SetGlobalAlpha(float alpha) override;

        virtual float GetGlobalAlpha() const override;

        // Basic drawing
        virtual void DrawLine(float x, float y, float x1, float y1) override;

        virtual void DrawRectangle(float x, float y, float w, float h) override;

        virtual void FillRectangle(float x, float y, float w, float h) override;

        virtual void DrawRoundedRectangle(float x, float y, float w, float h, float radius) override;

        virtual void FillRoundedRectangle(float x, float y, float w, float h, float radius) override;

        virtual void DrawCircle(float x, float y, float radius) override;

        virtual void FillCircle(float x, float y, float radius) override;

        virtual void DrawEllipse(float x, float y, float w, float h) override;

        virtual void FillEllipse(float x, float y, float w, float h) override;

        virtual void DrawArc(float x, float y, float radius, float startAngle, float endAngle) override;

        virtual void FillArc(float x, float y, float radius, float startAngle, float endAngle) override;

        virtual void
        DrawBezier(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2, const Point2Df &end) override;

        virtual void DrawPath(const std::vector<Point2Df> &points, bool closePath = false) override;

        virtual void FillPath(const std::vector<Point2Df> &points) override;

        // Text rendering
        virtual void DrawText(const std::string &text, float x, float y) override;

        virtual void DrawTextInRect(const std::string &text, float x, float y, float w, float h) override;

        virtual bool MeasureText(const std::string &text, int &w, int &h) override;

        // Image rendering
        virtual void DrawImage(const std::string &imagePath, float x, float y) override;

        virtual void DrawImage(const std::string &imagePath, float x, float y, float w, float h) override;

        virtual void DrawImage(const std::string &imagePath, const Rect2Df &srcRect, const Rect2Df &destRect) override;

        // ===== ENHANCED IMAGE RENDERING METHODS =====
        virtual bool IsImageFormatSupported(const std::string &filePath);

        virtual bool GetImageDimensions(const std::string &imagePath, int &w, int &h);

        void DrawImageWithFilter(const std::string &imagePath, float x, float y, float w, float h,
                                 cairo_filter_t filter = CAIRO_FILTER_BILINEAR);

        void DrawImageTiled(const std::string &imagePath, float x, float y, float w, float h);

        // ===== IMAGE CACHE MANAGEMENT =====
        void ClearImageCache();

        void SetImageCacheSize(size_t maxSizeBytes);

        size_t GetImageCacheMemoryUsage();

        // ===== CONTEXT MANAGEMENT =====
        void InvalidateContext();

        void UpdateContext(cairo_t *newCairoContext);

        bool ValidateContext() const;

        // Pixel operations
//        virtual void SetPixel(const Point2Di &point, const Color &color) override;
//        virtual Color GetPixel(const Point2Di &point) override;

        virtual void Clear(const Color &color) override;

        // Utility functions
        virtual void Flush() override;

        virtual void *GetNativeContext() override;

        // ===== CAIRO-SPECIFIC METHODS =====
        void SetCairoColor(const Color &color);

        cairo_t *GetCairo() const { return cairo; }

        PangoContext *GetPangoContext() const { return pangoContext; }

        cairo_surface_t *GetCairoSurface() const {
            return cairo ? cairo_get_target(cairo) : nullptr;
        }

        // ===== PIXEL OPERATIONS SUPPORT =====
        IPixelBuffer *SavePixelRegion(const Rect2Di &region) override;

        bool RestorePixelRegion(const Rect2Di &region, IPixelBuffer *buf) override;
//        bool SaveRegionAsImage(const Rect2Di& region, const std::string& filename) override;
    };

    // ===== CAIRO FILTER CONSTANTS =====
    // These provide easier access to Cairo filter types
    namespace CairoFilters {
        constexpr cairo_filter_t Fast = CAIRO_FILTER_FAST;
        constexpr cairo_filter_t Good = CAIRO_FILTER_GOOD;
        constexpr cairo_filter_t Best = CAIRO_FILTER_BEST;
        constexpr cairo_filter_t Nearest = CAIRO_FILTER_NEAREST;
        constexpr cairo_filter_t Bilinear = CAIRO_FILTER_BILINEAR;
        constexpr cairo_filter_t Gaussian = CAIRO_FILTER_GAUSSIAN;
    }

    // ===== CONVENIENCE FUNCTIONS FOR IMAGE RENDERING =====

    // Load image and get dimensions without rendering
    inline Point2Df GetImageDimensions(const std::string& imagePath) {
        // This would typically use the current render context
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            int w, h;
            if (linuxCtx->GetImageDimensions(imagePath, w, h)) {
                return Point2Df(w, h);
            }
        }
        return Point2Df(0, 0);
    }

    // Check if image format is supported
    inline bool IsImageFormatSupported(const std::string& imagePath) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            return linuxCtx->IsImageFormatSupported(imagePath);
        }
        return false;
    }

    // Draw image with high-quality filtering
    inline void DrawImageHighQuality(const std::string& imagePath, const Rect2Df& destRect) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageWithFilter(imagePath, destRect.x, destRect.y, destRect.width, destRect.height, CAIRO_FILTER_BEST);
        }
    }

    // Draw image with pixelated (nearest neighbor) filtering
    inline void DrawImagePixelated(const std::string& imagePath, const Rect2Df& destRect) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageWithFilter(imagePath, destRect.x, destRect.y, destRect.width, destRect.height, CAIRO_FILTER_NEAREST);
        }
    }

    // Draw repeating background image
    inline void DrawImageBackground(const std::string& imagePath, const Rect2Df& area) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageTiled(imagePath, area.x, area.y, area.width, area.height);
        }
    }
} // namespace UltraCanvas
#endif //INC_3_ULTRACANVASX11RENDERCONTEXT_H
