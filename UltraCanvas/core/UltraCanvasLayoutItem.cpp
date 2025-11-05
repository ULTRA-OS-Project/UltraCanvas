// core/UltraCanvasLayoutItem.cpp
// Implementation of layout item classes
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasLayoutItem.h"
#include "UltraCanvasUIElement.h"
#include <algorithm>

namespace UltraCanvas {

// ===== BASE LAYOUT ITEM IMPLEMENTATION =====

UltraCanvasLayoutItem::UltraCanvasLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
    : element(elem)
    {}

float UltraCanvasLayoutItem::GetPreferredWidth() const {
    if (element) {
        return static_cast<float>(element->GetWidth());
    }
    return 0;
}

float UltraCanvasLayoutItem::GetPreferredHeight() const {
    if (element) {
        return static_cast<float>(element->GetHeight());
    }
    return 0;
}

void UltraCanvasLayoutItem::ApplyToElement() {
    if (element) {
        element->SetBounds(computedX, computedY, computedWidth, computedHeight);
    }
}

// ===== BOX LAYOUT ITEM IMPLEMENTATION =====
UltraCanvasBoxLayoutItem::UltraCanvasBoxLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
        : UltraCanvasLayoutItem(elem) {
}

float UltraCanvasBoxLayoutItem::GetPreferredWidth() const {
    switch (widthMode) {
        case SizeMode::Fixed:
            return fixedWidth;
        
        case SizeMode::Auto:
            if (element) {
                return static_cast<float>(element->GetWidth());
            }
            return 0;
        
        case SizeMode::Fill:
            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

float UltraCanvasBoxLayoutItem::GetPreferredHeight() const {
    switch (heightMode) {
        case SizeMode::Fixed:
            return fixedHeight;
        
        case SizeMode::Auto:
            if (element) {
                return static_cast<float>(element->GetHeight());
            }
            return 0;
        
        case SizeMode::Fill:
            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

// ===== GRID LAYOUT ITEM IMPLEMENTATION =====

UltraCanvasGridLayoutItem::UltraCanvasGridLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
    : UltraCanvasLayoutItem(elem) {
}

float UltraCanvasGridLayoutItem::GetPreferredWidth() const {
    switch (widthMode) {
        case SizeMode::Fixed:
            return fixedWidth;
        
        case SizeMode::Auto:
        case SizeMode::Fill:
            if (element) {
                return static_cast<float>(element->GetWidth());
            }
            return 0;
        
//        case SizeMode::Fill:
//            if (element) {
//                return static_cast<float>(element->GetWidth());
//            }
//            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

float UltraCanvasGridLayoutItem::GetPreferredHeight() const {
    switch (heightMode) {
        case SizeMode::Fixed:
            return fixedHeight;
        
        case SizeMode::Auto:
            if (element) {
                return static_cast<float>(element->GetHeight());
            }
            return 0;
        
        case SizeMode::Fill:
            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

// ===== FLEX LAYOUT ITEM IMPLEMENTATION =====

UltraCanvasFlexLayoutItem::UltraCanvasFlexLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
    : UltraCanvasLayoutItem(elem) {
}

float UltraCanvasFlexLayoutItem::GetPreferredWidth() const {
    if (flexBasis > 0) {
        return flexBasis;
    }
    
    switch (widthMode) {
        case SizeMode::Fixed:
            return fixedWidth;
        
        case SizeMode::Auto:
            if (element) {
                return static_cast<float>(element->GetWidth());
            }
            return 0;
        
        case SizeMode::Fill:
            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

float UltraCanvasFlexLayoutItem::GetPreferredHeight() const {
    if (flexBasis > 0) {
        return flexBasis;
    }
    
    switch (heightMode) {
        case SizeMode::Fixed:
            return fixedHeight;
        
        case SizeMode::Auto:
            if (element) {
                return static_cast<float>(element->GetHeight());
            }
            return 0;
        
        case SizeMode::Fill:
            return 0; // Will be calculated by layout
        
        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container
        
        default:
            return 0;
    }
}

} // namespace UltraCanvas
