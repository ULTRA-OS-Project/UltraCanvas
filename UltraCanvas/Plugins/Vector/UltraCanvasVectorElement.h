// UltraCanvasVectorElement.h
// UI Element for Vector Document Display and Interaction
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorRenderer.h"
#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace UltraCanvas {

    enum class VectorScaleMode { NoScale, Fit, Fill, Stretch, FitWidth, FitHeight };
    enum class VectorAlignment { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight };
    enum class VectorInteractionMode { NoInteraction, Pan, Zoom, PanZoom, Select };

    struct VectorElementState {
        bool IsLoading = false;
        bool HasError = false;
        bool IsDirty = true;
        std::string ErrorMessage;
        float LoadProgress = 0.0f;
    };

    struct VectorElementOptions {
        VectorScaleMode ScaleMode = VectorScaleMode::Fit;
        VectorAlignment Alignment = VectorAlignment::Center;
        bool EnableAntialiasing = true;
        VectorInteractionMode InteractionMode = VectorInteractionMode::NoInteraction;
        float MinZoom = 0.1f;
        float MaxZoom = 10.0f;
        float ZoomStep = 0.1f;
        bool EnableMouseWheel = true;
        Color BackgroundColor = Colors::Transparent;
        bool ShowBorder = false;
        Color BorderColor = Color(200, 200, 200, 255);
        float BorderWidth = 1.0f;
        bool ShowDebugInfo = false;
    };

    using VectorLoadCallback = std::function<void(bool success, const std::string& message)>;
    using VectorRenderCallback = std::function<void(double renderTimeMs)>;
    using VectorSelectionCallback = std::function<void(const std::string& elementId)>;
    using VectorZoomCallback = std::function<void(float zoom)>;
    using VectorPanCallback = std::function<void(float panX, float panY)>;

    class UltraCanvasVectorElement : public UltraCanvasUIElement {
    private:
        std::shared_ptr<VectorStorage::VectorDocument> document;
        std::unique_ptr<VectorRenderer> renderer;
        float zoomLevel = 1.0f;
        Point2Df panOffset{0.0f, 0.0f};
        VectorStorage::Matrix3x3 viewTransform;
        bool isPanning = false;
        Point2Di lastMousePos{0, 0};
        std::string selectedElementId;
        std::string hoveredElementId;
        VectorElementOptions options;
        VectorElementState state;
        VectorLoadCallback onLoad;
        VectorRenderCallback onRender;
        VectorSelectionCallback onSelection;
        VectorZoomCallback onZoomChange;
        VectorPanCallback onPanChange;
        std::string sourceFilePath;

    public:
        UltraCanvasVectorElement(const std::string& identifier = "VectorElement", int x = 0, int y = 0, int width = 400, int height = 300);
        virtual ~UltraCanvasVectorElement();

        void SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc);
        std::shared_ptr<VectorStorage::VectorDocument> GetDocument() const { return document; }
        void ClearDocument();
        bool HasDocument() const { return document != nullptr; }

        void SetZoom(float zoom);
        float GetZoom() const { return zoomLevel; }
        void ZoomIn();
        void ZoomOut();
        void ZoomToFit();
        void ZoomToActualSize();

        void SetPan(float x, float y);
        Point2Df GetPan() const { return panOffset; }
        void Pan(float dx, float dy);
        void CenterDocument();
        void ResetView();

        void SetOptions(const VectorElementOptions& opts);
        const VectorElementOptions& GetOptions() const { return options; }
        void SetScaleMode(VectorScaleMode mode);
        void SetAlignment(VectorAlignment align);
        void SetInteractionMode(VectorInteractionMode mode);
        void SetBackgroundColor(const Color& color);

        const VectorElementState& GetState() const { return state; }
        bool IsLoading() const { return state.IsLoading; }
        bool HasError() const { return state.HasError; }
        std::string GetErrorMessage() const { return state.ErrorMessage; }

        std::string GetSelectedElementId() const { return selectedElementId; }
        void SelectElement(const std::string& elementId);
        void ClearSelection();
        std::shared_ptr<VectorStorage::VectorElement> GetSelectedElement() const;

        Point2Df ScreenToDocument(int screenX, int screenY) const;
        Point2Di DocumentToScreen(float docX, float docY) const;

        void SetOnLoadCallback(VectorLoadCallback cb) { onLoad = cb; }
        void SetOnRenderCallback(VectorRenderCallback cb) { onRender = cb; }
        void SetOnSelectionCallback(VectorSelectionCallback cb) { onSelection = cb; }
        void SetOnZoomChangeCallback(VectorZoomCallback cb) { onZoomChange = cb; }
        void SetOnPanChangeCallback(VectorPanCallback cb) { onPanChange = cb; }

        Size2Df GetDocumentSize() const;
        Rect2Df GetDocumentViewBox() const;
        size_t GetLayerCount() const;
        std::vector<std::string> GetLayerNames() const;
        void SetLayerVisible(const std::string& layerName, bool visible);
        bool IsLayerVisible(const std::string& layerName) const;

        void Render(IRenderContext* ctx) override;
        bool OnEvent(const UCEvent& event) override;
        VectorRenderer* GetRenderer() const { return renderer.get(); }

    protected:
        void UpdateViewTransform();
        void RenderBackground(IRenderContext* ctx);
        void RenderDocument(IRenderContext* ctx);
        void RenderBorder(IRenderContext* ctx);
        void RenderDebugInfo(IRenderContext* ctx);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        void SetError(const std::string& message);
        void ClearError();
        std::string HitTest(int x, int y) const;
    };

    inline std::shared_ptr<UltraCanvasVectorElement> CreateVectorElement(
            const std::string& identifier = "VectorElement", int x = 0, int y = 0, int width = 400, int height = 300) {
        return std::make_shared<UltraCanvasVectorElement>(identifier, x, y, width, height);
    }

} // namespace UltraCanvas