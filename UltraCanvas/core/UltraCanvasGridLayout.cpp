// core/UltraCanvasGridLayout.cpp
// Implementation of grid layout
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasGridLayout.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasUIElement.h"
#include <algorithm>
#include <numeric>

namespace UltraCanvas {

    UltraCanvasGridLayoutItem::UltraCanvasGridLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
            : UltraCanvasLayoutItem(elem) {
    }

    int UltraCanvasGridLayoutItem::GetPreferredWidth() const {
        switch (widthMode) {
            case SizeMode::Fixed:
                return fixedWidth;

            case SizeMode::Auto:
            case SizeMode::Fill:
                if (element) {
                    return element->GetPreferredWidth() + element->GetTotalMarginHorizontal();
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

    int UltraCanvasGridLayoutItem::GetPreferredHeight() const {
        switch (heightMode) {
            case SizeMode::Fixed:
                return fixedHeight;

            case SizeMode::Auto:
            case SizeMode::Fill:
                if (element) {
                    return element->GetPreferredHeight() + element->GetTotalMarginVertical();
                }
                return 0;

            case SizeMode::Percentage:
                return 0; // Will be calculated by layout based on container

            default:
                return 0;
        }
    }

// ===== CONSTRUCTORS =====
UltraCanvasGridLayout::UltraCanvasGridLayout(UltraCanvasContainer* parent, int rows, int columns)
    : UltraCanvasLayout(parent) {
    SetGridSize(rows, columns);
}

// ===== GRID SIZE =====

void UltraCanvasGridLayout::SetGridSize(int rows, int columns) {
    rowDefinitions.clear();
    columnDefinitions.clear();
    
    for (int i = 0; i < rows; ++i) {
        rowDefinitions.push_back(GridRowColumnDefinition::Auto());
    }
    
    for (int i = 0; i < columns; ++i) {
        columnDefinitions.push_back(GridRowColumnDefinition::Auto());
    }

    InvalidateContainerLayout();
}

// ===== ITEM MANAGEMENT =====

UltraCanvasGridLayoutItem* UltraCanvasGridLayout::GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> element) const {
    if (element) {
        for(auto &item : items) {
            if (item->GetElement() == element) {
                return item.get();
            }
        }
    }
    return nullptr;
}

UltraCanvasGridLayoutItem* UltraCanvasGridLayout::GetItemAt(int index) const {
    if (index >= 0 && index < static_cast<int>(items.size())) {
        return items[index].get();
    }
    return nullptr;
}

UltraCanvasGridLayoutItem* UltraCanvasGridLayout::GetItemAt(int row, int column) const {
    for (const auto& item : items) {
        if (item->GetRow() == row && item->GetColumn() == column) {
            return item.get();
        }
    }
    return nullptr;
}

UltraCanvasLayoutItem* UltraCanvasGridLayout::InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) {
    if (!element) return nullptr;

    auto existingItem = GetItemForUIElement(element);
    if (existingItem) {
        return existingItem;
    }

    auto item = std::make_unique<UltraCanvasGridLayoutItem>(element);
    auto itemPtr = item.get();

    if (index >= 0 && index <= static_cast<int>(items.size())) {
        items.insert(items.begin() + index, std::move(item));
    } else {
        items.push_back(std::move(item));
    }

    if (parentContainer && element->GetParentContainer() == nullptr) {
        parentContainer->AddChild(element);
    }

    return itemPtr;
}

void UltraCanvasGridLayout::RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) {
    if (!element) return;

    auto it = std::find_if(items.begin(), items.end(),
                           [&element](const std::unique_ptr<UltraCanvasGridLayoutItem>& item) {
                               return item->GetElement() == element;
                           });

    if (it != items.end()) {
        items.erase(it);

        // Remove from parent container if we have one
        if (parentContainer) {
            parentContainer->RemoveChild(element);
        }
    }
}

void UltraCanvasGridLayout::ClearItems() {
    items.clear();
    InvalidateContainerLayout();
}

// ===== GRID LAYOUT SPECIFIC =====
UltraCanvasGridLayoutItem* UltraCanvasGridLayout::AddUIElement(std::shared_ptr<UltraCanvasUIElement> element,
                                      int row, int column, int rowSpan, int columnSpan) {
    if (!element) return nullptr;
    
    auto item = static_cast<UltraCanvasGridLayoutItem*>(InsertUIElement(element, -1));
    item->SetPosition(row, column);
    item->SetSpan(rowSpan, columnSpan);
    item->SetWidthMode(SizeMode::Auto);
    EnsureGridSize(row, column, rowSpan, columnSpan);

    return item;
}

void UltraCanvasGridLayout::EnsureGridSize(int row, int column, int rowSpan, int columnSpan) {
    int requiredRows = row + rowSpan;
    int requiredColumns = column + columnSpan;
    
    // Expand rows if needed
    while (static_cast<int>(rowDefinitions.size()) < requiredRows) {
        rowDefinitions.push_back(GridRowColumnDefinition::Auto());
    }
    
    // Expand columns if needed
    while (static_cast<int>(columnDefinitions.size()) < requiredColumns) {
        columnDefinitions.push_back(GridRowColumnDefinition::Auto());
    }
}

// ===== LAYOUT CALCULATION =====

void UltraCanvasGridLayout::PerformLayout() {
    if (items.empty() || rowDefinitions.empty() || columnDefinitions.empty() || !parentContainer) return;
    
    Rect2Di contentRect = parentContainer->GetContentRect();
    
    // Calculate row heights and column widths
    CalculateRowHeights(contentRect.height);
    CalculateColumnWidths(contentRect.width);
    
    // Position items in grid
    PositionItems();
    
    // Apply computed geometry to elements
    for (auto& item : items) {
        item->ApplyToElement();
    }
}

void UltraCanvasGridLayout::CalculateRowHeights(int availableHeight) {
    int rowCount = static_cast<int>(rowDefinitions.size());
    computedRowHeights.clear();
    computedRowHeights.resize(rowCount, 0);

    // First pass: calculate content sizes for auto rows
    std::vector<int> contentHeights(rowCount, 0);
    for (const auto& item : items) {
        if (item->GetRowSpan() == 1 && item->IsVisible()) {
            int row = item->GetRow();
            if (row >= 0 && row < rowCount) {
                int itemHeight = item->GetPreferredHeight();
                // If preferred height is 0 and element exists, use element height
                contentHeights[row] = std::max(contentHeights[row], itemHeight);
            }
        }
    }

    // Calculate fixed and percent sizes first
    int usedHeight = 0;
    int autoCount = 0;
    int totalStarWeight = 0;

    for (int i = 0; i < rowCount; ++i) {
        const auto& def = rowDefinitions[i];

        if (def.sizeMode == GridSizeMode::Fixed) {
            computedRowHeights[i] = def.size;
            usedHeight += def.size;
        } else if (def.sizeMode == GridSizeMode::Percent) {
            computedRowHeights[i] = availableHeight * (static_cast<float>(def.size) / 100.0f);
            usedHeight += computedRowHeights[i];
        } else if (def.sizeMode == GridSizeMode::Auto) {
            // Use content size, with minimum of 20 if no content
            computedRowHeights[i] = std::max(contentHeights[i], 1);
            usedHeight += computedRowHeights[i];
            autoCount++;
        } else if (def.sizeMode == GridSizeMode::Star) {
            totalStarWeight += def.size;
        }

        // Apply min/max constraints
        computedRowHeights[i] = std::clamp(computedRowHeights[i], def.minSize, def.maxSize);
    }

    // Distribute remaining space to Star-sized rows
    float remainingHeight = availableHeight - usedHeight;
    float starUnit = (totalStarWeight > 0 && remainingHeight > 0) ?
                     remainingHeight / static_cast<float>(totalStarWeight) : 0;

    for (int i = 0; i < rowCount; ++i) {
        const auto& def = rowDefinitions[i];

        if (def.sizeMode == GridSizeMode::Star) {
            computedRowHeights[i] = static_cast<int>(starUnit * static_cast<float>(def.size));
            // Apply min/max constraints
            computedRowHeights[i] = std::clamp(computedRowHeights[i], def.minSize, def.maxSize);
        }
    }
}

void UltraCanvasGridLayout::CalculateColumnWidths(int availableWidth) {
    int columnCount = static_cast<int>(columnDefinitions.size());
    computedColumnWidths.clear();
    computedColumnWidths.resize(columnCount, 0);

    // First pass: calculate content sizes for auto columns
    std::vector<int> contentWidths(columnCount, 0);
    for (const auto& item : items) {
        if (item->GetColumnSpan() == 1 && item->IsVisible()) {
            int column = item->GetColumn();
            if (column >= 0 && column < columnCount) {
                int itemWidth = item->GetPreferredWidth();
                contentWidths[column] = std::max(contentWidths[column], itemWidth);
            }
        }
    }

    // Calculate fixed and percent sizes first
    int usedWidth = 0;
    int autoCount = 0;
    int totalStarWeight = 0;

    for (int i = 0; i < columnCount; ++i) {
        const auto& def = columnDefinitions[i];

        if (def.sizeMode == GridSizeMode::Fixed) {
            computedColumnWidths[i] = def.size;
            usedWidth += def.size;
        } else if (def.sizeMode == GridSizeMode::Percent) {
            computedColumnWidths[i] = availableWidth * (static_cast<float>(def.size) / 100.0f);
            usedWidth += computedColumnWidths[i];
        } else if (def.sizeMode == GridSizeMode::Auto) {
            // Use content size, with minimum of 50 if no content
            computedColumnWidths[i] = std::max(contentWidths[i], 5);
            usedWidth += computedColumnWidths[i];
            autoCount++;
        } else if (def.sizeMode == GridSizeMode::Star) {
            totalStarWeight += def.size;
        }

        // Apply min/max constraints
        computedColumnWidths[i] = std::clamp(computedColumnWidths[i], def.minSize, def.maxSize);
    }

    if (columnCount > 0) {
        usedWidth += (columnCount - 1) * spacing;
    }

    // Distribute remaining space to Star-sized columns
    float remainingWidth = availableWidth - usedWidth;
    float starUnit = (totalStarWeight > 0 && remainingWidth > 0) ?
                     remainingWidth / static_cast<float>(totalStarWeight) : 0;

    for (int i = 0; i < columnCount; ++i) {
        const auto& def = columnDefinitions[i];

        if (def.sizeMode == GridSizeMode::Star) {
            computedColumnWidths[i] = static_cast<int>(starUnit * static_cast<float>(def.size));
            // Apply min/max constraints
            computedColumnWidths[i] = std::clamp(computedColumnWidths[i], def.minSize, def.maxSize);
        }
    }
}

void UltraCanvasGridLayout::PositionItems() {
    for (auto &item: items) {
        if (!item->IsVisible()) continue;

        Rect2Di cellBounds = GetCellBounds(item->GetRow(), item->GetColumn(),
                                           item->GetRowSpan(), item->GetColumnSpan());

        // Calculate item size based on size mode
        int itemWidth = 0;
        int itemHeight = 0;

        // Determine width based on mode
        if (item->GetWidthMode() == SizeMode::Fixed) {
            itemWidth = item->GetFixedWidth();
        } else if (item->GetWidthMode() == SizeMode::Auto) {
            itemWidth = std::min(item->GetPreferredWidth(), cellBounds.width);
        } else if (item->GetWidthMode() == SizeMode::Fill) {
            itemWidth = cellBounds.width;
        } else {
            // Default to preferred width
            itemWidth = std::min(item->GetPreferredWidth(), cellBounds.width);
        }

        // Determine height based on mode
        if (item->GetHeightMode() == SizeMode::Fixed) {
            itemHeight = item->GetFixedHeight();
        } else if (item->GetHeightMode() == SizeMode::Auto) {
            itemHeight = std::min(item->GetPreferredHeight(), cellBounds.height);
        } else if (item->GetHeightMode() == SizeMode::Fill) {
            itemHeight = cellBounds.height;
        } else {
            // Default to preferred height
            itemHeight = std::min(item->GetPreferredHeight(), cellBounds.height);
        }

        // Clamp to min/max constraints
        itemWidth = std::clamp(itemWidth, item->GetMinimumWidth(),
                               std::min(item->GetMaximumWidth(), cellBounds.width));
        itemHeight = std::clamp(itemHeight, item->GetMinimumHeight(),
                                std::min(item->GetMaximumHeight(), cellBounds.height));

        // Calculate position based on alignment
        float itemX = cellBounds.x;
        float itemY = cellBounds.y;

        // Apply horizontal alignment (only if not Fill)
        LayoutItemAlignment hAlign = item->GetHorizontalAlignment();
        if (hAlign == LayoutItemAlignment::Center) {
            itemX += (cellBounds.width - itemWidth) / 2;
        } else if (hAlign == LayoutItemAlignment::End) {
            itemX += cellBounds.width - itemWidth;
        } else if (hAlign == LayoutItemAlignment::Fill) {
            // Override width to fill
            itemWidth = cellBounds.width;
        }
        // LayoutItemAlignment::Start uses itemX as-is

        // Apply vertical alignment (only if not Fill)
        LayoutItemAlignment vAlign = item->GetVerticalAlignment();
        if (vAlign == LayoutItemAlignment::Center) {
            itemY += (cellBounds.height - itemHeight) / 2;
        } else if (vAlign == LayoutItemAlignment::End) {
            itemY += cellBounds.height - itemHeight;
        } else if (vAlign == LayoutItemAlignment::Fill) {
            // Override height to fill
            itemHeight = cellBounds.height;
        }
        // LayoutItemAlignment::Start uses itemY as-is

        // Set computed geometry with margins
        item->SetComputedGeometry(
                itemX + item->GetMarginLeft(),
                itemY + item->GetMarginTop(),
                itemWidth,
                itemHeight
        );
    }
}

Rect2Di UltraCanvasGridLayout::GetCellBounds(int row, int column, int rowSpan, int columnSpan) const {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    
    // Calculate X position
    for (int c = 0; c < column && c < static_cast<int>(computedColumnWidths.size()); ++c) {
        x += computedColumnWidths[c] + spacing;
    }
    
    // Calculate Y position
    for (int r = 0; r < row && r < static_cast<int>(computedRowHeights.size()); ++r) {
        y += computedRowHeights[r] + spacing;
    }
    
    // Calculate width (including spanning)
    for (int c = 0; c < columnSpan && (column + c) < static_cast<int>(computedColumnWidths.size()); ++c) {
        width += computedColumnWidths[column + c];
        if (c < columnSpan - 1) width += spacing;
    }
    
    // Calculate height (including spanning)
    for (int r = 0; r < rowSpan && (row + r) < static_cast<int>(computedRowHeights.size()); ++r) {
        height += computedRowHeights[row + r];
        if (r < rowSpan - 1) height += spacing;
    }
    
    return Rect2Di(x, y, width, height);
}

int UltraCanvasGridLayout::CalculateSize(const GridRowColumnDefinition& def,
                                          int availableSpace, int contentSize) const {
    switch (def.sizeMode) {
        case GridSizeMode::Fixed:
            return def.size;
        
        case GridSizeMode::Auto:
            return contentSize;
        
        case GridSizeMode::Percent:
            return availableSpace * (static_cast<float>(def.size) / 100.0f);
        
        case GridSizeMode::Star:
            // Calculated separately
            return 0;
        
        default:
            return 0;
    }
}

int UltraCanvasGridLayout::GetFixedAndPercentSize(
    const std::vector<GridRowColumnDefinition>& definitions,
    int availableSpace) const {
    
    float total = 0;
    for (const auto& def : definitions) {
        if (def.sizeMode == GridSizeMode::Fixed) {
            total += def.size;
        } else if (def.sizeMode == GridSizeMode::Percent) {
            total += availableSpace * def.size / 100.0f;
        }
    }
    
    // Add spacing between rows/columns
    if (definitions.size() > 1) {
        total += spacing * (definitions.size() - 1);
    }
    
    return static_cast<int>(total);
}

int UltraCanvasGridLayout::GetTotalStarWeight(
    const std::vector<GridRowColumnDefinition>& definitions) const {
    
    int total = 0;
    for (const auto& def : definitions) {
        if (def.sizeMode == GridSizeMode::Star) {
            total += def.size;
        }
    }
    return total;
}

// ===== SIZE CALCULATION =====

Size2Di UltraCanvasGridLayout::CalculateMinimumSize() const {
    int width = 0;
    int height = 0;
    
    // Calculate minimum based on row/column definitions
    for (const auto& def : rowDefinitions) {
        height += static_cast<int>(def.minSize);
    }
    if (rowDefinitions.size() > 1) {
        height += spacing * (rowDefinitions.size() - 1);
    }
    
    for (const auto& def : columnDefinitions) {
        width += static_cast<int>(def.minSize);
    }
    if (columnDefinitions.size() > 1) {
        width += spacing * (columnDefinitions.size() - 1);
    }
    
//    width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
//    height += GetTotalPaddingVertical() + GetTotalMarginVertical();
    
    return Size2Di(width, height);
}

Size2Di UltraCanvasGridLayout::CalculatePreferredSize() const {
    // For preferred size, use auto-sized content
    // This is a simplified calculation
    return CalculateMinimumSize();
}

Size2Di UltraCanvasGridLayout::CalculateMaximumSize() const {
    return Size2Di(10000, 10000); // Typically unlimited
}

} // namespace UltraCanvas
