// UltraCanvasVectorRenderer.h
// Vector Graphics Rendering for UltraCanvas
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
//
// REFACTORED: Removed IVectorRenderer, IVectorVisitor, SoftwareVectorRenderer,
// HardwareVectorRenderer, CairoVectorRenderer, VectorRendererFactory.
// Single VectorRenderer class using IRenderContext.
#pragma once

#include "UltraCanvasVectorStorage.h"
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
        Rect2Df ViewportBounds;
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

        void RenderRect(const VectorRect& rect);
        void RenderCircle(const VectorCircle& circle);
        void RenderEllipse(const VectorEllipse& ellipse);
        void RenderLine(const VectorLine& line);
        void RenderPolyline(const VectorPolyline& polyline);
        void RenderPolygon(const VectorPolygon& polygon);
        void RenderPath(const VectorPath& path);
        void RenderText(const VectorText& text);
        void RenderImage(const VectorImage& image);
        void RenderGroup(const VectorGroup& group);
        void RenderUse(const VectorUse& use);

        void ApplyStyle(const VectorStyle& style);
        void ApplyFill(const FillData& fill);
        void ApplyStroke(const StrokeData& stroke);
        void ApplyTransform(const Matrix3x3& transform);

        void SetupGradient(const GradientData& gradient, const Rect2Df& bounds);
        void SetupLinearGradient(const LinearGradientData& grad, const Rect2Df& bounds);
        void SetupRadialGradient(const RadialGradientData& grad, const Rect2Df& bounds);

        void BuildPath(const PathData& pathData);
        bool IsVisible(const VectorElement& element) const;
        bool IsInViewport(const Rect2Df& bounds) const;
        void RenderDebugBounds(const Rect2Df& bounds);
    };

// Utility functions
    bool HitTestElement(const VectorElement& element, const Point2Df& point);
    std::vector<const VectorElement*> HitTestDocument(const VectorDocument& document, const Point2Df& point);
    Rect2Df CalculateDocumentBounds(const VectorDocument& document);

} // namespace UltraCanvas