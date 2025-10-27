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
                Rect2Di contentArea = pc->GetContentArea();
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
                Rect2Di contentArea = pc->GetContentArea();
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

    int UltraCanvasUIElement::GetXInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += (pc->GetContentArea().x - pc->GetHorizontalScrollPosition());
                pc = pc->parentContainer;
            }
        }
        return pos + properties.x_pos;
    }

    int UltraCanvasUIElement::GetYInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += (pc->GetContentArea().y - pc->GetVerticalScrollPosition());
                pc = pc->parentContainer;
            }
        }
        return pos + properties.y_pos;
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

    void UltraCanvasUIElement::SetVisible(bool visible) {
        properties.Visible = visible;
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
}
