// UltraCanvasUIElement.cpp
// Modern C++ base class system for all UI components
// Version: 3.0.0
// Last Modified: 2025-01-04
// Author: UltraCanvas Framework
#include "UltraCanvasUIElement.h"
#include "UltraCanvasBaseWindow.h"
namespace UltraCanvas {
    void UltraCanvasElement::AddChild(UltraCanvasElement *child) {
        if (!child || child == this) return;

        // Remove from previous parent
        if (child->parent) {
            child->parent->RemoveChild(child);
        }

        // Add to this element
        children.push_back(child);
        child->parent = this;
        child->properties.ParentObject = this->properties.IdentifierID;

        // Notify derived classes
        OnChildAdded(child);
    }

    void UltraCanvasElement::RemoveChild(UltraCanvasElement *child) {
        auto it = std::find(children.begin(), children.end(), child);
        if (it != children.end()) {
            (*it)->parent = nullptr;
            (*it)->properties.ParentObject = 0;
            children.erase(it);

            // Notify derived classes
            OnChildRemoved(child);
        }
    }

    UltraCanvasElement *UltraCanvasElement::FindChildById(const std::string &id) {
        for (auto* child : children) {
            if (child->GetIdentifier() == id) {
                return child;
            }
        }
        return nullptr;
    }

    void UltraCanvasElement::SetNeedsRedraw() {
        if (window) {
            window->SetNeedsRedraw(true);
        }
    }

    bool UltraCanvasElement::IsAncestorOf(const UltraCanvasElement *element) const {
        if (!element) return false;

        const UltraCanvasElement* current = element->parent;
        while (current) {
            if (current == this) return true;
            current = current->parent;
        }
        return false;
    }
}
