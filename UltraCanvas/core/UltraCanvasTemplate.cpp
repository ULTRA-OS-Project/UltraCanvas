// UltraCanvasTemplate.cpp
// Template system implementation for reusable UI component layouts
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasTemplate.h"
#include "../include/UltraCanvasButton.h"
#include "../include/UltraCanvasLabel.h"
#include "../include/UltraCanvasDropDown.h"
#include "../include/UltraCanvasSeparator.h"
#include "../include/UltraCanvasDrawingSurface.h"

namespace UltraCanvas {

// ===== TEMPLATE CONSTRUCTOR =====
UltraCanvasTemplate::UltraCanvasTemplate(const std::string& identifier, 
                                         long id, long x, long y, long w, long h)
    : UltraCanvasContainer(identifier, id, x, y, w, h) {
    
    // Initialize default settings
    dimensions = TemplateDimensions::Auto();
    
    // Default appearance
    appearance.backgroundColor = Color(245, 245, 245);
    appearance.borderColor = Color(200, 200, 200);
    appearance.borderWidth = 1.0f;
    appearance.SetPadding(4.0f);
    
    // Default placement rule (horizontal flow)
    placementRule = TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 4.0f);
    
    // Default scroll settings
    scrollSettings.horizontal = TemplateScrollMode::Auto;
    scrollSettings.vertical = TemplateScrollMode::Off;
    
    // Register default element factories
    RegisterDefaultFactories();
}

// ===== CONFIGURATION METHODS =====
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
    auto it = std::find_if(elementDescriptors.begin(), elementDescriptors.end(),
        [&identifier](const TemplateElementDescriptor& desc) {
            return desc.identifier == identifier;
        });
    
    if (it != elementDescriptors.end()) {
        elementDescriptors.erase(it);
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
    ClearChildren();
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
    ClearChildren();
    
    // Build new elements
    BuildElements();
    
    // Apply layout
    ApplyLayout();
    
    isDirty = false;
}

void UltraCanvasTemplate::RefreshLayout() {
    if (!templateElements.empty()) {
        ApplyLayout();
    }
}

void UltraCanvasTemplate::UpdateElementProperties() {
    // Update properties of existing elements based on descriptors
    for (size_t i = 0; i < elementDescriptors.size() && i < templateElements.size(); ++i) {
        auto& desc = elementDescriptors[i];
        auto& element = templateElements[i];
        
        if (element) {
            // Update common properties
            element->SetVisible(true);
            element->SetActive(true);
            
            // Update element-specific properties based on type
            if (desc.elementType == "Button") {
                auto button = std::dynamic_pointer_cast<UltraCanvasButton>(element);
                if (button) {
                    button->SetText(desc.text);
                    if (!desc.iconPath.empty()) {
                        button->SetIcon(desc.iconPath);
                    }
                }
            }
            else if (desc.elementType == "Label") {
                auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(element);
                if (label) {
                    label->SetText(desc.text);
                }
            }
            // Add more element types as needed
        }
    }
}

// ===== SIZE CALCULATION =====
Point2D UltraCanvasTemplate::CalculateRequiredSize() const {
    if (templateElements.empty()) {
        return Point2D(dimensions.fixedWidth, dimensions.fixedHeight);
    }
    
    // Calculate based on placement rule
    float totalWidth = appearance.paddingLeft + appearance.paddingRight;
    float totalHeight = appearance.paddingTop + appearance.paddingBottom;
    
    switch (placementRule.type) {
        case TemplatePlacementType::Flow:
        case TemplatePlacementType::Stack: {
            if (placementRule.direction == LayoutDirection::Horizontal) {
                float maxHeight = 0;
                for (const auto& element : templateElements) {
                    if (element && element->IsVisible()) {
                        totalWidth += element->GetWidth() + placementRule.spacing;
                        maxHeight = std::max(maxHeight, static_cast<float>(element->GetHeight()));
                    }
                }
                totalWidth -= placementRule.spacing; // Remove last spacing
                totalHeight += maxHeight;
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
            float maxElementWidth = 0, maxElementHeight = 0;
            
            for (const auto& element : templateElements) {
                if (element && element->IsVisible()) {
                    visibleCount++;
                    maxElementWidth = std::max(maxElementWidth, static_cast<float>(element->GetWidth()));
                    maxElementHeight = std::max(maxElementHeight, static_cast<float>(element->GetHeight()));
                }
            }
            
            int columns = placementRule.gridColumns;
            int rows = (visibleCount + columns - 1) / columns;
            
            totalWidth += columns * maxElementWidth + (columns - 1) * placementRule.spacing;
            totalHeight += rows * maxElementHeight + (rows - 1) * placementRule.spacing;
            break;
        }
        
        default:
            // For other placement types, use bounding box
            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
            for (const auto& element : templateElements) {
                if (element && element->IsVisible()) {
                    minX = std::min(minX, static_cast<float>(element->GetX()));
                    minY = std::min(minY, static_cast<float>(element->GetY()));
                    maxX = std::max(maxX, static_cast<float>(element->GetX() + element->GetWidth()));
                    maxY = std::max(maxY, static_cast<float>(element->GetY() + element->GetHeight()));
                }
            }
            
            if (minX != FLT_MAX) {
                totalWidth = maxX - minX + appearance.paddingLeft + appearance.paddingRight;
                totalHeight = maxY - minY + appearance.paddingTop + appearance.paddingBottom;
            }
            break;
    }
    
    // Add drag handle size
    if (dragHandle.enabled) {
        if (dragHandle.position == LayoutDockSide::Left || dragHandle.position == LayoutDockSide::Right) {
            totalWidth += dragHandle.width;
        } else {
            totalHeight += dragHandle.width;
        }
    }
    
    return Point2D(totalWidth, totalHeight);
}

void UltraCanvasTemplate::FitToContent() {
    Point2D requiredSize = CalculateRequiredSize();
    SetWidth(static_cast<long>(requiredSize.x));
    SetHeight(static_cast<long>(requiredSize.y));
}

void UltraCanvasTemplate::ApplyToContainer(const Rect2D& containerRect) {
    float finalWidth = GetWidth();
    float finalHeight = GetHeight();
    
    // Apply dimension rules
    switch (dimensions.widthMode) {
        case TemplateSizeMode::Fixed:
            finalWidth = dimensions.fixedWidth;
            break;
        case TemplateSizeMode::Fill:
            finalWidth = containerRect.width;
            break;
        case TemplateSizeMode::Percent:
            finalWidth = containerRect.width * (dimensions.percentWidth / 100.0f);
            break;
        case TemplateSizeMode::Auto:
            finalWidth = CalculateRequiredSize().x;
            break;
    }
    
    switch (dimensions.heightMode) {
        case TemplateSizeMode::Fixed:
            finalHeight = dimensions.fixedHeight;
            break;
        case TemplateSizeMode::Fill:
            finalHeight = containerRect.height;
            break;
        case TemplateSizeMode::Percent:
            finalHeight = containerRect.height * (dimensions.percentHeight / 100.0f);
            break;
        case TemplateSizeMode::Auto:
            finalHeight = CalculateRequiredSize().y;
            break;
    }
    
    // Apply constraints
    finalWidth = std::max(dimensions.minWidth, std::min(dimensions.maxWidth, finalWidth));
    finalHeight = std::max(dimensions.minHeight, std::min(dimensions.maxHeight, finalHeight));
    
    SetWidth(static_cast<long>(finalWidth));
    SetHeight(static_cast<long>(finalHeight));
    
    RefreshLayout();
}

// ===== RENDERING =====
void UltraCanvasTemplate::Render() {
        IRenderContext *ctx = GetRenderContext();
    if (!IsVisible()) return;
    
    ctx->PushState();
    
    // Rebuild template if dirty
    if (isDirty) {
        RebuildTemplate();
    }
    
    // Draw template background
    DrawTemplateBackground();
    
    // Draw drag handle
    if (dragHandle.enabled) {
        DrawDragHandle();
    }
    
    // Render child elements (handled by UltraCanvasContainer)
    UltraCanvasContainer::Render();
}

// ===== EVENT HANDLING =====
bool UltraCanvasTemplate::OnEvent(const UCEvent& event) {
    if (!IsActive() || !IsVisible()) return false;
    
    // Handle drag functionality first
    if (dragHandle.enabled) {
        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.mouse.button == UCMouseButton::Left) {
                    // Check if click is on drag handle
                    Rect2D handleRect = GetDragHandleRect();
                    if (handleRect.Contains(Point2D(event.mouse.x, event.mouse.y))) {
                        StartDrag(Point2D(event.mouse.x, event.mouse.y));
                        return; // Don't pass to children
                    }
                }
                break;
                
            case UCEventType::MouseMove:
                if (isDragging) {
                    UpdateDrag(Point2D(event.mouse.x, event.mouse.y));
                    return; // Don't pass to children
                }
                break;
                
            case UCEventType::MouseUp:
                if (isDragging && event.mouse.button == UCMouseButton::Left) {
                    EndDrag();
                    return; // Don't pass to children
                }
                break;
                
            default:
                break;
        }
        return false;
    }
    
    // Pass event to container for child handling
    UltraCanvasContainer::OnEvent(event);
}

// ===== DRAG FUNCTIONALITY =====
void UltraCanvasTemplate::StartDrag(const Point2D& startPosition) {
    isDragging = true;
    dragStartPosition = startPosition;
    dragOffset = Point2D(startPosition.x - GetX(), startPosition.y - GetY());
}

void UltraCanvasTemplate::UpdateDrag(const Point2D& currentPosition) {
    if (isDragging) {
        SetX(static_cast<long>(currentPosition.x - dragOffset.x));
        SetY(static_cast<long>(currentPosition.y - dragOffset.y));
    }
}

void UltraCanvasTemplate::EndDrag() {
    isDragging = false;
}

Rect2D UltraCanvasTemplate::GetDragHandleRect() const {
    Rect2D bounds = GetBounds();
    
    switch (dragHandle.position) {
        case LayoutDockSide::Left:
            return Rect2D(bounds.x, bounds.y, dragHandle.width, bounds.height);
        case LayoutDockSide::Right:
            return Rect2D(bounds.x + bounds.width - dragHandle.width, bounds.y, dragHandle.width, bounds.height);
        case LayoutDockSide::Top:
            return Rect2D(bounds.x, bounds.y, bounds.width, dragHandle.width);
        case LayoutDockSide::Bottom:
            return Rect2D(bounds.x, bounds.y + bounds.height - dragHandle.width, bounds.width, dragHandle.width);
        default:
            return Rect2D();
    }
}

// ===== ELEMENT FACTORY REGISTRATION =====
void UltraCanvasTemplate::RegisterElementFactory(const std::string& elementType,
                                                 std::function<std::shared_ptr<UltraCanvasUIElement>(const TemplateElementDescriptor&)> factory) {
    elementFactories[elementType] = factory;
}

// ===== INTERNAL METHODS =====
void UltraCanvasTemplate::BuildElements() {
    for (const auto& descriptor : elementDescriptors) {
        auto factory = elementFactories.find(descriptor.elementType);
        if (factory != elementFactories.end()) {
            auto element = factory->second(descriptor);
            if (element) {
                templateElements.push_back(element);
                AddChild(element);
            }
        }
    }
}

void UltraCanvasTemplate::ApplyLayout() {
    if (templateElements.empty()) return;
    
    // Calculate content area (excluding padding and drag handle)
    Rect2D contentArea = GetContentArea();
    if (dragHandle.enabled) {
        switch (dragHandle.position) {
            case LayoutDockSide::Left:
                contentArea.x += dragHandle.width;
                contentArea.width -= dragHandle.width;
                break;
            case LayoutDockSide::Right:
                contentArea.width -= dragHandle.width;
                break;
            case LayoutDockSide::Top:
                contentArea.y += dragHandle.width;
                contentArea.height -= dragHandle.width;
                break;
            case LayoutDockSide::Bottom:
                contentArea.height -= dragHandle.width;
                break;
        }
    }
    
    // Convert template elements to layout items
    std::vector<UltraCanvasUIElement*> elements;
    for (auto& element : templateElements) {
        if (element && element->IsVisible()) {
            elements.push_back(element.get());
        }
    }
    
    // Create layout parameters based on placement rule
    LayoutParams layoutParams;
    
    switch (placementRule.type) {
        case TemplatePlacementType::Flow:
            layoutParams.direction = placementRule.direction;
            layoutParams.wrap = placementRule.allowWrap ? LayoutWrap::Wrap : LayoutWrap::NoWrap;
            break;
            
        case TemplatePlacementType::Stack:
            layoutParams.direction = placementRule.direction;
            break;
            
        case TemplatePlacementType::Grid:
            layoutParams.direction = LayoutDirection::Grid;
            layoutParams.gridColumns = placementRule.gridColumns;
            break;
            
        case TemplatePlacementType::Dock:
            layoutParams.direction = LayoutDirection::Dock;
            break;
            
        default:
            layoutParams.direction = LayoutDirection::Absolute;
            break;
    }
    
    layoutParams.mainAlignment = placementRule.alignment;
    layoutParams.crossAlignment = placementRule.crossAlignment;
    layoutParams.spacing = static_cast<int>(placementRule.spacing);
    layoutParams.paddingLeft = static_cast<int>(appearance.paddingLeft);
    layoutParams.paddingRight = static_cast<int>(appearance.paddingRight);
    layoutParams.paddingTop = static_cast<int>(appearance.paddingTop);
    layoutParams.paddingBottom = static_cast<int>(appearance.paddingBottom);
    
    // Apply layout using UltraCanvas layout engine
    UltraCanvasLayoutEngine::PerformLayout(contentArea.width, contentArea.height, layoutParams, elements);
    
    // Offset elements to content area position
    for (auto& element : elements) {
        element->SetX(element->GetX() + static_cast<long>(contentArea.x));
        element->SetY(element->GetY() + static_cast<long>(contentArea.y));
    }
}

void UltraCanvasTemplate::DrawDragHandle() {
    if (!dragHandle.enabled) return;
    
    Rect2D handleRect = GetDragHandleRect();
    
    // Determine handle color based on state
    Color handleColor = dragHandle.handleColor;
    if (isDragging) {
        handleColor = dragHandle.dragColor;
    }
    // TODO: Add hover detection for hover color
    
    // Draw handle background
    ctx->FillRectangle(handleRect, handleColor);
    
    // Draw grip pattern
    if (dragHandle.gripPattern == "dots") {
        DrawDotGrip(handleRect);
    } else if (dragHandle.gripPattern == "lines") {
        DrawLineGrip(handleRect);
    } else if (dragHandle.gripPattern == "bars") {
        DrawBarGrip(handleRect);
    }
}

void UltraCanvasTemplate::DrawDotGrip(const Rect2D& rect) {
    Color dotColor = Color(100, 100, 100);
    float dotSize = 2.0f;
    float spacing = 4.0f;
    
    Point2D center = Point2D(rect.x + rect.width / 2, rect.y + rect.height / 2);
    
    // Draw a 3x3 grid of dots
    for (int row = -1; row <= 1; ++row) {
        for (int col = -1; col <= 1; ++col) {
            Point2D dotPos = Point2D(
                center.x + col * spacing,
                center.y + row * spacing
            );
            FillCircle(dotPos, dotSize, dotColor);
        }
    }
}

void UltraCanvasTemplate::DrawLineGrip(const Rect2D& rect) {
    Color lineColor = Color(100, 100, 100);
    float lineWidth = 1.0f;
    float spacing = 3.0f;
    
    if (dragHandle.position == LayoutDockSide::Left || dragHandle.position == LayoutDockSide::Right) {
        // Horizontal lines for vertical handles
        float startY = rect.y + rect.height * 0.3f;
        float endY = rect.y + rect.height * 0.7f;
        float x = rect.x + rect.width / 2;
        
        for (float y = startY; y <= endY; y += spacing) {
             ctx->DrawLine(Point2D(rect.x + 2, y), Point2D(rect.x + rect.width - 2, y), lineColor, lineWidth);
        }
    } else {
        // Vertical lines for horizontal handles
        float startX = rect.x + rect.width * 0.3f;
        float endX = rect.x + rect.width * 0.7f;
        float y = rect.y + rect.height / 2;
        
        for (float x = startX; x <= endX; x += spacing) {
             ctx->DrawLine(Point2D(x, rect.y + 2), Point2D(x, rect.y + rect.height - 2), lineColor, lineWidth);
        }
    }
}

void UltraCanvasTemplate::DrawBarGrip(const Rect2D& rect) {
    Color barColor = Color(120, 120, 120);
    
    if (dragHandle.position == LayoutDockSide::Left || dragHandle.position == LayoutDockSide::Right) {
        // Horizontal bars for vertical handles
        float barHeight = 2.0f;
        float spacing = 4.0f;
        float barWidth = rect.width - 4.0f;
        
        Point2D center = Point2D(rect.x + rect.width / 2, rect.y + rect.height / 2);
        
        for (int i = -1; i <= 1; ++i) {
            Rect2D barRect = Rect2D(
                rect.x + 2,
                center.y + i * spacing - barHeight / 2,
                barWidth,
                barHeight
            );
            ctx->FillRectangle(barRect, barColor);
        }
    } else {
        // Vertical bars for horizontal handles
        float barWidth = 2.0f;
        float spacing = 4.0f;
        float barHeight = rect.height - 4.0f;
        
        Point2D center = Point2D(rect.x + rect.width / 2, rect.y + rect.height / 2);
        
        for (int i = -1; i <= 1; ++i) {
            Rect2D barRect = Rect2D(
                center.x + i * spacing - barWidth / 2,
                rect.y + 2,
                barWidth,
                barHeight
            );
            ctx->FillRectangle(barRect, barColor);
        }
    }
}

void UltraCanvasTemplate::DrawTemplateBackground() {
    Rect2D bounds = GetBounds();
    
    // Draw background
    if (appearance.backgroundColor.a > 0) {
        ctx->FillRectangle(bounds, appearance.backgroundColor);
    }
    
    // Draw border
    if (appearance.borderWidth > 0 && appearance.borderColor.a > 0) {
        ctx->DrawRectangle(bounds, appearance.borderColor, appearance.borderWidth);
    }
    
    // Draw shadow
    if (appearance.hasShadow) {
        Rect2D shadowRect = Rect2D(
            bounds.x + appearance.shadowOffset.x,
            bounds.y + appearance.shadowOffset.y,
            bounds.width,
            bounds.height
        );
        ctx->FillRectangle(shadowRect, appearance.shadowColor);
    }
}

// ===== DEFAULT ELEMENT FACTORIES =====
void UltraCanvasTemplate::RegisterDefaultFactories() {
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
    auto button = std::make_shared<UltraCanvasButton>(desc.identifier, 0, 0, 0, 80, 24);
    
    button->SetText(desc.text);
    if (!desc.iconPath.empty()) {
        button->SetIcon(desc.iconPath);
    }
    if (!desc.tooltip.empty()) {
        button->SetTooltip(desc.tooltip);
    }
    
    // Set click callback
    if (desc.onClickCallback) {
        button->SetClickCallback(desc.onClickCallback);
    }
    
    return button;
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateLabelElement(const TemplateElementDescriptor& desc) {
    auto label = std::make_shared<UltraCanvasLabel>(desc.identifier, 0, 0, 0, 60, 20);
    
    label->SetText(desc.text);
    if (!desc.tooltip.empty()) {
        label->SetTooltip(desc.tooltip);
    }
    
    return label;
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateDropDownElement(const TemplateElementDescriptor& desc) {
    auto dropdown = std::make_shared<UltraCanvasDropDown>(desc.identifier, 0, 0, 0, 100, 24);
    
    // Add items from properties
    auto itemCountIt = desc.properties.find("item_count");
    if (itemCountIt != desc.properties.end()) {
        int itemCount = std::stoi(itemCountIt->second);
        for (int i = 0; i < itemCount; ++i) {
            auto itemIt = desc.properties.find("item_" + std::to_string(i));
            if (itemIt != desc.properties.end()) {
                dropdown->AddItem(itemIt->second);
            }
        }
    }
    
    // Set selection callback
    if (desc.onSelectionCallback) {
        dropdown->SetSelectionCallback(desc.onSelectionCallback);
    }
    
    if (!desc.tooltip.empty()) {
        dropdown->SetTooltip(desc.tooltip);
    }
    
    return dropdown;
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateSeparatorElement(const TemplateElementDescriptor& desc) {
    bool vertical = false;
    auto verticalIt = desc.properties.find("vertical");
    if (verticalIt != desc.properties.end()) {
        vertical = (verticalIt->second == "true");
    }
    
    auto separator = std::make_shared<UltraCanvasSeparator>(desc.identifier, 0, 0, 0, 
                                                           vertical ? 2 : 20, vertical ? 20 : 2);
    separator->SetVertical(vertical);
    
    return separator;
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasTemplate::CreateSpacerElement(const TemplateElementDescriptor& desc) {
    float size = 8.0f;
    auto sizeIt = desc.properties.find("size");
    if (sizeIt != desc.properties.end()) {
        size = std::stof(sizeIt->second);
    }
    
    // Create an invisible element as spacer
    auto spacer = std::make_shared<UltraCanvasUIElement>(desc.identifier, 0, 0, 0,
                                                      static_cast<long>(size), static_cast<long>(size));
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

UltraCanvasTemplateBuilder& UltraCanvasTemplateBuilder::AddDropDown(const std::string& id, const std::vector<std::string>& items,
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
    return std::move(template_);
}

// ===== PREDEFINED TEMPLATES =====
namespace TemplatePresets {

std::unique_ptr<UltraCanvasTemplate> CreateToolbar(const std::string& identifier) {
    return UltraCanvasTemplateBuilder(identifier)
        .SetDimensions(TemplateDimensions::Fixed(400, 32))
        .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 4.0f))
        .SetDragHandle(TemplateDragHandle::Left(8.0f))
        .Build();
}

std::unique_ptr<UltraCanvasTemplate> CreateVerticalPanel(const std::string& identifier) {
    return UltraCanvasTemplateBuilder(identifier)
        .SetDimensions(TemplateDimensions::Fixed(200, 400))
        .SetPlacementRule(TemplatePlacementRule::Stack(LayoutDirection::Vertical, 8.0f))
        .SetDragHandle(TemplateDragHandle::Top(8.0f))
        .Build();
}

std::unique_ptr<UltraCanvasTemplate> CreateStatusBar(const std::string& identifier) {
    auto appearance = TemplateAppearance();
    appearance.backgroundColor = Color(240, 240, 240);
    appearance.borderColor = Color(180, 180, 180);
    appearance.borderWidth = 1.0f;
    appearance.SetPadding(4.0f);
    
    return UltraCanvasTemplateBuilder(identifier)
        .SetDimensions(TemplateDimensions::Percent(100.0f, 24.0f))
        .SetAppearance(appearance)
        .SetPlacementRule(TemplatePlacementRule::Flow(LayoutDirection::Horizontal, 8.0f))
        .Build();
}

std::unique_ptr<UltraCanvasTemplate> CreateRibbon(const std::string& identifier) {
    auto placement = TemplatePlacementRule::Grid(6, 2, 4.0f);
    placement.alignment = LayoutAlignment::Start;
    placement.crossAlignment = LayoutAlignment::Center;
    
    return UltraCanvasTemplateBuilder(identifier)
        .SetDimensions(TemplateDimensions::Percent(100.0f, 64.0f))
        .SetPlacementRule(placement)
        .SetDragHandle(TemplateDragHandle::Left(8.0f))
        .Build();
}

std::unique_ptr<UltraCanvasTemplate> CreateSidebar(const std::string& identifier) {
    auto scrollSettings = TemplateScrollSettings();
    scrollSettings.vertical = TemplateScrollMode::Auto;
    scrollSettings.horizontal = TemplateScrollMode::Off;
    
    return UltraCanvasTemplateBuilder(identifier)
        .SetDimensions(TemplateDimensions::Fixed(250, 600))
        .SetPlacementRule(TemplatePlacementRule::Stack(LayoutDirection::Vertical, 4.0f))
        .SetScrollSettings(scrollSettings)
        .SetDragHandle(TemplateDragHandle::Top(8.0f))
        .Build();
}

} // namespace TemplatePresets

} // namespace UltraCanvas