// UltraCanvasVectorElement.cpp
// UI Element for Vector Document Display and Interaction
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorElement.h"
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

    using namespace VectorStorage;

    UltraCanvasVectorElement::UltraCanvasVectorElement(const std::string& identifier, int x, int y, int width, int height)
            : UltraCanvasUIElement(identifier) {
        SetPosition(x, y);
        SetSize(width, height);
        renderer = std::make_unique<VectorRenderer>();
        viewTransform = Matrix3x3::Identity();
    }

    UltraCanvasVectorElement::~UltraCanvasVectorElement() = default;

    void UltraCanvasVectorElement::SetDocument(std::shared_ptr<VectorDocument> doc) {
        document = doc;
        state.IsDirty = true;
        ClearError();
        ZoomToFit();
        if (onLoad) onLoad(true, "Document loaded");
    }

    void UltraCanvasVectorElement::ClearDocument() {
        document = nullptr;
        selectedElementId.clear();
        hoveredElementId.clear();
        zoomLevel = 1.0f;
        panOffset = {0, 0};
        state.IsDirty = true;
        ClearError();
    }

    void UltraCanvasVectorElement::SetZoom(float zoom) {
        zoom = std::clamp(zoom, options.MinZoom, options.MaxZoom);
        if (std::abs(zoom - zoomLevel) > 0.001f) {
            zoomLevel = zoom;
            UpdateViewTransform();
            state.IsDirty = true;
            if (onZoomChange) onZoomChange(zoomLevel);
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::ZoomIn() { SetZoom(zoomLevel + options.ZoomStep); }
    void UltraCanvasVectorElement::ZoomOut() { SetZoom(zoomLevel - options.ZoomStep); }

    void UltraCanvasVectorElement::ZoomToFit() {
        if (!document) return;
        Rect2Df docBounds = document->GetBoundingBox();
        if (docBounds.width <= 0 || docBounds.height <= 0) return;

        auto bounds = GetBounds();
        float scaleX = bounds.width / docBounds.width;
        float scaleY = bounds.height / docBounds.height;
        float scale = std::min(scaleX, scaleY) * 0.9f;

        zoomLevel = std::clamp(scale, options.MinZoom, options.MaxZoom);
        panOffset.x = (bounds.width - docBounds.width * zoomLevel) / 2 - docBounds.x * zoomLevel;
        panOffset.y = (bounds.height - docBounds.height * zoomLevel) / 2 - docBounds.y * zoomLevel;
        UpdateViewTransform();
        state.IsDirty = true;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ZoomToActualSize() {
        zoomLevel = 1.0f;
        CenterDocument();
    }

    void UltraCanvasVectorElement::SetPan(float x, float y) {
        panOffset = {x, y};
        UpdateViewTransform();
        state.IsDirty = true;
        if (onPanChange) onPanChange(panOffset.x, panOffset.y);
        RequestRedraw();
    }

    void UltraCanvasVectorElement::Pan(float dx, float dy) {
        SetPan(panOffset.x + dx, panOffset.y + dy);
    }

    void UltraCanvasVectorElement::CenterDocument() {
        if (!document) return;
        Rect2Df docBounds = document->GetBoundingBox();
        auto bounds = GetBounds();
        panOffset.x = (bounds.width - docBounds.width * zoomLevel) / 2 - docBounds.x * zoomLevel;
        panOffset.y = (bounds.height - docBounds.height * zoomLevel) / 2 - docBounds.y * zoomLevel;
        UpdateViewTransform();
        state.IsDirty = true;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ResetView() {
        zoomLevel = 1.0f;
        panOffset = {0, 0};
        UpdateViewTransform();
        if (document) ZoomToFit();
    }

    void UltraCanvasVectorElement::SetOptions(const VectorElementOptions& opts) {
        options = opts;
        state.IsDirty = true;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::SetScaleMode(VectorScaleMode mode) { options.ScaleMode = mode; state.IsDirty = true; RequestRedraw(); }
    void UltraCanvasVectorElement::SetAlignment(VectorAlignment align) { options.Alignment = align; state.IsDirty = true; RequestRedraw(); }
    void UltraCanvasVectorElement::SetInteractionMode(VectorInteractionMode mode) { options.InteractionMode = mode; }
    void UltraCanvasVectorElement::SetBackgroundColor(const Color& color) { options.BackgroundColor = color; RequestRedraw(); }

    void UltraCanvasVectorElement::SelectElement(const std::string& elementId) {
        if (selectedElementId != elementId) {
            selectedElementId = elementId;
            if (onSelection) onSelection(selectedElementId);
            RequestRedraw();
        }
    }

    void UltraCanvasVectorElement::ClearSelection() { SelectElement(""); }

    std::shared_ptr<VectorElement> UltraCanvasVectorElement::GetSelectedElement() const {
        if (!document || selectedElementId.empty()) return nullptr;
        return document->FindElementById(selectedElementId);
    }

    Point2Df UltraCanvasVectorElement::ScreenToDocument(int screenX, int screenY) const {
        auto bounds = GetBounds();
        float localX = screenX - bounds.x - panOffset.x;
        float localY = screenY - bounds.y - panOffset.y;
        return {localX / zoomLevel, localY / zoomLevel};
    }

    Point2Di UltraCanvasVectorElement::DocumentToScreen(float docX, float docY) const {
        auto bounds = GetBounds();
        int screenX = static_cast<int>(docX * zoomLevel + panOffset.x + bounds.x);
        int screenY = static_cast<int>(docY * zoomLevel + panOffset.y + bounds.y);
        return {screenX, screenY};
    }

    Size2Df UltraCanvasVectorElement::GetDocumentSize() const {
        return document ? document->Size : Size2Df{0, 0};
    }

    Rect2Df UltraCanvasVectorElement::GetDocumentViewBox() const {
        return document ? document->ViewBox : Rect2Df{0, 0, 0, 0};
    }

    size_t UltraCanvasVectorElement::GetLayerCount() const {
        return document ? document->Layers.size() : 0;
    }

    std::vector<std::string> UltraCanvasVectorElement::GetLayerNames() const {
        std::vector<std::string> names;
        if (document) for (const auto& layer : document->Layers) names.push_back(layer->Name);
        return names;
    }

    void UltraCanvasVectorElement::SetLayerVisible(const std::string& layerName, bool visible) {
        if (!document) return;
        for (auto& layer : document->Layers) {
            if (layer->Name == layerName) { layer->Visible = visible; state.IsDirty = true; RequestRedraw(); break; }
        }
    }

    bool UltraCanvasVectorElement::IsLayerVisible(const std::string& layerName) const {
        if (!document) return false;
        for (const auto& layer : document->Layers) if (layer->Name == layerName) return layer->Visible;
        return false;
    }

    void UltraCanvasVectorElement::UpdateViewTransform() {
        viewTransform = Matrix3x3::Translate(panOffset.x, panOffset.y) * Matrix3x3::Scale(zoomLevel, zoomLevel);
    }

    void UltraCanvasVectorElement::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;
        auto bounds = GetBounds();

        ctx->PushState();
        ctx->ClipRect(bounds.x, bounds.y, bounds.width, bounds.height);

        RenderBackground(ctx);

        if (state.HasError) {
            ctx->SetTextPaint(Colors::Red);
            ctx->DrawText("Error: " + state.ErrorMessage, bounds.x + 10, bounds.y + 20);
        } else if (document) {
            RenderDocument(ctx);
        }

        if (options.ShowBorder) RenderBorder(ctx);
        if (options.ShowDebugInfo) RenderDebugInfo(ctx);

        ctx->PopState();
    }

    void UltraCanvasVectorElement::RenderBackground(IRenderContext* ctx) {
        if (options.BackgroundColor.a > 0) {
            auto bounds = GetBounds();
            ctx->SetFillPaint(options.BackgroundColor);
            ctx->FillRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
        }
    }

    void UltraCanvasVectorElement::RenderDocument(IRenderContext* ctx) {
        if (!document || !renderer) return;
        auto bounds = GetBounds();
        auto startTime = std::chrono::high_resolution_clock::now();

        ctx->PushState();
        ctx->Translate(bounds.x + panOffset.x, bounds.y + panOffset.y);
        ctx->Scale(zoomLevel, zoomLevel);

        VectorRenderOptions renderOpts;
        renderOpts.EnableAntialiasing = options.EnableAntialiasing;
        renderOpts.ViewportBounds = {0, 0, bounds.width / zoomLevel, bounds.height / zoomLevel};
        renderOpts.ClipToViewport = true;
        renderer->SetOptions(renderOpts);
        renderer->RenderDocument(ctx, *document);

        ctx->PopState();

        auto endTime = std::chrono::high_resolution_clock::now();
        double renderTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        if (onRender) onRender(renderTime);
    }

    void UltraCanvasVectorElement::RenderBorder(IRenderContext* ctx) {
        auto bounds = GetBounds();
        ctx->SetStrokePaint(options.BorderColor);
        ctx->SetStrokeWidth(options.BorderWidth);
        ctx->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);
    }

    void UltraCanvasVectorElement::RenderDebugInfo(IRenderContext* ctx) {
        auto bounds = GetBounds();
        ctx->SetFillPaint(Color(0, 0, 0, 180));
        ctx->FillRectangle(bounds.x + 5, bounds.y + 5, 150, 60);
        ctx->SetTextPaint(Colors::White);
        ctx->SetFontSize(10);
        ctx->DrawText("Zoom: " + std::to_string(static_cast<int>(zoomLevel * 100)) + "%", bounds.x + 10, bounds.y + 20);
        ctx->DrawText("Pan: " + std::to_string(static_cast<int>(panOffset.x)) + ", " + std::to_string(static_cast<int>(panOffset.y)), bounds.x + 10, bounds.y + 35);
        if (document) ctx->DrawText("Layers: " + std::to_string(document->Layers.size()), bounds.x + 10, bounds.y + 50);
    }

    bool UltraCanvasVectorElement::OnEvent(const UCEvent& event) {
        auto bounds = GetBounds();
        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseDown ||
            event.type == UCEventType::MouseUp || event.type == UCEventType::MouseWheel) {
            if (event.x < bounds.x || event.x > bounds.x + bounds.width ||
                event.y < bounds.y || event.y > bounds.y + bounds.height) return false;
        }

        switch (event.type) {
            case UCEventType::MouseDown: return HandleMouseDown(event);
            case UCEventType::MouseUp: return HandleMouseUp(event);
            case UCEventType::MouseMove: return HandleMouseMove(event);
            case UCEventType::MouseWheel: return HandleMouseWheel(event);
            default: break;
        }
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseDown(const UCEvent& event) {
        if (options.InteractionMode == VectorInteractionMode::Pan ||
            options.InteractionMode == VectorInteractionMode::PanZoom) {
            isPanning = true;
            lastMousePos = {event.x, event.y};
            return true;
        }
        if (options.InteractionMode == VectorInteractionMode::Select) {
            std::string hit = HitTest(event.x, event.y);
            SelectElement(hit);
            return true;
        }
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseUp(const UCEvent& event) {
        if (isPanning) { isPanning = false; return true; }
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseMove(const UCEvent& event) {
        if (isPanning) {
            int dx = event.x - lastMousePos.x;
            int dy = event.y - lastMousePos.y;
            Pan(static_cast<float>(dx), static_cast<float>(dy));
            lastMousePos = {event.x, event.y};
            return true;
        }
        if (options.InteractionMode == VectorInteractionMode::Select) {
            std::string hit = HitTest(event.x, event.y);
            if (hit != hoveredElementId) { hoveredElementId = hit; RequestRedraw(); }
        }
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseWheel(const UCEvent& event) {
        if (!options.EnableMouseWheel) return false;
        if (options.InteractionMode == VectorInteractionMode::Zoom ||
            options.InteractionMode == VectorInteractionMode::PanZoom) {
            auto bounds = GetBounds();
            float mouseX = event.x - bounds.x;
            float mouseY = event.y - bounds.y;

            float oldZoom = zoomLevel;
            float newZoom = zoomLevel + (event.wheelDelta > 0 ? options.ZoomStep : -options.ZoomStep);
            newZoom = std::clamp(newZoom, options.MinZoom, options.MaxZoom);

            // Zoom towards mouse position
            float docX = (mouseX - panOffset.x) / oldZoom;
            float docY = (mouseY - panOffset.y) / oldZoom;
            zoomLevel = newZoom;
            panOffset.x = mouseX - docX * newZoom;
            panOffset.y = mouseY - docY * newZoom;

            UpdateViewTransform();
            state.IsDirty = true;
            if (onZoomChange) onZoomChange(zoomLevel);
            RequestRedraw();
            return true;
        }
        return false;
    }

    void UltraCanvasVectorElement::SetError(const std::string& message) {
        state.HasError = true;
        state.ErrorMessage = message;
        RequestRedraw();
    }

    void UltraCanvasVectorElement::ClearError() {
        state.HasError = false;
        state.ErrorMessage.clear();
    }

    std::string UltraCanvasVectorElement::HitTest(int x, int y) const {
        if (!document) return "";
        Point2Df docPt = ScreenToDocument(x, y);
        auto hits = HitTestDocument(*document, docPt);
        return hits.empty() ? "" : hits[0]->Id;
    }

} // namespace UltraCanvas