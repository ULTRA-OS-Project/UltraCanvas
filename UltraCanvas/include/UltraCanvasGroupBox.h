// include/UltraCanvasGroupBox.h
// Titled container ("group box" / "fieldset") that frames a set of child
// elements under a caption. Extends UltraCanvasContainer, so it inherits the
// full child-management / scrolling / layout machinery; this class adds the
// titled frame, an optional activator (checkbox or switch, on either side of
// the title bar) that enables/disables the contents, an optional info icon
// with help text, and optional collapsible behaviour.
// Version: 1.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>
#include <vector>
#include <utility>

namespace UltraCanvas {

// ===== TITLE HORIZONTAL ALIGNMENT =====
    enum class GroupBoxTitleAlignment {
        Left,
        Center,
        Right
    };

// ===== FRAME STYLE =====
    enum class GroupBoxFrameStyle {
        Framed,   // Classic fieldset: caption embedded in a gap in the top border line.
        Header,   // Caption sits in a header strip above the content, full border + separator.
        Flat      // No border; caption strip on top of an (optionally) filled background.
    };

// ===== ACTIVATOR (enable/disable toggle) =====
    enum class GroupBoxActivatorStyle {
        Checkbox,   // Square checkbox with a checkmark.
        Switch      // Sliding on/off switch (iOS-like pill).
    };

// ===== TITLE-BAR INDICATOR SIDE =====
    enum class GroupBoxIndicatorSide {
        Left,
        Right
    };

// ===== VISUAL STYLE =====
    struct GroupBoxVisualStyle {
        // ----- Frame -----
        Color backgroundColor = Colors::Transparent;     // fill behind the content
        Color borderColor     = Color(180, 180, 180, 255);
        float borderWidth     = 1.0f;
        float cornerRadius     = 4.0f;

        // ----- Title -----
        FontStyle titleFont;                              // family/size/weight/slant of the caption
        Color titleColor          = Color(60, 60, 60, 255);
        float titleIndent         = 8.0f;                 // horizontal inset of the caption from the frame edge (Left/Right)
        float titleGap            = 4.0f;                 // horizontal padding around the caption inside the border gap
        float titleVerticalPadding = 3.0f;                // extra height added above/below the caption text

        // ----- Header style extras -----
        Color headerBackgroundColor = Colors::Transparent;
        bool  showHeaderSeparator   = true;

        // ----- Activator (switch/checkbox) -----
        Color activatorOnColor  = Color(52, 199, 89, 255);   // switch track when on (iOS green)
        Color activatorOffColor = Color(200, 200, 200, 255); // switch track when off
        Color activatorKnobColor = Colors::White;

        // ----- Info icon -----
        Color infoIconColor      = Color(120, 130, 150, 255);
        Color infoIconHoverColor = Color(60, 110, 220, 255);

        // ----- Content -----
        float contentPadding = 8.0f;                      // padding inside the frame, around the children

        // Disabled (unchecked, checkable) caption tint.
        Color disabledTitleColor = Color(150, 150, 150, 255);

        static GroupBoxVisualStyle Default();
        // iOS-like grouped card: filled rounded background, bold header, separator.
        static GroupBoxVisualStyle Card();
    };

// ===== GROUP BOX =====
    class UltraCanvasGroupBox : public UltraCanvasContainer {
    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasGroupBox(const std::string& identifier, float x, float y, float w, float h,
                            const std::string& title = "");

        UltraCanvasGroupBox(const std::string& identifier, float w, float h,
                            const std::string& title = "")
                : UltraCanvasGroupBox(identifier, -1, -1, w, h, title) {}

        explicit UltraCanvasGroupBox(const std::string& identifier, const std::string& title = "")
                : UltraCanvasGroupBox(identifier, -1, -1, -1, -1, title) {}

        ~UltraCanvasGroupBox() override = default;

        // ===== TITLE =====
        void SetTitle(const std::string& newTitle);
        const std::string& GetTitle() const { return title; }

        void SetTitleAlignment(GroupBoxTitleAlignment alignment);
        GroupBoxTitleAlignment GetTitleAlignment() const { return titleAlignment; }

        void SetFrameStyle(GroupBoxFrameStyle newStyle);
        GroupBoxFrameStyle GetFrameStyle() const { return frameStyle; }

        // ===== STYLE =====
        void SetVisualStyle(const GroupBoxVisualStyle& newStyle);
        const GroupBoxVisualStyle& GetVisualStyle() const { return visualStyle; }

        // Convenience setters
        void SetTitleFont(const std::string& family, float size, FontWeight weight = FontWeight::Bold);
        void SetTitleColor(const Color& color);
        void SetBorderColor(const Color& color);
        void SetBorderWidth(float width);
        void SetCornerRadius(float radius);
        // Sets the fill behind the content. (Named distinctly from the base
        // UltraCanvasUIElement::SetBackgroundColor so the frame stays the single
        // source of truth for the box's visuals.)
        void SetGroupBackgroundColor(const Color& color);
        void SetContentPadding(float padding);

        // ===== ACTIVATOR (enable/disable toggle) =====
        // A checkable group box draws an activator (checkbox or switch) in its
        // title bar; switching it off disables (greys out) the contained
        // children. The activator can sit on either side of the title bar.
        void SetCheckable(bool checkable);
        bool IsCheckable() const { return checkable; }
        void SetChecked(bool checked);
        bool IsChecked() const { return checked; }

        void SetActivatorStyle(GroupBoxActivatorStyle style);   // Checkbox (default) or Switch
        GroupBoxActivatorStyle GetActivatorStyle() const { return activatorStyle; }
        void SetActivatorSide(GroupBoxIndicatorSide side);      // Left (default) or Right
        GroupBoxIndicatorSide GetActivatorSide() const { return activatorSide; }

        // Convenience: enable a switch-style activator on the given side in one call.
        void EnableActivatorSwitch(GroupBoxIndicatorSide side = GroupBoxIndicatorSide::Right);

        // ===== INFO ICON / HELP TEXT =====
        // An optional "ⓘ" icon in the title bar that reveals help text as a
        // tooltip on hover. Defaults to the right side.
        void SetInfoIcon(bool show);
        bool HasInfoIcon() const { return showInfoIcon; }
        void SetInfoIconSide(GroupBoxIndicatorSide side);
        GroupBoxIndicatorSide GetInfoIconSide() const { return infoIconSide; }
        // Sets the help text and shows the info icon (pass an empty string to hide).
        void SetHelpText(const std::string& text);
        const std::string& GetHelpText() const { return helpText; }

        // ===== COLLAPSIBLE =====
        // A collapsible group box draws a disclosure arrow before its caption;
        // clicking the caption collapses the box down to just its title bar.
        void SetCollapsible(bool collapsible);
        bool IsCollapsible() const { return collapsible; }
        void SetCollapsed(bool collapsed);
        bool IsCollapsed() const { return collapsed; }

        // ===== CALLBACKS =====
        std::function<void(bool)> onCheckedChanged;     // arg: new checked state
        std::function<void(bool)> onCollapsedChanged;   // arg: new collapsed state

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        std::string title;
        GroupBoxTitleAlignment titleAlignment = GroupBoxTitleAlignment::Left;
        GroupBoxFrameStyle frameStyle = GroupBoxFrameStyle::Framed;
        GroupBoxVisualStyle visualStyle;

        bool checkable = false;
        bool checked   = true;
        GroupBoxActivatorStyle activatorStyle = GroupBoxActivatorStyle::Checkbox;
        GroupBoxIndicatorSide  activatorSide  = GroupBoxIndicatorSide::Left;

        bool showInfoIcon = false;
        GroupBoxIndicatorSide infoIconSide = GroupBoxIndicatorSide::Right;
        std::string helpText;
        bool infoIconHovered = false;

        bool collapsible = false;
        bool collapsed   = false;
        bool titleHovered = false;

        // Saved content-box height so an expanded box can be restored after a
        // collapse (collapse forces an explicit pixel height of the title bar).
        CSSLayout::Dimension expandedHeight;

        // ----- Geometry helpers (element-local coordinates) -----
        bool  HasTitleContent() const;           // any caption / indicator present
        float TitleAreaHeight() const;           // height reserved for the caption strip
        Rect2Df TitleBarRect() const;            // clickable caption region
        float EffectiveBorderWidth() const;      // 0 for Flat, borderWidth otherwise
        float IndicatorSize() const;             // base checkbox / arrow / info box size
        Size2Df ActivatorSize() const;           // checkbox vs switch dimensions

        // Resolved layout of the caption row, computed against a render context.
        struct TitleLayout {
            bool   hasContent = false;
            bool   hasArrow = false;
            bool   hasActivator = false;
            bool   hasInfo = false;
            Rect2Df arrowRect;                    // disclosure arrow (collapsible)
            Rect2Df activatorRect;                // checkbox / switch
            Rect2Df infoRect;                     // info icon
            Point2Df textPos;                     // caption text top-left
            Size2Di textSize;
            // Top-border gap intervals (Framed style) covering on-border items.
            std::vector<std::pair<float, float>> borderGaps;
        };
        TitleLayout ComputeTitleLayout(IRenderContext* ctx) const;

        // ----- Rendering helpers -----
        void DrawFrame(IRenderContext* ctx, const TitleLayout& tl);
        void DrawTitle(IRenderContext* ctx, const TitleLayout& tl);
        void DrawCheckIndicator(IRenderContext* ctx, const Rect2Df& rect);
        void DrawSwitchIndicator(IRenderContext* ctx, const Rect2Df& rect);
        void DrawInfoIcon(IRenderContext* ctx, const Rect2Df& rect);
        void DrawDisclosureArrow(IRenderContext* ctx, const Rect2Df& rect);

        // ----- State plumbing -----
        void RecalculatePadding();                // fold border + caption into box padding
        void ApplyCheckStateToChildren();         // enable/disable children to match `checked`
        void ShowHelpTooltip();                   // surface helpText near the info icon
        void HideHelpTooltip();
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasGroupBox> CreateGroupBox(
            const std::string& identifier, float x, float y, float w, float h,
            const std::string& title = "") {
        return std::make_shared<UltraCanvasGroupBox>(identifier, x, y, w, h, title);
    }

    inline std::shared_ptr<UltraCanvasGroupBox> CreateGroupBox(
            const std::string& identifier, float w, float h, const std::string& title = "") {
        return std::make_shared<UltraCanvasGroupBox>(identifier, w, h, title);
    }

    inline std::shared_ptr<UltraCanvasGroupBox> CreateGroupBox(
            const std::string& identifier, const std::string& title) {
        return std::make_shared<UltraCanvasGroupBox>(identifier, title);
    }

} // namespace UltraCanvas
