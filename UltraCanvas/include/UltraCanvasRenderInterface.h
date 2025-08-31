// UltraCanvasRenderInterface.h - Enhanced Version
// Cross-platform rendering interface with improved context management
// Version: 2.2.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <thread>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <stack>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasWindow;
class UltraCanvasBaseWindow;
// ===== GRADIENT STRUCTURES =====
    struct GradientStop {
        float position;    // 0.0 to 1.0
        Color color;

        GradientStop(float pos = 0.0f, const Color& col = Colors::Black)
                : position(pos), color(col) {}
    };

    enum class GradientType {
        Linear,
        Radial,
        Conic
    };

    struct Gradient {
        GradientType type;
        Point2Df startPoint;
        Point2Df endPoint;
        float radius1, radius2;  // For radial gradients
        std::vector<GradientStop> stops;

        Gradient(GradientType gradType = GradientType::Linear) : type(gradType) {
            radius1 = radius2 = 0;
        }
    };

// ===== DRAWING STYLES =====
    enum class FillMode {
        NoneFill,
        Solid,
        Gradient,
        Pattern,
        Texture
    };

    enum class StrokeStyle {
        Solid,
        Dashed,
        Dotted,
        DashDot,
        Custom
    };

    enum class LineCap {
        Butt,
        Round,
        Square
    };

    enum class LineJoin {
        Miter,
        Round,
        Bevel
    };

    struct DrawingStyle {
        // Fill properties
        FillMode fillMode = FillMode::Solid;
        Color fillColor = Colors::White;
        Gradient fillGradient;
        std::string patternPath;

        // Stroke properties
        bool hasStroke = false;
        Color strokeColor = Colors::Black;
        float strokeWidth = 1.0f;
        StrokeStyle strokeStyle = StrokeStyle::Solid;
        LineCap lineCap = LineCap::Butt;
        LineJoin lineJoin = LineJoin::Miter;
        std::vector<float> dashPattern;

        // Shadow properties
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2Df shadowOffset = Point2Df(2, 2);
        float shadowBlur = 2.0f;

        // Alpha blending
        float globalAlpha = 1.0f;
    };

// ===== TEXT RENDERING STRUCTURES =====
    enum class TextAlign {
        Left,
        Center,
        Right,
        Justify
    };

    enum class TextBaseline {
        Top,
        Middle,
        Bottom,
        Baseline
    };

    enum class FontWeight {
        Normal,
        Bold,
        Light,
        ExtraBold
    };

    enum class FontStyle {
        Normal,
        Italic,
        Oblique
    };

    struct TextStyle {
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontStyle fontStyle = FontStyle::Normal;
        Color textColor = Colors::Black;
        TextAlign alignment = TextAlign::Left;
        TextBaseline baseline = TextBaseline::Baseline;
        float lineHeight = 1.2f;
        float letterSpacing = 0.0f;
        float wordSpacing = 0.0f;

        // Text effects
        bool hasUnderline = false;
        bool hasStrikethrough = false;
        bool hasOutline = false;
        Color outlineColor = Colors::Black;
        float outlineWidth = 1.0f;
    };

// ===== RENDERING STATE =====
    struct RenderState {
        DrawingStyle style;
        TextStyle textStyle;
        Rect2Df clipRect;
        Point2Df translation;
        float rotation = 0.0f;
        Point2Df scale = Point2Df(1.0f, 1.0f);
        float globalAlpha = 1.0f;

        RenderState() {
            clipRect = Rect2Df(0, 0, 10000, 10000); // Large default clip
        }
    };

// ===== UNIFIED RENDERING INTERFACE =====
    class IRenderContext {
    public:
        virtual ~IRenderContext() = default;

        // ===== STATE MANAGEMENT =====
        virtual void PushState() = 0;
        virtual void PopState() = 0;
        virtual void ResetState() = 0;

        // ===== TRANSFORMATION =====
        virtual void Translate(float x, float y) = 0;
        virtual void Rotate(float angle) = 0;
        virtual void Scale(float sx, float sy) = 0;
        virtual void SetTransform(float a, float b, float c, float d, float e, float f) = 0;
        virtual void ResetTransform() = 0;

        // ===== CLIPPING =====
        virtual void SetClipRect(float x, float y, float w, float h) = 0;
        virtual void ClearClipRect() = 0;
        virtual void IntersectClipRect(float x, float y, float w, float h) = 0;
//        virtual Rect2Df GetClipRect() const = 0;

        // ===== STYLE MANAGEMENT =====
        virtual void SetDrawingStyle(const DrawingStyle& style) = 0;
        virtual void SetTextStyle(const TextStyle& style) = 0;
        virtual void SetGlobalAlpha(float alpha) = 0;
        virtual float GetGlobalAlpha() const = 0;
        virtual const DrawingStyle& GetDrawingStyle() const = 0;
        virtual const TextStyle& GetTextStyle() const = 0;

        // ===== BASIC SHAPES =====
        virtual void DrawLine(float x, float y, float x1, float y1) = 0;
        virtual void DrawRectangle(float x, float y, float w, float h) = 0;
        virtual void FillRectangle(float x, float y, float w, float h) = 0;
        virtual void DrawRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void FillRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void DrawCircle(float x, float y, float radius) = 0;
        virtual void FillCircle(float x, float y, float radius) = 0;
        virtual void DrawEllipse(float x, float y, float w, float h) = 0;
        virtual void FillEllipse(float x, float y, float w, float h) = 0;
        virtual void DrawArc(float x, float y, float radius, float startAngle, float endAngle) = 0;
        virtual void FillArc(float x, float y, float radius, float startAngle, float endAngle) = 0;
        virtual void DrawBezier(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end) = 0;
        virtual void DrawPath(const std::vector<Point2Df>& points, bool closePath = false) = 0;
        virtual void FillPath(const std::vector<Point2Df>& points) = 0;

//        virtual void ClosePath() = 0;
//        virtual void FillPath() = 0;
//        virtual void StrokePath() = 0;

        // ===== TEXT RENDERING =====
        virtual void DrawText(const std::string& text, float x, float y) = 0;
        virtual void DrawTextInRect(const std::string& text, float x, float y, float w, float h) = 0;
        virtual bool MeasureText(const std::string& text, int& w, int& h) = 0;
        int GetTextWidth(const std::string& text) {
            int w, h;
            MeasureText(text, w, h);
            return w;
        };
        int GetTextHeight(const std::string& text) {
            int w, h;
            MeasureText(text, w, h);
            return h;
        };

        // ===== IMAGE RENDERING =====
        virtual void DrawImage(const std::string& imagePath, float x, float y) = 0;
        virtual void DrawImage(const std::string& imagePath, float x, float y, float w, float h) = 0;
        virtual void DrawImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;
        virtual bool IsImageFormatSupported(const std::string& filePath) = 0;
        virtual bool GetImageDimensions(const std::string& imagePath, int& w, int& h) = 0;

        // ===== PIXEL OPERATIONS =====
        virtual void SetPixel(const Point2Df& point, const Color& color) = 0;
        virtual Color GetPixel(const Point2Df& point) = 0;
        virtual void Clear(const Color& color) = 0;

        // ===== UTILITY FUNCTIONS =====
        virtual void Flush() = 0;
        virtual void* GetNativeContext() = 0;

        virtual void SetFillColor(const Color& color) {
            DrawingStyle style = GetDrawingStyle();
            style.fillColor = color;
            SetDrawingStyle(style);
        }

        virtual void SetStrokeColor(const Color& color) {
            DrawingStyle style = GetDrawingStyle();
            style.strokeColor = color;
            style.hasStroke = true;
            SetDrawingStyle(style);
        }

        virtual void SetStrokeWidth(float width) {
            DrawingStyle style = GetDrawingStyle();
            style.strokeWidth = width;
            style.hasStroke = (width > 0);
            SetDrawingStyle(style);
        }

        virtual void SetFont(const std::string& fontFamily, float fontSize) {
            TextStyle style = GetTextStyle();
            style.fontFamily = fontFamily;
            style.fontSize = fontSize;
            SetTextStyle(style);
        }

        virtual void SetTextColor(const Color& color) {
            TextStyle style = GetTextStyle();
            style.textColor = color;
            SetTextStyle(style);
        }

        virtual void SetTextAlign(TextAlign align) {
            TextStyle style = GetTextStyle();
            style.alignment = align;
            SetTextStyle(style);
        }
    };

// ===== ENHANCED RENDER CONTEXT MANAGER =====
class RenderContextManager {
private:
    // Thread-local storage for render contexts
    static thread_local IRenderContext* currentContext;
    static thread_local std::stack<IRenderContext*> contextStack;
    
    // Global mapping of windows to their render contexts (thread-safe)
    static std::unordered_map<void*, IRenderContext*> windowContextMap;
    static std::mutex windowContextMutex;
    
    // Current rendering window (thread-local)
    static thread_local UltraCanvasBaseWindow* currentWindow;
    
public:
    // ===== CURRENT CONTEXT MANAGEMENT =====
    static IRenderContext* GetCurrent() {
        return currentContext;
    }
    
    static void SetCurrent(IRenderContext* context) {
        currentContext = context;
    }
    
    // ===== CONTEXT STACK MANAGEMENT (for nested rendering) =====
    static void PushContext(IRenderContext* context) {
        contextStack.push(currentContext);
        currentContext = context;
    }
    
    static void PopContext() {
        if (!contextStack.empty()) {
            currentContext = contextStack.top();
            contextStack.pop();
        } else {
            currentContext = nullptr;
        }
    }
    
    // ===== WINDOW-CONTEXT ASSOCIATION =====
    static void RegisterWindowContext(UltraCanvasBaseWindow* window, IRenderContext* context) {
        std::lock_guard<std::mutex> lock(windowContextMutex);
        windowContextMap[window] = context;
    }
    
    static void UnregisterWindowContext(UltraCanvasBaseWindow* window) {
        std::lock_guard<std::mutex> lock(windowContextMutex);
        windowContextMap.erase(window);
    }
    
    static IRenderContext* GetWindowContext(UltraCanvasBaseWindow* window) {
        std::lock_guard<std::mutex> lock(windowContextMutex);
        auto it = windowContextMap.find(window);
        return (it != windowContextMap.end()) ? it->second : nullptr;
    }

    static IRenderContext* GetFirstWindowContext() {
        std::lock_guard<std::mutex> lock(windowContextMutex);
        if (!windowContextMap.empty()) {
            auto it = windowContextMap.begin();
            if (it->second) {
                return it->second;
            }
        }
        return nullptr;
    }

    // ===== WINDOW-AWARE RENDERING =====
    static void SetCurrentWindow(UltraCanvasBaseWindow* window) {
        currentWindow = window;
        if (window) {
            // Automatically set the context for this window
            IRenderContext* context = GetWindowContext(window);
            //std::cout << "SetCurrentWindow context=" << context << std::endl;
            if (context) {
                SetCurrent(context);
            }
        }
    }
    
    static UltraCanvasBaseWindow* GetCurrentWindow() {
        return currentWindow;
    }
    
    // ===== AUTOMATIC CONTEXT SCOPE GUARD =====
    class WindowRenderScope {
    private:
        IRenderContext* previousContext;
        UltraCanvasBaseWindow* previousWindow;
        
    public:
        WindowRenderScope(UltraCanvasBaseWindow* window) {
            previousContext = currentContext;
            previousWindow = currentWindow;
            
            SetCurrentWindow(window);
        }
        
        ~WindowRenderScope() {
            currentContext = previousContext;
            currentWindow = previousWindow;
        }
        
        // Non-copyable
        WindowRenderScope(const WindowRenderScope&) = delete;
        WindowRenderScope& operator=(const WindowRenderScope&) = delete;
    };
    
    // ===== RENDER CONTEXT SCOPE GUARD =====
    class RenderContextScope {
    private:
        IRenderContext* previousContext;
        
    public:
        RenderContextScope(IRenderContext* context) {
            previousContext = currentContext;
            currentContext = context;
        }
        
        ~RenderContextScope() {
            currentContext = previousContext;
        }
        
        // Non-copyable
        RenderContextScope(const RenderContextScope&) = delete;
        RenderContextScope& operator=(const RenderContextScope&) = delete;
    };
};

// ===== CONVENIENCE MACROS =====
#define ULTRACANVAS_WINDOW_RENDER_SCOPE(window) \
    RenderContextManager::WindowRenderScope _windowRenderScope(window)

#define ULTRACANVAS_CONTEXT_SCOPE(context) \
    RenderContextManager::RenderContextScope _contextScope(context)

// ===== ENHANCED CONVENIENCE FUNCTIONS =====

// Get current render context with validation
inline IRenderContext* GetRenderContext() {
    IRenderContext* ctx = RenderContextManager::GetCurrent();
    if (!ctx) {
        // Try to get context from current window
        UltraCanvasBaseWindow* window = RenderContextManager::GetCurrentWindow();
        std::cout << "GetRenderContext window=" << window << std::endl;
        if (window) {
            ctx = RenderContextManager::GetWindowContext(window);
            std::cout << "GetRenderContext ctx=" << ctx << std::endl;
            if (ctx) {
                RenderContextManager::SetCurrent(ctx);
            }
        }

        ctx = RenderContextManager::GetFirstWindowContext();
        if (ctx) {
            std::cout << "GetRenderContext: Retrieved default context" << std::endl;
            return ctx;
        }

    }
    return ctx;
}

inline void DrawLine(float x1, float y1, float x2, float y2) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawLine(x1, y1, x2, y2);
}

inline void DrawLine(int x1, int y1, int x2, int y2) {
    DrawLine(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2));
}

inline void DrawLine(const Point2Df& start, const Point2Df& end) {
    DrawLine(start.x, start.y, end.x, end.y);
}

inline void DrawLine(const Point2Di& start, const Point2Di& end) {
    DrawLine(start.x, start.y, end.x, end.y);
}

inline void DrawRectangle(float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawRectangle(x, y, w, h);
}
inline void DrawRectangle(int x, int y, int w, int h) {
    DrawRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}
inline void DrawRectangle(const Rect2Df& rect) {
    DrawRectangle(rect.x, rect.y, rect.width, rect.height);
}

inline void DrawRectangle(const Rect2Di& rect) {
    DrawRectangle(rect.x, rect.y, rect.width, rect.height);
}

inline void FillRectangle(float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->FillRectangle(x, y, w, h);
}
inline void FillRectangle(int x, int y, int w, int h) {
    FillRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}

inline void FillRectangle(const Rect2Df& rect) {
    FillRectangle(rect.x, rect.y, rect.width, rect.height);
}

inline void FillRectangle(const Rect2Di& rect) {
    FillRectangle(rect.x, rect.y, rect.width, rect.height);
}


inline void DrawRoundedRectangle(float x, float y, float w, float h, float radius) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawRoundedRectangle(x, y, w, h, radius);
}
inline void DrawRoundedRectangle(int x, int y, int w, int h, float radius) {
    DrawRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), radius);
}

inline void DrawRoundedRectangle(const Rect2Df& rect, float radius) {
    DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
}

inline void DrawRoundedRectangle(const Rect2Di& rect, float radius) {
    DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
}

inline void FillRoundedRectangle(float x, float y, float w, float h, float radius) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->FillRoundedRectangle(x, y, w, h, radius);
}
inline void FillRoundedRectangle(int x, int y, int w, int h, float radius) {
    FillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), radius);
}

inline void FillRoundedRectangle(const Rect2Df& rect, float radius) {
    FillRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
}

inline void FillRoundedRectangle(const Rect2Di& rect, float radius) {
    FillRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
}

inline void DrawCircle(float x, float y, float radius) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawCircle(x, y, radius);
}
inline void DrawCircle(int x, int y, float radius) {
    DrawCircle(static_cast<float>(x), static_cast<float>(y), radius);
}
inline void DrawCircle(const Point2Df& center, float radius) {
    DrawCircle(center.x, center.y, radius);
}
inline void DrawCircle(const Point2Di& center, float radius) {
    DrawCircle(center.x, center.y, radius);
}

inline void FillCircle(float x, float y, float radius) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->FillCircle(x, y, radius);
}
inline void FillCircle(int x, int y, float radius) {
    FillCircle(static_cast<float>(x), static_cast<float>(y), radius);
}
inline void FillCircle(const Point2Df& center, float radius) {
    FillCircle(center.x, center.y, radius);
}
inline void FillCircle(const Point2Di& center, float radius) {
    FillCircle(center.x, center.y, radius);
}

inline void DrawText(const std::string& text, float x, float y) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawText(text, x, y);
}
inline void DrawText(const std::string& text, int x, int y) {
    DrawText(text, static_cast<float>(x), static_cast<float>(y));
}
inline void DrawText(const std::string& text, const Point2Df& position) {
    DrawText(text, position.x, position.y);
}
inline void DrawText(const std::string& text, const Point2Di& position) {
    DrawText(text, position.x, position.y);
}

inline void DrawImage(const std::string& imagePath, float x, float y) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawImage(imagePath, x, y);
}
inline void DrawImage(const std::string& imagePath, int x, int y) {
    DrawImage(imagePath, static_cast<float>(x), static_cast<float>(y));
}
inline void DrawImage(const std::string& imagePath, const Point2Df& position) {
    DrawImage(imagePath, position.x, position.y);
}
inline void DrawImage(const std::string& imagePath, const Point2Di& position) {
    DrawImage(imagePath, position.x, position.y);
}

inline void DrawImage(const std::string& imagePath, float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->DrawImage(imagePath, x, y, w, h);
}
inline void DrawImage(const std::string& imagePath, int x, int y, int w, int h) {
    DrawImage(imagePath, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}
inline void DrawImage(const std::string& imagePath, const Rect2Df& position) {
    DrawImage(imagePath, position.x, position.y, position.width, position.height);
}
inline void DrawImage(const std::string& imagePath, const Rect2Di& position) {
    DrawImage(imagePath, position.x, position.y, position.width, position.height);;
}

// Style convenience functions with automatic context resolution
inline void SetFillColor(const Color& color) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetFillColor(color);
}

inline void SetStrokeColor(const Color& color) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetStrokeColor(color);
}

inline void SetStrokeWidth(float width) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetStrokeWidth(width);
}

inline void SetFont(const std::string& fontFamily, float fontSize) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetFont(fontFamily, fontSize);
}

inline void SetTextColor(const Color& color) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetTextColor(color);
}

inline void SetTextAlign(TextAlign align) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetTextAlign(align);
}

inline void SetDrawingStyle(const DrawingStyle& style) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetDrawingStyle(style);
}

inline void SetTextStyle(const TextStyle& style) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetTextStyle(style);
}

// State management convenience functions
inline void PushRenderState() {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->PushState();
}

inline void PopRenderState() {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->PopState();
}

inline void ClearClipRect() {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->ClearClipRect();
}

inline void SetClipRect(float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetClipRect(x, y, w, h);
}
inline void SetClipRect(int x, int y, int w, int h) {
    SetClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}
inline void SetClipRect(const Rect2Df& rect) {
    SetClipRect(rect.x, rect.y, rect.width, rect.height);
}
inline void SetClipRect(const Rect2Di& rect) {
    SetClipRect(rect.x, rect.y, rect.width, rect.height);
}
inline void IntersectClipRect(float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->IntersectClipRect(x, y, w, h);
}
inline void IntersectClipRect(int x, int y, int w, int h) {
    IntersectClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}
inline void IntersectClipRect(const Rect2Df& rect) {
    IntersectClipRect(rect.x, rect.y, rect.width, rect.height);
}
inline void IntersectClipRect(const Rect2Di& rect) {
    IntersectClipRect(rect.x, rect.y, rect.width, rect.height);
}

inline void ResetTransform() {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->ResetTransform();
}

inline void Translate(float x, float y) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->Translate(x, y);
}

inline void Rotate(float angle) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->Rotate(angle);
}

inline void Scale(float sx, float sy) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->Scale(sx, sy);
}

inline void SetGlobalAlpha(float alpha) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) ctx->SetGlobalAlpha(alpha);
}

inline int GetTextWidth(const std::string& text) {
    IRenderContext* ctx = GetRenderContext();
    return ctx ? ctx->GetTextWidth(text) : 0;
}

inline int GetTextHeight(const std::string& text) {
    IRenderContext* ctx = GetRenderContext();
    return ctx ? ctx->GetTextHeight(text) : 0;
}

inline bool MeasureText(const std::string& text, int& w, int& h) {
    w = 0;
    h = 0;
    IRenderContext* ctx = GetRenderContext();
    if (ctx) {
        return ctx->MeasureText(text, w, h);
    }
    return false;
}
inline Point2Di MeasureText(const std::string& text) {
    Point2Di p = {0, 0};
    MeasureText(text, p.x, p.y);
    return p;
}

inline Point2Df CalculateCenteredTextPosition(const std::string& text, const Rect2Df& bounds) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) {
        int txt_w, txt_h;
        ctx->MeasureText(text, txt_w, txt_h);
        return Point2Df(
                bounds.x + (bounds.width - static_cast<float>(txt_w)) / 2,     // Center horizontally
                bounds.y + (bounds.height - static_cast<float>(txt_h)) / 2   // Center vertically (baseline adjusted)
        );
    } else {
        return Point2Df(0, 0);
    }
}

// ===== ALTERNATIVE: USE DRAWTEXT WITH RECTANGLE =====
//inline void DrawCenteredText(const std::string& text, const Rect2Df& bounds) {
//    // Set text style for centering
//    // Draw text in the center of the rectangle
//    Point2Df center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
//    DrawText(text, center);
//}
inline void DrawTextInRect(const std::string& text, float x, float y, float w, float h) {
    IRenderContext* ctx = GetRenderContext();
    if (ctx) {
        ctx->DrawTextInRect(text, x, y, w, h);
    }
}

inline void DrawTextInRect(const std::string& text, const Rect2Di& bounds) {
    DrawTextInRect(text, static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.width), static_cast<float>(bounds.height));
}

inline void DrawTextInRect(const std::string& text, const Rect2Df& bounds) {
    DrawTextInRect(text, bounds.x, bounds.y, bounds.width, bounds.height);
}

// ===== ENHANCED RENDER STATE SCOPED GUARD =====
class RenderStateGuard {
public:
    RenderStateGuard() {
        IRenderContext* ctx = GetRenderContext();
        if (ctx) ctx->PushState();
    }
    
    ~RenderStateGuard() {
        IRenderContext* ctx = GetRenderContext();
        if (ctx) ctx->PopState();
    }
    
    // Prevent copying
    RenderStateGuard(const RenderStateGuard&) = delete;
    RenderStateGuard& operator=(const RenderStateGuard&) = delete;
};

// Enhanced macro for automatic state management
#undef ULTRACANVAS_RENDER_SCOPE
#define ULTRACANVAS_RENDER_SCOPE() RenderStateGuard _renderGuard

    inline void DrawLine(const Point2Df& start, const Point2Df& end, const Color &col) {
        ULTRACANVAS_RENDER_SCOPE();
        SetStrokeColor(col);
        DrawLine(start, end);
    }

    inline void DrawLine(float start_x, float start_y, float end_x, float end_y, const Color &col) {
        ULTRACANVAS_RENDER_SCOPE();
        SetStrokeColor(col);
        DrawLine(start_x, start_y, end_x, end_y);
    }

    inline void DrawLine(int start_x, int start_y, int end_x, int end_y, const Color &col) {
        ULTRACANVAS_RENDER_SCOPE();
        SetStrokeColor(col);
        DrawLine(start_x, start_y, end_x, end_y);
    }

// Draw filled rectangle with border
    inline void DrawFilledRectangle(const Rect2Df& rect, const Color& fillColor,
                               const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f, float borderRadius = 0.0f) {
        ULTRACANVAS_RENDER_SCOPE();

        // Draw fill background
        if (fillColor.a > 0) {
            SetFillColor(fillColor);
            FillRectangle(rect);  // <-- FIXED: Use FillRectangle instead of DrawRect
        }

        // Draw border
        if (borderColor.a > 0 && borderWidth > 0) {
            SetStrokeColor(borderColor);
            SetStrokeWidth(borderWidth);
            DrawRectangle(rect);  // <-- FIXED: Use DrawRectangle for stroke only
        }
    }

    inline void DrawFilledRectangle(const Rect2Di& rect, const Color& fillColor,
        const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f, float borderRadius = 0.0f) {
        DrawFilledRectangle(Rect2Df(rect.x, rect.y, rect.width, rect.height), fillColor, borderColor, borderWidth, borderRadius);
    }

    inline void DrawFilledCircle(const Point2Df& center, float radius, const Color& fillColor) {
        ULTRACANVAS_RENDER_SCOPE();

        if (fillColor.a > 0) {
            SetFillColor(fillColor);
            DrawingStyle style = GetRenderContext()->GetDrawingStyle();
            style.hasStroke = false;
            SetDrawingStyle(style);
            DrawCircle(center, radius);
        }
    }

// Draw text with background
    inline void DrawTextWithBackground(const std::string& text, const Point2Df& position,
                                       const Color& textColor, const Color& backgroundColor = Colors::Transparent) {
        ULTRACANVAS_RENDER_SCOPE();

        if (backgroundColor.a > 0) {
            int txt_w, txt_h;
            GetRenderContext()->MeasureText(text, txt_w, txt_h);
            DrawFilledRectangle(Rect2Df(position.x, position.y, txt_w, txt_h), backgroundColor);
        }

        SetTextColor(textColor);
        DrawText(text, position);
    }

// Draw gradient rectangle
    inline void DrawGradientRect(const Rect2Df& rect, const Color& startColor, const Color& endColor,
                                 bool horizontal = true) {
        ULTRACANVAS_RENDER_SCOPE();

        DrawingStyle style = GetRenderContext()->GetDrawingStyle();
        style.fillMode = FillMode::Gradient;
        style.fillGradient.type = GradientType::Linear;
        style.fillGradient.startPoint = Point2Df(rect.x, rect.y);
        style.fillGradient.endPoint = horizontal ?
                                      Point2Df(rect.x + rect.width, rect.y) :
                                      Point2Df(rect.x, rect.y + rect.height);
        style.fillGradient.stops.clear();
        style.fillGradient.stops.emplace_back(0.0f, startColor);
        style.fillGradient.stops.emplace_back(1.0f, endColor);
        style.hasStroke = false;

        SetDrawingStyle(style);
        DrawRectangle(rect);
    }

// Draw shadow
    inline void DrawShadow(const Rect2Df& rect, const Color& shadowColor = Color(0, 0, 0, 64),
                           const Point2Df& offset = Point2Df(2, 2)) {
        ULTRACANVAS_RENDER_SCOPE();

        Rect2Df shadowRect(rect.x + offset.x, rect.y + offset.y, rect.width, rect.height);
        SetFillColor(shadowColor);
        DrawingStyle style = GetRenderContext()->GetDrawingStyle();
        style.hasStroke = false;
        SetDrawingStyle(style);
        DrawRectangle(shadowRect);
    }
    inline void DrawShadow(const Rect2Di& rect, const Color& shadowColor = Color(0, 0, 0, 64),
                           const Point2Di& offset = Point2Di(2, 2)) {
        DrawShadow(Rect2Df(rect.x, rect.y, rect.width, rect.height),
                   shadowColor, Point2Df (offset.x, offset.y));
    }

} // namespace UltraCanvas