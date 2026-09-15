// UltraCanvasVectorRenderer.h
// Vector Graphics Rendering for UltraCanvas
// Version: 2.1.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
//
// REFACTORED: Removed IVectorRenderer, IVectorVisitor, SoftwareVectorRenderer,
// HardwareVectorRenderer, CairoVectorRenderer, VectorRendererFactory.
// Single VectorRenderer class using IRenderContext.
#pragma once

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasRenderContext.h"
#include <stack>
#include <memory>
#include <chrono>

namespace UltraCanvas {

    using namespace VectorStorage;

// ===== RENDER OPTIONS =====

    struct VectorRenderOptions {
        bool EnableAntialiasing = true;
        float CurveTolerance = 0.25f;
        Rect2Dd ViewportBounds;
        bool ClipToViewport = true;
        bool EnableCulling = true;
        bool RenderInvisibleElements = false;
        float PixelRatio = 1.0f;
        bool ShowBoundingBoxes = false;
        Color DebugColor = Color(255, 0, 255, 128);
    };

// ===== RENDER STATISTICS =====

    struct VectorRenderStats {
        uint32_t ElementsRendered = 0;
        uint32_t ElementsCulled = 0;
        uint32_t PathCommandsProcessed = 0;
        double RenderTimeMs = 0.0;
        void Reset() { ElementsRendered=0; ElementsCulled=0; PathCommandsProcessed=0; RenderTimeMs=0; }
    };

// ===== VECTOR RENDERER =====

    class VectorRenderer {
        friend bool BuildVectorElementOutline(IRenderContext* ctx, const VectorElement& element);
    public:
        VectorRenderer();
        ~VectorRenderer();

        void RenderDocument(IRenderContext* ctx, const VectorDocument& document);
        void RenderElement(IRenderContext* ctx, const VectorElement& element);
        void RenderLayer(IRenderContext* ctx, const VectorLayer& layer);

        void SetOptions(const VectorRenderOptions& opts) { options = opts; }
        const VectorRenderOptions& GetOptions() const { return options; }
        const VectorRenderStats& GetStats() const { return stats; }
        void ClearCaches();

    private:
        IRenderContext* ctx = nullptr;
        VectorRenderOptions options;
        VectorRenderStats stats;
        std::stack<float> opacityStack;
        float currentOpacity = 1.0f;
        const VectorDocument* currentDocument = nullptr;

        // Puts the element's outline on the context (rect, rounded rect,
        // circle, ellipse, line, polyline, polygon, path); false for kinds
        // without one. Shared by drawing, clipping and - later - hit testing.
        bool BuildElementPath(const VectorElement& element);
        void RenderShape(const VectorElement& element);
        void FillAndStroke(const VectorStyle& style);
        void RenderLine(const VectorLine& line);
        void RenderText(const VectorText& text);
        void RenderImage(const VectorImage& image);
        void RenderGroup(const VectorGroup& group);
        void RenderUse(const VectorUse& use);

        void ApplyStyle(const VectorStyle& style);
        void ApplyClip(const std::string& clipId);
        // `bounds` is the outline's extents in the element's own space - what
        // an objectBoundingBox gradient resolves against; `opacity` is the
        // fill-opacity / stroke-opacity folded into the paint's alpha.
        void ApplyFill(const FillData& fill, const Rect2Dd& bounds, float opacity);
        void ApplyStroke(const StrokeData& stroke, const Rect2Dd& bounds, float opacity);
        void ApplyTransform(const Matrix3x3& transform);

        void SetupGradient(const GradientData& gradient, const Rect2Dd& bounds, float opacity, bool forStroke);
        std::shared_ptr<IPaintPattern> MakeLinearGradient(const LinearGradientData& grad, const Rect2Dd& bounds, float opacity);
        std::shared_ptr<IPaintPattern> MakeRadialGradient(const RadialGradientData& grad, const Rect2Dd& bounds, float opacity);

        void BuildPath(const PathData& pathData);
        bool IsVisible(const VectorElement& element) const;
        bool IsInViewport(const Rect2Dd& bounds) const;
        void RenderDebugBounds(const Rect2Dd& bounds);
    };

// Utility functions
    // Puts the element's outline on the context in the element's own space
    // (rect, rounded rect, circle, ellipse, line, polyline, polygon, path);
    // false for kinds without one. What the renderer fills, the hit tester
    // asks IsPointInFill / IsPointInStroke about, and a clip is built from.
    bool BuildVectorElementOutline(IRenderContext* ctx, const VectorElement& element);
    bool HitTestElement(const VectorElement& element, const Point2Dd& point);
    std::vector<const VectorElement*> HitTestDocument(const VectorDocument& document, const Point2Dd& point);
    Rect2Dd CalculateDocumentBounds(const VectorDocument& document);

} // namespace UltraCanvas