// UltraCanvasVectorElement.cpp
// UI Element for Vector Document Display and Interaction
//
// The element owns the view transform - zoomLevel and panOffset - and
// VectorRenderer must therefore be given no viewport of its own, or its
// fit-to-viewport transform overrides this one (see RenderDocument).
//
// Everything here works in element-LOCAL coordinates: the parent container
// translates the render context to the element's origin before calling
// Render(), and delivers pointer events with the same origin subtracted.
// Version: 2.2.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework

#include "UltraCanvasVectorElement.h"
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

    using namespace VectorStorage;

    // The four-argument base constructor is what stamps the CSS box - an
    // explicit px width/height, and an absolutely-placed origin for a non-zero
    // (x, y). SetPosition()/SetSize() only write finalBounds, which the layout
    // engine then overwrites with the auto-sized box of a widget that declared
    // no size: the element collapsed to nothing and its container skipped it
    // entirely (UltraCanvasContainer::Render culls a child that does not
    // intersect the content area), so every drawing on the DWG/DXF demo page
    // was an empty white square. Passing 0 for a dimension still leaves it to
    // the parent, which is what the flex/grid callers want.
    UltraCanvasVectorElement::UltraCanvasVectorElement(const std::string& identifier, int x, int y, int width, int height)
            : UltraCanvasUIElement(identifier, static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(width), static_cast<float>(height)) {
        renderer = std::make_unique<VectorRenderer>();
        viewTransform = Matrix3x3::Identity();
    }

    UltraCanvasVectorElement::~UltraCanvasVectorElement() = default;

    void UltraCanvasVectorElement::SetDocument(std::shared_ptr<VectorDocument> doc) {
        document = doc;
        state.IsDirty = true;
        ClearError();
        fitPending = true;
        ZoomToFit();
        if (onLoad) onLoad(true, "Document loaded");
    }

    void UltraCanvasVectorElement::ClearDocument() {
        document = nullptr;
        selectedElementId.clear();
        hoveredElementId.clear();
        zoomLevel = 1.0f;
        panOffset = {0, 0};
        fitPending = false;
        fitZoom = 0.0f;
        state.IsDirty = true;
        ClearError();
    }

    void UltraCanvasVectorElement::SetZoom(float zoom) {
        zoom = std::clamp(zoom, MinAllowedZoom(), options.MaxZoom);
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
        // Not GetBoundingBox(): a fit is about what the reader of the drawing
        // wants to see, and one forgotten speck a quarter of a million units
        // away from the plans would otherwise shrink the whole drawing into a
        // corner of an empty sheet (see ContentBounds).
        Rect2Dd docBounds = ContentBounds(*document);
        if (docBounds.width <= 0 || docBounds.height <= 0) return;

        // A document is usually set while the page is still being built, so
        // the layout has not given this element a box yet and there is
        // nothing to fit to. Fitting to a zero box would leave the drawing
        // at MinZoom; the fit is remembered instead and redone from Render()
        // once the box is real.
        if (finalBounds.width <= 0 || finalBounds.height <= 0) {
            fitPending = true;
            return;
        }
        fitPending = false;

        float scaleX = finalBounds.width / docBounds.width;
        float scaleY = finalBounds.height / docBounds.height;
        float scale = std::min(scaleX, scaleY) * 0.9f;

        // A fit is by definition the scale the drawing needs, so it is not
        // clamped up to options.MinZoom - a 10 000-unit site plan in a 280 px
        // tile fits at 0.025 and used to be pinned at 0.1, which showed an
        // empty patch of the middle of the drawing. It does become the lower
        // limit for interactive zooming (MinAllowedZoom), so a zoom-out still
        // stops at the whole drawing.
        fitZoom = std::min(scale, options.MaxZoom);
        zoomLevel = fitZoom;
        panOffset.x = (finalBounds.width - docBounds.width * zoomLevel) / 2 - docBounds.x * zoomLevel;
        panOffset.y = (finalBounds.height - docBounds.height * zoomLevel) / 2 - docBounds.y * zoomLevel;
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
        Rect2Dd docBounds = ContentBounds(*document);
        auto bounds = GetBounds();
        panOffset.x = (finalBounds.width - docBounds.width * zoomLevel) / 2 - docBounds.x * zoomLevel;
        panOffset.y = (finalBounds.height - docBounds.height * zoomLevel) / 2 - docBounds.y * zoomLevel;
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

    // Both directions speak the element's own coordinates - the frame pointer
    // events arrive in and the frame Render() draws in.
    Point2Dd UltraCanvasVectorElement::ScreenToDocument(int screenX, int screenY) const {
        float localX = screenX - panOffset.x;
        float localY = screenY - panOffset.y;
        return {localX / zoomLevel, localY / zoomLevel};
    }

    Point2Di UltraCanvasVectorElement::DocumentToScreen(float docX, float docY) const {
        int screenX = static_cast<int>(docX * zoomLevel + panOffset.x);
        int screenY = static_cast<int>(docY * zoomLevel + panOffset.y);
        return {screenX, screenY};
    }

    Size2Dd UltraCanvasVectorElement::GetDocumentSize() const {
        return document ? document->Size : Size2Dd{0, 0};
    }

    Rect2Dd UltraCanvasVectorElement::GetDocumentViewBox() const {
        return document ? document->ViewBox : Rect2Dd{0, 0, 0, 0};
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

    float UltraCanvasVectorElement::MinAllowedZoom() const {
        return fitZoom > 0.0f ? std::min(options.MinZoom, fitZoom) : options.MinZoom;
    }

    void UltraCanvasVectorElement::UpdateViewTransform() {
        viewTransform = Matrix3x3::Translate(panOffset.x, panOffset.y) * Matrix3x3::Scale(zoomLevel, zoomLevel);
    }

    void UltraCanvasVectorElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!IsVisible()) return;
        auto bounds = GetLocalBounds();

        // The first frame after a document arrives is where the element
        // finally knows its size, so a fit asked for before the layout ran
        // happens here.
        if (fitPending && document) ZoomToFit();

        ctx->PushState();
        ctx->ClipRect(bounds);

        RenderBackground(ctx);

        if (state.HasError) {
            ctx->SetTextPaint(Colors::Red);
            ctx->DrawText("Error: " + state.ErrorMessage, Point2Dd(10, 20));
        } else if (document) {
            RenderDocument(ctx);
        }

        if (options.ShowBorder) RenderBorder(ctx);
        if (options.ShowDebugInfo) RenderDebugInfo(ctx);

        ctx->PopState();
    }

    void UltraCanvasVectorElement::RenderBackground(IRenderContext* ctx) {
        if (options.BackgroundColor.a > 0) {
            auto bounds = GetLocalBounds();
            ctx->SetFillPaint(options.BackgroundColor);
            ctx->FillRectangle(bounds);
        }
    }

    void UltraCanvasVectorElement::RenderDocument(IRenderContext* ctx) {
        if (!document || !renderer) return;
        auto startTime = std::chrono::high_resolution_clock::now();

        ctx->PushState();
        ctx->Translate(panOffset.x, panOffset.y);
        ctx->Scale(zoomLevel, zoomLevel);

        VectorRenderOptions renderOpts;
        renderOpts.EnableAntialiasing = options.EnableAntialiasing;
        // The view transform is this element's: zoomLevel and panOffset, set
        // just above. VectorRenderer applies a fit-to-viewport of its own
        // when it is given a viewport, and the two fought: expressing the
        // viewport in document units (finalBounds / zoomLevel) made the
        // renderer's scale cancel zoomLevel exactly - so zooming changed
        // nothing - and its centring of the ViewBox came on top of the
        // centring already in panOffset, which threw the drawing half a
        // viewport to the right whenever ZoomToFit ran with a real box. An
        // empty viewport leaves the transform to this element. It also turns
        // off culling (IsInViewport passes everything), which costs only
        // time: Render() already clips to this element's bounds.
        renderOpts.ViewportBounds = {0, 0, 0, 0};
        renderOpts.ClipToViewport = false;
        renderer->SetOptions(renderOpts);
        renderer->RenderDocument(ctx, *document);

        ctx->PopState();

        auto endTime = std::chrono::high_resolution_clock::now();
        double renderTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        if (onRender) onRender(renderTime);
    }

    void UltraCanvasVectorElement::RenderBorder(IRenderContext* ctx) {
        auto bounds = GetLocalBounds();
        ctx->SetStrokePaint(options.BorderColor);
        ctx->SetStrokeWidth(options.BorderWidth);
        ctx->DrawRectangle(bounds);
    }

    void UltraCanvasVectorElement::RenderDebugInfo(IRenderContext* ctx) {
        ctx->SetFillPaint(Color(0, 0, 0, 180));
        ctx->FillRectangle(Rect2Dd(5, 5, 150, 60));
        ctx->SetTextPaint(Colors::White);
        ctx->SetFontSize(10);
        ctx->DrawText("Zoom: " + std::to_string(static_cast<int>(zoomLevel * 100)) + "%", Point2Dd(10, 20));
        ctx->DrawText("Pan: " + std::to_string(static_cast<int>(panOffset.x)) + ", " + std::to_string(static_cast<int>(panOffset.y)), Point2Dd(10, 35));
        if (document) ctx->DrawText("Layers: " + std::to_string(document->Layers.size()), Point2Dd(10, 50));
    }

    bool UltraCanvasVectorElement::OnEvent(const UCEvent& event) {
        // The owner's callback comes first: a host that put the element on a
        // page to be clicked (the DWG demo's tiles open a fullscreen viewer on
        // MouseUp and update a status line on enter/leave) never saw an event,
        // because panning and selection are the only things handled below and
        // everything else was dropped.
        if (UltraCanvasUIElement::OnEvent(event)) return true;

        if (event.type == UCEventType::MouseMove || event.type == UCEventType::MouseDown ||
            event.type == UCEventType::MouseUp || event.type == UCEventType::MouseWheel) {
            if (!Contains(Point2Df(static_cast<float>(event.pointer.x),
                                   static_cast<float>(event.pointer.y)))) return false;
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
            lastMousePos = {event.pointer.x, event.pointer.y};
            return true;
        }
        if (options.InteractionMode == VectorInteractionMode::Select) {
            std::string hit = HitTest(event.pointer.x, event.pointer.y);
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
            int dx = event.pointer.x - lastMousePos.x;
            int dy = event.pointer.y - lastMousePos.y;
            Pan(static_cast<float>(dx), static_cast<float>(dy));
            lastMousePos = {event.pointer.x, event.pointer.y};
            return true;
        }
        if (options.InteractionMode == VectorInteractionMode::Select) {
            std::string hit = HitTest(event.pointer.x, event.pointer.y);
            if (hit != hoveredElementId) { hoveredElementId = hit; RequestRedraw(); }
        }
        return false;
    }

    bool UltraCanvasVectorElement::HandleMouseWheel(const UCEvent& event) {
        if (!options.EnableMouseWheel) return false;
        if (options.InteractionMode == VectorInteractionMode::Zoom ||
            options.InteractionMode == VectorInteractionMode::PanZoom) {
            float mouseX = static_cast<float>(event.pointer.x);
            float mouseY = static_cast<float>(event.pointer.y);

            // The document point under the cursor is captured once and held
            // there for the whole glide, so the drawing grows around the cursor
            // instead of drifting as the eased steps land.
            zoomAnchorX = mouseX;
            zoomAnchorY = mouseY;
            zoomAnchorDocX = (mouseX - panOffset.x) / zoomLevel;
            zoomAnchorDocY = (mouseY - panOffset.y) / zoomLevel;
            if (!zoomAnim.IsBound()) {
                zoomAnim.Bind([this] { return static_cast<double>(zoomLevel); },
                              [this](double z) {
                                  ApplyZoomLevelAtAnchor(static_cast<float>(z));
                              });
            }
            zoomAnim.AnimateBy(event.wheelDelta > 0 ? options.ZoomStep
                                                    : -options.ZoomStep,
                               MinAllowedZoom(), options.MaxZoom);
            return true;
        }
        return false;
    }

    // One eased step of a wheel zoom: set the level, then re-solve the pan that
    // keeps the gesture's anchor document point under the cursor.
    void UltraCanvasVectorElement::ApplyZoomLevelAtAnchor(float newZoom) {
        zoomLevel = std::clamp(newZoom, MinAllowedZoom(), options.MaxZoom);
        panOffset.x = zoomAnchorX - zoomAnchorDocX * zoomLevel;
        panOffset.y = zoomAnchorY - zoomAnchorDocY * zoomLevel;

        UpdateViewTransform();
        state.IsDirty = true;
        if (onZoomChange) onZoomChange(zoomLevel);
        RequestRedraw();
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
        Point2Dd docPt = ScreenToDocument(x, y);
        auto hits = HitTestDocument(*document, docPt);
        return hits.empty() ? "" : hits[0]->Id;
    }

} // namespace UltraCanvas