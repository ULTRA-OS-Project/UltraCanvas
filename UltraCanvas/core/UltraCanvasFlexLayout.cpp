// core/UltraCanvasFlexLayout.cpp
// Implementation of flexible layout (CSS Flexbox-style)
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasFlexLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include <algorithm>
#include <numeric>

namespace UltraCanvas {

// ===== CONSTRUCTORS =====

UltraCanvasFlexLayout::UltraCanvasFlexLayout(UltraCanvasContainer* parent, FlexDirection dir)
    : UltraCanvasLayout(parent), direction(dir) {
}

// ===== ITEM MANAGEMENT =====

UltraCanvasFlexLayoutItem* UltraCanvasFlexLayout::GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> element) const {
    if (element) {
        for(auto &item : items) {
            if (item->GetElement() == element) {
                return item.get();
            }
        }
    }
    return nullptr;
}

UltraCanvasFlexLayoutItem* UltraCanvasFlexLayout::GetItemAt(int index) const {
    if (index >= 0 && index < static_cast<int>(items.size())) {
        return items[index].get();
    }
    return nullptr;
}

UltraCanvasLayoutItem* UltraCanvasFlexLayout::InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) {
    if (!element) return nullptr;

    auto existingItem = GetItemForUIElement(element);
    if (existingItem) {
        return existingItem;
    }

    auto item = std::make_unique<UltraCanvasFlexLayoutItem>(element);
    auto itemPtr = item.get();

    if (index >= 0 && index <= static_cast<int>(items.size())) {
        items.insert(items.begin() + index, std::move(item));
    } else {
        items.push_back(std::move(item));
    }

    if (parentContainer && element->GetParentContainer() == nullptr) {
        parentContainer->AddChild(element);
    }

    Invalidate();
    return itemPtr;
}

UltraCanvasFlexLayoutItem* UltraCanvasFlexLayout::AddUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                                               float flexGrow, float flexShrink, float flexBasis) {
    if (!element) return nullptr;

    auto item = static_cast<UltraCanvasFlexLayoutItem*>(InsertUIElement(element, -1));
    item->SetFlex(flexGrow, flexShrink, flexBasis);
    return item;
}


    void UltraCanvasFlexLayout::RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) {
    if (!element) return;

    auto it = std::find_if(items.begin(), items.end(),
                           [&element](const std::unique_ptr<UltraCanvasFlexLayoutItem>& item) {
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

void UltraCanvasFlexLayout::ClearItems() {
    items.clear();
    Invalidate();
}

// ===== LAYOUT CALCULATION =====

void UltraCanvasFlexLayout::PerformLayout(const Rect2Di& containerBounds) {
    if (items.empty()) return;
    
    Rect2Di contentRect = GetContentRect(containerBounds);
    float containerMainSize = IsRowDirection() ? 
        static_cast<float>(contentRect.width) : static_cast<float>(contentRect.height);
    float containerCrossSize = IsRowDirection() ?
        static_cast<float>(contentRect.height) : static_cast<float>(contentRect.width);
    
    // Calculate flex lines (handles wrapping)
    std::vector<FlexLine> lines = CalculateFlexLines(containerMainSize);
    
    // Resolve flexible lengths for each line
    for (auto& line : lines) {
        ResolveFlexibleLengths(line, containerMainSize);
    }
    
    // Position items in each line
    for (auto& line : lines) {
        PositionMainAxis(line, containerMainSize);
        PositionCrossAxis(line, containerCrossSize);
    }
    
    // Position lines along cross axis
    PositionLines(lines, containerCrossSize);
    
    // Apply computed geometry to elements
    for (auto& item : items) {
        item->ApplyToElement();
    }
}

std::vector<UltraCanvasFlexLayout::FlexLine> UltraCanvasFlexLayout::CalculateFlexLines(
    float containerMainSize) const {
    
    std::vector<FlexLine> lines;
    
    if (wrap == FlexWrap::NoWrap) {
        // Single line - all items
        FlexLine line;
        for (const auto& item : items) {
            if (item->IsVisible()) {
                line.items.push_back(item.get());
            }
        }
        lines.push_back(line);
    } else {
        // Multiple lines - wrap items
        FlexLine currentLine;
        float currentMainSize = 0;
        
        for (const auto& item : items) {
            if (!item->IsVisible()) continue;
            
            float itemMainSize = GetItemMainSize(item.get());
            
            // Check if we need to wrap
            if (currentMainSize + itemMainSize > containerMainSize && !currentLine.items.empty()) {
                lines.push_back(currentLine);
                currentLine = FlexLine();
                currentMainSize = 0;
            }
            
            currentLine.items.push_back(item.get());
            currentMainSize += itemMainSize;
            if (!currentLine.items.empty()) {
                currentMainSize += (IsRowDirection() ? columnGap : rowGap);
            }
        }
        
        if (!currentLine.items.empty()) {
            lines.push_back(currentLine);
        }
    }
    
    return lines;
}

void UltraCanvasFlexLayout::PositionLines(std::vector<FlexLine>& lines, float containerCrossSize) {
    // Simplified implementation - positions lines sequentially
    float position = static_cast<float>(IsRowDirection() ? paddingTop + marginTop : paddingLeft + marginLeft);
    int gapSize = IsRowDirection() ? rowGap : columnGap;
    
    for (size_t i = 0; i < lines.size(); ++i) {
        auto& line = lines[i];
        
        // Calculate line cross size
        float lineCrossSize = 0;
        for (auto* item : line.items) {
            lineCrossSize = std::max(lineCrossSize, GetItemCrossSize(item));
        }
        
        // Offset all items in this line
        for (auto* item : line.items) {
            float currentPos = IsRowDirection() ? item->GetComputedY() : item->GetComputedX();
            float newPos = position + (currentPos - (IsRowDirection() ? paddingTop + marginTop : paddingLeft + marginLeft));
            
            if (IsRowDirection()) {
                item->SetComputedGeometry(item->GetComputedX(), newPos,
                                         item->GetComputedWidth(), item->GetComputedHeight());
            } else {
                item->SetComputedGeometry(newPos, item->GetComputedY(),
                                         item->GetComputedWidth(), item->GetComputedHeight());
            }
        }
        
        position += lineCrossSize;
        if (i < lines.size() - 1) {
            position += gapSize;
        }
    }
}

float UltraCanvasFlexLayout::GetItemMainSize(UltraCanvasFlexLayoutItem* item) const {
    if (!item) return 0;
    
    if (IsRowDirection()) {
        return item->GetPreferredWidth();
    } else {
        return item->GetPreferredHeight();
    }
}

float UltraCanvasFlexLayout::GetItemCrossSize(UltraCanvasFlexLayoutItem* item) const {
    if (!item) return 0;
    
    if (IsRowDirection()) {
        return item->GetPreferredHeight();
    } else {
        return item->GetPreferredWidth();
    }
}

// ===== SIZE CALCULATION =====

Size2Di UltraCanvasFlexLayout::CalculateMinimumSize() const {
    int width = 0;
    int height = 0;
    
    for (const auto& item : items) {
        if (!item->IsVisible()) continue;
        
        if (IsRowDirection()) {
            width += static_cast<int>(item->GetMinimumWidth());
            height = std::max(height, static_cast<int>(item->GetMinimumHeight()));
        } else {
            height += static_cast<int>(item->GetMinimumHeight());
            width = std::max(width, static_cast<int>(item->GetMinimumWidth()));
        }
    }
    
    width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
    height += GetTotalPaddingVertical() + GetTotalMarginVertical();
    
    return Size2Di(width, height);
}

Size2Di UltraCanvasFlexLayout::CalculatePreferredSize() const {
    int width = 0;
    int height = 0;
    
    for (const auto& item : items) {
        if (!item->IsVisible()) continue;
        
        if (IsRowDirection()) {
            width += static_cast<int>(item->GetPreferredWidth());
            height = std::max(height, static_cast<int>(item->GetPreferredHeight()));
        } else {
            height += static_cast<int>(item->GetPreferredHeight());
            width = std::max(width, static_cast<int>(item->GetPreferredWidth()));
        }
    }
    
    width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
    height += GetTotalPaddingVertical() + GetTotalMarginVertical();
    
    return Size2Di(width, height);
}

Size2Di UltraCanvasFlexLayout::CalculateMaximumSize() const {
    return Size2Di(10000, 10000); // Typically unlimited
}


void UltraCanvasFlexLayout::ResolveFlexibleLengths(FlexLine& line, float containerMainSize) {
    if (line.items.empty()) return;

    // First, initialize all items with their base sizes
    for (auto* item : line.items) {
        float baseWidth = item->GetPreferredWidth();
        float baseHeight = item->GetPreferredHeight();

        // Clamp to min/max constraints
        baseWidth = std::clamp(baseWidth, item->GetMinimumWidth(), item->GetMaximumWidth());
        baseHeight = std::clamp(baseHeight, item->GetMinimumHeight(), item->GetMaximumHeight());

        // Initialize computed geometry with base sizes
        item->SetComputedGeometry(0, 0, baseWidth, baseHeight);
    }

    // Calculate total flex grow and shrink
    float totalFlexGrow = 0;
    float totalFlexShrink = 0;
    float totalMainSize = 0;

    for (auto* item : line.items) {
        totalFlexGrow += item->GetFlexGrow();
        totalFlexShrink += item->GetFlexShrink();

        if (IsRowDirection()) {
            totalMainSize += item->GetComputedWidth();
        } else {
            totalMainSize += item->GetComputedHeight();
        }
    }

    // Add gaps
    int gapSize = IsRowDirection() ? columnGap : rowGap;
    totalMainSize += gapSize * (line.items.size() - 1);

    float remainingSpace = containerMainSize - totalMainSize;

    // Apply flex grow or shrink
    if (remainingSpace > 0 && totalFlexGrow > 0) {
        // Grow items
        float flexUnit = remainingSpace / totalFlexGrow;
        for (auto* item : line.items) {
            if (IsRowDirection()) {
                float newWidth = item->GetComputedWidth() + (flexUnit * item->GetFlexGrow());
                newWidth = std::clamp(newWidth, item->GetMinimumWidth(), item->GetMaximumWidth());
                item->SetComputedGeometry(item->GetComputedX(), item->GetComputedY(),
                                          newWidth, item->GetComputedHeight());
            } else {
                float newHeight = item->GetComputedHeight() + (flexUnit * item->GetFlexGrow());
                newHeight = std::clamp(newHeight, item->GetMinimumHeight(), item->GetMaximumHeight());
                item->SetComputedGeometry(item->GetComputedX(), item->GetComputedY(),
                                          item->GetComputedWidth(), newHeight);
            }
        }
    } else if (remainingSpace < 0 && totalFlexShrink > 0) {
        // Shrink items
        float flexUnit = -remainingSpace / totalFlexShrink;
        for (auto* item : line.items) {
            if (IsRowDirection()) {
                float newWidth = item->GetComputedWidth() - (flexUnit * item->GetFlexShrink());
                newWidth = std::clamp(newWidth, item->GetMinimumWidth(), item->GetMaximumWidth());
                item->SetComputedGeometry(item->GetComputedX(), item->GetComputedY(),
                                          newWidth, item->GetComputedHeight());
            } else {
                float newHeight = item->GetComputedHeight() - (flexUnit * item->GetFlexShrink());
                newHeight = std::clamp(newHeight, item->GetMinimumHeight(), item->GetMaximumHeight());
                item->SetComputedGeometry(item->GetComputedX(), item->GetComputedY(),
                                          item->GetComputedWidth(), newHeight);
            }
        }
    }
}

void UltraCanvasFlexLayout::PositionMainAxis(FlexLine& line, float containerMainSize) {
    if (line.items.empty()) return;

    // Calculate total main size of items (should use computed sizes now)
    float totalMainSize = 0;
    for (auto* item : line.items) {
        if (IsRowDirection()) {
            totalMainSize += item->GetComputedWidth();
        } else {
            totalMainSize += item->GetComputedHeight();
        }
    }

    // Add gaps
    int gapSize = IsRowDirection() ? columnGap : rowGap;
    totalMainSize += gapSize * (line.items.size() - 1);

    float remainingSpace = containerMainSize - totalMainSize;
    float position = static_cast<float>(IsRowDirection() ? paddingLeft + marginLeft : paddingTop + marginTop);

    // Apply justify content
    float itemSpacing = 0;
    switch (justifyContent) {
        case FlexJustifyContent::Start:
            break;

        case FlexJustifyContent::End:
            position += remainingSpace;
            break;

        case FlexJustifyContent::Center:
            position += remainingSpace / 2;
            break;

        case FlexJustifyContent::SpaceBetween:
            if (line.items.size() > 1) {
                itemSpacing = remainingSpace / (line.items.size() - 1);
            }
            break;

        case FlexJustifyContent::SpaceAround:
            itemSpacing = remainingSpace / line.items.size();
            position += itemSpacing / 2;
            break;

        case FlexJustifyContent::SpaceEvenly:
            itemSpacing = remainingSpace / (line.items.size() + 1);
            position += itemSpacing;
            break;
    }

    // Position each item along main axis
    for (size_t i = 0; i < line.items.size(); ++i) {
        auto* item = line.items[i];

        if (IsRowDirection()) {
            float itemWidth = item->GetComputedWidth();
            item->SetComputedGeometry(position + item->GetMarginLeft(), item->GetComputedY(),
                                      itemWidth, item->GetComputedHeight());
            position += itemWidth + item->GetTotalMarginHorizontal();
        } else {
            float itemHeight = item->GetComputedHeight();
            item->SetComputedGeometry(item->GetComputedX(), position + item->GetMarginTop(),
                                      item->GetComputedWidth(), itemHeight);
            position += itemHeight + item->GetTotalMarginVertical();
        }

        position += itemSpacing;
        if (i < line.items.size() - 1) {
            position += gapSize;
        }
    }
}

void UltraCanvasFlexLayout::PositionCrossAxis(FlexLine& line, float containerCrossSize) {
    for (auto* item : line.items) {
        float crossPosition = static_cast<float>(IsRowDirection() ? paddingTop + marginTop : paddingLeft + marginLeft);

        // Get item's cross size from computed dimensions
        float itemCrossSize = IsRowDirection() ? item->GetComputedHeight() : item->GetComputedWidth();

        // Apply align items or align self
        LayoutItemAlignment alignment = (item->GetAlignSelf() != LayoutItemAlignment::Auto) ?
                                        item->GetAlignSelf() : LayoutItemAlignment::Start;

        // Handle stretch alignment
        if (alignItems == FlexAlignItems::Stretch && alignment == LayoutItemAlignment::Auto) {
            itemCrossSize = containerCrossSize;
        } else if (alignItems == FlexAlignItems::Center || alignment == LayoutItemAlignment::Center) {
            crossPosition += (containerCrossSize - itemCrossSize) / 2;
        } else if (alignItems == FlexAlignItems::End || alignment == LayoutItemAlignment::End) {
            crossPosition += containerCrossSize - itemCrossSize;
        }

        // Update position with cross-axis alignment
        if (IsRowDirection()) {
            item->SetComputedGeometry(item->GetComputedX(), crossPosition + item->GetMarginTop(),
                                      item->GetComputedWidth(), itemCrossSize);
        } else {
            item->SetComputedGeometry(crossPosition + item->GetMarginLeft(), item->GetComputedY(),
                                      itemCrossSize, item->GetComputedHeight());
        }
    }
}
} // namespace UltraCanvas
