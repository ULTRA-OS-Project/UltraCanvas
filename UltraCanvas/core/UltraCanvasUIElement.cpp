// UltraCanvasUIElement.cpp
// UI base class implementation; geometry and box model live on
// UltraCanvas::CSSLayout::Element (the new base).
// Version: 4.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    UltraCanvasUIElement::~UltraCanvasUIElement() {
        auto app = UltraCanvasApplication::GetInstance();
        if (app) {
            app->CleanupElementReferences(this);
        }
    }

    UltraCanvasContainer* UltraCanvasUIElement::GetParentContainer() const {
        return dynamic_cast<UltraCanvasContainer*>(this->Parent());
    }

    Point2Df UltraCanvasUIElement::MapFromLocal(const Point2Df &localPos, UltraCanvasContainer* mapToParent) {
        Point2Df pos = localPos;
        if (auto* parentCont = GetParentContainer()) {
            auto pc = parentCont;
            while(pc) {
                Rect2Df contentArea = pc->GetContentRect();
                pos.x = pos.x + contentArea.x - pc->GetHorizontalScrollPosition();
                pos.y = pos.y + contentArea.y - pc->GetVerticalScrollPosition();
                if (pc == mapToParent) break;
                pc = pc->GetParentContainer();
            }
            pos.x += finalBounds.x;
            pos.y += finalBounds.y;
        }
        return pos;
    }

    Point2Df UltraCanvasUIElement::MapToLocal(const Point2Df &globalPos, UltraCanvasContainer* mapFromParent) {
        Point2Df pos = globalPos;
        if (auto* parentCont = GetParentContainer()) {
            auto pc = parentCont;
            while(pc) {
                Rect2Df contentArea = pc->GetContentRect();
                pos.x = pos.x - contentArea.x + pc->GetHorizontalScrollPosition();
                pos.y = pos.y - contentArea.y + pc->GetVerticalScrollPosition();
                if (pc == mapFromParent) break;
                pc = pc->GetParentContainer();
            }
            pos.x -= finalBounds.x;
            pos.y -= finalBounds.y;
        }
        return pos;
    }

    void UltraCanvasUIElement::InvalidateRect(const Rect2Df& localRect) {
        if (!window || localRect.width <= 0 || localRect.height <= 0) return;

        // Walk up looking for a popup ancestor; popups own their own dirty
        // queue so they can be re-rendered into their offscreen surface
        // independently of the main window content.
        UltraCanvasUIElement* popupAncestor = nullptr;
        for (UltraCanvasUIElement* cur = this; cur && cur != window; cur = cur->GetParentContainer()) {
            if (cur->isPopup) { popupAncestor = cur; break; }
        }

        Point2Df posInWindow = GetPositionInWindow();

        if (popupAncestor) {
            Point2Df popupPosInWindow = popupAncestor->GetPositionInWindow();
            Rect2Df rectInPopup(localRect.x + posInWindow.x - popupPosInWindow.x,
                                localRect.y + posInWindow.y - popupPosInWindow.y,
                                localRect.width, localRect.height);
            window->AddPopupDirtyRect(popupAncestor, rectInPopup);
        } else {
            Rect2Df rectInWindow(localRect.x + posInWindow.x,
                                 localRect.y + posInWindow.y,
                                 localRect.width, localRect.height);
            window->AddDirtyRectangle(rectInWindow);
        }
    }

    void UltraCanvasUIElement::RequestRedraw() {
        InvalidateRect(GetLocalBounds());
    }

    void UltraCanvasUIElement::RequestUpdateGeometry() {
        needsUpdateGeometry = true;
        if (!window || this == window) return;
        for (UltraCanvasUIElement* cur = this; cur && cur != window; cur = cur->GetParentContainer()) {
            if (cur->isPopup) {
                window->RequestPopupGeometry();
                return;
            }
        }
        window->RequestUpdateGeometry();
    }

    IRenderContext* UltraCanvasUIElement::GetRenderContext() const {
        if (renderContext) {
            return renderContext.get();
        }
        auto pc = GetParentContainer();
        while (pc) {
            if (pc->renderContext) {
                return pc->renderContext.get();
            }
            pc = pc->GetParentContainer();
        }
        return nullptr;
    }

    UltraCanvasContainer* UltraCanvasUIElement::GetRootContainer() {
        UltraCanvasContainer* root = GetParentContainer();
        while (root && root->GetParentContainer()) {
            root = root->GetParentContainer();
        }
        return root;
    }

    bool UltraCanvasUIElement::IsDescendantOf(const UltraCanvasContainer* container) const {
        if (!container) return false;

        UltraCanvasContainer* current = GetParentContainer();
        while (current) {
            if (current == container) return true;
            current = current->GetParentContainer();
        }
        return false;
    }

    void UltraCanvasUIElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        auto bnds = GetLocalBounds();
        float leftWidth   = GetBorderLeftWidth();
        float rightWidth  = GetBorderRightWidth();
        float topWidth    = GetBorderTopWidth();
        float bottomWidth = GetBorderBottomWidth();
        if (leftWidth > 0 || rightWidth > 0 || topWidth > 0 || bottomWidth > 0) {
            float leftRadius = 0, rightRadius = 0, topRadius = 0, bottomRadius = 0;
            Color leftColor   = Colors::Transparent;
            Color rightColor  = Colors::Transparent;
            Color topColor    = Colors::Transparent;
            Color bottomColor = Colors::Transparent;
            UCDashPattern leftDash, rightDash, topDash, bottomDash;

            const BordersVisual* bv = bordersVisual ? &(*bordersVisual) : nullptr;
            if (leftWidth > 0 && bv) {
                leftRadius = bv->left.radius;
                leftColor  = bv->left.color;
                leftDash   = bv->left.dashPattern;
            }
            if (rightWidth > 0 && bv) {
                rightRadius = bv->right.radius;
                rightColor  = bv->right.color;
                rightDash   = bv->right.dashPattern;
            }
            if (topWidth > 0 && bv) {
                topRadius = bv->top.radius;
                topColor  = bv->top.color;
                topDash   = bv->top.dashPattern;
            }
            if (bottomWidth > 0 && bv) {
                bottomRadius = bv->bottom.radius;
                bottomColor  = bv->bottom.color;
                bottomDash   = bv->bottom.dashPattern;
            }
            if (backgroundColor.a > 0) {
                ctx->SetFillPaint(backgroundColor);
            }
            ctx->DrawRoundedRectangleWidthBorders(bnds, backgroundColor.a > 0,
                                                  leftWidth, rightWidth, topWidth, bottomWidth,
                                                  leftColor, rightColor, topColor, bottomColor,
                                                  leftRadius, rightRadius, topRadius, bottomRadius,
                                                  leftDash, rightDash, topDash, bottomDash);
        } else {
            if (backgroundColor.a > 0) {
                ctx->SetFillPaint(backgroundColor);
                ctx->FillRectangle(bnds);
            }
        }
    }

    Point2Df UltraCanvasUIElement::GetPositionInWindow() const {
        Point2Df pos;
        if (auto* parentCont = GetParentContainer()) {
            auto pc = parentCont;
            while(pc) {
                pos.x += (pc->GetContentRect().x - pc->GetHorizontalScrollPosition());
                pos.y += (pc->GetContentRect().y - pc->GetVerticalScrollPosition());
                pc = pc->GetParentContainer();
            }
        }
        pos.x += finalBounds.x;
        pos.y += finalBounds.y;
        return pos;
    }

    float UltraCanvasUIElement::GetXInWindow() {
        float pos = 0.0f;
        if (auto* parentCont = GetParentContainer()) {
            auto pc = parentCont;
            while(pc) {
                pos += (pc->GetContentRect().x - pc->GetHorizontalScrollPosition());
                pc = pc->GetParentContainer();
            }
        }
        return pos + finalBounds.x;
    }

    float UltraCanvasUIElement::GetYInWindow() {
        float pos = 0.0f;
        if (auto* parentCont = GetParentContainer()) {
            auto pc = parentCont;
            while(pc) {
                pos += (pc->GetContentRect().y - pc->GetVerticalScrollPosition());
                pc = pc->GetParentContainer();
            }
        }
        return pos + finalBounds.y;
    }

    bool UltraCanvasUIElement::SetFocus(bool focus) {
        if (focus) {
            if (!window) {
                debugOutput << "Warning: Element " << GetIdentifier() << " has no window assigned" << std::endl;
                return false;
            }
            return window->RequestElementFocus(this);
        } else {
            if (window && window->GetFocusedElement() == this) {
                window->ClearFocus();
                return true;
            }
        }

        return false;
    }

    bool UltraCanvasUIElement::IsFocused() const {
        if (window && window->IsWindowFocused() && window->GetFocusedElement() == this) {
            return true;
        }
        return false;
    }

    void UltraCanvasUIElement::SetVisible(bool vis) {
        if (visible == vis) {
            return;
        }
        visible = vis;
        // Invalidate the rect we're about to vacate or occupy, in parent space.
        Rect2Df b = GetBounds();
        if (auto* parentCont = GetParentContainer()) {
            parentCont->InvalidateRect(b);
            parentCont->InvalidateLayout();
        } else if (window && this != window) {
            window->AddDirtyRectangle(b);
        }
        if (!visible) {
            SetFocus(false);
        }
        RequestUpdateGeometry();
    }

    void UltraCanvasUIElement::SetWindow(UltraCanvasWindowBase *win) {
        if (win == nullptr && window) {
            SetFocus(false);
        }
        window = win;
    }

    void UltraCanvasUIElement::SetOriginalSize(float w, float h) {
        size.width  = CSSLayout::Dimension::Px(w);
        size.height = CSSLayout::Dimension::Px(h);
        if (auto* parentCont = GetParentContainer()) {
            parentCont->InvalidateLayout();
        } else {
            SetSize(w, h);
        }
    }

    void UltraCanvasUIElement::SetBounds(const Rect2Df &b) {
        Rect2Df oldBounds = GetBounds();
        if (oldBounds == b) return;
        if (oldBounds.Size() != b.Size()) {
            RequestUpdateGeometry();
        }
        if (auto* parentCont = GetParentContainer()) {
            parentCont->RequestUpdateGeometry();
        }
        finalBounds = CSSLayout::LayoutRect{b.x, b.y, b.width, b.height};
        // Invalidate union of old+new bounds in parent space — bounds are stored
        // in parent coords, so this is what the parent (or window) needs to repaint.
        Rect2Df damage = oldBounds.Union(b);
        if (auto* parentCont = GetParentContainer()) {
            parentCont->InvalidateRect(damage);
        } else if (window && this != window) {
            window->AddDirtyRectangle(damage);
        }
    }

    void UltraCanvasUIElement::SetEventCallback(std::function<bool(const UCEvent &)> callback) {
        eventCallback = callback;
    }

    bool UltraCanvasUIElement::OnEvent(const UCEvent &event) {
        if (eventCallback) {
            return eventCallback(event);
        }
        return false;
    }

}
