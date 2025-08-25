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
//            window->SetNeedsRedraw(true);
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
//        Point2D mousePos(event.x, event.y);
//
//        // Convert to local coordinates
//        Point2D localMousePos = ConvertToLocalCoordinates(mousePos);
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
//                                                 const Point2D &parentLocalPos) const {
//        UCEvent childEvent = originalEvent;
//        if (originalEvent.IsMouseEvent()) {
//            Point2D childPos = child->GetPosition();
//            childEvent.x = parentLocalPos.x - childPos.x;
//            childEvent.y = parentLocalPos.y - childPos.y;
//        }
//
//        return childEvent;
//    }

    // new here
    Point2D UltraCanvasElement::ConvertWindowToLocalCoordinates(const Point2D &globalPos) {
        Point2D elementPos = GetPositionInWindow();
        return Point2D(globalPos.x - elementPos.x, globalPos.y - elementPos.y);
    }

    void UltraCanvasElement::RequestRedraw() {
        if (window) {
            window->SetNeedsRedraw(true);
        }
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

    long UltraCanvasElement::GetXInWindow() {
        if (parentContainer) {
            return parentContainer->GetXInWindow() + properties.x_pos;
        }
        return properties.x_pos;
    }

    long UltraCanvasElement::GetYInWindow() {
        if (parentContainer) {
            return parentContainer->GetYInWindow() + properties.y_pos;
        }
        return properties.y_pos;
    }

}
