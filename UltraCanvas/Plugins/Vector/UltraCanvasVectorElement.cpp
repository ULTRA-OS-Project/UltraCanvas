// UltraCanvas/core/UltraCanvasVectorElement.cpp
// UI Element for Vector Document Display and Interaction
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorElement.h"
#include "UltraCanvasVectorConverter.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    using namespace VectorStorage;
    using namespace VectorConverter;

// ===== CONSTRUCTOR / DESTRUCTOR =====

    UltraCanvasVectorElement::UltraCanvasVectorElement(
            const std::string& identifier,
            int x, int y, int width, int height)
            : UltraCanvasUIElement(identifier, x, y, width, height)
            , renderer(std::make_unique<VectorRenderer>())
            , zoomLevel(1.0f)
            , panOffset{0.0f, 0.0f}
            , viewTransform(Matrix3x3::Identity())
    {
        // Set default mouse pointer
        mousePtr = MousePointer::Default;

        // Initialize view transform
        UpdateViewTransform();
    }

    UltraCanvasVectorElement::~UltraCanvasVectorElement() = default;

// ===== DOCUMENT LOADING =====

    bool UltraCanvasVectorElement::LoadFromFile(const std::string& filePath) {
        // Auto-detect format from extension
        VectorFormat format = DetectFormatFromExtension(filePath);
        return LoadFromFile(filePath, format);
    }

    bool UltraCanvasVectorElement::LoadFromFile(const std::string& filePath, VectorFormat format) {
        state.IsLoading = true;
        state.HasError = false;
        state.ErrorMessage.clear();
        state.LoadProgress = 0.0f;
        InvalidateCache();
        RequestRedraw();

        try {
            // Get appropriate converter
            auto converter = CreateConverter(format);
            if (!converter) {
                SetError("No converter available for format: " + std::to_string(static_cast<int>(format)));
                if (onLoad) onLoad(false, state.ErrorMessage);
                return false;
            }

            // Check if file exists
            std::ifstream file(filePath);
            if (!file.is_open()) {
                SetError("Failed to open file: " + filePath);
                if (onLoad) onLoad(false, state.ErrorMessage);
                return false;
            }
            file.close();

            // Set up progress callback
            ConversionOptions convOptions;
            convOptions.ProgressCallback = [this](float progress) {
                state.LoadProgress = progress;
                RequestRedraw();
            };

            // Import document
            document = converter->Import(filePath, convOptions);

            if (!document) {
                SetError("Failed to parse vector document");
                if (onLoad) onLoad(false, state.ErrorMessage);
                return false;
            }

            // Store source info
            sourceFilePath = filePath;
            sourceFormat = format;

            // Initialize view
            ZoomToFit();

            state.IsLoading = false;
            state.IsDirty = true;
            state.LoadProgress = 1.0f;

            if (onLoad) onLoad(true, "");
            RequestRedraw();

            return true;

        } catch (const std::exception& e) {
            SetError(std::string("Exception loading file: ") + e.what());
            if (onLoad) onLoad(false, state.ErrorMessage);
            return false;
        }
    }

    bool UltraCanvasVectorElement::LoadFromString(const std::string& data, VectorFormat format) {
        state.IsLoading = true;
        state.HasError = false;
        state.ErrorMessage.clear();
        InvalidateCache();

        try {
            auto converter = CreateConverter(format);
            if (!converter) {
                SetError("No converter available for format");
                if (onLoad) onLoad(false, state.ErrorMessage);
                return false;
            }

            document = converter->ImportFromString(data, ConversionOptions());

            if (!document) {
                SetError("Failed to parse vector data");
                if (onLoad) onLoad(false, state.ErrorMessage);
                return false;
            }

            sourceFilePath.clear();
            sourceFormat = format;

            ZoomToFit();

            state.IsLoading = false;
            state.IsDirty = true;

            if (onLoad) onLoad(true, "");
            RequestRedraw();

            return true;

        } catch (const std::exception& e) {
            SetError(std::string("Exception parsing data: ") + e.what());
            if (onLoad) onLoad(false, state.ErrorMessage);
            return false;
        }
    }

    bool UltraCanvasVectorElement::LoadFromStream(std::istream& stream, VectorFormat format) {
        std::ostringstream ss;
        ss << stream.rdbuf();
        return LoadFromString(ss.str(), format);
    }

    void UltraCanvasVectorElement::SetDocument(std::shared_ptr<VectorDocument> doc) {
        document = doc;
        sourceFilePath.clear();
        sourceFormat = VectorFormat::Unknown;

        InvalidateCache();
        ClearError();

        if (document) {
            ZoomToFit();
        }

        state.IsDirty = true;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ClearDocument() {
        document.reset();
        sourceFilePath.clear();
        sourceFormat = VectorFormat::Unknown;
        selectedElementId.clear();
        hoveredElementId.clear();

        InvalidateCache();
        ClearError();
        ResetView();

        RequestRedraw();
    }

    bool UltraCanvasVectorElement::Reload() {
        if (sourceFilePath.empty()) {
            return false;
        }
        return LoadFromFile(sourceFilePath, sourceFormat);
    }

// ===== EXPORT =====

    bool UltraCanvasVectorElement::ExportToFile(const std::string& filePath, VectorFormat format) {
        if (!document) return false;

        auto converter = CreateConverter(format);
        if (!converter) return false;

        return converter->Export(*document, filePath, ConversionOptions());
    }

    std::string UltraCanvasVectorElement::ExportToString(VectorFormat format) {
        if (!document) return "";

        auto converter = CreateConverter(format);
        if (!converter) return "";

        return converter->ExportToString(*document, ConversionOptions());
    }

// ===== VIEW CONTROL =====

    void UltraCanvasVectorElement::SetZoom(float zoom) {
        zoom = std::clamp(zoom, options.MinZoom, options.MaxZoom);

        if (std::abs(zoomLevel - zoom) > 0.0001f) {
            zoomLevel = zoom;
            UpdateViewTransform();
            InvalidateCache();
            NotifyZoomChanged();
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::ZoomIn() {
        SetZoom(zoomLevel * (1.0f + options.ZoomStep));
    }

    void UltraCanvasVectorElement::ZoomOut() {
        SetZoom(zoomLevel / (1.0f + options.ZoomStep));
    }

    void UltraCanvasVectorElement::ZoomToFit() {
        if (!document) {
            zoomLevel = 1.0f;
            panOffset = {0.0f, 0.0f};
            UpdateViewTransform();
            return;
        }

        Rect2Df docBounds = document->ViewBox;
        if (docBounds.width <= 0 || docBounds.height <= 0) {
            docBounds = document->GetBoundingBox();
        }

        if (docBounds.width <= 0 || docBounds.height <= 0) {
            zoomLevel = 1.0f;
            panOffset = {0.0f, 0.0f};
            UpdateViewTransform();
            return;
        }

        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        float scaleX = elementWidth / docBounds.width;
        float scaleY = elementHeight / docBounds.height;

        zoomLevel = std::min(scaleX, scaleY);
        zoomLevel = std::clamp(zoomLevel, options.MinZoom, options.MaxZoom);

        // Center the document
        panOffset.x = (elementWidth - docBounds.width * zoomLevel) / 2.0f - docBounds.x * zoomLevel;
        panOffset.y = (elementHeight - docBounds.height * zoomLevel) / 2.0f - docBounds.y * zoomLevel;

        UpdateViewTransform();
        InvalidateCache();
        NotifyZoomChanged();
        NotifyPanChanged();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ZoomToActualSize() {
        SetZoom(1.0f);
        CenterDocument();
    }

    void UltraCanvasVectorElement::ZoomToSelection() {
        if (selectedElementId.empty() || !document) return;

        auto element = document->FindElementById(selectedElementId);
        if (!element) return;

        Rect2Df bounds = element->GetBoundingBox();
        if (bounds.width <= 0 || bounds.height <= 0) return;

        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        // Add padding
        float padding = 20.0f;
        float scaleX = (elementWidth - 2 * padding) / bounds.width;
        float scaleY = (elementHeight - 2 * padding) / bounds.height;

        zoomLevel = std::min(scaleX, scaleY);
        zoomLevel = std::clamp(zoomLevel, options.MinZoom, options.MaxZoom);

        CenterOnPoint(bounds.x + bounds.width / 2.0f, bounds.y + bounds.height / 2.0f);
    }

    void UltraCanvasVectorElement::SetPan(float x, float y) {
        if (std::abs(panOffset.x - x) > 0.01f || std::abs(panOffset.y - y) > 0.01f) {
            panOffset.x = x;
            panOffset.y = y;
            UpdateViewTransform();
            InvalidateCache();
            NotifyPanChanged();
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::Pan(float dx, float dy) {
        SetPan(panOffset.x + dx, panOffset.y + dy);
    }

    void UltraCanvasVectorElement::CenterDocument() {
        if (!document) return;

        Rect2Df docBounds = document->ViewBox;
        if (docBounds.width <= 0 || docBounds.height <= 0) {
            docBounds = document->GetBoundingBox();
        }

        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        panOffset.x = (elementWidth - docBounds.width * zoomLevel) / 2.0f - docBounds.x * zoomLevel;
        panOffset.y = (elementHeight - docBounds.height * zoomLevel) / 2.0f - docBounds.y * zoomLevel;

        UpdateViewTransform();
        InvalidateCache();
        NotifyPanChanged();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::CenterOnPoint(float x, float y) {
        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        panOffset.x = elementWidth / 2.0f - x * zoomLevel;
        panOffset.y = elementHeight / 2.0f - y * zoomLevel;

        UpdateViewTransform();
        InvalidateCache();
        NotifyPanChanged();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ResetView() {
        zoomLevel = 1.0f;
        panOffset = {0.0f, 0.0f};
        viewTransform = Matrix3x3::Identity();

        if (document) {
            ZoomToFit();
        } else {
            UpdateViewTransform();
            InvalidateCache();
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::SetViewTransform(const Matrix3x3& transform) {
        viewTransform = transform;
        InvalidateCache();
        RequestRedraw();
    }

// ===== OPTIONS =====

    void UltraCanvasVectorElement::SetOptions(const VectorElementOptions& opts) {
        options = opts;
        UpdateViewTransform();
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::SetScaleMode(VectorScaleMode mode) {
        if (options.ScaleMode != mode) {
            options.ScaleMode = mode;
            UpdateViewTransform();
            InvalidateCache();
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::SetAlignment(VectorAlignment align) {
        if (options.Alignment != align) {
            options.Alignment = align;
            UpdateViewTransform();
            InvalidateCache();
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::SetInteractionMode(VectorInteractionMode mode) {
        options.InteractionMode = mode;

        // Update cursor based on mode
        switch (mode) {
            case VectorInteractionMode::Pan:
            case VectorInteractionMode::PanZoom:
                mousePtr = MousePointer::Move;
                break;
            case VectorInteractionMode::Select:
                mousePtr = MousePointer::Default;
                break;
            default:
                mousePtr = MousePointer::Default;
                break;
        }
    }

    void UltraCanvasVectorElement::SetBackgroundColor(const Color& color) {
        options.BackgroundColor = color;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::EnableAntialiasing(bool enable) {
        options.EnableAntialiasing = enable;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::EnableCaching(bool enable) {
        options.EnableCaching = enable;
        if (!enable) {
            InvalidateCache();
        }
    }

// ===== ELEMENT SELECTION =====

    void UltraCanvasVectorElement::SelectElement(const std::string& elementId) {
        if (selectedElementId != elementId) {
            selectedElementId = elementId;
            if (onSelection) {
                onSelection(elementId);
            }
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::ClearSelection() {
        if (!selectedElementId.empty()) {
            selectedElementId.clear();
            if (onSelection) {
                onSelection("");
            }
            RequestRedraw();
        }
    }

    std::shared_ptr<VectorElement> UltraCanvasVectorElement::GetSelectedElement() const {
        if (selectedElementId.empty() || !document) return nullptr;
        return document->FindElementById(selectedElementId);
    }

    std::shared_ptr<VectorElement> UltraCanvasVectorElement::GetElementAt(int x, int y) const {
        if (!document) return nullptr;

        Point2Df docPoint = ScreenToDocument(x, y);

        // TODO: Implement proper hit testing through document layers
        // This requires traversing the element tree and checking bounds
        return nullptr;
    }

// ===== COORDINATE CONVERSION =====

    Point2Df UltraCanvasVectorElement::ScreenToDocument(int screenX, int screenY) const {
        // Convert screen coordinates to local element coordinates
        float localX = static_cast<float>(screenX - GetX());
        float localY = static_cast<float>(screenY - GetY());

        // Apply inverse view transform
        float docX = (localX - panOffset.x) / zoomLevel;
        float docY = (localY - panOffset.y) / zoomLevel;

        return Point2Df{docX, docY};
    }

    Point2Df UltraCanvasVectorElement::ScreenToDocument(const Point2Di& screenPos) const {
        return ScreenToDocument(screenPos.x, screenPos.y);
    }

    Point2Di UltraCanvasVectorElement::DocumentToScreen(float docX, float docY) const {
        float screenX = docX * zoomLevel + panOffset.x + GetX();
        float screenY = docY * zoomLevel + panOffset.y + GetY();

        return Point2Di{static_cast<int>(screenX), static_cast<int>(screenY)};
    }

    Point2Di UltraCanvasVectorElement::DocumentToScreen(const Point2Df& docPos) const {
        return DocumentToScreen(docPos.x, docPos.y);
    }

// ===== DOCUMENT INFO =====

    Size2Df UltraCanvasVectorElement::GetDocumentSize() const {
        if (!document) return Size2Df{0, 0};
        return document->Size;
    }

    Rect2Df UltraCanvasVectorElement::GetDocumentViewBox() const {
        if (!document) return Rect2Df{0, 0, 0, 0};
        return document->ViewBox;
    }

    std::string UltraCanvasVectorElement::GetDocumentTitle() const {
        if (!document) return "";
        return document->Title;
    }

    std::string UltraCanvasVectorElement::GetDocumentDescription() const {
        if (!document) return "";
        return document->Description;
    }

    size_t UltraCanvasVectorElement::GetLayerCount() const {
        if (!document) return 0;
        return document->Layers.size();
    }

    std::vector<std::string> UltraCanvasVectorElement::GetLayerNames() const {
        std::vector<std::string> names;
        if (!document) return names;

        for (const auto& layer : document->Layers) {
            names.push_back(layer->Name);
        }
        return names;
    }

// ===== LAYER VISIBILITY =====

    void UltraCanvasVectorElement::SetLayerVisible(const std::string& layerName, bool visible) {
        if (!document) return;

        auto layer = document->GetLayer(layerName);
        if (layer) {
            layer->Visible = visible;
            InvalidateCache();
            RequestRedraw();
        }
    }

    bool UltraCanvasVectorElement::IsLayerVisible(const std::string& layerName) const {
        if (!document) return false;

        auto layer = document->GetLayer(layerName);
        return layer ? layer->Visible : false;
    }

    void UltraCanvasVectorElement::ShowAllLayers() {
        if (!document) return;

        for (auto& layer : document->Layers) {
            layer->Visible = true;
        }
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::HideAllLayers() {
        if (!document) return;

        for (auto& layer : document->Layers) {
            layer->Visible = false;
        }
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasVectorElement::InvalidateCache() {
        cacheValid = false;
        state.IsDirty = true;
    }
// UltraCanvasVectorElement.cpp - Part 2
// Rendering and Event Handling Implementation

// ===== RENDERING =====

    void UltraCanvasVectorElement::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        auto startTime = std::chrono::high_resolution_clock::now();

        ctx->PushState();

        // Clip to element bounds
        Rect2Di elementBounds = GetBounds();
        ctx->SetClipRect(elementBounds.x, elementBounds.y,
                         elementBounds.width, elementBounds.height);

        // Render background
        RenderBackground(ctx);

        // Handle different states
        if (state.IsLoading) {
            RenderLoadingIndicator(ctx);
        } else if (state.HasError) {
            RenderErrorState(ctx);
        } else if (document) {
            RenderDocument(ctx);
        }

        // Render border if enabled
        if (options.ShowBorder) {
            RenderBorder(ctx);
        }

        // Render debug info if enabled
        if (options.ShowDebugInfo) {
            RenderDebugInfo(ctx);
        }

        ctx->PopState();

        // Calculate and report render time
        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = endTime - startTime;
        lastRenderTime = std::chrono::steady_clock::now();

        if (onRender) {
            onRender(duration.count());
        }

        state.IsDirty = false;
    }

    void UltraCanvasVectorElement::RenderBackground(IRenderContext* ctx) {
        if (options.BackgroundColor.a == 0) return;

        Rect2Di bounds = GetBounds();
        ctx->SetFillPaint(options.BackgroundColor);
        ctx->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
    }

    void UltraCanvasVectorElement::RenderDocument(IRenderContext* ctx) {
        if (!document || !renderer) return;

        Rect2Di bounds = GetBounds();

        // Save state before applying view transform
        ctx->PushState();

        // Translate to element position
        ctx->Translate(static_cast<float>(bounds.x), static_cast<float>(bounds.y));

        // Apply pan offset
        ctx->Translate(panOffset.x, panOffset.y);

        // Apply zoom
        ctx->Scale(zoomLevel, zoomLevel);

        // Set up render options
        VectorRenderer::RenderOptions renderOpts;
        renderOpts.EnableAntialiasing = options.EnableAntialiasing;
        renderOpts.EnableCulling = true;
        renderOpts.ViewportBounds = Rect2Df{
                -panOffset.x / zoomLevel,
                -panOffset.y / zoomLevel,
                static_cast<float>(bounds.width) / zoomLevel,
                static_cast<float>(bounds.height) / zoomLevel
        };
        renderOpts.PixelRatio = options.Quality;

        renderer->SetRenderOptions(renderOpts);

        // Render the document
        renderer->RenderDocument(ctx, *document);

        // Render selection highlight if an element is selected
        if (!selectedElementId.empty()) {
            auto selectedElement = document->FindElementById(selectedElementId);
            if (selectedElement) {
                RenderSelectionHighlight(ctx, selectedElement);
            }
        }

        // Render hover highlight if in select mode
        if (options.InteractionMode == VectorInteractionMode::Select && !hoveredElementId.empty()) {
            auto hoveredElement = document->FindElementById(hoveredElementId);
            if (hoveredElement && hoveredElementId != selectedElementId) {
                RenderHoverHighlight(ctx, hoveredElement);
            }
        }

        ctx->PopState();
    }

    void UltraCanvasVectorElement::RenderSelectionHighlight(IRenderContext* ctx,
                                                            const std::shared_ptr<VectorElement>& element) {
        if (!element) return;

        Rect2Df bounds = element->GetBoundingBox();

        // Draw selection rectangle
        ctx->SetStrokePaint(Color(0, 120, 215, 255));  // Blue selection color
        ctx->SetStrokeWidth(2.0f / zoomLevel);
        ctx->SetLineDash({4.0f / zoomLevel, 4.0f / zoomLevel}, 0);
        ctx->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        // Draw selection handles at corners and midpoints
        float handleSize = 6.0f / zoomLevel;
        ctx->SetFillPaint(Colors::White);
        ctx->SetStrokePaint(Color(0, 120, 215, 255));
        ctx->SetStrokeWidth(1.0f / zoomLevel);
        ctx->SetLineDash({}, 0);  // Solid line

        // Corner handles
        float handles[][2] = {
                {bounds.x, bounds.y},
                {bounds.x + bounds.width, bounds.y},
                {bounds.x, bounds.y + bounds.height},
                {bounds.x + bounds.width, bounds.y + bounds.height},
                {bounds.x + bounds.width / 2, bounds.y},
                {bounds.x + bounds.width / 2, bounds.y + bounds.height},
                {bounds.x, bounds.y + bounds.height / 2},
                {bounds.x + bounds.width, bounds.y + bounds.height / 2}
        };

        for (const auto& h : handles) {
            ctx->FillRectangle(h[0] - handleSize / 2, h[1] - handleSize / 2, handleSize, handleSize);
            ctx->DrawRectangle(h[0] - handleSize / 2, h[1] - handleSize / 2, handleSize, handleSize);
        }
    }

    void UltraCanvasVectorElement::RenderHoverHighlight(IRenderContext* ctx,
                                                        const std::shared_ptr<VectorElement>& element) {
        if (!element) return;

        Rect2Df bounds = element->GetBoundingBox();

        // Draw hover rectangle with semi-transparent fill
        ctx->SetFillPaint(Color(0, 120, 215, 30));
        ctx->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

        ctx->SetStrokePaint(Color(0, 120, 215, 150));
        ctx->SetStrokeWidth(1.0f / zoomLevel);
        ctx->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
    }

    void UltraCanvasVectorElement::RenderBorder(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();

        ctx->SetStrokePaint(options.BorderColor);
        ctx->SetStrokeWidth(options.BorderWidth);
        ctx->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
    }

    void UltraCanvasVectorElement::RenderDebugInfo(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();

        // Prepare debug text
        std::string debugText;
        debugText += "Zoom: " + std::to_string(static_cast<int>(zoomLevel * 100)) + "%\n";
        debugText += "Pan: (" + std::to_string(static_cast<int>(panOffset.x)) + ", "
                     + std::to_string(static_cast<int>(panOffset.y)) + ")\n";

        if (document) {
            debugText += "Doc Size: " + std::to_string(static_cast<int>(document->Size.width))
                         + "x" + std::to_string(static_cast<int>(document->Size.height)) + "\n";
            debugText += "Layers: " + std::to_string(document->Layers.size()) + "\n";
        }

        auto stats = renderer->GetRenderStats();
        debugText += "Elements: " + std::to_string(stats.ElementsRendered) + "\n";
        debugText += "Render: " + std::to_string(static_cast<int>(stats.RenderTimeMs)) + "ms";

        // Draw debug background
        ctx->SetFillPaint(Color(0, 0, 0, 180));
        ctx->FillRectangle(bounds.x + 5, bounds.y + 5, 150, 100);

        // Draw debug text
        ctx->SetTextPaint(Colors::White);
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(11);
        ctx->DrawText(debugText, bounds.x + 10, bounds.y + 20);
    }

    void UltraCanvasVectorElement::RenderLoadingIndicator(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();

        // Draw centered loading text
        ctx->SetTextPaint(Color(100, 100, 100, 255));
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(14);

        std::string loadingText = "Loading... " + std::to_string(static_cast<int>(state.LoadProgress * 100)) + "%";

        // Center the text
        float textWidth = ctx->GetTextWidth(loadingText);
        float x = bounds.x + (bounds.width - textWidth) / 2;
        float y = bounds.y + bounds.height / 2;

        ctx->DrawText(loadingText, x, y);

        // Draw progress bar
        float barWidth = 200;
        float barHeight = 6;
        float barX = bounds.x + (bounds.width - barWidth) / 2;
        float barY = y + 20;

        // Background
        ctx->SetFillPaint(Color(220, 220, 220, 255));
        ctx->FillRectangle(barX, barY, barWidth, barHeight);

        // Progress
        ctx->SetFillPaint(Color(0, 120, 215, 255));
        ctx->FillRectangle(barX, barY, barWidth * state.LoadProgress, barHeight);
    }

    void UltraCanvasVectorElement::RenderErrorState(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();

        // Draw error icon and message
        ctx->SetTextPaint(Color(200, 50, 50, 255));
        ctx->SetFontFace("Sans", FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(14);

        std::string errorTitle = "Error Loading Document";
        float titleWidth = ctx->GetTextWidth(errorTitle);
        float x = bounds.x + (bounds.width - titleWidth) / 2;
        float y = bounds.y + bounds.height / 2 - 20;

        ctx->DrawText(errorTitle, x, y);

        // Draw error message
        ctx->SetTextPaint(Color(100, 100, 100, 255));
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(12);

        float msgWidth = ctx->GetTextWidth(state.ErrorMessage);
        x = bounds.x + (bounds.width - msgWidth) / 2;
        y += 25;

        ctx->DrawText(state.ErrorMessage, x, y);
    }

// ===== EVENT HANDLING =====

    bool UltraCanvasVectorElement::OnEvent(const UCEvent& event) {
        if (!IsVisible()) return false;

        // Check if event is within our bounds
        if (event.Type == UCEventType::MouseMove ||
            event.Type == UCEventType::MouseDown ||
            event.Type == UCEventType::MouseUp ||
            event.Type == UCEventType::MouseWheel) {

            Rect2Di bounds = GetBounds();
            if (!bounds.Contains(Point2Di{event.MouseX, event.MouseY})) {
                return false;
            }
        }

        switch (event.Type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);

            case UCEventType::KeyDown:
                return HandleKeyPress(event);

            default:
                break;
        }

        return UltraCanvasUIElement::OnEvent(event);
    }

    bool UltraCanvasVectorElement::HandleMouseDown(const UCEvent& event) {
        if (event.Button != UCMouseButton::Left) return false;

        lastMousePos = {event.MouseX, event.MouseY};
        dragStartPos = lastMousePos;

        switch (options.InteractionMode) {
            case VectorInteractionMode::Pan:
            case VectorInteractionMode::PanZoom:
                isPanning = true;
                mousePtr = MousePointer::Move;
                return true;

            case VectorInteractionMode::Select:
            {
                std::string hitId = HitTest(event.MouseX, event.MouseY);
                if (!hitId.empty()) {
                    SelectElement(hitId);
                } else {
                    ClearSelection();
                }
            }
                return true;

            default:
                break;
        }

        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseUp(const UCEvent& event) {
        if (isPanning) {
            isPanning = false;

            if (options.InteractionMode == VectorInteractionMode::Pan ||
                options.InteractionMode == VectorInteractionMode::PanZoom) {
                mousePtr = MousePointer::Move;
            } else {
                mousePtr = MousePointer::Default;
            }
            return true;
        }

        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseMove(const UCEvent& event) {
        Point2Di currentPos = {event.MouseX, event.MouseY};

        if (isPanning) {
            float dx = static_cast<float>(currentPos.x - lastMousePos.x);
            float dy = static_cast<float>(currentPos.y - lastMousePos.y);

            Pan(dx, dy);

            lastMousePos = currentPos;
            return true;
        }

        // Update hover state in select mode
        if (options.InteractionMode == VectorInteractionMode::Select) {
            std::string hitId = HitTest(event.MouseX, event.MouseY);
            if (hitId != hoveredElementId) {
                hoveredElementId = hitId;
                RequestRedraw();
            }
            return true;
        }

        lastMousePos = currentPos;
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseWheel(const UCEvent& event) {
        if (!options.EnableMouseWheel) return false;

        if (options.InteractionMode == VectorInteractionMode::Zoom ||
            options.InteractionMode == VectorInteractionMode::PanZoom) {

            // Get mouse position relative to document
            Point2Df docPointBefore = ScreenToDocument(event.MouseX, event.MouseY);

            // Calculate new zoom
            float zoomFactor = event.ScrollDelta > 0 ? (1.0f + options.ZoomStep) : (1.0f / (1.0f + options.ZoomStep));
            float newZoom = zoomLevel * zoomFactor;
            newZoom = std::clamp(newZoom, options.MinZoom, options.MaxZoom);

            if (std::abs(newZoom - zoomLevel) > 0.0001f) {
                zoomLevel = newZoom;

                // Adjust pan to keep mouse position fixed
                Point2Di screenPos = {event.MouseX - GetX(), event.MouseY - GetY()};
                panOffset.x = screenPos.x - docPointBefore.x * zoomLevel;
                panOffset.y = screenPos.y - docPointBefore.y * zoomLevel;

                UpdateViewTransform();
                InvalidateCache();
                NotifyZoomChanged();
                NotifyPanChanged();
                RequestRedraw();
            }

            return true;
        }

        return false;
    }

    bool UltraCanvasVectorElement::HandleKeyPress(const UCEvent& event) {
        // Handle keyboard shortcuts
        switch (event.Key) {
            case UCKeys::Plus:
            case UCKeys::Equal:
                ZoomIn();
                return true;

            case UCKeys::Minus:
                ZoomOut();
                return true;

            case UCKeys::Key0:
                ZoomToActualSize();
                return true;

            case UCKeys::Home:
                ZoomToFit();
                return true;

            case UCKeys::Escape:
                ClearSelection();
                return true;

            default:
                break;
        }

        return false;
    }

// ===== INTERNAL HELPERS =====

    void UltraCanvasVectorElement::UpdateViewTransform() {
        viewTransform = Matrix3x3::Identity();
        viewTransform = viewTransform * Matrix3x3::Translate(panOffset.x, panOffset.y);
        viewTransform = viewTransform * Matrix3x3::Scale(zoomLevel, zoomLevel);
    }

    void UltraCanvasVectorElement::CalculateScaling(float& scaleX, float& scaleY) const {
        if (!document) {
            scaleX = scaleY = 1.0f;
            return;
        }

        float docWidth = document->Size.width;
        float docHeight = document->Size.height;

        if (docWidth <= 0 || docHeight <= 0) {
            scaleX = scaleY = 1.0f;
            return;
        }

        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        switch (options.ScaleMode) {
            case VectorScaleMode::None:
                scaleX = scaleY = 1.0f;
                break;

            case VectorScaleMode::Fit:
                scaleX = scaleY = std::min(elementWidth / docWidth, elementHeight / docHeight);
                break;

            case VectorScaleMode::Fill:
                scaleX = scaleY = std::max(elementWidth / docWidth, elementHeight / docHeight);
                break;

            case VectorScaleMode::Stretch:
                scaleX = elementWidth / docWidth;
                scaleY = elementHeight / docHeight;
                break;

            case VectorScaleMode::FitWidth:
                scaleX = scaleY = elementWidth / docWidth;
                break;

            case VectorScaleMode::FitHeight:
                scaleX = scaleY = elementHeight / docHeight;
                break;
        }
    }

    Point2Df UltraCanvasVectorElement::CalculateOffset() const {
        if (!document) return Point2Df{0, 0};

        float scaleX, scaleY;
        CalculateScaling(scaleX, scaleY);

        float scaledWidth = document->Size.width * scaleX;
        float scaledHeight = document->Size.height * scaleY;

        float elementWidth = static_cast<float>(GetWidth());
        float elementHeight = static_cast<float>(GetHeight());

        float offsetX = 0, offsetY = 0;

        switch (options.Alignment) {
            case VectorAlignment::TopLeft:
            case VectorAlignment::CenterLeft:
            case VectorAlignment::BottomLeft:
                offsetX = 0;
                break;
            case VectorAlignment::TopCenter:
            case VectorAlignment::Center:
            case VectorAlignment::BottomCenter:
                offsetX = (elementWidth - scaledWidth) / 2;
                break;
            case VectorAlignment::TopRight:
            case VectorAlignment::CenterRight:
            case VectorAlignment::BottomRight:
                offsetX = elementWidth - scaledWidth;
                break;
        }

        switch (options.Alignment) {
            case VectorAlignment::TopLeft:
            case VectorAlignment::TopCenter:
            case VectorAlignment::TopRight:
                offsetY = 0;
                break;
            case VectorAlignment::CenterLeft:
            case VectorAlignment::Center:
            case VectorAlignment::CenterRight:
                offsetY = (elementHeight - scaledHeight) / 2;
                break;
            case VectorAlignment::BottomLeft:
            case VectorAlignment::BottomCenter:
            case VectorAlignment::BottomRight:
                offsetY = elementHeight - scaledHeight;
                break;
        }

        return Point2Df{offsetX, offsetY};
    }

    std::string UltraCanvasVectorElement::HitTest(int x, int y) const {
        if (!document) return "";

        Point2Df docPoint = ScreenToDocument(x, y);

        // Iterate through layers and elements in reverse order (top to bottom)
        for (auto it = document->Layers.rbegin(); it != document->Layers.rend(); ++it) {
            const auto& layer = *it;
            if (!layer->Visible) continue;

            // Check children in reverse order
            std::string hitId = HitTestGroup(*layer, docPoint);
            if (!hitId.empty()) {
                return hitId;
            }
        }

        return "";
    }

    std::string UltraCanvasVectorElement::HitTestGroup(const VectorGroup& group, const Point2Df& point) const {
        // Check children in reverse order
        for (auto it = group.Children.rbegin(); it != group.Children.rend(); ++it) {
            const auto& child = *it;
            if (!child || !child->Style.Visible) continue;

            // Check if it's a group
            if (child->Type == VectorElementType::Group ||
                child->Type == VectorElementType::Layer) {
                auto childGroup = std::dynamic_pointer_cast<VectorGroup>(child);
                if (childGroup) {
                    std::string hitId = HitTestGroup(*childGroup, point);
                    if (!hitId.empty()) return hitId;
                }
            }

            // Check bounds
            Rect2Df bounds = child->GetBoundingBox();
            if (bounds.Contains(point)) {
                if (!child->Id.empty()) {
                    return child->Id;
                }
            }
        }

        return "";
    }

    void UltraCanvasVectorElement::SetError(const std::string& message) {
        state.HasError = true;
        state.IsLoading = false;
        state.ErrorMessage = message;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ClearError() {
        state.HasError = false;
        state.ErrorMessage.clear();
    }

    void UltraCanvasVectorElement::NotifyZoomChanged() {
        if (onZoomChange) {
            onZoomChange(zoomLevel);
        }
    }

    void UltraCanvasVectorElement::NotifyPanChanged() {
        if (onPanChange) {
            onPanChange(panOffset.x, panOffset.y);
        }
    }

} // namespace UltraCanvas