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
UltraCanvasBoxLayoutItem::UltraCanvasBoxLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem)
        : UltraCanvasLayoutItem(elem) {
}

int UltraCanvasBoxLayoutItem::GetPreferredWidth() const {
    switch (widthMode) {
        case SizeMode::Fixed:
            return fixedWidth;

        case SizeMode::Auto:
            return element->GetPreferredWidth() + element->GetTotalMarginHorizontal();

        case SizeMode::Fill:
            return 0; // Will be calculated by layout

        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container

        default:
            return 0;
    }
}

int UltraCanvasBoxLayoutItem::GetPreferredHeight() const {
    switch (heightMode) {
        case SizeMode::Fixed:
            return fixedHeight;

        case SizeMode::Auto:
            return element->GetPreferredHeight() + element->GetTotalMarginVertical();

        case SizeMode::Fill:
            return 0; // Will be calculated by layout

        case SizeMode::Percentage:
            return 0; // Will be calculated by layout based on container

        default:
            return 0;
    }
}

// ===== CONSTRUCTORS =====

    UltraCanvasBoxLayout::UltraCanvasBoxLayout(UltraCanvasContainer* parent, BoxLayoutDirection dir)
        : UltraCanvasLayout(parent), direction(dir) {
    }

// ===== ITEM MANAGEMENT =====

    UltraCanvasBoxLayoutItem* UltraCanvasBoxLayout::GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> element) const {
        if (element) {
            for(auto &item : items) {
                if (item->GetElement() == element) {
                    return item.get();
                }
            }
        }
        return nullptr;
    }


    void UltraCanvasBoxLayout::RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) {
        if (!element) return;

        auto it = std::find_if(items.begin(), items.end(),
            [&element](const std::unique_ptr<UltraCanvasBoxLayoutItem>& item) {
                return item->GetElement() == element;
            });

        if (it != items.end()) {
            items.erase(it);

            // Remove from parent container if we have one
            if (parentContainer && element->GetParentContainer() == parentContainer) {
                parentContainer->RemoveChild(element);
            }
        }
    }

    UltraCanvasBoxLayoutItem* UltraCanvasBoxLayout::GetItemAt(int index) const {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            return items[index].get();
        }
        return nullptr;
    }

    void UltraCanvasBoxLayout::ClearItems() {
        items.clear();
        InvalidateContainerLayout();
    }

// ===== BOX LAYOUT SPECIFIC =====
    UltraCanvasLayoutItem* UltraCanvasBoxLayout::InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) {
        if (!element) return nullptr;

        auto existingItem = GetItemForUIElement(element);
        if (existingItem) {
            return existingItem;
        }

        auto item = std::make_unique<UltraCanvasBoxLayoutItem>(element);
        auto itemPtr = item.get();
        if (index >= 0 && index <= static_cast<int>(items.size())) {
            items.insert(items.begin() + index, std::move(item));
        } else {
            items.push_back(std::move(item));
        }

        if (parentContainer) {
            parentContainer->AddChild(element);
        }
        return itemPtr;
    }

    UltraCanvasBoxLayoutItem* UltraCanvasBoxLayout::AddUIElement(std::shared_ptr<UltraCanvasUIElement> element, float stretch) {
        UltraCanvasBoxLayoutItem* item = static_cast<UltraCanvasBoxLayoutItem*>(InsertUIElement(element, -1));
        item->SetStretch(stretch);
        return item;
    }

    void UltraCanvasBoxLayout::AddSpacing(int size) {
        auto item = std::make_unique<UltraCanvasBoxLayoutItem>();
        item->SetFixedSize(size, size);
        item->SetStretch(0);
        items.push_back(std::move(item));

        InvalidateContainerLayout();
    }

    void UltraCanvasBoxLayout::AddStretch(int stretch) {
        auto item = std::make_unique<UltraCanvasBoxLayoutItem>();
        item->SetSizeMode(SizeMode::Fill, SizeMode::Fill);
        item->SetStretch(stretch);
        items.push_back(std::move(item));

        InvalidateContainerLayout();
    }

    // ===== LAYOUT CALCULATION =====

    void UltraCanvasBoxLayout::PerformLayout() {
        if (items.empty() || !parentContainer) return;


        Rect2Di contentRect = parentContainer->GetContentRect();

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
        int availableWidth = contentRect.width;
        int availableHeight = contentRect.height;

        // Calculate total fixed size and total stretch
        int totalFixedSize = CalculateTotalFixedSize();
        int totalSpacing = CalculateTotalSpacing();
        float totalStretch = CalculateTotalStretch();

        // Calculate remaining space for stretching
        int remainingSpace = availableWidth - totalFixedSize - totalSpacing;
        float stretchUnit = (totalStretch > 0 && remainingSpace > 0) ? static_cast<float>(remainingSpace) / totalStretch : 0;

        // Position items
        int currentX = 0;

        for (size_t i = 0; i < items.size(); ++i) {
            auto& item = items[i];
            if (!item->IsVisible()) continue;
            auto crossAxisAlignment = item->GetAlignment();

            // Calculate width
            int itemWidth = 0;
            if (item->GetWidthMode() == SizeMode::Fixed) {
                itemWidth = item->GetFixedWidth();
            } else if (item->GetWidthMode() == SizeMode::Fill || item->GetStretch() > 0) {
                itemWidth = static_cast<int>(stretchUnit * item->GetStretch());
            } else {
                itemWidth = item->GetPreferredWidth();
            }

            // Clamp to min/max
            itemWidth = std::clamp(itemWidth, item->GetMinimumWidth(), item->GetMaximumWidth());

            // Calculate height based on cross-axis alignment
            int itemHeight = 0;
            int itemY = 0;

            if (item->GetHeightMode() == SizeMode::Fixed) {
                itemHeight = item->GetFixedHeight();
            } else if (item->GetHeightMode() == SizeMode::Fill ||
                       crossAxisAlignment == LayoutItemAlignment::Fill) {
                itemHeight = availableHeight;
            } else {
                itemHeight = item->GetPreferredHeight();
            }

            itemHeight = std::clamp(itemHeight, item->GetMinimumHeight(), item->GetMaximumHeight());

            if (crossAxisAlignment == LayoutItemAlignment::Center) {
                itemY += (availableHeight - itemHeight) / 2;
            } else if (crossAxisAlignment == LayoutItemAlignment::End) {
                itemY += availableHeight - itemHeight;
            }

            // Set computed geometry
            item->SetComputedGeometry(
                currentX + item->GetMarginLeft(),
                itemY + item->GetMarginTop(),
                itemWidth - item->GetTotalMarginHorizontal(),
                itemHeight - item->GetTotalMarginVertical()
            );

            // Move to next position
            currentX += itemWidth;
            if (i < items.size() - 1) {
                currentX += spacing;
            }
        }

        // Apply main axis alignment
        if (mainAxisAlignment != LayoutAlignment::Start && remainingSpace > 0) {
            int offset = 0;
            if (mainAxisAlignment == LayoutAlignment::Center) {
                offset = remainingSpace / 2;
            } else if (mainAxisAlignment == LayoutAlignment::End) {
                offset = remainingSpace;
            }

            if (offset > 0) {
                for (auto& item : items) {
                    int x = item->GetComputedX() + offset;
                    item->SetComputedGeometry(x, item->GetComputedY(),
                                             item->GetComputedWidth(), item->GetComputedHeight());
                }
            }
        }
    }

    void UltraCanvasBoxLayout::LayoutVertical(const Rect2Di& contentRect) {
        int availableWidth = contentRect.width;
        int availableHeight = contentRect.height;

        // Calculate total fixed size and total stretch
        int totalFixedSize = CalculateTotalFixedSize();
        int totalSpacing = CalculateTotalSpacing();
        float totalStretch = CalculateTotalStretch();

        // Calculate remaining space for stretching
        int remainingSpace = availableHeight - totalFixedSize - totalSpacing;
        float stretchUnit = (totalStretch > 0 && remainingSpace > 0) ? static_cast<float>(remainingSpace) / totalStretch : 0;

        // Position items
        int currentY = 0;

        for (size_t i = 0; i < items.size(); ++i) {
            auto& item = items[i];
            auto crossAxisAlignment = item->GetAlignment();
            if (!item->IsVisible()) continue;

            // Calculate height
            int itemHeight = 0;
            if (item->GetHeightMode() == SizeMode::Fixed) {
                itemHeight = item->GetFixedHeight();
            } else if (item->GetHeightMode() == SizeMode::Fill || item->GetStretch() > 0) {
                itemHeight = static_cast<int>(stretchUnit * item->GetStretch());
            } else {
                itemHeight = item->GetPreferredHeight();
            }

            // Clamp to min/max
            itemHeight = std::clamp(itemHeight, item->GetMinimumHeight(), item->GetMaximumHeight());

            // Calculate width based on cross-axis alignment
            int itemWidth = 0;
            int itemX = 0;

            if (item->GetWidthMode() == SizeMode::Fixed) {
                itemWidth = item->GetFixedWidth();
            } else if (item->GetWidthMode() == SizeMode::Fill ||
                       crossAxisAlignment == LayoutItemAlignment::Fill) {
                itemWidth = availableWidth;
            } else {
                itemWidth = item->GetPreferredWidth();
            }

            itemWidth = std::clamp(itemWidth, item->GetMinimumWidth(), item->GetMaximumWidth());

            // Apply cross-axis alignment for width
            if (crossAxisAlignment == LayoutItemAlignment::Center) {
                itemX += (availableWidth - itemWidth) / 2;
            } else if (crossAxisAlignment == LayoutItemAlignment::End) {
                itemX += availableWidth - itemWidth;
            }

            // Set computed geometry
            item->SetComputedGeometry(
                itemX + item->GetMarginLeft(),
                currentY + item->GetMarginTop(),
                itemWidth - item->GetTotalMarginHorizontal(),
                itemHeight - item->GetTotalMarginVertical()
            );

            // Move to next position
            currentY += itemHeight;
            if (i < items.size() - 1) {
                currentY += spacing;
            }
        }

        // Apply main axis alignment
        if (mainAxisAlignment != LayoutAlignment::Start && remainingSpace > 0) {
            int offset = 0;
            if (mainAxisAlignment == LayoutAlignment::Center) {
                offset = remainingSpace / 2;
            } else if (mainAxisAlignment == LayoutAlignment::End) {
                offset = remainingSpace;
            }

            if (offset > 0) {
                for (auto& item : items) {
                    int y = item->GetComputedY() + offset;
                    item->SetComputedGeometry(item->GetComputedX(), y,
                                             item->GetComputedWidth(), item->GetComputedHeight());
                }
            }
        }
    }

    float UltraCanvasBoxLayout::CalculateTotalStretch() const {
        float total = 0;
        for (const auto& item : items) {
            if (item->IsVisible()) {
                total += item->GetStretch();
            }
        }
        return total;
    }

    int UltraCanvasBoxLayout::CalculateTotalFixedSize() const {
        int total = 0;

        for (const auto& item : items) {
            if (!item->IsVisible()) continue;

            if (direction == BoxLayoutDirection::Horizontal) {
                if (item->GetWidthMode() == SizeMode::Fixed || item->GetStretch() == 0) {
                    total += item->GetPreferredWidth();
                }
            } else {
                if (item->GetHeightMode() == SizeMode::Fixed || item->GetStretch() == 0) {
                    total += item->GetPreferredHeight();
                }
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
                width += item->GetMinimumWidth() + item->GetTotalMarginHorizontal();
                height = std::max(height, item->GetMinimumHeight() + item->GetTotalMarginVertical());
            }
            width += CalculateTotalSpacing();
        } else {
            for (const auto& item : items) {
                if (!item->IsVisible()) continue;
                height += item->GetMinimumHeight() + item->GetTotalMarginVertical();
                width = std::max(width, static_cast<int>(item->GetMinimumWidth()) + item->GetTotalMarginHorizontal());
            }
            height += CalculateTotalSpacing();
        }

//        width += GetTotalPaddingHorizontal() + GetTotalMarginHorizontal();
//        height += GetTotalPaddingVertical() + GetTotalMarginVertical();
//
        return Size2Di(width, height);
    }

    Size2Di UltraCanvasBoxLayout::CalculatePreferredSize() const {
        int width = 0;
        int height = 0;

        if (direction == BoxLayoutDirection::Horizontal) {
            for (const auto& item : items) {
                if (!item->IsVisible()) continue;
                width += item->GetPreferredWidth();
                height = std::max(height, item->GetPreferredHeight());
            }
            width += CalculateTotalSpacing();
        } else {
            for (const auto& item : items) {
                if (!item->IsVisible()) continue;
                height += item->GetPreferredHeight();
                width = std::max(width, item->GetPreferredWidth());
            }
            height += CalculateTotalSpacing();
        }

        return Size2Di(width, height);
    }

    Size2Di UltraCanvasBoxLayout::CalculateMaximumSize() const {
        return Size2Di(10000, 10000); // Typically unlimited
    }

} // namespace UltraCanvas
