// core/UltraCanvasBoxLayout.cpp
// Implementation of box layout (horizontal/vertical)
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include <algorithm>
#include <numeric>

namespace UltraCanvas {

// ===== CONSTRUCTORS =====

UltraCanvasBoxLayout::UltraCanvasBoxLayout(BoxLayoutDirection dir)
    : direction(dir) {
}

UltraCanvasBoxLayout::UltraCanvasBoxLayout(UltraCanvasContainer* parent, BoxLayoutDirection dir)
    : UltraCanvasLayout(parent), direction(dir) {
}

// ===== ITEM MANAGEMENT =====

void UltraCanvasBoxLayout::AddChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) {
    if (!item) return;
    
    // Try to cast to BoxLayoutItem
    auto boxItem = std::dynamic_pointer_cast<UltraCanvasBoxLayoutItem>(item);
    if (boxItem) {
        items.push_back(boxItem);
    } else {
        // Wrap in BoxLayoutItem
        auto newBoxItem = std::make_shared<UltraCanvasBoxLayoutItem>();
        newBoxItem->SetElement(item->GetElement());
        items.push_back(newBoxItem);
    }
    
    Invalidate();
}

void UltraCanvasBoxLayout::AddChildElement(std::shared_ptr<UltraCanvasUIElement> element) {
    if (!element) return;
    
    // Create default box layout item
    auto item = std::make_shared<UltraCanvasBoxLayoutItem>(element);
    items.push_back(item);
    
    // Add element to parent container if we have one
    if (parentContainer) {
        parentContainer->AddChild(element);
    }
    
    Invalidate();
}

void UltraCanvasBoxLayout::RemoveChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) {
    auto boxItem = std::dynamic_pointer_cast<UltraCanvasBoxLayoutItem>(item);
    if (!boxItem) return;
    
    auto it = std::find(items.begin(), items.end(), boxItem);
    if (it != items.end()) {
        items.erase(it);
        Invalidate();
    }
}

void UltraCanvasBoxLayout::RemoveChildElement(std::shared_ptr<UltraCanvasUIElement> element) {
    if (!element) return;
    
    auto it = std::find_if(items.begin(), items.end(),
        [&element](const std::shared_ptr<UltraCanvasBoxLayoutItem>& item) {
            return item->GetElement() == element;
        });
    
    if (it != items.end()) {
        items.erase(it);
        
        // Remove from parent container if we have one
        if (parentContainer) {
            parentContainer->RemoveChild(element);
        }
        
        Invalidate();
    }
}

std::shared_ptr<UltraCanvasLayoutItem> UltraCanvasBoxLayout::GetItemAt(int index) const {
    if (index >= 0 && index < static_cast<int>(items.size())) {
        return items[index];
    }
    return nullptr;
}

void UltraCanvasBoxLayout::ClearItems() {
    items.clear();
    Invalidate();
}

// ===== BOX LAYOUT SPECIFIC =====

void UltraCanvasBoxLayout::AddItem(std::shared_ptr<UltraCanvasBoxLayoutItem> item, int stretch) {
    if (!item) return;
    
    item->SetStretch(stretch);
    items.push_back(item);
    Invalidate();
}

void UltraCanvasBoxLayout::AddElement(std::shared_ptr<UltraCanvasUIElement> element, int stretch) {
    if (!element) return;
    
    auto item = std::make_shared<UltraCanvasBoxLayoutItem>(element);
    item->SetStretch(stretch);
    items.push_back(item);
    
    if (parentContainer) {
        parentContainer->AddChild(element);
    }
    
    Invalidate();
}

void UltraCanvasBoxLayout::AddSpacing(int size) {
    auto item = std::make_shared<UltraCanvasBoxLayoutItem>();
    item->SetFixedSize(size, size);
    item->SetStretch(0);
    items.push_back(item);
    Invalidate();
}

void UltraCanvasBoxLayout::AddStretch(int stretch) {
    auto item = std::make_shared<UltraCanvasBoxLayoutItem>();
    item->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
    item->SetStretch(stretch);
    items.push_back(item);
    Invalidate();
}

void UltraCanvasBoxLayout::InsertItem(int index, std::shared_ptr<UltraCanvasBoxLayoutItem> item, int stretch) {
    if (!item) return;
    
    item->SetStretch(stretch);
    
    if (index >= 0 && index <= static_cast<int>(items.size())) {
        items.insert(items.begin() + index, item);
    } else {
        items.push_back(item);
    }
    
    Invalidate();
}

// ===== LAYOUT CALCULATION =====

void UltraCanvasBoxLayout::PerformLayout(const Rect2Di& containerBounds) {
    if (items.empty()) return;
    
    Rect2Di contentRect = GetContentRect(containerBounds);
    
    if (direction == BoxLayoutDirection::Horizontal) {
        LayoutHorizontal(contentRect);
    } else {
        LayoutVertical(contentRect);
    }
    
    // Apply computed geometry to elements
    for (auto& item : items) {
        item->ApplyToElement();
    }
}

void UltraCanvasBoxLayout::LayoutHorizontal(const Rect2Di& contentRect) {
    float availableWidth = static_cast<float>(contentRect.width);
    float availableHeight = static_cast<float>(contentRect.height);
    
    // Calculate total fixed size and total stretch
    float totalFixedSize = CalculateTotalFixedSize();
    int totalSpacing = CalculateTotalSpacing();
    int totalStretch = CalculateTotalStretch();
    
    // Calculate remaining space for stretching
    float remainingSpace = availableWidth - totalFixedSize - totalSpacing;
    float stretchUnit = (totalStretch > 0 && remainingSpace > 0) ? remainingSpace / totalStretch : 0;
    
    // Position items
    float currentX = 0;
    
    for (size_t i = 0; i < items.size(); ++i) {
        auto& item = items[i];
        if (!item->IsVisible()) continue;
        
        // Calculate width
        float itemWidth = 0;
        if (item->GetWidthMode() == SizeMode::Fixed) {
            itemWidth = item->GetFixedWidth();
        } else if (item->GetWidthMode() == SizeMode::Fill || item->GetStretch() > 0) {
            itemWidth = stretchUnit * item->GetStretch();
        } else {
            itemWidth = item->GetPreferredWidth();
        }
        
        // Clamp to min/max
        itemWidth = std::clamp(itemWidth, item->GetMinimumWidth(), item->GetMaximumWidth());
        
        // Calculate height based on cross-axis alignment
        float itemHeight = 0;
        float itemY = 0;
        
        if (item->GetHeightMode() == SizeMode::Fixed) {
            itemHeight = item->GetFixedHeight();
        } else if (item->GetHeightMode() == SizeMode::Fill || 
                   crossAxisAlignment == LayoutAlignment::Fill) {
            itemHeight = availableHeight;
        } else {
            itemHeight = item->GetPreferredHeight();
        }
        
        itemHeight = std::clamp(itemHeight, item->GetMinimumHeight(), item->GetMaximumHeight());
        
        // Apply cross-axis alignment
        ApplyCrossAxisAlignment(item.get(), itemY, availableHeight);
        
        // Set computed geometry
        item->SetComputedGeometry(
            currentX + item->GetMarginLeft(),
            itemY + item->GetMarginTop(),
            itemWidth,
            itemHeight
        );
        
        // Move to next position
        currentX += itemWidth + item->GetTotalMarginHorizontal();
        if (i < items.size() - 1) {
            currentX += spacing;
        }
    }
    
    // Apply main axis alignment
    if (mainAxisAlignment != LayoutAlignment::Start && remainingSpace > 0) {
        float offset = 0;
        if (mainAxisAlignment == LayoutAlignment::Center) {
            offset = remainingSpace / 2;
        } else if (mainAxisAlignment == LayoutAlignment::End) {
            offset = remainingSpace;
        }
        
        if (offset > 0) {
            for (auto& item : items) {
                float x = item->GetComputedX() + offset;
                item->SetComputedGeometry(x, item->GetComputedY(), 
                                         item->GetComputedWidth(), item->GetComputedHeight());
            }
        }
    }
}

void UltraCanvasBoxLayout::LayoutVertical(const Rect2Di& contentRect) {
    float availableWidth = static_cast<float>(contentRect.width);
    float availableHeight = static_cast<float>(contentRect.height);
    
    // Calculate total fixed size and total stretch
    float totalFixedSize = CalculateTotalFixedSize();
    int totalSpacing = CalculateTotalSpacing();
    int totalStretch = CalculateTotalStretch();
    
    // Calculate remaining space for stretching
    float remainingSpace = availableHeight - totalFixedSize - totalSpacing;
    float stretchUnit = (totalStretch > 0 && remainingSpace > 0) ? remainingSpace / totalStretch : 0;
    
    // Position items
    float currentY = 0;
    
    for (size_t i = 0; i < items.size(); ++i) {
        auto& item = items[i];
        if (!item->IsVisible()) continue;
        
        // Calculate height
        float itemHeight = 0;
        if (item->GetHeightMode() == SizeMode::Fixed) {
            itemHeight = item->GetFixedHeight();
        } else if (item->GetHeightMode() == SizeMode::Fill || item->GetStretch() > 0) {
            itemHeight = stretchUnit * item->GetStretch();
        } else {
            itemHeight = item->GetPreferredHeight();
        }
        
        // Clamp to min/max
        itemHeight = std::clamp(itemHeight, item->GetMinimumHeight(), item->GetMaximumHeight());
        
        // Calculate width based on cross-axis alignment
        float itemWidth = 0;
        float itemX = 0;
        
        if (item->GetWidthMode() == SizeMode::Fixed) {
            itemWidth = item->GetFixedWidth();
        } else if (item->GetWidthMode() == SizeMode::Fill || 
                   crossAxisAlignment == LayoutAlignment::Fill) {
            itemWidth = availableWidth;
        } else {
            itemWidth = item->GetPreferredWidth();
        }
        
        itemWidth = std::clamp(itemWidth, item->GetMinimumWidth(), item->GetMaximumWidth());
        
        // Apply cross-axis alignment for width
        if (crossAxisAlignment == LayoutAlignment::Center) {
            itemX += (availableWidth - itemWidth) / 2;
        } else if (crossAxisAlignment == LayoutAlignment::End) {
            itemX += availableWidth - itemWidth;
        }
        
        // Set computed geometry
        item->SetComputedGeometry(
            itemX + item->GetMarginLeft(),
            currentY + item->GetMarginTop(),
            itemWidth,
            itemHeight
        );
        
        // Move to next position
        currentY += itemHeight + item->GetTotalMarginVertical();
        if (i < items.size() - 1) {
            currentY += spacing;
        }
    }
    
    // Apply main axis alignment
    if (mainAxisAlignment != LayoutAlignment::Start && remainingSpace > 0) {
        float offset = 0;
        if (mainAxisAlignment == LayoutAlignment::Center) {
            offset = remainingSpace / 2;
        } else if (mainAxisAlignment == LayoutAlignment::End) {
            offset = remainingSpace;
        }
        
        if (offset > 0) {
            for (auto& item : items) {
                float y = item->GetComputedY() + offset;
                item->SetComputedGeometry(item->GetComputedX(), y,
                                         item->GetComputedWidth(), item->GetComputedHeight());
            }
        }
    }
}

void UltraCanvasBoxLayout::ApplyCrossAxisAlignment(UltraCanvasBoxLayoutItem* item, 
                                                   float crossStart, float crossSize) {
    if (!item) return;
    
    float itemCrossSize = (direction == BoxLayoutDirection::Horizontal) ? 
                          item->GetComputedHeight() : item->GetComputedWidth();
    
    float offset = 0;
    if (crossAxisAlignment == LayoutAlignment::Center) {
        offset = (crossSize - itemCrossSize) / 2;
    } else if (crossAxisAlignment == LayoutAlignment::End) {
        offset = crossSize - itemCrossSize;
    }
    
    if (direction == BoxLayoutDirection::Horizontal) {
        float y = crossStart + offset;
        item->SetComputedGeometry(item->GetComputedX(), y,
                                 item->GetComputedWidth(), item->GetComputedHeight());
    } else {
        float x = crossStart + offset;
        item->SetComputedGeometry(x, item->GetComputedY(),
                                 item->GetComputedWidth(), item->GetComputedHeight());
    }
}

int UltraCanvasBoxLayout::CalculateTotalStretch() const {
    int total = 0;
    for (const auto& item : items) {
        if (item->IsVisible()) {
            total += item->GetStretch();
        }
    }
    return total;
}

float UltraCanvasBoxLayout::CalculateTotalFixedSize() const {
    float total = 0;
    
    for (const auto& item : items) {
        if (!item->IsVisible()) continue;
        
        if (direction == BoxLayoutDirection::Horizontal) {
            if (item->GetWidthMode() == SizeMode::Fixed || item->GetStretch() == 0) {
                total += item->GetPreferredWidth();
            }
            total += item->GetTotalMarginHorizontal();
        } else {
            if (item->GetHeightMode() == SizeMode::Fixed || item->GetStretch() == 0) {
                total += item->GetPreferredHeight();
            }
            total += item->GetTotalMarginVertical();
        }
    }
    
    return total;
}

int UltraCanvasBoxLayout::CalculateTotalSpacing() const {
    int visibleCount = 0;
    for (const auto& item : items) {
        if (item->IsVisible()) visibleCount++;
    }
    return (visibleCount > 1) ? spacing * (visibleCount - 1) : 0;
}

// ===== SIZE CALCULATION =====

Size2Di UltraCanvasBoxLayout::CalculateMinimumSize() const {
    int width = 0;
    int height = 0;
    
    if (direction == BoxLayoutDirection::Horizontal) {
        for (const auto& item : items) {
            if (!item->IsVisible()) continue;
            width += static_cast<int>(item->GetMinimumWidth()) + item->GetTotalMarginHorizontal();
            height = std::max(height, static_cast<int>(item->GetMinimumHeight()) + item->GetTotalMarginVertical());
        }
        width += CalculateTotalSpacing();
    } else {
        for (const auto& item : items) {
            if (!item->IsVisible()) continue;
            height += static_cast<int>(item->GetMinimumHeight()) + item->GetTotalMarginVertical();
            width = std::max(width, static_cast<int>(item->GetMinimumWidth()) + item->GetTotalMarginHorizontal());
        }
        height += CalculateTotalSpacing();
    }
    
    width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
    height += GetTotalPaddingVertical() + GetTotalMarginVertical();
    
    return Size2Di(width, height);
}

Size2Di UltraCanvasBoxLayout::CalculatePreferredSize() const {
    int width = 0;
    int height = 0;
    
    if (direction == BoxLayoutDirection::Horizontal) {
        for (const auto& item : items) {
            if (!item->IsVisible()) continue;
            width += static_cast<int>(item->GetPreferredWidth()) + item->GetTotalMarginHorizontal();
            height = std::max(height, static_cast<int>(item->GetPreferredHeight()) + item->GetTotalMarginVertical());
        }
        width += CalculateTotalSpacing();
    } else {
        for (const auto& item : items) {
            if (!item->IsVisible()) continue;
            height += static_cast<int>(item->GetPreferredHeight()) + item->GetTotalMarginVertical();
            width = std::max(width, static_cast<int>(item->GetPreferredWidth()) + item->GetTotalMarginHorizontal());
        }
        height += CalculateTotalSpacing();
    }
    
    width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
    height += GetTotalPaddingVertical() + GetTotalMarginVertical();
    
    return Size2Di(width, height);
}

Size2Di UltraCanvasBoxLayout::CalculateMaximumSize() const {
    return Size2Di(10000, 10000); // Typically unlimited
}

} // namespace UltraCanvas
