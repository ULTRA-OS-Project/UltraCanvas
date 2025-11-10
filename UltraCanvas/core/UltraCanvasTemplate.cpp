// UltraCanvasTemplate.cpp
// Template system implementation for creating reusable UI component layouts
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "../include/UltraCanvasTemplate.h"
#include "../include/UltraCanvasButton.h"
#include "../include/UltraCanvasLabel.h"
#include "../include/UltraCanvasDropdown.h"
#include "../include/UltraCanvasSeparator.h"
#include "../include/UltraCanvasRenderContext.h"
#include "../include/UltraCanvasLayoutEngine.h"
#include <algorithm>
#include <cstdlib>

namespace UltraCanvas {

// ===== CONSTRUCTOR =====
    UltraCanvasTemplate::UltraCanvasTemplate(const std::string& identifier,
                                             long id, long x, long y, long w, long h)
            : UltraCanvasContainer(identifier, id, x, y, w, h),
              isDirty(true),
              isDragging(false),
              dragStartPosition(0.0f, 0.0f),
              dragOffset(0.0f, 0.0f) {

        // Initialize default settings
        dimensions = TemplateDimensions::Auto();
        appearance = TemplateAppearance();
        placementRule = TemplatePlacementRule::Flow();
        scrollSettings = TemplateScrollSettings();
        dragHandle = TemplateDragHandle();

        // Register default element factories
        RegisterDefaultFactories();
    }

// ===== TEMPLATE CONFIGURATION =====
    void UltraCanvasTemplate::SetDimensions(const TemplateDimensions& dims) {
        dimensions = dims;
        isDirty = true;
    }

    void UltraCanvasTemplate::SetScrollSettings(const TemplateScrollSettings& settings) {
        scrollSettings = settings;
        isDirty = true;
    }

    void UltraCanvasTemplate::SetAppearance(const TemplateAppearance& app) {
        appearance = app;
        isDirty = true;
    }

    void UltraCanvasTemplate::SetPlacementRule(const TemplatePlacementRule& rule) {
        placementRule = rule;
        isDirty = true;
    }

    void UltraCanvasTemplate::SetDragHandle(const TemplateDragHandle& handle) {
        dragHandle = handle;
        isDirty = true;
    }

// ===== ELEMENT MANAGEMENT =====
    void UltraCanvasTemplate::AddElement(const TemplateElementDescriptor& descriptor) {
        elementDescriptors.push_back(descriptor);
        isDirty = true;
    }

    void UltraCanvasTemplate::InsertElement(size_t index, const TemplateElementDescriptor& descriptor) {
        if (index <= elementDescriptors.size()) {
            elementDescriptors.insert(elementDescriptors.begin() + index, descriptor);
            isDirty = true;
        }
    }

    void UltraCanvasTemplate::RemoveElement(const std::string& identifier) {
        auto it = std::remove_if(elementDescriptors.begin(), elementDescriptors.end(),
                                 [&identifier](const TemplateElementDescriptor& desc) {
                                     return desc.identifier == identifier;
                                 });

        if (it != elementDescriptors.end()) {
            elementDescriptors.erase(it, elementDescriptors.end());
            isDirty = true;
        }
    }

    void UltraCanvasTemplate::RemoveElementAt(size_t index) {
        if (index < elementDescriptors.size()) {
            elementDescriptors.erase(elementDescriptors.begin() + index);
            isDirty = true;
        }
    }

    void UltraCanvasTemplate::ClearElements() {
        elementDescriptors.clear();
        templateElements.clear();
        RemoveAllElements(); // Clear container children
        isDirty = true;
    }

// ===== ELEMENT ACCESS =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::GetElement(const std::string& identifier) {
        for (auto& element : templateElements) {
            if (element && element->GetIdentifier() == identifier) {
                return element;
            }
        }
        return nullptr;
    }

// ===== TEMPLATE OPERATIONS =====
    void UltraCanvasTemplate::RebuildTemplate() {
        // Clear existing elements
        templateElements.clear();
        RemoveAllElements();

        // Build new elements
        BuildElements();

        // Apply layout
        ApplyLayout();

        isDirty = false;
    }

    void UltraCanvasTemplate::RefreshLayout() {
        if (isDirty) {
            RebuildTemplate();
        } else {
            ApplyLayout();
        }
    }

    void UltraCanvasTemplate::UpdateElementProperties() {
        for (size_t i = 0; i < elementDescriptors.size() && i < templateElements.size(); ++i) {
            const auto& desc = elementDescriptors[i];
            auto element = templateElements[i];

            if (element) {
                element->SetVisible(true);
                element->SetEnabled(true);

                // Update type-specific properties
                if (desc.elementType == "Button") {
                    auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element);
                    if (button) {
                        button->SetText(desc.text);
                        if (!desc.iconPath.empty()) {
                            button->SetIcon(desc.iconPath);
                        }
                        if (!desc.tooltip.empty()) {
                            button->SetTooltip(desc.tooltip);
                        }
                    }
                }
                else if (desc.elementType == "Label") {
                    auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element);
                    if (label) {
                        label->SetText(desc.text);
                    }
                }
                else if (desc.elementType == "DropDown") {
                    auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(element);
                    if (dropdown) {
                        // Parse items from properties
                        int itemCount = 0;
                        auto countIt = desc.properties.find("item_count");
                        if (countIt != desc.properties.end()) {
                            itemCount = std::stoi(countIt->second);
                        }

                        dropdown->ClearItems();
                        for (int i = 0; i < itemCount; ++i) {
                            auto itemIt = desc.properties.find("item_" + std::to_string(i));
                            if (itemIt != desc.properties.end()) {
                                dropdown->AddItem(itemIt->second);
                            }
                        }
                    }
                }
            }
        }
    }

// ===== SIZE CALCULATION =====
    Point2Df UltraCanvasTemplate::CalculateRequiredSize() const {
        if (templateElements.empty()) {
            return Point2Df(dimensions.fixedWidth, dimensions.fixedHeight);
        }

        float totalWidth = appearance.paddingLeft + appearance.paddingRight;
        float totalHeight = appearance.paddingTop + appearance.paddingBottom;

        switch (placementRule.type) {
            case TemplatePlacementType::Flow:
            case TemplatePlacementType::Stack: {
                if (placementRule.direction == LayoutDirection::Horizontal) {
                    float maxHeight = 0;
                    float currentRowWidth = appearance.paddingLeft;
                    float currentRowHeight = 0;

                    for (const auto& element : templateElements) {
                        if (element && element->IsVisible()) {
                            float elementWidth = static_cast<float>(element->GetWidth());
                            float elementHeight = static_cast<float>(element->GetHeight());

                            // Check if we need to wrap
                            if (placementRule.allowWrap &&
                                currentRowWidth + elementWidth + placementRule.spacing > GetWidth() - appearance.paddingRight) {
                                // Start new row
                                totalHeight += currentRowHeight + placementRule.spacing;
                                currentRowWidth = appearance.paddingLeft;
                                currentRowHeight = 0;
                            }

                            currentRowWidth += elementWidth + placementRule.spacing;
                            currentRowHeight = std::max(currentRowHeight, elementHeight);
                            maxHeight = std::max(maxHeight, elementHeight);
                            totalWidth = std::max(totalWidth, currentRowWidth);
                        }
                    }

                    totalHeight += currentRowHeight;
                    if (!placementRule.allowWrap) {
                        totalHeight = appearance.paddingTop + appearance.paddingBottom + maxHeight;
                    }
                } else {
                    float maxWidth = 0;
                    for (const auto& element : templateElements) {
                        if (element && element->IsVisible()) {
                            totalHeight += element->GetHeight() + placementRule.spacing;
                            maxWidth = std::max(maxWidth, static_cast<float>(element->GetWidth()));
                        }
                    }
                    totalHeight -= placementRule.spacing; // Remove last spacing
                    totalWidth += maxWidth;
                }
                break;
            }

            case TemplatePlacementType::Grid: {
                int visibleCount = 0;
                float maxElementWidth = 0;
                float maxElementHeight = 0;

                for (const auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        visibleCount++;
                        maxElementWidth = std::max(maxElementWidth, static_cast<float>(element->GetWidth()));
                        maxElementHeight = std::max(maxElementHeight, static_cast<float>(element->GetHeight()));
                    }
                }

                int actualCols = placementRule.gridColumns;
                int actualRows = (placementRule.gridRows > 0) ? placementRule.gridRows :
                                 (visibleCount + actualCols - 1) / actualCols;

                totalWidth += actualCols * maxElementWidth + (actualCols - 1) * placementRule.spacing;
                totalHeight += actualRows * maxElementHeight + (actualRows - 1) * placementRule.spacing;
                break;
            }

            case TemplatePlacementType::Dock:
            case TemplatePlacementType::Fixed:
            case TemplatePlacementType::Absolute:
            case TemplatePlacementType::Relative: {
                for (const auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        float right = element->GetX() + element->GetWidth();
                        float bottom = element->GetY() + element->GetHeight();
                        totalWidth = std::max(totalWidth, right + appearance.paddingRight);
                        totalHeight = std::max(totalHeight, bottom + appearance.paddingBottom);
                    }
                }
                break;
            }
        }

        // Apply dimension constraints
        switch (dimensions.widthMode) {
            case TemplateSizeMode::Fixed:
                totalWidth = dimensions.fixedWidth;
                break;
            case TemplateSizeMode::Auto:
                // Already calculated
                break;
            case TemplateSizeMode::Fill:
                totalWidth = GetParent() ? GetParent()->GetWidth() : dimensions.fixedWidth;
                break;
            case TemplateSizeMode::Percent:
                totalWidth = GetParent() ? GetParent()->GetWidth() * dimensions.percentWidth / 100.0f : dimensions.fixedWidth;
                break;
        }

        switch (dimensions.heightMode) {
            case TemplateSizeMode::Fixed:
                totalHeight = dimensions.fixedHeight;
                break;
            case TemplateSizeMode::Auto:
                // Already calculated
                break;
            case TemplateSizeMode::Fill:
                totalHeight = GetParent() ? GetParent()->GetHeight() : dimensions.fixedHeight;
                break;
            case TemplateSizeMode::Percent:
                totalHeight = GetParent() ? GetParent()->GetHeight() * dimensions.percentHeight / 100.0f : dimensions.fixedHeight;
                break;
        }

        // Apply min/max constraints
        totalWidth = std::max(dimensions.minWidth, std::min(totalWidth, dimensions.maxWidth));
        totalHeight = std::max(dimensions.minHeight, std::min(totalHeight, dimensions.maxHeight));

        return Point2Df(totalWidth, totalHeight);
    }

    void UltraCanvasTemplate::FitToContent() {
        Point2Df requiredSize = CalculateRequiredSize();
        SetWidth(static_cast<long>(requiredSize.x));
        SetHeight(static_cast<long>(requiredSize.y));
    }

    void UltraCanvasTemplate::ApplyToContainer(const Rect2Df& containerRect) {
        SetX(static_cast<long>(containerRect.x + appearance.marginLeft));
        SetY(static_cast<long>(containerRect.y + appearance.marginTop));
        SetWidth(static_cast<long>(containerRect.width - appearance.marginLeft - appearance.marginRight));
        SetHeight(static_cast<long>(containerRect.height - appearance.marginTop - appearance.marginBottom));
        RefreshLayout();
    }

// ===== RENDERING =====
    void UltraCanvasTemplate::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        // Rebuild if necessary
        if (isDirty) {
            RebuildTemplate();
        }

        auto* ctx = GetRenderContext();
        if (!ctx) return;

        ULTRACANVAS_RENDER_SCOPE(ctx);

        // Draw template background
        DrawTemplateBackground();

        // Draw drag handle if enabled
        if (dragHandle.enabled) {
            DrawDragHandle();
        }

        // Apply clipping for scrollable content
        if (scrollSettings.horizontal != TemplateScrollMode::Off ||
            scrollSettings.vertical != TemplateScrollMode::Off) {
            ctx->PushState();
            ctx->ClipRect(
                    static_cast<float>(GetX()) + appearance.paddingLeft,
                    static_cast<float>(GetY()) + appearance.paddingTop,
                    static_cast<float>(GetWidth()) - appearance.paddingLeft - appearance.paddingRight,
                    static_cast<float>(GetHeight()) - appearance.paddingTop - appearance.paddingBottom
            );
        }

        // Render container children (the template elements)
        UltraCanvasContainer::Render(IRenderContext* ctx);

        // Restore clipping
        if (scrollSettings.horizontal != TemplateScrollMode::Off ||
            scrollSettings.vertical != TemplateScrollMode::Off) {
            ctx->PopState();
        }
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasTemplate::OnEvent(const UCEvent& event) {
        if (IsDisabled()) return false;

        // Handle drag functionality
        if (dragHandle.enabled) {
            Point2Df mousePos(static_cast<float>(event.mouse.x), static_cast<float>(event.mouse.y));

            switch (event.type) {
                case UCEventType::MouseDown:
                    if (event.mouse.button == UCMouseButton::Left) {
                        // Check if click is on drag handle
                        Rect2Df handleRect;
                        float x = static_cast<float>(GetX());
                        float y = static_cast<float>(GetY());
                        float w = static_cast<float>(GetWidth());
                        float h = static_cast<float>(GetHeight());

                        switch (dragHandle.position) {
                            case LayoutDockSide::Left:
                                handleRect = Rect2Df(x, y, dragHandle.width, h);
                                break;
                            case LayoutDockSide::Right:
                                handleRect = Rect2Df(x + w - dragHandle.width, y, dragHandle.width, h);
                                break;
                            case LayoutDockSide::Top:
                                handleRect = Rect2Df(x, y, w, dragHandle.width);
                                break;
                            case LayoutDockSide::Bottom:
                                handleRect = Rect2Df(x, y + h - dragHandle.width, w, dragHandle.width);
                                break;
                            default:
                                break;
                        }

                        if (handleRect.Contains(mousePos)) {
                            StartDrag(mousePos);
                            return true;
                        }
                    }
                    break;

                case UCEventType::MouseMove:
                    if (isDragging) {
                        UpdateDrag(mousePos);
                        return true;
                    }
                    break;

                case UCEventType::MouseUp:
                    if (isDragging && event.mouse.button == UCMouseButton::Left) {
                        EndDrag();
                        return true;
                    }
                    break;

                default:
                    break;
            }
        }

        // Pass event to container for child handling
        return UltraCanvasContainer::OnEvent(event);
    }

// ===== DRAG FUNCTIONALITY =====
    void UltraCanvasTemplate::StartDrag(const Point2Df& startPosition) {
        isDragging = true;
        dragStartPosition = startPosition;
        dragOffset = Point2Df(startPosition.x - GetX(), startPosition.y - GetY());
    }

    void UltraCanvasTemplate::UpdateDrag(const Point2Df& currentPosition) {
        if (isDragging) {
            SetX(static_cast<long>(currentPosition.x - dragOffset.x));
            SetY(static_cast<long>(currentPosition.y - dragOffset.y));
        }
    }

    void UltraCanvasTemplate::EndDrag() {
        isDragging = false;
    }

// ===== ELEMENT FACTORY REGISTRATION =====
    void UltraCanvasTemplate::RegisterElementFactory(const std::string& elementType,
                                                     std::function<std::shared_ptr<UltraCanvasUIElement>(const TemplateElementDescriptor&)> factory) {
        elementFactories[elementType] = factory;
    }

// ===== INTERNAL METHODS =====
    void UltraCanvasTemplate::BuildElements() {
        for (const auto& desc : elementDescriptors) {
            std::shared_ptr<UltraCanvasUIElement> element;

            // Check for custom factory first
            auto factoryIt = elementFactories.find(desc.elementType);
            if (factoryIt != elementFactories.end()) {
                element = factoryIt->second(desc);
            }
                // Otherwise use default factories
            else if (desc.elementType == "Button") {
                element = CreateButtonElement(desc);
            }
            else if (desc.elementType == "Label") {
                element = CreateLabelElement(desc);
            }
            else if (desc.elementType == "DropDown") {
                element = CreateDropDownElement(desc);
            }
            else if (desc.elementType == "Separator") {
                element = CreateSeparatorElement(desc);
            }
            else if (desc.elementType == "Spacer") {
                element = CreateSpacerElement(desc);
            }

            if (element) {
                templateElements.push_back(element);
                AddElement(element.get());
            }
        }
    }

    void UltraCanvasTemplate::ApplyLayout() {
        if (templateElements.empty()) return;

        float baseX = static_cast<float>(GetX()) + appearance.paddingLeft;
        float baseY = static_cast<float>(GetY()) + appearance.paddingTop;
        float currentX = baseX;
        float currentY = baseY;

        switch (placementRule.type) {
            case TemplatePlacementType::Flow: {
                if (placementRule.direction == LayoutDirection::Horizontal) {
                    float rowHeight = 0;
                    float availableWidth = GetWidth() - appearance.paddingLeft - appearance.paddingRight;

                    for (auto& element : templateElements) {
                        if (element && element->IsVisible()) {
                            float elementWidth = static_cast<float>(element->GetWidth());
                            float elementHeight = static_cast<float>(element->GetHeight());

                            // Check if we need to wrap
                            if (placementRule.allowWrap &&
                                currentX - baseX + elementWidth > availableWidth) {
                                currentX = baseX;
                                currentY += rowHeight + placementRule.spacing;
                                rowHeight = 0;
                            }

                            element->SetX(static_cast<long>(currentX));
                            element->SetY(static_cast<long>(currentY));

                            currentX += elementWidth + placementRule.itemSpacing;
                            rowHeight = std::max(rowHeight, elementHeight);
                        }
                    }
                } else {
                    for (auto& element : templateElements) {
                        if (element && element->IsVisible()) {
                            element->SetX(static_cast<long>(currentX));
                            element->SetY(static_cast<long>(currentY));
                            currentY += element->GetHeight() + placementRule.itemSpacing;
                        }
                    }
                }
                break;
            }

            case TemplatePlacementType::Stack: {
                // Stack elements with optional alignment
                for (auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        float elementX = currentX;
                        float elementY = currentY;

                        // Apply alignment
                        if (placementRule.direction == LayoutDirection::Horizontal) {
                            switch (placementRule.crossAlignment) {
                                case LayoutAlignment::Center:
                                    elementY = baseY + (GetHeight() - appearance.paddingTop - appearance.paddingBottom - element->GetHeight()) / 2;
                                    break;
                                case LayoutAlignment::End:
                                    elementY = GetY() + GetHeight() - appearance.paddingBottom - element->GetHeight();
                                    break;
                                default:
                                    break;
                            }
                            element->SetX(static_cast<long>(elementX));
                            element->SetY(static_cast<long>(elementY));
                            currentX += element->GetWidth() + placementRule.spacing;
                        } else {
                            switch (placementRule.crossAlignment) {
                                case LayoutAlignment::Center:
                                    elementX = baseX + (GetWidth() - appearance.paddingLeft - appearance.paddingRight - element->GetWidth()) / 2;
                                    break;
                                case LayoutAlignment::End:
                                    elementX = GetX() + GetWidth() - appearance.paddingRight - element->GetWidth();
                                    break;
                                default:
                                    break;
                            }
                            element->SetX(static_cast<long>(elementX));
                            element->SetY(static_cast<long>(elementY));
                            currentY += element->GetHeight() + placementRule.spacing;
                        }
                    }
                }
                break;
            }

            case TemplatePlacementType::Grid: {
                int col = 0;
                int row = 0;
                float cellWidth = (GetWidth() - appearance.paddingLeft - appearance.paddingRight -
                                   (placementRule.gridColumns - 1) * placementRule.spacing) / placementRule.gridColumns;
                float cellHeight = 30.0f; // Default cell height

                if (placementRule.gridRows > 0) {
                    cellHeight = (GetHeight() - appearance.paddingTop - appearance.paddingBottom -
                                  (placementRule.gridRows - 1) * placementRule.spacing) / placementRule.gridRows;
                }

                for (auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        float x = baseX + col * (cellWidth + placementRule.spacing);
                        float y = baseY + row * (cellHeight + placementRule.spacing);

                        element->SetX(static_cast<long>(x));
                        element->SetY(static_cast<long>(y));

                        col++;
                        if (col >= placementRule.gridColumns) {
                            col = 0;
                            row++;
                        }
                    }
                }
                break;
            }

            case TemplatePlacementType::Dock: {
                // Dock elements to specified sides
                Rect2Df availableRect(baseX, baseY,
                                      GetWidth() - appearance.paddingLeft - appearance.paddingRight,
                                      GetHeight() - appearance.paddingTop - appearance.paddingBottom);

                for (auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        switch (placementRule.dockSide) {
                            case LayoutDockSide::Left:
                                element->SetX(static_cast<long>(availableRect.x));
                                element->SetY(static_cast<long>(availableRect.y));
                                element->SetHeight(static_cast<long>(availableRect.height));
                                availableRect.x += element->GetWidth() + placementRule.spacing;
                                availableRect.width -= element->GetWidth() + placementRule.spacing;
                                break;

                            case LayoutDockSide::Right:
                                element->SetX(static_cast<long>(availableRect.x + availableRect.width - element->GetWidth()));
                                element->SetY(static_cast<long>(availableRect.y));
                                element->SetHeight(static_cast<long>(availableRect.height));
                                availableRect.width -= element->GetWidth() + placementRule.spacing;
                                break;

                            case LayoutDockSide::Top:
                                element->SetX(static_cast<long>(availableRect.x));
                                element->SetY(static_cast<long>(availableRect.y));
                                element->SetWidth(static_cast<long>(availableRect.width));
                                availableRect.y += element->GetHeight() + placementRule.spacing;
                                availableRect.height -= element->GetHeight() + placementRule.spacing;
                                break;

                            case LayoutDockSide::Bottom:
                                element->SetX(static_cast<long>(availableRect.x));
                                element->SetY(static_cast<long>(availableRect.y + availableRect.height - element->GetHeight()));
                                element->SetWidth(static_cast<long>(availableRect.width));
                                availableRect.height -= element->GetHeight() + placementRule.spacing;
                                break;

                            case LayoutDockSide::Fill:
                                element->SetX(static_cast<long>(availableRect.x));
                                element->SetY(static_cast<long>(availableRect.y));
                                element->SetWidth(static_cast<long>(availableRect.width));
                                element->SetHeight(static_cast<long>(availableRect.height));
                                break;

                            default:
                                break;
                        }
                    }
                }
                break;
            }

            case TemplatePlacementType::Fixed:
            case TemplatePlacementType::Absolute: {
                // Use element's constraint positions
                for (size_t i = 0; i < templateElements.size() && i < elementDescriptors.size(); ++i) {
                    auto& element = templateElements[i];
                    const auto& desc = elementDescriptors[i];

                    if (element && element->IsVisible()) {
                        // Use constraints if available
                        float x = baseX + desc.constraints.position.x + placementRule.offsetX;
                        float y = baseY + desc.constraints.position.y + placementRule.offsetY;

                        element->SetX(static_cast<long>(x));
                        element->SetY(static_cast<long>(y));

                        if (desc.constraints.size.width > 0) {
                            element->SetWidth(static_cast<long>(desc.constraints.size.width));
                        }
                        if (desc.constraints.size.height > 0) {
                            element->SetHeight(static_cast<long>(desc.constraints.size.height));
                        }
                    }
                }
                break;
            }

            case TemplatePlacementType::Relative: {
                // Position relative to previous element
                for (auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        element->SetX(static_cast<long>(currentX + placementRule.offsetX));
                        element->SetY(static_cast<long>(currentY + placementRule.offsetY));

                        // Update position for next element
                        if (placementRule.direction == LayoutDirection::Horizontal) {
                            currentX = element->GetX() + element->GetWidth() + placementRule.spacing;
                        } else {
                            currentY = element->GetY() + element->GetHeight() + placementRule.spacing;
                        }
                    }
                }
                break;
            }
        }
    }

    void UltraCanvasTemplate::DrawDragHandle() {
        auto* ctx = GetRenderContext();
        if (!ctx) return;

        float x = static_cast<float>(GetX());
        float y = static_cast<float>(GetY());
        float w = static_cast<float>(GetWidth());
        float h = static_cast<float>(GetHeight());

        Rect2Df handleRect;
        switch (dragHandle.position) {
            case LayoutDockSide::Left:
                handleRect = Rect2Df(x, y, dragHandle.width, h);
                break;
            case LayoutDockSide::Right:
                handleRect = Rect2Df(x + w - dragHandle.width, y, dragHandle.width, h);
                break;
            case LayoutDockSide::Top:
                handleRect = Rect2Df(x, y, w, dragHandle.width);
                break;
            case LayoutDockSide::Bottom:
                handleRect = Rect2Df(x, y + h - dragHandle.width, w, dragHandle.width);
                break;
            default:
                return;
        }

        // Draw handle background
        Color handleColor = isDragging ? dragHandle.dragColor : dragHandle.handleColor;
        ctx->SetFillPaint(handleColor);
        ctx->FillRectangle(handleRect.x, handleRect.y, handleRect.width, handleRect.height);

        // Draw grip pattern
        ctx->SetStrokePaint(Color(100, 100, 100));
        ctx->SetLineWidth(1.0f);

        float centerX = handleRect.x + handleRect.width / 2;
        float centerY = handleRect.y + handleRect.height / 2;

        if (dragHandle.gripPattern == "dots") {
            // Draw dot pattern
            ctx->SetFillPaint(Color(80, 80, 80));
            for (int i = -2; i <= 2; ++i) {
                for (int j = -1; j <= 1; ++j) {
                    if (dragHandle.position == LayoutDockSide::Top || dragHandle.position == LayoutDockSide::Bottom) {
                        ctx->FillCircle(centerX + i * 8, centerY + j * 4, 1.5f);
                    } else {
                        ctx->FillCircle(centerX + j * 4, centerY + i * 8, 1.5f);
                    }
                }
            }
        } else if (dragHandle.gripPattern == "lines") {
            // Draw line pattern
            for (int i = -2; i <= 2; ++i) {
                if (dragHandle.position == LayoutDockSide::Top || dragHandle.position == LayoutDockSide::Bottom) {
                    float lineX = centerX + i * 6;
                    ctx->DrawLine(lineX, handleRect.y + 2, lineX, handleRect.y + handleRect.height - 2);
                } else {
                    float lineY = centerY + i * 6;
                    ctx->DrawLine(handleRect.x + 2, lineY, handleRect.x + handleRect.width - 2, lineY);
                }
            }
        } else if (dragHandle.gripPattern == "bars") {
            // Draw bar pattern
            for (int i = -1; i <= 1; ++i) {
                if (dragHandle.position == LayoutDockSide::Top || dragHandle.position == LayoutDockSide::Bottom) {
                    float barX = centerX + i * 10 - 3;
                    ctx->FillRectangle(barX, handleRect.y + 2, 6, handleRect.height - 4);
                } else {
                    float barY = centerY + i * 10 - 3;
                    ctx->FillRectangle(handleRect.x + 2, barY, handleRect.width - 4, 6);
                }
            }
        }
    }

    void UltraCanvasTemplate::DrawTemplateBackground() {
        auto* ctx = GetRenderContext();
        if (!ctx) return;

        float x = static_cast<float>(GetX());
        float y = static_cast<float>(GetY());
        float w = static_cast<float>(GetWidth());
        float h = static_cast<float>(GetHeight());

        // Draw shadow if enabled
        if (appearance.hasShadow) {
            ctx->SetFillPaint(appearance.shadowColor);
            if (appearance.cornerRadius > 0) {
                ctx->FillRoundedRectangle(
                        x + appearance.shadowOffset.x,
                        y + appearance.shadowOffset.y,
                        w, h, appearance.cornerRadius
                );
            } else {
                ctx->FillRectangle(
                        x + appearance.shadowOffset.x,
                        y + appearance.shadowOffset.y,
                        w, h
                );
            }
        }

        // Draw background
        if (appearance.backgroundColor.a > 0) {
            ctx->SetFillPaint(appearance.backgroundColor);
            if (appearance.cornerRadius > 0) {
                ctx->FillRoundedRectangle(x, y, w, h, appearance.cornerRadius);
            } else {
                ctx->FillRectangle(x, y, w, h);
            }
        }

        // Draw border
        if (appearance.borderWidth > 0 && appearance.borderColor.a > 0) {
            ctx->SetStrokePaint(appearance.borderColor);
            ctx->SetLineWidth(appearance.borderWidth);
            if (appearance.cornerRadius > 0) {
                ctx->DrawRoundedRectangle(x, y, w, h, appearance.cornerRadius);
            } else {
                ctx->DrawRectangle(x, y, w, h);
            }
        }
    }

// ===== DEFAULT ELEMENT FACTORIES =====
    void UltraCanvasTemplate::RegisterDefaultFactories() {
        // Register built-in element type factories
        RegisterElementFactory("Button", [this](const TemplateElementDescriptor& desc) {
            return CreateButtonElement(desc);
        });

        RegisterElementFactory("Label", [this](const TemplateElementDescriptor& desc) {
            return CreateLabelElement(desc);
        });

        RegisterElementFactory("DropDown", [this](const TemplateElementDescriptor& desc) {
            return CreateDropDownElement(desc);
        });

        RegisterElementFactory("Separator", [this](const TemplateElementDescriptor& desc) {
            return CreateSeparatorElement(desc);
        });

        RegisterElementFactory("Spacer", [this](const TemplateElementDescriptor& desc) {
            return CreateSpacerElement(desc);
        });
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateButtonElement(const TemplateElementDescriptor& desc) {
        auto button = std::make_shared<UltraCanvasButton>(
                desc.identifier, 0, 0, 0,
                static_cast<long>(desc.constraints.size.width > 0 ? desc.constraints.size.width : 80),
                static_cast<long>(desc.constraints.size.height > 0 ? desc.constraints.size.height : 30)
        );

        button->SetText(desc.text);
        if (!desc.iconPath.empty()) {
            button->SetIcon(desc.iconPath);
        }
        if (!desc.tooltip.empty()) {
            button->SetTooltip(desc.tooltip);
        }
        if (desc.onClickCallback) {
            button->SetOnClick(desc.onClickCallback);
        }

        return button;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateLabelElement(const TemplateElementDescriptor& desc) {
        auto label = std::make_shared<UltraCanvasLabel>(
                desc.identifier, 0, 0, 0,
                static_cast<long>(desc.constraints.size.width > 0 ? desc.constraints.size.width : 100),
                static_cast<long>(desc.constraints.size.height > 0 ? desc.constraints.size.height : 20)
        );

        label->SetText(desc.text);

        return label;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateDropDownElement(const TemplateElementDescriptor& desc) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(
                desc.identifier, 0, 0, 0,
                static_cast<long>(desc.constraints.size.width > 0 ? desc.constraints.size.width : 120),
                static_cast<long>(desc.constraints.size.height > 0 ? desc.constraints.size.height : 30)
        );

        // Parse items from properties
        int itemCount = 0;
        auto countIt = desc.properties.find("item_count");
        if (countIt != desc.properties.end()) {
            itemCount = std::stoi(countIt->second);
        }

        for (int i = 0; i < itemCount; ++i) {
            auto itemIt = desc.properties.find("item_" + std::to_string(i));
            if (itemIt != desc.properties.end()) {
                dropdown->AddItem(itemIt->second);
            }
        }

        if (desc.onSelectionCallback) {
            dropdown->SetOnSelectionChanged([callback = desc.onSelectionCallback](int index) {
                // Convert index to string for the callback
                callback(std::to_string(index));
            });
        }

        return dropdown;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateSeparatorElement(const TemplateElementDescriptor& desc) {
        bool isVertical = false;
        auto vertIt = desc.properties.find("vertical");
        if (vertIt != desc.properties.end()) {
            isVertical = (vertIt->second == "true");
        }

        auto separator = std::make_shared<UltraCanvasSeparator>(
                desc.identifier, 0, 0, 0,
                static_cast<long>(isVertical ? 2 : desc.constraints.size.width > 0 ? desc.constraints.size.width : 100),
                static_cast<long>(isVertical ? (desc.constraints.size.height > 0 ? desc.constraints.size.height : 100) : 2)
        );

        return separator;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateSpacerElement(const TemplateElementDescriptor& desc) {
        float size = 8.0f;
        auto sizeIt = desc.properties.find("size");
        if (sizeIt != desc.properties.end()) {
            size = std::stof(sizeIt->second);
        }

        // Create an invisible element that takes up space
        auto spacer = std::make_shared<UltraCanvasUIElement>(
                desc.identifier, 0, 0, 0,
                static_cast<long>(size), static_cast<long>(size)
        );

        spacer->SetVisible(false); // Invisible but takes up space in layout

        return spacer;
    }

// ===== TEMPLATE BUILDER IMPLEMENTATION =====
    UltraCanvasTemplateBuilder::UltraCanvasTemplateBuilder(const std::string& identifier) {
        template_ = std::make_unique<UltraCanvasTemplate>(identifier);
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::SetDimensions(const TemplateDimensions& dims) {
        template_->SetDimensions(dims);
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::SetAppearance(const TemplateAppearance& app) {
        template_->SetAppearance(app);
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::SetPlacementRule(const TemplatePlacementRule& rule) {
        template_->SetPlacementRule(rule);
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::SetDragHandle(const TemplateDragHandle& handle) {
        template_->SetDragHandle(handle);
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::SetScrollSettings(const TemplateScrollSettings& settings) {
        template_->SetScrollSettings(settings);
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddButton(const std::string& id, const std::string& text,
                                                                      const std::string& icon, std::function<void()> onClick) {
        template_->AddElement(TemplateElementDescriptor::Button(id, text, icon, onClick));
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddLabel(const std::string& id, const std::string& text) {
        template_->AddElement(TemplateElementDescriptor::Label(id, text));
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddDropDown(const std::string& id,
                                                                        const std::vector<std::string>& items,
                                                                        std::function<void(const std::string&)> onSelect) {
        template_->AddElement(TemplateElementDescriptor::DropDown(id, items, onSelect));
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddSeparator(bool vertical) {
        template_->AddElement(TemplateElementDescriptor::Separator("", vertical));
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddSpacer(float size) {
        template_->AddElement(TemplateElementDescriptor::Spacer("", size));
        return *this;
    }

    UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddElement(const TemplateElementDescriptor& descriptor) {
        template_->AddElement(descriptor);
        return *this;
    }

    std::unique_ptr<UltraCanvasTemplate> UltraCanvasTemplateBuilder::Build() {
        template_->RebuildTemplate();
        return std::move(template_);
    }

// ===== TEMPLATE PRESETS IMPLEMENTATION =====
    namespace TemplatePresets {

        std::unique_ptr<UltraCanvasTemplate> CreateToolbar(const std::string& identifier) {
            UltraCanvasTemplateBuilder builder(identifier);

            TemplateAppearance appearance;
            appearance.backgroundColor = Color(240, 240, 240);
            appearance.borderColor = Color(200, 200, 200);
            appearance.borderWidth = 1.0f;
            appearance.SetPadding(4.0f);

            builder.SetAppearance(appearance)
                    .SetDimensions(TemplateDimensions::Fixed(0, 40)) // Width will be set by container
                    .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 2.0f));

            return builder.Build();
        }

        std::unique_ptr<UltraCanvasTemplate> CreateVerticalPanel(const std::string& identifier) {
            UltraCanvasTemplateBuilder builder(identifier);

            TemplateAppearance appearance;
            appearance.backgroundColor = Color(245, 245, 245);
            appearance.borderColor = Color(220, 220, 220);
            appearance.borderWidth = 1.0f;
            appearance.SetPadding(8.0f);

            builder.SetAppearance(appearance)
                    .SetDimensions(TemplateDimensions::Fixed(200, 0)) // Height will be set by container
                    .SetPlacementRule(TemplatePlacementRule::Stack(LayoutDirection::Vertical, 4.0f))
                    .SetScrollSettings(TemplateScrollSettings{TemplateScrollMode::Off, TemplateScrollMode::Auto});

            return builder.Build();
        }

        std::unique_ptr<UltraCanvasTemplate> CreateStatusBar(const std::string& identifier) {
            UltraCanvasTemplateBuilder builder(identifier);

            TemplateAppearance appearance;
            appearance.backgroundColor = Color(235, 235, 235);
            appearance.borderColor = Color(200, 200, 200);
            appearance.borderWidth = 1.0f;
            appearance.paddingLeft = 8.0f;
            appearance.paddingRight = 8.0f;
            appearance.paddingTop = 2.0f;
            appearance.paddingBottom = 2.0f;

            builder.SetAppearance(appearance)
                    .SetDimensions(TemplateDimensions::Fixed(0, 24))
                    .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 16.0f));

            return builder.Build();
        }

        std::unique_ptr<UltraCanvasTemplate> CreateRibbon(const std::string& identifier) {
            UltraCanvasTemplateBuilder builder(identifier);

            TemplateAppearance appearance;
            appearance.backgroundColor = Color(250, 250, 250);
            appearance.borderColor = Color(210, 210, 210);
            appearance.borderWidth = 1.0f;
            appearance.SetPadding(8.0f);

            builder.SetAppearance(appearance)
                    .SetDimensions(TemplateDimensions::Fixed(0, 120))
                    .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 8.0f));

            return builder.Build();
        }

        std::unique_ptr<UltraCanvasTemplate> CreateSidebar(const std::string& identifier) {
            UltraCanvasTemplateBuilder builder(identifier);

            TemplateAppearance appearance;
            appearance.backgroundColor = Color(248, 248, 248);
            appearance.borderColor = Color(220, 220, 220);
            appearance.borderWidth = 1.0f;
            appearance.SetPadding(12.0f);

            TemplateDragHandle handle = TemplateDragHandle::Right(8.0f);

            builder.SetAppearance(appearance)
                    .SetDimensions(TemplateDimensions::Fixed(250, 0))
                    .SetPlacementRule(TemplatePlacementRule::Stack(LayoutDirection::Vertical, 8.0f))
                    .SetDragHandle(handle)
                    .SetScrollSettings(TemplateScrollSettings{TemplateScrollMode::Off, TemplateScrollMode::Auto});

            return builder.Build();
        }

    } // namespace TemplatePresets

} // namespace UltraCanvas