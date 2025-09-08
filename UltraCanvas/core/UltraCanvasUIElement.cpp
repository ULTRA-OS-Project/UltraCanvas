// UltraCanvasUIElement.cpp
// Modern C++ base class system for all UI components
// Version: 3.0.0
// Last Modified: 2025-01-04
// Author: UltraCanvas Framework
#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"

namespace UltraCanvas {
//    void UltraCanvasElement::AddChild(UltraCanvasElement *child) {
//        if (!child || child == this) return;
//
//        // Remove from previous parent
//        if (child->parent) {
//            child->parent->RemoveChild(child);
//        }
//
//        if (child->GetZIndex() == 0) {
//            // Auto-assign appropriate z-index based on element type
//            std::string typeName = typeid(*child).name();
//
//            if (typeName.find("Menu") != std::string::npos) {
//                child->SetZIndex(UltraCanvas::ZLayers::Menus);
//            }
//            else if (typeName.find("Dropdown") != std::string::npos) {
//                child->SetZIndex(UltraCanvas::ZLayers::Dropdowns);
//            }
//            else if (typeName.find("Tooltip") != std::string::npos) {
//                child->SetZIndex(UltraCanvas::ZLayers::Tooltips);
//            }
//            else {
//                child->SetZIndex(UltraCanvas::ZLayers::Controls);
//            }
//        }
//
//        // Add to this element
//        children.push_back(child);
//        child->parent = this;
//        child->properties.ParentObject = this->properties.IdentifierID;
//
//        // Propagate window reference
//        if (window) {
//            child->SetWindow(window);
//            if (child->IsHandleOutsideClicks()) {
//                window->
//            }
//        }
//
//        // Notify derived classes
//        OnChildAdded(child);
//
//        // Request redraw
//        RequestRedraw();
//    }
//
//    void UltraCanvasElement::RemoveChild(UltraCanvasElement *child) {
//        auto it = std::find(children.begin(), children.end(), child);
//        if (it != children.end()) {
//            (*it)->parent = nullptr;
//            (*it)->properties.ParentObject = 0;
//            (*it)->SetWindow(nullptr);
//            children.erase(it);
//
//            // Notify derived classes
//            OnChildRemoved(child);
//
//            // Request redraw
//            RequestRedraw();
//        }
//    }
//
//    UltraCanvasElement *UltraCanvasElement::FindChildById(const std::string &id) {
//        // Check direct children first
//        for (auto* child : children) {
//            if (child->GetIdentifier() == id) {
//                return child;
//            }
//        }
//
//        // Then check recursively
//        for (auto* child : children) {
//            UltraCanvasElement* found = child->FindChildById(id);
//            if (found) {
//                return found;
//            }
//        }
//
//        return nullptr;
//    }
//    bool UltraCanvasElement::IsAncestorOf(const UltraCanvasElement *element) const {
//        if (!element) return false;
//
//        const UltraCanvasElement* current = element->parent;
//        while (current) {
//            if (current == this) return true;
//            current = current->parent;
//        }
//        return false;
//    }



//    void UltraCanvasElement::RequestRedraw() {
//        if (window) {
//            window->RequestRedraw(true);
//        }
//    }
//
//    bool UltraCanvasElement::OnEvent(const UCEvent& event) {
//        if (eventCallback) {
//            return eventCallback(event);
//        }
//        return false;
//    }
//
//    void UltraCanvasElement::SetAbsoluteX(long x) {
//        if (parent) {
//            properties.x_pos = x - parent->GetAbsoluteX();
//        } else {
//            properties.x_pos = x;
//        }
//    }
//
//    long UltraCanvasElement::GetAbsoluteY() const {
//        if (parent) {
//            return parent->GetAbsoluteY() + properties.y_pos;
//        }
//        return properties.y_pos;
//    }
//
//    void UltraCanvasElement::SetAbsoluteY(long y) {
//        if (parent) {
//            properties.y_pos = y - parent->GetAbsoluteY();
//        } else {
//            properties.y_pos = y;
//        }
//    }
//
//    long UltraCanvasElement::GetAbsoluteX() const {
//        if (parent) {
//            return parent->GetAbsoluteX() + properties.x_pos;
//        }
//        return properties.x_pos;
//    }
//
//    UltraCanvasElement *UltraCanvasElement::GetRoot() {
//        UltraCanvasElement* root = this;
//        while (root->parent) {
//            root = root->parent;
//        }
//        return root;
//    }
//
//    bool UltraCanvasElement::ForwardEventToChildren(const UCEvent &event) {
//        if (children.empty()) return false;
//
//        bool anyHandled = false;
//
//        // Route based on event type
//        if (event.IsMouseEvent()) {
//            anyHandled = ForwardMouseEventToChildren(event);
//        } else if (event.IsKeyboardEvent()) {
//            anyHandled = ForwardKeyboardEventToChildren(event);
//        } else {
//            anyHandled = ForwardGeneralEventToChildren(event);
//        }
//
//        return anyHandled;
//    }
//
//    bool UltraCanvasElement::ForwardMouseEventToChildren(const UCEvent &event) {
//        Point2Di mousePos(event.x, event.y);
//
//        // Convert to local coordinates
//        Point2Di localMousePos = ConvertToLocalCoordinates(mousePos);
//
//        // Check children in reverse order (topmost first for z-order)
//        for (auto it = children.rbegin(); it != children.rend(); ++it) {
//            UltraCanvasElement* child = *it;
//
//            if (!child || !child->IsVisible() || !child->IsEnabled()) {
//                continue;
//            }
//
//            // Check if mouse position is within child bounds
//            if (child->Contains(localMousePos)) {
//                // Create event with coordinates relative to child
//                UCEvent childEvent = CreateChildEvent(child, event, localMousePos);
//
//                // Forward to child (which will recursively handle its children)
//                if (child->OnEvent(childEvent)) {
//                    return true; // Event consumed by this child hierarchy
//                }
//            }
//        }
//
//        return false;
//    }
//
//    bool UltraCanvasElement::ForwardKeyboardEventToChildren(const UCEvent &event) {
//        // Find focused child and forward to it
//        for (UltraCanvasElement* child : children) {
//            if (child && child->IsVisible() && child->IsEnabled() && child->IsFocused()) {
//                if (child->OnEvent(event)) {
//                    return true; // Event handled by focused child
//                }
//                break; // Only one element should be focused
//            }
//        }
//
//        return false;
//    }
//
//    bool UltraCanvasElement::ForwardGeneralEventToChildren(const UCEvent &event) {
//        bool anyHandled = false;
//
//        for (UltraCanvasElement* child : children) {
//            if (child && child->IsVisible() && child->IsEnabled()) {
//                if (child->OnEvent(event)) {
//                    anyHandled = true;
//                    // Continue to other children - some events may need to reach multiple elements
//                }
//            }
//        }
//
//        return anyHandled;
//    }
//
//
//    UCEvent UltraCanvasElement::CreateChildEvent(UltraCanvasElement *child, const UCEvent &originalEvent,
//                                                 const Point2Di &parentLocalPos) const {
//        UCEvent childEvent = originalEvent;
//        if (originalEvent.IsMouseEvent()) {
//            Point2Di childPos = child->GetPosition();
//            childEvent.x = parentLocalPos.x - childPos.x;
//            childEvent.y = parentLocalPos.y - childPos.y;
//        }
//
//        return childEvent;
//    }

    // new here
    void UltraCanvasElement::ConvertWindowToContainerCoordinates(int &x, int &y) {
        if (parentContainer) {
            Point2Di elementPos = parentContainer->GetPositionInWindow();
            x -= elementPos.x;
            y -= elementPos.y;
        }
    }

    Point2Di UltraCanvasElement::ConvertWindowToContainerCoordinates(const Point2Di &globalPos) {
        Point2Di pos = globalPos;
        ConvertWindowToContainerCoordinates(pos.x, pos.y);
        return pos;
    }

    void UltraCanvasElement::RequestRedraw() {
        if (window) {
            window->MarkElementDirty(this);
        }
    }

    void UltraCanvasElement::RequestFullRedraw() {
        if (window) {
            window->RequestFullRedraw();
        }
    }

    IRenderContext* UltraCanvasElement::GetRenderContext() const {
        if (window) {
            return window->GetRenderContext();
        }
        return nullptr;
    }

    void UltraCanvasElement::AddThisPopupElementToWindow() {
        if (window) {
            window->AddPopupElement(this);
        }
    }

    void UltraCanvasElement::RemoveThisPopupElementFromWindow() {
        if (window) {
            window->RemovePopupElement(this);
        }
    }

    UltraCanvasContainer* UltraCanvasElement::GetRootContainer() {
        UltraCanvasContainer* root = parentContainer;
        while (root && root->GetParentContainer()) {
            root = root->GetParentContainer();
        }
        return root;
    }

    bool UltraCanvasElement::IsDescendantOf(const UltraCanvasContainer* container) const {
        if (!container) return false;

        UltraCanvasContainer* current = parentContainer;
        while (current) {
            if (current == container) return true;
            current = current->GetParentContainer();
        }
        return false;
    }

//    int UltraCanvasElement::GetXInWindow() {
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
//    int UltraCanvasElement::GetYInWindow() {
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
//        }
//
//        return totalY;
//    }

    int UltraCanvasElement::GetXInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += pc->GetContentArea().x;
                pc = pc->parentContainer;
            }
        }
        return pos + properties.x_pos;
    }

    int UltraCanvasElement::GetYInWindow() {
        int pos = 0;
        if (parentContainer) {
            auto pc = parentContainer;
            while(pc) {
                pos += pc->GetContentArea().y;
                pc = pc->parentContainer;
            }
        }
        return pos + properties.y_pos;
    }

    bool UltraCanvasElement::SetFocus(bool focus) {
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

    bool UltraCanvasElement::IsFocused() const {
        if (window && window->IsWindowFocused() && window->GetFocusedElement() == this) {
            return true;
        }

        return false;
    }

}
