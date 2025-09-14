// UltraCanvasX11RenderContext.h
// Linux/X11 platform support implementation for UltraCanvas Framework
// Version: 1.0.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
//

#ifndef INC_3_ULTRACANVASX11RENDERCONTEXT_H
#define INC_3_ULTRACANVASX11RENDERCONTEXT_H


// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasRenderContext.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasRenderContext.h"

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

        class LinuxCairoDoubleBuffer : public IDoubleBuffer {
        private:
            mutable std::mutex bufferMutex;

            // Window surface (target)
            cairo_surface_t* windowSurface;
            cairo_t* windowContext;

            // Staging surface (back buffer)
            cairo_surface_t* stagingSurface;
            cairo_t* stagingContext;

            int bufferWidth;
            int bufferHeight;
            bool isValid;

        public:
            LinuxCairoDoubleBuffer();
            virtual ~LinuxCairoDoubleBuffer();

            bool Initialize(int width, int height, void* windowSurface) override;
            bool Resize(int newWidth, int newHeight) override;
            void* GetStagingContext() override { return stagingContext; }
            void* GetStagingSurface() override { return stagingSurface; }
            void SwapBuffers() override;
            void Cleanup() override;

            int GetWidth() const override { return bufferWidth; }
            int GetHeight() const override { return bufferHeight; }
            bool IsValid() const override { return isValid; }

        private:
            bool CreateStagingSurface();
            void DestroyStagingSurface();
        };


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

        XImageBuffer(XImageBuffer&& other) noexcept;

        XImageBuffer& operator=(XImageBuffer&& other) noexcept;
        bool IsValid() const { return ximage != nullptr && pixels != nullptr && width > 0 && height > 0; }
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

        bool IsValid() const  override;
        size_t GetSizeInBytes() const override;
        uint32_t* GetPixelData() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        void Clear();

        // Convert to traditional buffer if needed (for cross-platform compatibility)
        std::vector<uint32_t> ToTraditionalBuffer();
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

        // double-buffer
        LinuxCairoDoubleBuffer doubleBuffer;
        // Original window surface references
        cairo_surface_t* originalWindowSurface;
        cairo_t* originalWindowContext;

        bool doubleBufferingEnabled = false;

        // Internal helper methods
        void ApplyDrawingStyle(const DrawingStyle &style);
        void ApplyTextStyle(const TextStyle &style);
        void ApplyFillStyle(const DrawingStyle &style);
        void ApplyStrokeStyle(const DrawingStyle &style);
        void ApplyGradientFill(const Gradient& gradient);

        PangoFontDescription *CreatePangoFont(const TextStyle &style);

        // Add a flag to track if we're being destroyed
        bool destroying = false;
        bool enableDoubleBuffering = false;

        bool SaveXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                             X11PixelBuffer &buffer);

        bool RestoreXlibSurface(cairo_surface_t *surface, const Rect2Di &region,
                                X11PixelBuffer &buffer);

        bool SaveImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                              X11PixelBuffer &buffer);

        bool RestoreImageSurface(cairo_surface_t *surface, const Rect2Di &region,
                                 X11PixelBuffer &buffer);

    public:
        LinuxRenderContext(cairo_t *cairoContext, cairo_surface_t *cairo_surface, int width, int height, bool enableDoubleBuffering);

        virtual ~LinuxRenderContext();

        // ===== INHERITED FROM IRenderContext =====

        // State management
        void PushState() override;
        void PopState() override;
        void ResetState() override;

        // Transformation
        void Translate(float x, float y) override;
        void Rotate(float angle) override;
        void Scale(float sx, float sy) override;
        void SetTransform(float a, float b, float c, float d, float e, float f) override;
        void ResetTransform() override;

        // Clipping
        void SetClipRect(float x, float y, float w, float h) override;
        void ClearClipRect() override;
        void IntersectClipRect(float x, float y, float w, float h) override;

        // Style management
        void SetDrawingStyle(const DrawingStyle &style) override;
        const DrawingStyle &GetDrawingStyle() const override;
        void SetTextStyle(const TextStyle &style) override;
        const TextStyle &GetTextStyle() const override;

        void SetGlobalAlpha(float alpha) override;
        float GetGlobalAlpha() const override;

        void SetFillGradient(const Color& startColor, const Color& endColor,
                             const Point2Df& startPoint, const Point2Df& endPoint);

        // Basic drawing
        void DrawLine(float x, float y, float x1, float y1) override;
        void DrawRectangle(float x, float y, float w, float h) override;
        void FillRectangle(float x, float y, float w, float h) override;
        void DrawRoundedRectangle(float x, float y, float w, float h, float radius) override;
        void FillRoundedRectangle(float x, float y, float w, float h, float radius) override;
        void DrawCircle(float x, float y, float radius) override;
        void FillCircle(float x, float y, float radius) override;
        void DrawEllipse(float x, float y, float w, float h) override;
        void FillEllipse(float x, float y, float w, float h) override;
        void DrawArc(float x, float y, float radius, float startAngle, float endAngle) override;
        void FillArc(float x, float y, float radius, float startAngle, float endAngle) override;
        void DrawBezier(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2, const Point2Df &end) override;
        void DrawPath(const std::vector<Point2Df> &points, bool closePath) override;
        void FillPath(const std::vector<Point2Df> &points) override;

        // Text rendering
        void DrawText(const std::string &text, float x, float y) override;
        void DrawTextInRect(const std::string &text, float x, float y, float w, float h) override;
        bool MeasureText(const std::string &text, int &w, int &h) override;

        // Image rendering
        void DrawImage(const std::string &imagePath, float x, float y) override;
        void DrawImage(const std::string &imagePath, float x, float y, float w, float h) override;
        void DrawImage(const std::string &imagePath, const Rect2Df &srcRect, const Rect2Df &destRect) override;

        // ===== ENHANCED IMAGE RENDERING METHODS =====
        bool IsImageFormatSupported(const std::string &filePath) override;
        bool GetImageDimensions(const std::string &imagePath, int &w, int &h) override;

        void DrawImageWithFilter(const std::string &imagePath, float x, float y, float w, float h,
                                 cairo_filter_t filter = CAIRO_FILTER_BILINEAR);

        void DrawImageTiled(const std::string &imagePath, float x, float y, float w, float h);

        // ===== IMAGE CACHE MANAGEMENT =====
        void ClearImageCache();

        void SetImageCacheSize(size_t maxSizeBytes);

        size_t GetImageCacheMemoryUsage();

        // ===== CONTEXT MANAGEMENT =====
        void UpdateContext(cairo_t *newCairoContext);

        // Pixel operations
//        virtual void SetPixel(const Point2Di &point, const Color &color) override;
//        virtual Color GetPixel(const Point2Di &point) override;

        void Clear(const Color &color) override;

        // Utility functions
        void Flush() override;

        void *GetNativeContext() override;

        // ===== CAIRO-SPECIFIC METHODS =====
        void SetCairoColor(const Color &color);

        cairo_t *GetCairo() const { return cairo; }

        PangoContext *GetPangoContext() const { return pangoContext; }

        cairo_surface_t *GetCairoSurface() const {
            return cairo ? cairo_get_target(cairo) : nullptr;
        }

        // ===== PIXEL OPERATIONS SUPPORT =====
        bool PaintPixelBuffer(int x, int y, int width, int height, uint32_t* pixels) override;

        IPixelBuffer *SavePixelRegion(const Rect2Di &region) override;

        bool RestorePixelRegion(const Rect2Di &region, IPixelBuffer *buf) override;
//        bool SaveRegionAsImage(const Rect2Di& region, const std::string& filename) override;


        // ===== DOUBLE BUFFERING CONTROL =====
    public:
        // Enable double buffering for this render context
        bool EnableDoubleBuffering(int width, int height);
        void DisableDoubleBuffering();
        // Handle window resize - automatically resizes buffer if enabled
        void OnWindowResize(int newWidth, int newHeight);

    private:
        // Internal helper methods
        void SwitchToStagingSurface();
        void SwitchToWindowSurface();
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

} // namespace UltraCanvas
#endif //INC_3_ULTRACANVASX11RENDERCONTEXT_H
