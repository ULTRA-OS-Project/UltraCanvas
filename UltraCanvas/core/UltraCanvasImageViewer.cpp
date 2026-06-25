// core/UltraCanvasImageViewer.cpp
// Implementation of the reusable lightbox image viewer (zoom / pan image +
// info panel in its own window). Shared by the markdown renderer and Album.
// Version: 1.0.0
// Last Modified: 2026-06-25
// Author: UltraCanvas Framework

#include "UltraCanvasImageViewer.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasApplication.h"
#include <algorithm>

namespace UltraCanvas {

// ===== ZOOM / PAN IMAGE =====

    UltraCanvasZoomPanImage::UltraCanvasZoomPanImage(const std::string& elemId)
        : UltraCanvasUIElement(elemId, 0.0f, 0.0f, 0.0f, 0.0f) {
        SetMouseCursor(UCMouseCursor::SizeAll);
    }

    void UltraCanvasZoomPanImage::SetImage(std::shared_ptr<UCImage> img) {
        image = std::move(img);
        needsFit = true;
        RequestRedraw();
    }

    void UltraCanvasZoomPanImage::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!IsVisible()) return;
        const Rect2Df b = GetLocalBounds();
        if (b.width <= 0 || b.height <= 0) return;

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(b.x, b.y, b.width, b.height));
        ctx->DrawFilledRectangle(Rect2Dd(b.x, b.y, b.width, b.height), canvasColor, 0.0f);

        if (image && image->IsValid()) {
            const double iw = std::max(1, image->GetWidth());
            const double ih = std::max(1, image->GetHeight());
            const double fit = std::min(b.width / iw, b.height / ih);
            if (needsFit) { zoom = 1.0; panX = panY = 0.0; needsFit = false; }
            const double s     = fit * zoom;
            const double dispW = iw * s;
            const double dispH = ih * s;
            const double left  = b.width  * 0.5 + panX - dispW * 0.5;
            const double top   = b.height * 0.5 + panY - dispH * 0.5;
            ctx->DrawImage(*image, Rect2Dd(left, top, dispW, dispH), ImageFitMode::Fill);
        }
        ctx->PopState();
    }

    bool UltraCanvasZoomPanImage::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseWheel:
                return HandleZoom(event);
            case UCEventType::MouseDown:
                if (Contains(event.pointer)) {
                    dragging = true;
                    lastX = event.pointer.x;
                    lastY = event.pointer.y;
                    // Capture so the drag keeps tracking even when the pointer
                    // leaves the element bounds.
                    if (auto* app = UltraCanvasApplication::GetInstance()) {
                        app->CaptureMouse(this);
                    }
                    return true;
                }
                return false;
            case UCEventType::MouseMove:
                if (dragging) {
                    panX += event.pointer.x - lastX;
                    panY += event.pointer.y - lastY;
                    lastX = event.pointer.x;
                    lastY = event.pointer.y;
                    RequestRedraw();
                    return true;
                }
                return false;
            case UCEventType::MouseUp:
                if (dragging) {
                    dragging = false;
                    if (auto* app = UltraCanvasApplication::GetInstance()) {
                        app->ReleaseMouse();
                    }
                    return true;
                }
                return false;
            case UCEventType::MouseDoubleClick:
                needsFit = true;   // reset to fit-to-view
                RequestRedraw();
                return true;
            default:
                return false;
        }
    }

    bool UltraCanvasZoomPanImage::HandleZoom(const UCEvent& event) {
        if (!image || !image->IsValid()) return false;
        const Rect2Df b = GetLocalBounds();
        const double iw = std::max(1, image->GetWidth());
        const double ih = std::max(1, image->GetHeight());
        const double fit = std::min(b.width / iw, b.height / ih);
        if (fit <= 0.0) return false;

        const double sOld    = fit * zoom;
        const double leftOld = b.width  * 0.5 + panX - iw * sOld * 0.5;
        const double topOld  = b.height * 0.5 + panY - ih * sOld * 0.5;
        // Image-space point under the cursor — kept fixed across the zoom.
        const double imgX = (event.pointer.x - leftOld) / sOld;
        const double imgY = (event.pointer.y - topOld)  / sOld;

        const double step = (event.wheelDelta > 0) ? 1.15 : (1.0 / 1.15);
        double newZoom = std::max(1.0, std::min(zoom * step, 40.0));
        // Cap the rasterized size so very large SVG zooms stay responsive.
        const double maxDim = 8000.0;
        if (iw * fit * newZoom > maxDim || ih * fit * newZoom > maxDim) {
            newZoom = std::max(1.0, std::min(maxDim / (iw * fit), maxDim / (ih * fit)));
        }
        if (newZoom == zoom) return true;
        zoom = newZoom;

        const double sNew = fit * zoom;
        panX = (event.pointer.x - b.width  * 0.5) - (imgX * sNew - iw * sNew * 0.5);
        panY = (event.pointer.y - b.height * 0.5) - (imgY * sNew - ih * sNew * 0.5);
        RequestRedraw();
        return true;
    }

// ===== LIGHTBOX VIEWER WINDOW =====

    void UltraCanvasImageViewer::Close() {
        if (window) {
            window->Close();
            window.reset();
        }
    }

    void UltraCanvasImageViewer::Show(const std::string& imagePath,
                                      const ImageViewerInfo& info,
                                      UltraCanvasWindowBase* host,
                                      const Color& imageBackground) {
        // Reuse a single window: close any previous one before opening anew.
        Close();

        WindowConfig cfg;
        cfg.title     = info.title.empty() ? "Image" : info.title;
        cfg.type      = WindowType::Standard;   // an explicit, movable/resizable window
        cfg.resizable = true;
        // Position the viewer over the host window so it opens where the user is
        // looking rather than at the screen origin.
        if (host) {
            cfg.width  = std::max(640, (int)host->GetWidth());
            cfg.height = std::max(480, (int)host->GetHeight());
            int hx = 0, hy = 0;
            host->GetScreenPosition(hx, hy);
            cfg.x = hx;
            cfg.y = hy;
            cfg.parentWindow = host;
        } else {
            cfg.width  = 1040;
            cfg.height = 720;
        }
        window = CreateWindow(cfg);
        if (!window) return;
        window->SetBackgroundColor(Color(22, 22, 26, 255));
        window->layout.SetFlexColumn()
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // Image area: fills the top, zoom + pan enabled.
        auto image = std::make_shared<UltraCanvasZoomPanImage>("ImageViewerImage");
        image->SetCanvasColor(imageBackground);
        image->LoadFromFile(imagePath);
        image->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        window->AddChild(image);

        // Info panel pinned below the image.
        auto panel = std::make_shared<UltraCanvasContainer>("ImageViewerPanel");
        panel->SetBackgroundColor(Color(30, 30, 36, 255));
        panel->layout.SetFlexColumn().SetFlexGap(4)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        panel->SetPadding(12, 18, 12, 18);
        panel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        if (!info.title.empty()) {
            auto titleLbl = std::make_shared<UltraCanvasLabel>("ImageViewerTitle", 0, 0, 0, 26);
            titleLbl->SetText(info.title);
            titleLbl->SetFontSize(18);
            titleLbl->SetFontWeight(FontWeight::Bold);
            titleLbl->SetTextColor(Color(245, 245, 248, 255));
            titleLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(titleLbl);
        }

        if (!info.subtitle.empty()) {
            auto subLbl = std::make_shared<UltraCanvasLabel>("ImageViewerSubtitle", 0, 0, 0, 18);
            subLbl->SetText(info.subtitle);
            subLbl->SetFontSize(12);
            subLbl->SetTextColor(Color(170, 170, 178, 255));
            subLbl->SetWrap(TextWrap::WrapWord);
            subLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(subLbl);
        }

        if (!info.description.empty()) {
            auto descLbl = std::make_shared<UltraCanvasLabel>("ImageViewerDescription", 0, 0, 0, 48);
            descLbl->SetText(info.description);
            descLbl->SetFontSize(13);
            descLbl->SetTextColor(Color(214, 214, 220, 255));
            descLbl->SetWrap(TextWrap::WrapWord);
            descLbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(descLbl);
        }

        if (!info.hint.empty()) {
            auto hint = std::make_shared<UltraCanvasLabel>("ImageViewerHint", 0, 0, 0, 16);
            hint->SetText(info.hint);
            hint->SetFontSize(10);
            hint->SetTextColor(Color(120, 120, 128, 255));
            hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(hint);
        }

        window->AddChild(panel);

        // Esc closes the viewer.
        window->eventCallback = [this](const UCEvent& event) {
            if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                Close();
                return true;
            }
            return false;
        };

        window->Show();
        // Reassert the position after Show: some X11 window managers ignore the
        // create-time coordinates for top-level windows, so move it over the host
        // window explicitly once it exists.
        if (host) {
            window->SetWindowPosition(cfg.x, cfg.y);
        }
    }

} // namespace UltraCanvas
