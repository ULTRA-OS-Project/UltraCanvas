// include/UltraCanvasSegmentedControl.h
// Segmented control component for mutually exclusive selection between options
// Version: 1.0.0
// Last Modified: 2025-10-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>
#include <set>

namespace UltraCanvas {

// ===== SEGMENTED CONTROL ENUMS =====

    enum class SegmentWidthMode {
        Equal,        // All segments have equal width
        FitContent,   // Segments sized to fit their content
        Custom        // Custom width per segment
    };

    enum class SegmentSelectionMode {
        Single,       // Only one segment can be selected at a time (default)
        Multiple,     // Multiple segments can be selected simultaneously
        Toggle        // Each segment acts as an independent toggle
    };
// ===== SEGMENT DATA STRUCTURE =====

    struct SegmentData {
        std::string text;
        std::string iconPath;
        bool enabled = true;
        TextAlignment alignment;
        float customWidth = 0.0f;  // Used when SegmentWidthMode::Custom

        SegmentData() = default;
        SegmentData(const std::string& txt, TextAlignment al) : text(txt), alignment(al) {}
        SegmentData(const std::string& txt, const std::string& icon, TextAlignment al)
                : text(txt), iconPath(icon), alignment(al) {}

        bool HasIcon() const { return !iconPath.empty(); }
        bool HasText() const { return !text.empty(); }
    };

// ===== SEGMENTED CONTROL APPEARANCE =====

    struct SegmentedControlStyle {
        // Colors for different states
        Color normalColor = Colors::ButtonFace;
        Color selectedColor = Colors::Selection;
        Color hoverColor = Colors::SelectionHover;
        Color disabledColor = Colors::LightGray;

        Color normalTextColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
        Color hoverTextColor = Colors::TextDefault;
        Color disabledTextColor = Colors::TextDisabled;

        Color borderColor = Colors::ButtonShadow;
        float borderWidth = 1.0f;

        Color separatorColor = Color(200, 200, 200, 255);
        float separatorWidth = 1.0f;

        // Layout
        float cornerRadius = 5.0f;
        int paddingHorizontal = 10;
        int paddingVertical = 6;
        int iconSpacing = 6;
        int segmentSpacing = 0;  // Space between segments (usually 0 for bordered style)

        // Typography
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Animation
        bool enableAnimation = false;
        float animationDuration = 0.15f;

        // Icons
        int iconSize = 16;

        static SegmentedControlStyle Default() {
            return SegmentedControlStyle();
        }

        static SegmentedControlStyle Modern() {
            SegmentedControlStyle style;
            style.normalColor = Color(255, 255, 255, 0);
            style.selectedColor = Color(255, 255, 255, 255);
            style.hoverColor = Color(255, 255, 255, 128);
            style.borderColor = Color(0, 122, 255, 255);
            style.normalTextColor = Color(0, 122, 255, 255);
            style.selectedTextColor = Color(0, 122, 255, 255);
            style.borderWidth = 1.0f;
            style.cornerRadius = 8.0f;
            return style;
        }

        static SegmentedControlStyle Flat() {
            SegmentedControlStyle style;
            style.normalColor = Colors::Transparent;
            style.selectedColor = Color(0, 120, 215, 255);
            style.hoverColor = Color(0, 120, 215, 64);
            style.borderWidth = 0.0f;
            style.separatorWidth = 0.0f;
            style.segmentSpacing = 4;
            style.cornerRadius = 4.0f;
            return style;
        }

        static SegmentedControlStyle Bar() {
            SegmentedControlStyle style;
            style.separatorWidth = 0.0f;
            return style;
        }
    };

// ===== MAIN SEGMENTED CONTROL CLASS =====

    class UltraCanvasSegmentedControl : public UltraCanvasUIElement {
    private:
        // Segments
        std::vector<SegmentData> segments;
// Selection state
        SegmentSelectionMode selectionMode = SegmentSelectionMode::Single;

        std::set<int> selectedIndices;  // For multiple selection mode
        int selectedIndex = -1;  // For single selection mode
        int hoveredIndex = -1;
        int pressedIndex = -1;

        // Appearance
        SegmentedControlStyle style;
        SegmentWidthMode widthMode = SegmentWidthMode::Equal;

        // Layout cache
        std::vector<Rect2Di> segmentRects;
        bool layoutDirty = true;

        // Animation
        float selectionAnimationProgress = 1.0f;
        int animationFromIndex = -1;
        int animationToIndex = -1;
        std::chrono::steady_clock::time_point animationStartTime;

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasSegmentedControl(const std::string &identifier = "SegmentedControl",
                                    long id = 0, long x = 0, long y = 0, long w = 300, long h = 32)
                : UltraCanvasUIElement(identifier, id, x, y, w, h) {

            properties.MousePtr = MousePointer::Hand;
            properties.MouseCtrl = MouseControls::Button;
        }

        bool AcceptsFocus() const override { return true; }

        // ===== SEGMENT MANAGEMENT =====

        int AddSegment(const std::string &text, TextAlignment alignment = TextAlignment::Center);
        int AddSegment(const std::string &text, const std::string &iconPath, TextAlignment alignment = TextAlignment::Center);
        int InsertSegment(int index, const std::string &text, TextAlignment alignment=TextAlignment::Center);
        void RemoveSegment(int index);
        void ClearSegments();

        int GetSegmentCount() const {
            return static_cast<int>(segments.size());
        }

        // ===== SEGMENT PROPERTIES =====

        void SetSegmentText(int index, const std::string &text);
        std::string GetSegmentText(int index) const;

        void SetSegmentIcon(int index, const std::string &iconPath);
        void SetSegmentEnabled(int index, bool enabled);
        bool IsSegmentEnabled(int index) const;
        void SetSegmentWidth(int index, float width);

        // ===== SELECTION =====

        void SetSelectionMode(SegmentSelectionMode mode);
        SegmentSelectionMode GetSelectionMode() const { return selectionMode; }

        // ===== SINGLE SELECTION (Legacy compatibility) =====

        void SetSelectedIndex(int index);
        int GetSelectedIndex() const;
        std::string GetSelectedText() const;

        // ===== MULTIPLE SELECTION =====

        void SetSelectedIndices(const std::vector<int>& indices);
        std::vector<int> GetSelectedIndices() const;

        void SelectSegment(int index, bool select = true);
        void ToggleSegmentSelection(int index);
        bool IsSegmentSelected(int index) const;

        void SelectAll();
        void DeselectAll();

        std::vector<std::string> GetSelectedTexts() const;

        void SetAllowNoSelection(bool allow);

        // ===== STYLING =====
        void SetStyle(SegmentedControlStyle newStyle);
        SegmentedControlStyle GetStyle() const { return style; }

        void SetWidthMode(SegmentWidthMode mode);
        SegmentWidthMode GetWidthMode() const { return widthMode; }

        // ===== RENDERING =====

        void Render() override;

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent &event) override;

        // ===== CALLBACKS =====

        std::function<void(int)> onSegmentSelected;      // Called when selection changes
        std::function<void(const std::vector<int>&)> onSelectionChanged;  // Called when selection changes (multiple mode)
        std::function<void(int)> onSegmentClick;         // Called when segment is clicked
        std::function<void(int)> onSegmentHover;         // Called when segment is hovered

    private:
        bool allowNoSelection = false;

        bool CanDeselectSegment(int index) const;
        bool CanSelectSegment(int index) const;

        // ===== LAYOUT CALCULATION =====

        void CalculateLayout(IRenderContext *ctx);
        int CalculateSegmentContentWidth(IRenderContext *ctx, const SegmentData &segment);

        // ===== RENDERING HELPERS =====

        void RenderSegments(IRenderContext *ctx);
        void RenderSegment(IRenderContext *ctx, int index);

        // ===== ANIMATION =====

        void UpdateAnimation();

        // ===== EVENT HANDLERS =====

        bool HandleMouseDown(const UCEvent &event);
        bool HandleMouseUp(const UCEvent &event);
        bool HandleMouseMove(const UCEvent &event);
        bool HandleKeyDown(const UCEvent &event);

        int GetSegmentAtPosition(int x, int y) const;
    };
// ===== FACTORY FUNCTIONS =====

    inline std::shared_ptr<UltraCanvasSegmentedControl> CreateSegmentedControl(
            const std::string& identifier, long id, long x, long y, long w, long h) {
        return UltraCanvasUIElementFactory::CreateWithID<UltraCanvasSegmentedControl>(
                id, identifier, id, x, y, w, h
        );
    }

    inline std::shared_ptr<UltraCanvasSegmentedControl> CreateSegmentedControl(
            const std::string& identifier, long id, const Rect2Di& bounds) {
        return CreateSegmentedControl(
                identifier, id, bounds.x, bounds.y, bounds.width, bounds.height
        );
    }

// ===== BUILDER PATTERN =====

    class SegmentedControlBuilder {
    private:
        std::shared_ptr<UltraCanvasSegmentedControl> control;

    public:
        SegmentedControlBuilder(const std::string& identifier, long id, long x, long y, long w, long h) {
            control = CreateSegmentedControl(identifier, id, x, y, w, h);
        }

        SegmentedControlBuilder& AddSegment(const std::string& text) {
            control->AddSegment(text);
            return *this;
        }

        SegmentedControlBuilder& AddSegment(const std::string& text, const std::string& icon) {
            control->AddSegment(text, icon);
            return *this;
        }

        SegmentedControlBuilder& SetStyle(SegmentedControlStyle style) {
            control->SetStyle(style);
            return *this;
        }

        SegmentedControlBuilder& SetWidthMode(SegmentWidthMode mode) {
            control->SetWidthMode(mode);
            return *this;
        }

        SegmentedControlBuilder& SetSelectedIndex(int index) {
            control->SetSelectedIndex(index);
            return *this;
        }

        SegmentedControlBuilder& AllowNoSelection(bool allow) {
            control->SetAllowNoSelection(allow);
            return *this;
        }

        SegmentedControlBuilder& OnSegmentSelected(std::function<void(int)> callback) {
            control->onSegmentSelected = callback;
            return *this;
        }

        SegmentedControlBuilder& OnSegmentClick(std::function<void(int)> callback) {
            control->onSegmentClick = callback;
            return *this;
        }

        std::shared_ptr<UltraCanvasSegmentedControl> Build() {
            return control;
        }
    };

} // namespace UltraCanvas