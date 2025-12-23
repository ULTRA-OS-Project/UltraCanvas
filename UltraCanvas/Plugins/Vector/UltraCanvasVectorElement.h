// UltraCanvas/include/UltraCanvasVectorElement.h
// UI Element for Vector Document Display and Interaction
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorRenderer.h"
#include "UltraCanvasVectorConverter.h"
#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace UltraCanvas {

// ===== VECTOR ELEMENT ENUMERATIONS =====

    enum class VectorScaleMode {
        NoneMode,           // No scaling - render at original size
        Fit,            // Fit entire document within bounds (preserve aspect ratio)
        Fill,           // Fill bounds completely (may crop)
        Stretch,        // Stretch to fill bounds (may distort)
        FitWidth,       // Fit to width, allow vertical overflow/crop
        FitHeight       // Fit to height, allow horizontal overflow/crop
    };

    enum class VectorAlignment {
        TopLeft,
        TopCenter,
        TopRight,
        CenterLeft,
        Center,
        CenterRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    enum class VectorInteractionMode {
        NoneMode,           // No interaction
        Pan,            // Allow panning
        Zoom,           // Allow zooming
        PanZoom,        // Allow both pan and zoom
        Select          // Allow element selection
    };

// ===== VECTOR ELEMENT STATE =====

    struct VectorElementState {
        bool IsLoading = false;
        bool HasError = false;
        bool IsAnimating = false;
        bool IsDirty = true;
        std::string ErrorMessage;
        float LoadProgress = 0.0f;
    };

// ===== VECTOR ELEMENT OPTIONS =====

    struct VectorElementOptions {
        // Rendering
        VectorScaleMode ScaleMode = VectorScaleMode::Fit;
        VectorAlignment Alignment = VectorAlignment::Center;
        bool EnableAntialiasing = true;
        bool EnableCaching = true;
        float Quality = 1.0f;  // 0.0 to 1.0

        // Interaction
        VectorInteractionMode InteractionMode = VectorInteractionMode::NoneMode;
        float MinZoom = 0.1f;
        float MaxZoom = 10.0f;
        float ZoomStep = 0.1f;
        bool EnableMouseWheel = true;

        // Appearance
        Color BackgroundColor = Colors::Transparent;
        bool ShowBorder = false;
        Color BorderColor = Color(200, 200, 200, 255);
        float BorderWidth = 1.0f;

        // Debug
        bool ShowDebugInfo = false;
        bool ShowBoundingBoxes = false;
    };

// ===== VECTOR ELEMENT CALLBACKS =====

    using VectorLoadCallback = std::function<void(bool success, const std::string& message)>;
    using VectorRenderCallback = std::function<void(double renderTimeMs)>;
    using VectorSelectionCallback = std::function<void(const std::string& elementId)>;
    using VectorZoomCallback = std::function<void(float zoom)>;
    using VectorPanCallback = std::function<void(float panX, float panY)>;

// ===== ULTRACANVAS VECTOR ELEMENT CLASS =====

    class UltraCanvasVectorElement : public UltraCanvasUIElement {
    private:
        // Document and rendering
        std::shared_ptr<VectorStorage::VectorDocument> document;
        std::unique_ptr<VectorRenderer> renderer;

        // View state
        float zoomLevel = 1.0f;
        Point2Df panOffset{0.0f, 0.0f};
        VectorStorage::Matrix3x3 viewTransform;

        // Interaction state
        bool isPanning = false;
        Point2Di lastMousePos{0, 0};
        Point2Di dragStartPos{0, 0};
        std::string selectedElementId;
        std::string hoveredElementId;

        // Options and state
        VectorElementOptions options;
        VectorElementState state;

        // Callbacks
        VectorLoadCallback onLoad;
        VectorRenderCallback onRender;
        VectorSelectionCallback onSelection;
        VectorZoomCallback onZoomChange;
        VectorPanCallback onPanChange;

        // Source info
        std::string sourceFilePath;
        VectorConverter::VectorFormat sourceFormat = VectorConverter::VectorFormat::Unknown;

        // Rendering cache
        bool cacheValid = false;
        std::chrono::steady_clock::time_point lastRenderTime;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasVectorElement(
                const std::string& identifier = "VectorElement",
                int x = 0, int y = 0,
                int width = 400, int height = 300);

        virtual ~UltraCanvasVectorElement();

        // ===== DOCUMENT LOADING =====

        // Load from file (auto-detect format)
        bool LoadFromFile(const std::string& filePath);

        // Load from file with explicit format
        bool LoadFromFile(const std::string& filePath, VectorConverter::VectorFormat format);

        // Load from memory/string
        bool LoadFromString(const std::string& data, VectorConverter::VectorFormat format);

        // Load from stream
        bool LoadFromStream(std::istream& stream, VectorConverter::VectorFormat format);

        // Set document directly
        void SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc);

        // Get current document
        std::shared_ptr<VectorStorage::VectorDocument> GetDocument() const { return document; }

        // Clear document
        void ClearDocument();

        // Reload from original source
        bool Reload();

        // ===== EXPORT =====

        bool ExportToFile(const std::string& filePath, VectorConverter::VectorFormat format);
        std::string ExportToString(VectorConverter::VectorFormat format);

        // ===== VIEW CONTROL =====

        // Zoom
        void SetZoom(float zoom);
        float GetZoom() const { return zoomLevel; }
        void ZoomIn();
        void ZoomOut();
        void ZoomToFit();
        void ZoomToActualSize();
        void ZoomToSelection();

        // Pan
        void SetPan(float x, float y);
        Point2Df GetPan() const { return panOffset; }
        void Pan(float dx, float dy);
        void CenterDocument();
        void CenterOnPoint(float x, float y);

        // Combined view
        void ResetView();
        void SetViewTransform(const VectorStorage::Matrix3x3& transform);
        VectorStorage::Matrix3x3 GetViewTransform() const { return viewTransform; }

        // ===== OPTIONS =====

        void SetOptions(const VectorElementOptions& opts);
        const VectorElementOptions& GetOptions() const { return options; }

        void SetScaleMode(VectorScaleMode mode);
        VectorScaleMode GetScaleMode() const { return options.ScaleMode; }

        void SetAlignment(VectorAlignment align);
        VectorAlignment GetAlignment() const { return options.Alignment; }

        void SetInteractionMode(VectorInteractionMode mode);
        VectorInteractionMode GetInteractionMode() const { return options.InteractionMode; }

        void SetBackgroundColor(const Color& color);
        Color GetBackgroundColor() const { return options.BackgroundColor; }

        void EnableAntialiasing(bool enable);
        bool IsAntialiasingEnabled() const { return options.EnableAntialiasing; }

        void EnableCaching(bool enable);
        bool IsCachingEnabled() const { return options.EnableCaching; }

        // ===== STATE =====

        const VectorElementState& GetState() const { return state; }
        bool IsLoading() const { return state.IsLoading; }
        bool HasError() const { return state.HasError; }
        std::string GetErrorMessage() const { return state.ErrorMessage; }
        bool HasDocument() const { return document != nullptr; }

        // ===== ELEMENT SELECTION =====

        std::string GetSelectedElementId() const { return selectedElementId; }
        void SelectElement(const std::string& elementId);
        void ClearSelection();

        std::shared_ptr<VectorStorage::VectorElement> GetSelectedElement() const;
        std::shared_ptr<VectorStorage::VectorElement> GetElementAt(int x, int y) const;

        // ===== COORDINATE CONVERSION =====

        // Screen coordinates to document coordinates
        Point2Df ScreenToDocument(int screenX, int screenY) const;
        Point2Df ScreenToDocument(const Point2Di& screenPos) const;

        // Document coordinates to screen coordinates
        Point2Di DocumentToScreen(float docX, float docY) const;
        Point2Di DocumentToScreen(const Point2Df& docPos) const;

        // ===== CALLBACKS =====

        void SetOnLoadCallback(VectorLoadCallback callback) { onLoad = callback; }
        void SetOnRenderCallback(VectorRenderCallback callback) { onRender = callback; }
        void SetOnSelectionCallback(VectorSelectionCallback callback) { onSelection = callback; }
        void SetOnZoomChangeCallback(VectorZoomCallback callback) { onZoomChange = callback; }
        void SetOnPanChangeCallback(VectorPanCallback callback) { onPanChange = callback; }

        // ===== DOCUMENT INFO =====

        Size2Df GetDocumentSize() const;
        Rect2Df GetDocumentViewBox() const;
        std::string GetDocumentTitle() const;
        std::string GetDocumentDescription() const;
        size_t GetLayerCount() const;
        std::vector<std::string> GetLayerNames() const;

        // ===== LAYER VISIBILITY =====

        void SetLayerVisible(const std::string& layerName, bool visible);
        bool IsLayerVisible(const std::string& layerName) const;
        void ShowAllLayers();
        void HideAllLayers();

        // ===== RENDERING (Override) =====

        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING (Override) =====

        bool OnEvent(const UCEvent& event) override;

        // ===== RENDERER ACCESS =====

        VectorRenderer* GetRenderer() const { return renderer.get(); }
        void InvalidateCache();

    protected:
        // ===== INTERNAL METHODS =====

        void UpdateViewTransform();
        void CalculateScaling(float& scaleX, float& scaleY) const;
        Point2Df CalculateOffset() const;

        void RenderBackground(IRenderContext* ctx);
        void RenderDocument(IRenderContext* ctx);
        void RenderBorder(IRenderContext* ctx);
        void RenderDebugInfo(IRenderContext* ctx);
        void RenderLoadingIndicator(IRenderContext* ctx);
        void RenderErrorState(IRenderContext* ctx);

        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseWheel(const UCEvent& event);
        bool HandleKeyPress(const UCEvent& event);

        void SetError(const std::string& message);
        void ClearError();

        void NotifyZoomChanged();
        void NotifyPanChanged();

        // Hit testing
        std::string HitTest(int x, int y) const;
        std::string HitTestGroup(const VectorStorage::VectorGroup& group, const Point2Df& point) const;

        // Selection rendering
        void RenderSelectionHighlight(IRenderContext* ctx,
                                      const std::shared_ptr<VectorStorage::VectorElement>& element);
        void RenderHoverHighlight(IRenderContext* ctx,
                                  const std::shared_ptr<VectorStorage::VectorElement>& element);
    };

// ===== FACTORY HELPER =====

    inline std::shared_ptr<UltraCanvasVectorElement> CreateVectorElement(
            const std::string& identifier = "VectorElement",
            int x = 0, int y = 0,
            int width = 400, int height = 300) {
        return std::make_shared<UltraCanvasVectorElement>(identifier, x, y, width, height);
    }

    inline std::shared_ptr<UltraCanvasVectorElement> CreateVectorElementFromFile(
            const std::string& filePath,
            const std::string& identifier = "VectorElement",
            int x = 0, int y = 0,
            int width = 400, int height = 300) {
        auto element = std::make_shared<UltraCanvasVectorElement>(identifier, x, y, width, height);
        element->LoadFromFile(filePath);
        return element;
    }

} // namespace UltraCanvas