// include/UltraCanvasToolbar.h
// Comprehensive cross-platform toolbar component with advanced features
// Version: 1.3.0
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_TOOLBAR_H
#define ULTRACANVAS_TOOLBAR_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
    class UltraCanvasToolbar;

// ===== TOOLBAR ENUMERATIONS =====

    enum class ToolbarOrientation {
        Horizontal = 0,
        Vertical = 1
    };

    enum class ToolbarPosition {
        Top = 0,
        Bottom = 1,
        Left = 2,
        Right = 3,
        Floating = 4
    };

    enum class ToolbarStyle {
        Standard = 0,      // Classic toolbar with buttons
        Flat = 1,          // Flat design without borders
        Raised = 2,        // Raised with shadows
        Docked = 3,        // Docked style (like macOS dock)
        Ribbon = 4,        // Ribbon-style with multiple rows
        StatusBar = 5,     // Status bar at bottom
        Sidebar = 6        // Vertical sidebar
    };

    enum class ToolbarOverflowMode {
        OverflowNone = 0,           // No overflow handling
        Wrap = 1,           // Wrap items to new line
        Menu = 2,           // Move to overflow menu
        Scroll = 3,         // Allow scrolling
        Hide = 4            // Hide overflow items
    };

    enum class ToolbarIconSize {
        Small = 0,          // 16x16
        Medium = 1,         // 24x24
        Large = 2,          // 32x32
        ExtraLarge = 3,     // 48x48
        Huge = 4,           // 64x64
        Custom = 5          // User-defined size
    };

    enum class ToolbarVisibility {
        AlwaysVisible = 0,
        AutoHide = 1,
        OnHover = 2,
        OnDemand = 3
    };

    enum class ToolbarDragMode {
        DragNone = 0,
        Movable = 1,
        ReorderItems = 2,
        Both = 3
    };

// ===== TOOLBAR APPEARANCE CONFIGURATION =====

    struct ToolbarAppearance {
        // Colors
        ToolbarStyle style = ToolbarStyle::Standard;
        
        Color foregroundColor = Colors::Black;
        Color backgroundColor = Color(245, 245, 245, 255);
        Color separatorColor = Color(200, 200, 200, 255);
        Color hoverBackgroundColor = Color(225, 235, 255, 255);
        Color activeBackgroundColor = Color(204, 228, 247, 255);
        Color disabledBackgroundColor = Color(220, 220, 220, 255);

//        Color hoverForegroundColor = Colors::Black;
//        Color activeForegroundColor = Colors::Black;
//        Color disabledForegroundColor = Color(80,80,80, 255);

        // Spacing
        float itemSpacing = 4.0f;
        float groupSpacing = 8.0f;

        // Shadow (for Docked style)
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 60);
        Point2Di shadowOffset = Point2Di(0, 2);
        float shadowBlur = 4.0f;

        // Animation
        bool enableAnimations = true;
        float animationDuration = 0.2f; // seconds

        // Icon appearance
        ToolbarIconSize iconSize = ToolbarIconSize::Medium;
        int customIconWidth = 24;
        int customIconHeight = 24;
        bool showIconLabels = true;
        bool centerIcons = false;

        // Dock-style effects (for ToolbarStyle::Docked)
        bool enableMagnification = false;
        float magnificationScale = 1.5f;
        float magnificationRadius = 100.0f;

        static ToolbarAppearance Default() {
            return ToolbarAppearance();
        }

        static ToolbarAppearance Flat() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Flat;
            app.hasShadow = false;
            app.backgroundColor = Color(240, 240, 240, 255);
            app.hoverBackgroundColor = Color(220, 220, 255);
            return app;
        }

        static ToolbarAppearance MacOSDock() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Docked;
            app.backgroundColor = Color(255, 255, 255, 200);
            app.hasShadow = true;
            app.shadowColor = Color(0, 0, 0, 100);
            app.shadowOffset = Point2Di(0, 4);
            app.shadowBlur = 8.0f;
            app.enableMagnification = true;
            app.magnificationScale = 1.8f;
            app.showIconLabels = false;
            app.centerIcons = true;
            app.iconSize = ToolbarIconSize::Large;
            return app;
        }

        static ToolbarAppearance Ribbon() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Ribbon;
            app.backgroundColor = Color(248, 248, 248, 255);
            app.groupSpacing = 16.0f;
            return app;
        }

        static ToolbarAppearance StatusBar() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::StatusBar;
            return app;
        }

        static ToolbarAppearance Sidebar() {
            ToolbarAppearance app;
            app.style = ToolbarStyle::Sidebar;
            return app;
        }
    };

// ===== MAIN TOOLBAR CLASS =====

    class UltraCanvasToolbar : public UltraCanvasContainer {
    private:
        // Configuration
        ToolbarOrientation toolbarOrientation = ToolbarOrientation::Horizontal;
        ToolbarPosition toolbarPosition = ToolbarPosition::Top;
        ToolbarAppearance toolbarAppearance;
        ToolbarOverflowMode overflowMode = ToolbarOverflowMode::OverflowNone;
        ToolbarVisibility toolbarVisibility = ToolbarVisibility::AlwaysVisible;
        ToolbarDragMode toolbarDragMode = ToolbarDragMode::DragNone;

        // Layout is configured directly on the inherited CSSLayout::Element::layout
        // (display = Flex, direction set per orientation). No separate pointer.

        // Widgets are stored as real children (CSSLayout::Element::Children()).
        // This map provides id-based lookup for GetWidget(id); only identified
        // widgets are mapped (anonymous spacers/separators are not).
        std::unordered_map<std::string, std::shared_ptr<UltraCanvasUIElement>> widgetMap;

        // Overflow management
        std::shared_ptr<UltraCanvasMenu> overflowMenu;
        std::shared_ptr<UltraCanvasButton> overflowButton;

        // Auto-hide state
        bool isAutoHidden = false;
        bool isHovered = false;
        float autoHideDelay = 0.5f; // seconds

        // Drag state
        bool isDragging = false;
        Point2Di dragStartPos;
        Point2Di originalPos;

        // Magnification (for dock-style toolbars)
        int hoveredItemIndex = -1;
        Point2Di mousePosition;

    public:
        UltraCanvasToolbar(const std::string& identifier, float x, float y,
                           float width, float height);

        UltraCanvasToolbar(const std::string& identifier, float width, float height)
            : UltraCanvasToolbar(identifier, -1, -1, width, height) {}

        explicit UltraCanvasToolbar(const std::string& identifier)
            : UltraCanvasToolbar(identifier, -1, -1, -1, -1) {}

        virtual ~UltraCanvasToolbar() = default;

        // ===== CONFIGURATION =====
        void SetOrientation(ToolbarOrientation orient);
        void SetToolbarPosition(ToolbarPosition pos);
        void SetAppearance(const ToolbarAppearance& app);
        void SetOverflowMode(ToolbarOverflowMode mode);
        void SetVisibility(ToolbarVisibility vis);
        void SetDragMode(ToolbarDragMode mode);

        ToolbarOrientation GetOrientation() const { return toolbarOrientation; }
        ToolbarPosition GetPosition() const { return toolbarPosition; }
        const ToolbarAppearance& GetAppearance() const { return toolbarAppearance; }

        // ===== ITEM MANAGEMENT =====
        // The toolbar is a facade over its flex children: each Add* creates a
        // real widget and adds it as a child. Removal works by id or index.
        void RemoveItem(const std::string& identifier);
        void RemoveItemAt(int index);
        void ClearItems();

        // Widget access. GetWidget(id) returns the widget registered under id;
        // GetWidgetAt(index) returns the child at the given position.
        std::shared_ptr<UltraCanvasUIElement> GetWidget(const std::string& identifier);
        std::shared_ptr<UltraCanvasUIElement> GetWidgetAt(int index);
        int GetItemCount() const { return static_cast<int>(GetChildCount()); }

        // Convenience builders — each creates a real widget, adds it as a child,
        // and returns the typed shared_ptr for inline configuration.
        std::shared_ptr<UltraCanvasButton> AddButton(
                const std::string& id, const std::string& text,
                const std::string& icon = "", std::function<void()> onClick = nullptr);
        std::shared_ptr<UltraCanvasButton> AddToggleButton(
                const std::string& id, const std::string& text,
                const std::string& icon = "", std::function<void(bool)> onToggle = nullptr);
        std::shared_ptr<UltraCanvasDropdown> AddDropdownButton(
                const std::string& id, const std::string& text,
                const std::vector<std::string>& items,
                std::function<void(const std::string&)> onSelect = nullptr);
        std::shared_ptr<UltraCanvasSeparator> AddSeparator(const std::string& id = "");
        std::shared_ptr<UltraCanvasSpacer> AddSpacer(int size = 8);
        std::shared_ptr<UltraCanvasSpacer> AddStretch(float stretch = 1.0f);
        std::shared_ptr<UltraCanvasLabel> AddLabel(const std::string& id, const std::string& text);
        std::shared_ptr<UltraCanvasTextInput> AddSearchBox(
                const std::string& id, const std::string& placeholder = "Search...",
                std::function<void(const std::string&)> onTextChange = nullptr);

        // ===== LAYOUT =====
        void HandleOverflow();

        // ===== RENDERING =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== AUTO-HIDE =====
        void SetAutoHideDelay(float delay) { autoHideDelay = delay; }
        void ShowToolbar();
        void HideToolbar();
        bool IsAutoHidden() const { return isAutoHidden; }

        // ===== DRAG & DROP =====
        void EnableItemReordering(bool enable);
        void BeginDrag(const Point2Di& startPos);
        void UpdateDrag(const Point2Di& currentPos);
        void EndDrag();

        // ===== CALLBACKS =====
        std::function<void()> onToolbarShow;
        std::function<void()> onToolbarHide;
        std::function<void(const std::string&)> onItemAdded;
        std::function<void(const std::string&)> onItemRemoved;
        std::function<void(int, int)> onItemReordered;
        std::function<void(ToolbarPosition)> onPositionChanged;

    private:
        // Internal helpers
        void CreateLayout();
        // Add `w` as a child and, if `id` is non-empty, register it for lookup.
        std::shared_ptr<UltraCanvasUIElement> RegisterWidget(
                const std::string& id, std::shared_ptr<UltraCanvasUIElement> w);
        // Apply the toolbar appearance (style/colors/icon size) to one button.
        void ApplyButtonAppearance(const std::shared_ptr<UltraCanvasButton>& button);
        // Re-apply the toolbar appearance across all current children.
        void ApplyAppearanceToChildren();
        void CreateOverflowMenu();
        void UpdateOverflowButton();
        void CalculateMagnification();
        void RenderDockMagnification(IRenderContext* ctx);
        void RenderShadow(IRenderContext* ctx);
    };

// ===== TOOLBAR BUILDER =====

    class UltraCanvasToolbarBuilder {
    private:
        std::shared_ptr<UltraCanvasToolbar> toolbar;

    public:
        UltraCanvasToolbarBuilder(const std::string& identifier);

        UltraCanvasToolbarBuilder& SetOrientation(ToolbarOrientation orient);
        UltraCanvasToolbarBuilder& SetToolbarPosition(ToolbarPosition pos);
        UltraCanvasToolbarBuilder& SetAppearance(const ToolbarAppearance& app);
        UltraCanvasToolbarBuilder& SetOverflowMode(ToolbarOverflowMode mode);
        UltraCanvasToolbarBuilder& SetDimensions(int x, int y, int width, int height);

        UltraCanvasToolbarBuilder& AddButton(const std::string& id, const std::string& text,
                                             const std::string& icon = "",
                                             std::function<void()> onClick = nullptr);
        UltraCanvasToolbarBuilder& AddToggleButton(const std::string& id, const std::string& text,
                                                   const std::string& icon = "",
                                                   std::function<void(bool)> onToggle = nullptr);
        UltraCanvasToolbarBuilder& AddDropdownButton(const std::string& id, const std::string& text,
                                                     const std::vector<std::string>& items,
                                                     std::function<void(const std::string&)> onSelect = nullptr);
        UltraCanvasToolbarBuilder& AddSeparator(const std::string& id = "");
        UltraCanvasToolbarBuilder& AddSpacer(int size = 8);
        UltraCanvasToolbarBuilder& AddStretch(float stretch = 1.0f);
        UltraCanvasToolbarBuilder& AddLabel(const std::string& id, const std::string& text);

        std::shared_ptr<UltraCanvasToolbar> Build();
    };

// ===== PRESET TOOLBAR FACTORIES =====

    namespace ToolbarPresets {
        // Standard horizontal toolbar
        std::shared_ptr<UltraCanvasToolbar> CreateStandardToolbar(
                const std::string& identifier = "Toolbar"
        );

        // macOS-style dock
        std::shared_ptr<UltraCanvasToolbar> CreateDockStyleToolbar(
                const std::string& identifier = "Dock"
        );

        // Ribbon-style toolbar with multiple rows
        std::shared_ptr<UltraCanvasToolbar> CreateRibbonToolbar(
                const std::string& identifier = "Ribbon"
        );

        // Vertical sidebar
        std::shared_ptr<UltraCanvasToolbar> CreateSidebarToolbar(
                const std::string& identifier = "Sidebar"
        );

        // Status bar
        std::shared_ptr<UltraCanvasToolbar> CreateStatusBar(
                const std::string& identifier = "StatusBar"
        );
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_TOOLBAR_H
