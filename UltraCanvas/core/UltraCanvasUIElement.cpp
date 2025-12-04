// UltraCanvasUIElement.cpp
// Modern C++ base class system for all UI components
// Version: 3.0.0
// Last Modified: 2025-01-04
// Author: UltraCanvas Framework
#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {
    // new here
    void UltraCanvasUIElement::ConvertWindowToParentContainerCoordinates(int &x, int &y) {
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                Rect2Di contentArea = pc->GetContentRect();
                x = x - contentArea.x + pc->GetHorizontalScrollPosition();
                y = y - contentArea.y + pc->GetVerticalScrollPosition();
                pc = pc->GetParentContainer();
            }
        }
    }

    Point2Di UltraCanvasUIElement::ConvertWindowToParentContainerCoordinates(const Point2Di &globalPos) {
        Point2Di pos = globalPos;
        ConvertWindowToParentContainerCoordinates(pos.x, pos.y);
        return pos;
    }

    void UltraCanvasUIElement::ConvertContainerToWindowCoordinates(int &x, int &y) {
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                Rect2Di contentArea = pc->GetContentRect();
                x = x + contentArea.x - pc->GetHorizontalScrollPosition();
                y = y + contentArea.y - pc->GetVerticalScrollPosition();
                pc = pc->GetParentContainer();
            }
        }
    }

    Point2Di UltraCanvasUIElement::ConvertContainerToWindowCoordinates(const Point2Di &localPos) {
        Point2Di pos = localPos;
        ConvertContainerToWindowCoordinates(pos.x, pos.y);
        return pos;
    }

    void UltraCanvasUIElement::RequestRedraw() {
        if (window) {
            window->MarkElementDirty(this);
        }
    }

//    void UltraCanvasUIElement::RequestFullRedraw() {
//        if (window) {
//            window->RequestFullRedraw();
//        }
//    }

    IRenderContext* UltraCanvasUIElement::GetRenderContext() const {
        if (window) {
            return window->GetRenderContext();
        }
        return nullptr;
    }

    void UltraCanvasUIElement::AddThisPopupElementToWindow() {
        if (window) {
            window->AddPopupElement(this);
        }
    }

    void UltraCanvasUIElement::RemoveThisPopupElementFromWindow() {
        if (window) {
            window->RemovePopupElement(this);
        }
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

//    int UltraCanvasUIElement::GetXInWindow() {
//        int totalX = properties.x_pos;
//
//        if (parentContainer) {
//            // Get parent's position in window coordinates
//            int parentWindowX = parentContainer->GetXInWindow();
//
//            // Add parent's content area offset
//            Rect2Di parentContentArea = parentContainer->GetContentArea();
//
//            // Calculate final position: parent window position + content area offset + our relative position
//            totalX = parentWindowX + parentContentArea.x + properties.x_pos;
//        }
//
//        return totalX;
//    }
//
//// Fixed version of GetYInWindow() in UltraCanvasUIElement.cpp
//    int UltraCanvasUIElement::GetYInWindow() {
//        int totalY = properties.y_pos;
//
//        if (parentContainer) {
//            // Get parent's position in window coordinates
//            int parentWindowY = parentContainer->GetYInWindow();
//
//            // Add parent's content area offset
//            Rect2Di parentContentArea = parentContainer->GetContentArea();
//
//            // Calculate final position: parent window position + content area offset + our relative position
//            totalY = parentWindowY + parentContentArea.y + properties.y_pos;
//        }7
//
//        return totalY;
//    }
    void UltraCanvasUIElement::Render(IRenderContext* ctx) {
        auto bnds = GetBounds();
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
            ctx->DrawRoundedRectangleWidthBorders(bnds.x, bnds.y, bnds.width, bnds.height, backgroundColor.a > 0,
                                                  leftWidth, rightWidth, topWidth, bottomWidth,
                                                  leftColor, rightColor, topColor, bottomColor,
                                                  leftRadius, rightRadius, topRadius, bottomRadius,
                                                  leftDash, rightDash, topDash, bottomDash);
        } else {
            if (backgroundColor.a > 0) {
                ctx->SetFillPaint(backgroundColor);
                ctx->FillRectangle(bnds.x, bnds.y, bnds.width, bnds.height);
            }
        }
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
                std::cerr << "Warning: Element " << GetIdentifier() << " has no window assigned" << std::endl;
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
        if (window) {
            SetFocus(false);
            window->RequestRedraw();
        }
    }

    void UltraCanvasUIElement::SetWindow(UltraCanvasWindowBase *win) {
        if (win == nullptr && window) {
            SetFocus(false);
            RemoveThisPopupElementFromWindow();
        }
        window = win;
    }

    void UltraCanvasUIElement::SetOriginalSize(int w, int h) {
        originalSize.width = w;
        originalSize.height = h;
        if (parentContainer) {
            parentContainer->InvalidateLayout();
        } else {
            SetSize(w, h);
        }
    }
}
