// include/UltraCanvasTemplate.h
// Template system for creating reusable UI component layouts with placement rules
// Version: 1.0.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasLayoutEngine.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cfloat>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasButton;
    class UltraCanvasLabel;
    class UltraCanvasDropDown;
    class UltraCanvasSeparator;

// ===== TEMPLATE SIZING MODES =====
    enum class TemplateSizeMode {
        Fixed,      // Fixed size in pixels
        Auto,       // Size based on content
        Fill,       // Fill available space
        Percent     // Percentage of container
    };

// ===== TEMPLATE DIMENSIONS =====
    struct TemplateDimensions {
        TemplateSizeMode widthMode = TemplateSizeMode::Auto;
        TemplateSizeMode heightMode = TemplateSizeMode::Auto;
        float fixedWidth = 200.0f;
        float fixedHeight = 32.0f;
        float percentWidth = 100.0f;   // 0-100%
        float percentHeight = 100.0f;  // 0-100%
        float minWidth = 0.0f;
        float minHeight = 0.0f;
        float maxWidth = FLT_MAX;
        float maxHeight = FLT_MAX;

        static TemplateDimensions Fixed(float width, float height) {
            TemplateDimensions dims;
            dims.widthMode = TemplateSizeMode::Fixed;
            dims.heightMode = TemplateSizeMode::Fixed;
            dims.fixedWidth = width;
            dims.fixedHeight = height;
            return dims;
        }

        static TemplateDimensions Auto() {
            TemplateDimensions dims;
            dims.widthMode = TemplateSizeMode::Auto;
            dims.heightMode = TemplateSizeMode::Auto;
            return dims;
        }

        static TemplateDimensions Fill() {
            TemplateDimensions dims;
            dims.widthMode = TemplateSizeMode::Fill;
            dims.heightMode = TemplateSizeMode::Fill;
            return dims;
        }

        static TemplateDimensions Percent(float widthPercent, float heightPercent) {
            TemplateDimensions dims;
            dims.widthMode = TemplateSizeMode::Percent;
            dims.heightMode = TemplateSizeMode::Percent;
            dims.percentWidth = widthPercent;
            dims.percentHeight = heightPercent;
            return dims;
        }
    };

// ===== TEMPLATE SCROLLBAR SETTINGS =====
    enum class TemplateScrollMode {
        Off,    // No scrollbars
        Auto,   // Show when needed
        On      // Always show
    };

    struct TemplateScrollSettings {
        TemplateScrollMode horizontal = TemplateScrollMode::Off;
        TemplateScrollMode vertical = TemplateScrollMode::Auto;
        float scrollbarWidth = 16.0f;
        Color scrollbarTrackColor = Color(240, 240, 240);
        Color scrollbarThumbColor = Color(200, 200, 200);
        Color scrollbarThumbHoverColor = Color(160, 160, 160);
    };

// ===== TEMPLATE APPEARANCE =====
    struct TemplateAppearance {
        Color backgroundColor = Colors::Transparent;
        Color borderColor = Color(128, 128, 128);
        float borderWidth = 0.0f;
        float cornerRadius = 0.0f;

        // Padding inside template
        float paddingLeft = 4.0f;
        float paddingRight = 4.0f;
        float paddingTop = 4.0f;
        float paddingBottom = 4.0f;

        // Margin around template
        float marginLeft = 0.0f;
        float marginRight = 0.0f;
        float marginTop = 0.0f;
        float marginBottom = 0.0f;

        // Shadow
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 64);
        Point2Df shadowOffset = Point2Df(2.0f, 2.0f);
        float shadowBlur = 4.0f;

        void SetPadding(float padding) {
            paddingLeft = paddingRight = paddingTop = paddingBottom = padding;
        }

        void SetMargin(float margin) {
            marginLeft = marginRight = marginTop = marginBottom = margin;
        }
    };

// ===== TEMPLATE PLACEMENT RULES =====
    enum class TemplatePlacementType {
        Fixed,          // Fixed position
        Flow,           // Flow left to right, top to bottom
        Stack,          // Stack vertically or horizontally
        Grid,           // Grid layout
        Dock,           // Dock to edges
        Absolute,       // Absolute positioning
        Relative        // Relative to previous element
    };

    struct TemplatePlacementRule {
        TemplatePlacementType type = TemplatePlacementType::Flow;
        LayoutDirection direction = LayoutDirection::Horizontal;
        LayoutAlignment alignment = LayoutAlignment::Start;
        LayoutAlignment crossAlignment = LayoutAlignment::Center;

        // Spacing
        float spacing = 4.0f;
        float itemSpacing = 2.0f;

        // Grid specific
        int gridColumns = 1;
        int gridRows = 1;

        // Dock specific
        LayoutDockSide dockSide = LayoutDockSide::Top;

        // Position offsets
        float offsetX = 0.0f;
        float offsetY = 0.0f;

        // Wrapping
        bool allowWrap = false;

        static TemplatePlacementRule Flow(LayoutDirection dir = LayoutDirection::Horizontal, float spacing = 4.0f) {
            TemplatePlacementRule rule;
            rule.type = TemplatePlacementType::Flow;
            rule.direction = dir;
            rule.spacing = spacing;
            return rule;
        }

        static TemplatePlacementRule Stack(LayoutDirection dir = LayoutDirection::Vertical, float spacing = 4.0f) {
            TemplatePlacementRule rule;
            rule.type = TemplatePlacementType::Stack;
            rule.direction = dir;
            rule.spacing = spacing;
            return rule;
        }

        static TemplatePlacementRule Grid(int columns, int rows = 0, float spacing = 4.0f) {
            TemplatePlacementRule rule;
            rule.type = TemplatePlacementType::Grid;
            rule.gridColumns = columns;
            rule.gridRows = rows;
            rule.spacing = spacing;
            return rule;
        }

        static TemplatePlacementRule Dock(LayoutDockSide side) {
            TemplatePlacementRule rule;
            rule.type = TemplatePlacementType::Dock;
            rule.dockSide = side;
            return rule;
        }
    };

// ===== TEMPLATE ELEMENT DESCRIPTOR =====
    struct TemplateElementDescriptor {
        std::string elementType;        // "Button", "Label", "DropDown", "Separator", etc.
        std::string identifier;         // Unique identifier for element
        std::string text;              // Display text
        std::string iconPath;          // Icon file path
        std::string tooltip;           // Tooltip text

        // Element-specific properties
        std::unordered_map<std::string, std::string> properties;

        // Layout constraints for this element
        LayoutConstraints constraints;

        // Event callbacks
        std::function<void()> onClickCallback;
        std::function<void(const std::string&)> onSelectionCallback;

        // Factory methods for common elements
        static TemplateElementDescriptor Button(const std::string& id, const std::string& text,
                                                const std::string& icon = "", std::function<void()> onClick = nullptr) {
            TemplateElementDescriptor desc;
            desc.elementType = "Button";
            desc.identifier = id;
            desc.text = text;
            desc.iconPath = icon;
            desc.onClickCallback = onClick;
            return desc;
        }

        static TemplateElementDescriptor Label(const std::string& id, const std::string& text) {
            TemplateElementDescriptor desc;
            desc.elementType = "Label";
            desc.identifier = id;
            desc.text = text;
            return desc;
        }

        static TemplateElementDescriptor DropDown(const std::string& id, const std::vector<std::string>& items,
                                                  std::function<void(const std::string&)> onSelect = nullptr) {
            TemplateElementDescriptor desc;
            desc.elementType = "DropDown";
            desc.identifier = id;
            desc.onSelectionCallback = onSelect;

            // Store items as properties
            for (size_t i = 0; i < items.size(); ++i) {
                desc.properties["item_" + std::to_string(i)] = items[i];
            }
            desc.properties["item_count"] = std::to_string(items.size());

            return desc;
        }

        static TemplateElementDescriptor Separator(const std::string& id = "", bool vertical = false) {
            TemplateElementDescriptor desc;
            desc.elementType = "Separator";
            desc.identifier = id.empty() ? "sep_" + std::to_string(rand()) : id;
            desc.properties["vertical"] = vertical ? "true" : "false";
            return desc;
        }

        static TemplateElementDescriptor Spacer(const std::string& id = "", float size = 8.0f) {
            TemplateElementDescriptor desc;
            desc.elementType = "Spacer";
            desc.identifier = id.empty() ? "spacer_" + std::to_string(rand()) : id;
            desc.properties["size"] = std::to_string(size);
            return desc;
        }
    };

// ===== TEMPLATE DRAG HANDLE =====
    struct TemplateDragHandle {
        bool enabled = false;
        LayoutDockSide position = LayoutDockSide::Left;
        float width = 8.0f;
        Color handleColor = Color(180, 180, 180);
        Color hoverColor = Color(160, 160, 160);
        Color dragColor = Color(140, 140, 140);
        std::string gripPattern = "dots";  // "dots", "lines", "bars"

        static TemplateDragHandle Left(float width = 8.0f) {
            TemplateDragHandle handle;
            handle.enabled = true;
            handle.position = LayoutDockSide::Left;
            handle.width = width;
            return handle;
        }

        static TemplateDragHandle Top(float height = 8.0f) {
            TemplateDragHandle handle;
            handle.enabled = true;
            handle.position = LayoutDockSide::Top;
            handle.width = height;
            return handle;
        }
    };

// ===== BASE TEMPLATE CLASS =====
    class UltraCanvasTemplate : public UltraCanvasContainer {
    private:
        // Template configuration
        TemplateDimensions dimensions;
        TemplateScrollSettings scrollSettings;
        TemplateAppearance appearance;
        TemplatePlacementRule placementRule;
        TemplateDragHandle dragHandle;

        // Template elements
        std::vector<TemplateElementDescriptor> elementDescriptors;
        std::vector<std::shared_ptr<UltraCanvasUIElement>> templateElements;

        // State
        bool isDirty = true;
        bool isDragging = false;
        Point2Df dragStartPosition;
        Point2Df dragOffset;

        // Element factory
        std::unordered_map<std::string, std::function<std::shared_ptr<UltraCanvasUIElement>(const TemplateElementDescriptor&)>> elementFactories;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasTemplate(const std::string& identifier = "Template",
                            long id = 0, long x = 0, long y = 0, long w = 200, long h = 32);

        virtual ~UltraCanvasTemplate() = default;

        // ===== TEMPLATE CONFIGURATION =====
        void SetDimensions(const TemplateDimensions& dims);
        const TemplateDimensions& GetDimensions() const { return dimensions; }

        void SetScrollSettings(const TemplateScrollSettings& settings);
        const TemplateScrollSettings& GetScrollSettings() const { return scrollSettings; }

        void SetAppearance(const TemplateAppearance& app);
        const TemplateAppearance& GetAppearance() const { return appearance; }

        void SetPlacementRule(const TemplatePlacementRule& rule);
        const TemplatePlacementRule& GetPlacementRule() const { return placementRule; }

        void SetDragHandle(const TemplateDragHandle& handle);
        const TemplateDragHandle& GetDragHandle() const { return dragHandle; }

        // ===== ELEMENT MANAGEMENT =====
        void AddElement(const TemplateElementDescriptor& descriptor);
        void InsertElement(size_t index, const TemplateElementDescriptor& descriptor);
        void RemoveElement(const std::string& identifier);
        void RemoveElementAt(size_t index);
        void ClearElements();

        // ===== ELEMENT ACCESS =====
        std::shared_ptr<UltraCanvasUIElement> GetElement(const std::string& identifier);
        template<typename T>
        std::shared_ptr<T> GetElementAs(const std::string& identifier) {
            return std::dynamic_pointer_cast<T>(GetElement(identifier));
        }

        size_t GetElementCount() const { return elementDescriptors.size(); }
        const std::vector<TemplateElementDescriptor>& GetElementDescriptors() const { return elementDescriptors; }

        // ===== TEMPLATE OPERATIONS =====
        void RebuildTemplate();
        void RefreshLayout();
        void UpdateElementProperties();

        // ===== SIZE CALCULATION =====
        Point2Df CalculateRequiredSize() const;
        void FitToContent();
        void ApplyToContainer(const Rect2Df& containerRect);

        // ===== RENDERING =====
        void Render(IRenderContext* ctx) override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override;

        // ===== DRAG FUNCTIONALITY =====
        bool IsDraggable() const { return dragHandle.enabled; }
        bool IsDragging() const { return isDragging; }
        void StartDrag(const Point2Df& startPosition);
        void UpdateDrag(const Point2Df& currentPosition);
        void EndDrag();

        // ===== ELEMENT FACTORY REGISTRATION =====
        void RegisterElementFactory(const std::string& elementType,
                                    std::function<std::shared_ptr<UltraCanvasUIElement>(const TemplateElementDescriptor&)> factory);

    protected:
        // ===== INTERNAL METHODS =====
        virtual void BuildElements();
        virtual void ApplyLayout();
        virtual void DrawDragHandle();
        virtual void DrawTemplateBackground();

        // Default element factories
        void RegisterDefaultFactories();
        std::shared_ptr<UltraCanvasUIElement> CreateButtonElement(const TemplateElementDescriptor& desc);
        std::shared_ptr<UltraCanvasUIElement> CreateLabelElement(const TemplateElementDescriptor& desc);
        std::shared_ptr<UltraCanvasUIElement> CreateDropDownElement(const TemplateElementDescriptor& desc);
        std::shared_ptr<UltraCanvasUIElement> CreateSeparatorElement(const TemplateElementDescriptor& desc);
        std::shared_ptr<UltraCanvasUIElement> CreateSpacerElement(const TemplateElementDescriptor& desc);
    };

// ===== TEMPLATE BUILDER =====
    class UltraCanvasTemplateBuilder {
    private:
        std::unique_ptr<UltraCanvasTemplate> template_;

    public:
        UltraCanvasTemplateBuilder(const std::string& identifier = "Template");

        // Configuration methods
        UltraCanvasTemplateBuilder& SetDimensions(const TemplateDimensions& dims);
        UltraCanvasTemplateBuilder& SetAppearance(const TemplateAppearance& app);
        UltraCanvasTemplateBuilder& SetPlacementRule(const TemplatePlacementRule& rule);
        UltraCanvasTemplateBuilder& SetDragHandle(const TemplateDragHandle& handle);
        UltraCanvasTemplateBuilder& SetScrollSettings(const TemplateScrollSettings& settings);

        // Element addition methods
        UltraCanvasTemplateBuilder& AddButton(const std::string& id, const std::string& text,
                                              const std::string& icon = "", std::function<void()> onClick = nullptr);
        UltraCanvasTemplateBuilder& AddLabel(const std::string& id, const std::string& text);
        UltraCanvasTemplateBuilder& AddDropDown(const std::string& id, const std::vector<std::string>& items,
                                                std::function<void(const std::string&)> onSelect = nullptr);
        UltraCanvasTemplateBuilder& AddSeparator(bool vertical = false);
        UltraCanvasTemplateBuilder& AddSpacer(float size = 8.0f);
        UltraCanvasTemplateBuilder& AddElement(const TemplateElementDescriptor& descriptor);

        // Build method
        std::unique_ptr<UltraCanvasTemplate> Build();
    };

// ===== PREDEFINED TEMPLATES =====
    namespace TemplatePresets {
        // Create a horizontal toolbar template
        std::unique_ptr<UltraCanvasTemplate> CreateToolbar(const std::string& identifier = "Toolbar");

        // Create a vertical panel template
        std::unique_ptr<UltraCanvasTemplate> CreateVerticalPanel(const std::string& identifier = "VerticalPanel");

        // Create a status bar template
        std::unique_ptr<UltraCanvasTemplate> CreateStatusBar(const std::string& identifier = "StatusBar");

        // Create a ribbon-style toolbar
        std::unique_ptr<UltraCanvasTemplate> CreateRibbon(const std::string& identifier = "Ribbon");

        // Create a sidebar template
        std::unique_ptr<UltraCanvasTemplate> CreateSidebar(const std::string& identifier = "Sidebar");
    }

} // namespace UltraCanvas