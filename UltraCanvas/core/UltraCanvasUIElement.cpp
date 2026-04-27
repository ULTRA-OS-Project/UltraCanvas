// UltraCanvasUIElement.cpp
// Modern C++ base class system for all UI components
// Version: 3.0.0
// Last Modified: 2025-01-04
// Author: UltraCanvas Framework
#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    // new here
    UltraCanvasUIElement::~UltraCanvasUIElement() {
    }

    Point2Di UltraCanvasUIElement::MapFromLocal(const Point2Di &localPos, UltraCanvasContainer* mapToParent) {
        Point2Di pos = localPos;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                Rect2Di contentArea = pc->GetContentRect();
                pos.x = pos.x + contentArea.x - pc->GetHorizontalScrollPosition();
                pos.y = pos.y + contentArea.y - pc->GetVerticalScrollPosition();
                if (pc == mapToParent) break;
                pc = pc->GetParentContainer();
            }
            pos.x += bounds.x;
            pos.y += bounds.y;
        }
        return pos;
    }

    Point2Di UltraCanvasUIElement::MapToLocal(const Point2Di &globalPos, UltraCanvasContainer* mapFromParent) {
        Point2Di pos = globalPos;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                Rect2Di contentArea = pc->GetContentRect();
                pos.x = pos.x - contentArea.x + pc->GetHorizontalScrollPosition();
                pos.y = pos.y - contentArea.y + pc->GetVerticalScrollPosition();
                if (pc == mapFromParent) break;
                pc = pc->GetParentContainer();
            }
            pos.x -= bounds.x;
            pos.y -= bounds.y;
        }
        return pos;
    }

    void UltraCanvasUIElement::RequestRedraw() {
        needsRedraw = true;
        if (!window || this == window) return;
        for (UltraCanvasUIElement* cur = this; cur && cur != window; cur = cur->parentContainer) {
            if (cur->isPopup) {
                window->RequestPopupRedraw();
                return;
            }
        }
        window->RequestWindowRedraw();
    }

    void UltraCanvasUIElement::RequestUpdateGeometry() {
        needsUpdateGeometry = true;
        if (!window || this == window) return;
        for (UltraCanvasUIElement* cur = this; cur && cur != window; cur = cur->parentContainer) {
            if (cur->isPopup) {
                window->RequestPopupGeometry();
                return;
            }
        }
        window->RequestUpdateGeometry();
    }

//    void UltraCanvasUIElement::RequestFullRedraw() {
//        if (window) {
//            window->RequestFullRedraw();
//        }
//    }

    IRenderContext* UltraCanvasUIElement::GetRenderContext() const {
        if (renderContext) {
            return renderContext.get();
        }
        auto pc = parentContainer;
        while (pc) {
            if (pc->renderContext) {
                return pc->renderContext.get();
            }
            pc = pc->parentContainer;
        }
        return nullptr;
    }

    UltraCanvasContainer* UltraCanvasUIElement::GetRootContainer() {
        UltraCanvasContainer* root = parentContainer;
        while (root && root->GetParentContainer()) {
            root = root->GetParentContainer();
        }
        return root;
    }

    bool UltraCanvasUIElement::IsDescendantOf(const UltraCanvasContainer* container) const {
        if (!container) return false;

        UltraCanvasContainer* current = parentContainer;
        while (current) {
            if (current == container) return true;
            current = current->GetParentContainer();
        }
        return false;
    }

    void UltraCanvasUIElement::Render(IRenderContext* ctx) {
        auto bnds = GetElementLocalBounds();
        int leftWidth = GetBorderLeftWidth();
        int rightWidth = GetBorderRightWidth();
        int topWidth = GetBorderTopWidth();
        int bottomWidth = GetBorderBottomWidth();
        if (leftWidth > 0 || rightWidth > 0 || topWidth > 0 || bottomWidth > 0) {
            int leftRadius = 0;
            int rightRadius = 0;
            int topRadius = 0;
            int bottomRadius = 0;
            Color leftColor = Colors::Transparent;
            Color rightColor = Colors::Transparent;
            Color topColor = Colors::Transparent;
            Color bottomColor = Colors::Transparent;
            UCDashPattern leftDash;
            UCDashPattern rightDash;
            UCDashPattern topDash;
            UCDashPattern bottomDash;

            if (leftWidth > 0) {
                leftRadius = borderLeft->radius;
                leftColor = borderLeft->color;
                leftDash = borderLeft->dashPattern;
            }
            if (rightWidth > 0) {
                rightRadius = borderRight->radius;
                rightColor = borderRight->color;
                rightDash = borderRight->dashPattern;
            }
            if (topWidth > 0) {
                topRadius = borderTop->radius;
                topColor = borderTop->color;
                topDash = borderTop->dashPattern;
            }
            if (bottomWidth > 0) {
                bottomRadius = borderBottom->radius;
                bottomColor = borderBottom->color;
                bottomDash = borderBottom->dashPattern;
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

    Point2Di UltraCanvasUIElement::GetPositionInWindow() const {
        Point2Di pos;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos.x += (pc->GetContentRect().x - pc->GetHorizontalScrollPosition());
                pos.y += (pc->GetContentRect().y - pc->GetVerticalScrollPosition());
                pc = pc->parentContainer;
            }
        }
        pos.x += bounds.x;
        pos.y += bounds.y;
        return pos;
    }

    int UltraCanvasUIElement::GetXInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += (pc->GetContentRect().x - pc->GetHorizontalScrollPosition());
                pc = pc->parentContainer;
            }
        }
        return pos + bounds.x;
    }

    int UltraCanvasUIElement::GetYInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += (pc->GetContentRect().y - pc->GetVerticalScrollPosition());
                pc = pc->parentContainer;
            }
        }
        return pos + bounds.y;
    }

    bool UltraCanvasUIElement::SetFocus(bool focus) {
        // If trying to set focus, delegate to window's focus management
        if (focus) {
            if (!window) {
                debugOutput << "Warning: Element " << GetIdentifier() << " has no window assigned" << std::endl;
                return false;
            }

            // Request focus through the window's focus management system
            return window->RequestElementFocus(this);
        } else {
            // If trying to remove focus, clear focus at window level
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
        if (parentContainer) {
            parentContainer->InvalidateLayout();
        }
        SetFocus(false);
        RequestUpdateGeometry();
    }

    void UltraCanvasUIElement::SetWindow(UltraCanvasWindowBase *win) {
        if (win == nullptr && window) {
            SetFocus(false);
        }
        window = win;
    }

    void UltraCanvasUIElement::SetOriginalSize(int w, int h) {
        explicitSize.width = w;
        explicitSize.height = h;
        if (parentContainer) {
            parentContainer->InvalidateLayout();
        } else {
            SetSize(w, h);
        }
    }

    void UltraCanvasUIElement::SetBounds(const Rect2Di &b) {
        if (bounds != b) {
            if (bounds.Size() != b.Size()) {
                RequestUpdateGeometry();
            }
            if (parentContainer) {
                parentContainer->RequestUpdateGeometry();
            }
            bounds = b;
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
