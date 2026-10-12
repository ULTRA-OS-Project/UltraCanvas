// core/UltraCanvasFileDragIcon.cpp
// The icon of a file about to be written, dragged onto a folder to say where
// it goes. See UltraCanvasFileDragIcon.h.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileDragIcon.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace UltraCanvas {

    namespace {
        // Badge: the icon and the name, as the file display's drag badge.
        constexpr int kBadgeIcon = 20;
        constexpr int kBadgePadX = 8;
        constexpr int kBadgePadY = 6;
        constexpr int kBadgeGap = 6;
        constexpr int kBadgeMaxText = 220;
        constexpr float kBadgeFontSize = 11.0f;

        std::string LastComponent(const std::string& path) {
            std::string name = path;
            while (name.size() > 1 && (name.back() == '/' || name.back() == '\\')) name.pop_back();
            const size_t slash = name.find_last_of("/\\");
            return slash == std::string::npos ? name : name.substr(slash + 1);
        }

        std::string ExtensionTag(const std::string& name) {
            const size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size()) return std::string();
            std::string ext = name.substr(dot + 1, 4);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return ext;
        }
    } // namespace

    UltraCanvasFileDragIcon::UltraCanvasFileDragIcon(const std::string& identifier,
                                                     long x, long y, long w, long h)
            : UltraCanvasUIElement(identifier, x, y, w, h) {
        // An open hand would say "drag me" best; the pointing hand is the
        // nearest the platforms all have.
        mouseCursor = UCMouseCursor::Hand;
    }

    UltraCanvasFileDragIcon::~UltraCanvasFileDragIcon() {
        // A dialog closed in the middle of a drag (Escape, a timer) takes the
        // icon with it: the pointer and the window's overlay are given back.
        if (mouseCaptured) {
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                if (app->GetCapturedElement() == this) app->ReleaseMouse();
            }
        }
        if (overlayShown) {
            if (auto* win = GetWindow()) win->ClearDragOverlay(this);
        }
    }

    void UltraCanvasFileDragIcon::SetFileName(const std::string& name, bool isFolder) {
        if (name == fileName && isFolder == folder) return;
        fileName = name;
        folder = isFolder;
        if (dragging) MeasureBadge();
        RequestRedraw();
    }

    void UltraCanvasFileDragIcon::SetIconPainter(IconPainter painter) {
        iconPainter = std::move(painter);
        RequestRedraw();
    }

// ----- drawing -----

    void UltraCanvasFileDragIcon::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        const Rect2Df content = GetLocalContentRect();
        // A square in the middle of the box, a little in from its edges.
        const int edge = static_cast<int>(std::min(content.width, content.height)) - 2;
        if (edge <= 4) return;
        const Rect2Di rect(static_cast<int>(content.x + (content.width - edge) / 2.0f),
                           static_cast<int>(content.y + (content.height - edge) / 2.0f),
                           edge, edge);
        // While it is being dragged the icon stays put, faded: what moves is
        // the badge under the pointer. Faded as a group, because a fill's
        // colour does not take the context's alpha.
        const bool faded = dragging || IsDisabled();
        ctx->PushState();
        if (faded) ctx->BeginGroup();
        DrawIcon(ctx, rect);
        if (faded) ctx->EndGroup(0.4);
        ctx->PopState();
    }

    void UltraCanvasFileDragIcon::DrawIcon(IRenderContext* ctx, const Rect2Di& rect) {
        if (iconPainter) {
            iconPainter(ctx, fileName, folder, rect);
            return;
        }
        if (folder) {
            // A tab above the body, in the file display's folder yellow.
            const Color tab(222, 170, 60, 255);
            const Color body(236, 190, 85, 255);
            const double tabH = std::max(2.0, rect.height * 0.18);
            ctx->SetFillPaint(tab);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y, rect.width * 0.45, tabH * 2), 2);
            ctx->SetFillPaint(body);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y + tabH, rect.width, rect.height - tabH), 2);
            return;
        }
        DrawDocumentGlyph(ctx, rect, fileName, Color(96, 125, 160, 255), Colors::White);
    }

    void UltraCanvasFileDragIcon::DrawDocumentGlyph(IRenderContext* ctx, const Rect2Di& rect,
                                                    const std::string& name, const Color& band,
                                                    const Color& ink) {
        if (!ctx || rect.width < 6 || rect.height < 6) return;
        // The page: three quarters as wide as it is tall, centred, its top
        // right corner folded down.
        const double h = rect.height;
        const double w = std::min<double>(rect.width, h * 0.78);
        const double x = rect.x + (rect.width - w) / 2.0;
        const double y = rect.y;
        const double fold = w * 0.28;
        const double line = std::max(1.0, h / 40.0);
        ctx->PushState();
        ctx->SetStrokeWidth(static_cast<float>(line));
        ctx->SetStrokePaint(Color(110, 110, 118, 255));
        const std::vector<Point2Dd> page = {
                {x + line / 2, y + line / 2}, {x + w - fold, y + line / 2}, {x + w - line / 2, y + fold},
                {x + w - line / 2, y + h - line / 2}, {x + line / 2, y + h - line / 2}};
        ctx->SetFillPaint(Color(253, 253, 254, 255));
        ctx->FillLinePath(page);
        ctx->DrawLinePath(page, true);
        const std::vector<Point2Dd> corner = {
                {x + w - fold, y + line / 2}, {x + w - fold, y + fold}, {x + w - line / 2, y + fold}};
        ctx->SetFillPaint(Color(222, 222, 228, 255));
        ctx->FillLinePath(corner);
        ctx->DrawLinePath(corner, true);

        // The band with the type on it, across the lower half and out over
        // the page's right edge, as a label stuck on.
        const Rect2Dd label(x + w * 0.16, y + h * 0.50, w * 0.90, h * 0.28);
        ctx->SetFillPaint(band);
        ctx->FillRectangle(label);
        const std::string tag = ExtensionTag(LastComponent(name));
        if (!tag.empty() && rect.height >= 28) {
            FontStyle font;
            font.fontSize = std::max(7.0, label.height * 0.58);
            font.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(font);
            ctx->SetTextPaint(ink);
            const Size2Di ts = ctx->GetTextLineDimensions(tag);
            ctx->DrawText(tag, Point2Dd(label.x + (label.width - ts.width) / 2.0,
                                        label.y + (label.height - ts.height) / 2.0));
        }
        ctx->PopState();
    }

    std::string UltraCanvasFileDragIcon::BadgeLabel() const {
        const std::string name = LastComponent(fileName);
        return name.empty() ? std::string(folder ? "Folder" : "File") : name;
    }

    void UltraCanvasFileDragIcon::MeasureBadge() {
        // Measured once per drag: the name does not change while it runs.
        // Without a context (not on a window) an estimate keeps it near.
        const std::string label = BadgeLabel();
        Size2Di ts(std::min(kBadgeMaxText, static_cast<int>(label.size()) * 7), 16);
        if (IRenderContext* ctx = GetRenderContext()) {
            FontStyle font;
            font.fontSize = kBadgeFontSize;
            ctx->PushState();
            ctx->SetFontStyle(font);
            ts = ctx->GetTextLineDimensions(label);
            ts.width = std::min(ts.width, kBadgeMaxText);
            ctx->PopState();
        }
        badgeSize = Size2Di(kBadgePadX * 2 + kBadgeIcon + kBadgeGap + ts.width,
                            std::max(kBadgeIcon, ts.height) + kBadgePadY * 2);
    }

    Rect2Di UltraCanvasFileDragIcon::BadgeRect(const Point2Di& windowPoint) const {
        int bx = windowPoint.x + 14;
        int by = windowPoint.y + 14;
        if (auto* win = GetWindow()) {
            // Kept inside the window: above the pointer at the bottom edge,
            // left of it at the right edge.
            int ww = 0, wh = 0;
            win->GetWindowSize(ww, wh);
            if (bx + badgeSize.width > ww) bx = windowPoint.x - badgeSize.width - 6;
            if (by + badgeSize.height > wh) by = windowPoint.y - badgeSize.height - 6;
            bx = std::max(0, bx);
            by = std::max(0, by);
        }
        return Rect2Di(bx, by, badgeSize.width, badgeSize.height);
    }

    void UltraCanvasFileDragIcon::ShowBadge(const Point2Di& windowPoint) {
        auto* win = GetWindow();
        if (!win) return;
        win->SetDragOverlay(this, BadgeRect(windowPoint),
                [this](IRenderContext* ctx, const Rect2Di& rect) { DrawBadge(ctx, rect); });
        overlayShown = true;
    }

    void UltraCanvasFileDragIcon::HideBadge() {
        if (!overlayShown) return;
        overlayShown = false;
        if (auto* win = GetWindow()) win->ClearDragOverlay(this);
    }

    void UltraCanvasFileDragIcon::DrawBadge(IRenderContext* ctx, const Rect2Di& rect) {
        // Window coordinates, drawn above every element.
        ctx->PushState();
        ctx->SetFillPaint(Color(255, 255, 255, 235));
        ctx->FillRoundedRectangle(Rect2Dd(rect), 5);
        ctx->SetStrokePaint(Color(0, 120, 215, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(rect), 5);
        DrawIcon(ctx, Rect2Di(rect.x + kBadgePadX, rect.y + (rect.height - kBadgeIcon) / 2,
                              kBadgeIcon, kBadgeIcon));
        FontStyle font;
        font.fontSize = kBadgeFontSize;
        ctx->SetFontStyle(font);
        ctx->SetTextPaint(Color(30, 30, 35, 255));
        const Rect2Dd textRect(rect.x + kBadgePadX + kBadgeIcon + kBadgeGap, rect.y,
                               rect.width - (kBadgePadX * 2 + kBadgeIcon + kBadgeGap), rect.height);
        ctx->ClipRect(Rect2Di(static_cast<int>(textRect.x), rect.y,
                              static_cast<int>(textRect.width), rect.height));
        const Size2Di ts = ctx->GetTextLineDimensions(BadgeLabel());
        ctx->DrawText(BadgeLabel(), Point2Dd(textRect.x, rect.y + (rect.height - ts.height) / 2.0));
        ctx->PopState();
    }

// ----- the gesture -----

    bool UltraCanvasFileDragIcon::OnEvent(const UCEvent& event) {
        switch (event.type) {
            case UCEventType::MouseDown: {
                if (dragging) return true;   // the running drag owns the pointer
                if (event.button != UCMouseButton::Left || IsDisabled()) return false;
                pressed = true;
                pressPoint = event.pointerWindow;
                if (auto* app = UltraCanvasApplication::GetInstance()) {
                    app->CaptureMouse(this);
                    mouseCaptured = true;
                }
                return true;
            }
            case UCEventType::MouseMove: {
                if (!pressed) return false;
                const Point2Di p = event.pointerWindow;
                if (!dragging) {
                    if (std::abs(p.x - pressPoint.x) < dragThreshold &&
                        std::abs(p.y - pressPoint.y) < dragThreshold)
                        return true;
                    BeginDrag(p);
                }
                UpdateDrag(p);
                return true;
            }
            case UCEventType::MouseUp: {
                if (!pressed) return false;
                if (event.button != UCMouseButton::Left) return true;
                if (dragging) {
                    FinishDrag(event.pointerWindow);
                } else {
                    EndGesture();
                    if (onClick) onClick();
                }
                return true;
            }
            case UCEventType::KeyDown:
                if (dragging && event.virtualKey == UCKeys::Escape) {
                    CancelDrag();
                    return true;
                }
                return false;
            default:
                break;
        }
        return UltraCanvasUIElement::OnEvent(event);
    }

    void UltraCanvasFileDragIcon::BeginDrag(const Point2Di& windowPoint) {
        dragging = true;
        dragPoint = ClampToWindow(windowPoint);
        MeasureBadge();
        ShowBadge(dragPoint);
        RequestRedraw();
        if (onDragStarted) onDragStarted();
    }

    void UltraCanvasFileDragIcon::UpdateDrag(const Point2Di& windowPoint) {
        dragPoint = ClampToWindow(windowPoint);
        UltraCanvasUIElement* target = TargetAt(dragPoint);
        if (hoverTarget.lock().get() != target) {
            LeaveHoverTarget(dragPoint);
            if (target) {
                hoverTarget = target->weak_from_this();
                SendDragEvent(target, UCEventType::DragEnter, dragPoint);
            }
        }
        if (target) SendDragEvent(target, UCEventType::DragOver, dragPoint);
        ShowBadge(dragPoint);
    }

    void UltraCanvasFileDragIcon::FinishDrag(const Point2Di& windowPoint) {
        const Point2Di p = ClampToWindow(windowPoint);
        // The target is asked where the drop lands while it still shows it:
        // a file display forgets the folder it framed on DragLeave.
        UltraCanvasUIElement* target = TargetAt(p);
        if (target && hoverTarget.lock().get() != target) {
            // Released somewhere the last move did not reach: let it look.
            LeaveHoverTarget(p);
            hoverTarget = target->weak_from_this();
            SendDragEvent(target, UCEventType::DragEnter, p);
            SendDragEvent(target, UCEventType::DragOver, p);
        }
        // The callback may navigate, rebuild the listing or close the dialog
        // (and with it this icon): the gesture is ended first from what is
        // held now, and nothing of `this` is touched after the call.
        std::weak_ptr<UltraCanvasUIElement> self = weak_from_this();
        auto dropped = onDropped;
        if (dropped) dropped(target, p);
        if (self.expired()) return;
        EndGesture();
    }

    void UltraCanvasFileDragIcon::CancelDrag() {
        if (!pressed && !dragging) return;
        const bool wasDragging = dragging;
        EndGesture();
        if (wasDragging && onDragCancelled) onDragCancelled();
    }

    void UltraCanvasFileDragIcon::EndGesture() {
        LeaveHoverTarget(dragPoint);
        if (mouseCaptured) {
            mouseCaptured = false;
            if (auto* app = UltraCanvasApplication::GetInstance()) {
                if (app->GetCapturedElement() == this) app->ReleaseMouse();
            }
        }
        HideBadge();
        const bool wasDragging = dragging;
        pressed = false;
        dragging = false;
        if (wasDragging) RequestRedraw();
    }

    Point2Di UltraCanvasFileDragIcon::ClampToWindow(const Point2Di& windowPoint) const {
        auto* win = GetWindow();
        if (!win) return windowPoint;
        int ww = 0, wh = 0;
        win->GetWindowSize(ww, wh);
        return Point2Di(std::clamp(windowPoint.x, 0, std::max(0, ww - 1)),
                        std::clamp(windowPoint.y, 0, std::max(0, wh - 1)));
    }

    UltraCanvasUIElement* UltraCanvasFileDragIcon::TargetAt(const Point2Di& windowPoint) const {
        auto* win = GetWindow();
        if (!win) return nullptr;
        UltraCanvasUIElement* hit = win->FindElementAtPoint(
                Point2Df(static_cast<float>(windowPoint.x), static_cast<float>(windowPoint.y)), true);
        // The icon is no target for itself, and the window is no target at all.
        if (hit == this || hit == win) return nullptr;
        return hit;
    }

    void UltraCanvasFileDragIcon::SendDragEvent(UltraCanvasUIElement* element, UCEventType type,
                                                const Point2Di& windowPoint) {
        auto* win = GetWindow();
        auto* app = UltraCanvasApplication::GetInstance();
        // An element that has left this window meanwhile is told nothing.
        if (!element || !win || !app || element->GetWindow() != win) return;
        UCEvent e;
        e.type = type;
        e.targetWindow = win->GetWindowWeakPtr();
        e.nativeWindowHandle = win->GetNativeHandle();
        e.pointerWindow = windowPoint;
        e.pointer = windowPoint;
        e.dragMimeType = kDragMimeType;
        e.dragData = fileName;
        // No droppedFiles: there is no file yet, and a target that takes
        // files must not try to copy one.
        app->HandleEventWithBubbling(element, e);
    }

    void UltraCanvasFileDragIcon::LeaveHoverTarget(const Point2Di& windowPoint) {
        std::shared_ptr<UltraCanvasUIElement> previous = hoverTarget.lock();
        hoverTarget.reset();
        if (previous) SendDragEvent(previous.get(), UCEventType::DragLeave, windowPoint);
    }

} // namespace UltraCanvas
