// include/UltraCanvasGroupBox.h
// Titled container ("group box" / "fieldset") that frames a set of child
// elements under a caption. Extends UltraCanvasContainer, so it inherits the
// full child-management / scrolling / layout machinery; this class adds the
// titled frame, an optional activator (checkbox or switch, on either side of
// the title bar) that enables/disables the contents, an optional info icon
// with help text, an optional close button, and optional collapsible behaviour.
// Version: 1.7.0 - a tool panel's header: SetMoreButton / onMoreClicked (a "…" that opens the
//                  topic's dialog), SetHeaderChip (a count or "in a table"), SetHeaderFocusable
//                  (Enter / Space fold it, Shift+F10 or Menu runs onHeaderContextMenu, a focus
//                  ring), SetHeaderDraggable (onHeaderDragStarted / Moved / Ended; a click still
//                  folds), a right-click on the header (onHeaderContextMenu); the Header style's
//                  strip is clipped to the frame's rounded corners
// Version: 1.6.0 - SectionCard(theme): the card from the theme (a dark card on a dark page), SectionCard()
//                  of the current one; SectionPageColor(theme): the page under it
// Version: 1.5.0 - GroupBoxVisualStyle::FromTheme, ApplyTheme: the group box follows the theme; a SetVisualStyle
//                  by hand stops the following
// Version: 1.4.0 - SetCloseButton / onCloseClicked: an "x" at the right end of the
//                  title bar, for a box that is a panel the user dismisses
// Version: 1.3.0 - GroupBoxVisualStyle::SectionCard(): one section of a window as a white
//                  card on the light Colors::SectionPage (UltraMail's panes, UltraPaint's
//                  side panel), shared instead of copied per app
// Version: 1.2.0
// Last Modified: 2026-11-27
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

        // ----- Close button (and the "…" button, which shares its look) -----
        Color closeIconColor           = Color(120, 130, 150, 255);
        Color closeIconHoverColor      = Color(40, 40, 40, 255);
        Color closeHoverBackgroundColor = Color(0, 0, 0, 28);

        // ----- Header chip (SetHeaderChip) -----
        Color chipBackgroundColor = Color(228, 230, 235, 255);
        Color chipTextColor       = Color(90, 95, 105, 255);
        float chipFontSize        = 10.0f;

        // ----- Focus ring around a focusable header (SetHeaderFocusable) -----
        Color focusRingColor = Color(0, 120, 215, 255);

        // ----- Content -----
        float contentPadding = 8.0f;                      // padding inside the frame, around the children

        // Disabled (unchecked, checkable) caption tint.
        Color disabledTitleColor = Color(150, 150, 150, 255);

        static GroupBoxVisualStyle Default();
        // iOS-like grouped card: filled rounded background, bold header, separator.
        static GroupBoxVisualStyle Card();
        // One section of a window as a card - UltraMail's folder,
        // message-list and message panes, UltraPaint's side panel: a hairline
        // border, 8 px corners and the caption in bold grey over a hairline
        // separator. Meant for GroupBoxFrameStyle::Header, on a page of
        // SectionPageColor(theme). Under a light theme its colours are the
        // Colors::SectionCard* ones (a white card on Colors::SectionPage),
        // and a list on it takes Colors::ListRowHover / ListRowSelected;
        // under a dark one the card is the theme's panel in a hairline
        // (ThemeRoles::line) with the caption in muted ink, on the ground.
        // The caption is 11 px: an application with another body size sets
        // titleFont.fontSize to it, and contentPadding to what its content
        // wants (8 px here). Without a theme: the current one.
        static GroupBoxVisualStyle SectionCard(const UltraCanvasTheme& theme);
        static GroupBoxVisualStyle SectionCard();
        // The page a section card sits on: Colors::SectionPage under a light
        // theme, the theme's ground under a dark one.
        static Color SectionPageColor(const UltraCanvasTheme& theme);
        static Color SectionPageColor();

        // The theme's roles over a base's own shape (widths, radius, indents,
        // padding, the caption's weight) and its fills: a filled frame or
        // header takes the raised surface and the panel, a transparent one
        // stays transparent; the frame in a hairline, the caption in ink at
        // the body size, faint ink disabled; the activator's track in the
        // accent and the control border with the raised surface for its
        // knob; the info and close icons in muted ink, the accent and ink
        // when hovered, the hover fill behind the close button
        static GroupBoxVisualStyle FromTheme(const UltraCanvasTheme& theme, const GroupBoxVisualStyle& base);
        static GroupBoxVisualStyle FromTheme(const UltraCanvasTheme& theme) { return FromTheme(theme, Default()); }
        static GroupBoxVisualStyle FromTheme() { return FromTheme(UltraCanvasTheme::Current()); }
    };

// ===== GROUP BOX =====
    class UltraCanvasGroupBox : public UltraCanvasContainer {
    public:
        // ===== ACCESSIBILITY =====
        // A group named by its title.
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Group; }
        std::string GetAccessibleName() const override {
            return GetAccessibleNameOverride().empty() ? title : GetAccessibleNameOverride();
        }

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
        void ApplyTheme(const UltraCanvasTheme& theme) override;
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

        // ===== CLOSE BUTTON =====
        // An optional "x" at the right end of the title bar, for a box that is
        // a panel the user dismisses (the media viewer's Details panel). A
        // click runs onCloseClicked; the box does not hide itself - the host
        // decides what closing means.
        void SetCloseButton(bool show);
        bool HasCloseButton() const { return showCloseButton; }
        // The close button's tooltip; empty (the default) is "Close" in the
        // user's language.
        void SetCloseButtonTooltip(const std::string& text) { closeTooltip = text; }

        // ===== MORE BUTTON ("…") =====
        // An optional "…" at the right of the title bar (left of the close
        // button): the dialog that holds every setting of the box's topic -
        // a tool panel's "Paragraph" opening the Paragraph dialog. A click
        // runs onMoreClicked.
        void SetMoreButton(bool show);
        bool HasMoreButton() const { return showMoreButton; }
        // Its tooltip - the dialog's name; empty (the default) is "Options".
        void SetMoreButtonTooltip(const std::string& text) { moreTooltip = text; }

        // ===== HEADER CHIP =====
        // A small rounded label at the right of the title: a count ("8") or
        // why the box is shown ("in a table"). Empty (the default) shows none.
        // Its colours are chipBackgroundColor / chipTextColor.
        void SetHeaderChip(const std::string& text);
        const std::string& GetHeaderChip() const { return headerChip; }

        // ===== THE HEADER AS A FOCUS STOP =====
        // The box takes the keyboard focus for its title bar: Tab reaches it
        // before its contents (a click does not take it, so the document
        // keeps its caret). Enter or Space folds a collapsible box; Shift+F10
        // or the Menu key runs onHeaderContextMenu below the title bar. A
        // focus ring is drawn around the title bar while it has the focus.
        void SetHeaderFocusable(bool focusable);
        bool IsHeaderFocusable() const { return headerFocusable; }
        bool AcceptsFocus() const override { return headerFocusable; }

        // ===== DRAGGING BY THE HEADER =====
        // A press on the title bar (not on its buttons) that moves a few
        // pixels before its release is a drag: onHeaderDragStarted, then
        // onHeaderDragMoved for every move and onHeaderDragEnded at the
        // release, the pointer in window coordinates. A press released where
        // it began still folds the box, on the release. The box does not move
        // itself; its host (a tool panel stack) reorders.
        void SetHeaderDraggable(bool draggable);
        bool IsHeaderDraggable() const { return headerDraggable; }
        bool IsHeaderDragging() const { return headerDragging; }
        // Ends a drag under way without a drop: onHeaderDragEnded(.., true).
        void CancelHeaderDrag();

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
        std::function<void()>     onCloseClicked;       // the close button was clicked
        std::function<void()>     onMoreClicked;        // the "…" button was clicked
        // A right-click on the title bar, or Shift+F10 / Menu on a focused
        // header: where a menu goes, in window coordinates.
        std::function<void(const Point2Di& windowPos)> onHeaderContextMenu;
        std::function<void(const Point2Di& windowPos)> onHeaderDragStarted;
        std::function<void(const Point2Di& windowPos)> onHeaderDragMoved;
        std::function<void(const Point2Di& windowPos, bool cancelled)> onHeaderDragEnded;

        // ===== ACCESSIBILITY ACTION =====
        // A collapsible box opens or closes ("Open" / "Close").
        std::string GetAccessibleActionName() const override;
        bool DoAccessibleAction() override;

        // The title bar, in the box's own coordinates.
        Rect2Df GetTitleBarRect() const { return TitleBarRect(); }

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

        bool showCloseButton = false;
        bool closeHovered    = false;
        std::string closeTooltip;   // empty: "Close", in the user's language

        bool showMoreButton = false;
        bool moreHovered    = false;
        std::string moreTooltip;    // empty: "Options", in the user's language

        std::string headerChip;

        bool headerFocusable = false;
        bool headerDraggable = false;
        bool headerPressPending = false;   // a press on the title bar, not yet a click or a drag
        bool headerDragging     = false;
        Point2Di headerPressStart;

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
            bool   hasClose = false;
            bool   hasMore = false;
            bool   hasChip = false;
            Rect2Df arrowRect;                    // disclosure arrow (collapsible)
            Rect2Df activatorRect;                // checkbox / switch
            Rect2Df infoRect;                     // info icon
            Rect2Df closeRect;                    // close button (rightmost)
            Rect2Df moreRect;                     // "…" button, left of the close button
            Rect2Df chipRect;                     // header chip, right of the caption
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
        void DrawCloseButton(IRenderContext* ctx, const Rect2Df& rect);
        void DrawMoreButton(IRenderContext* ctx, const Rect2Df& rect);
        void DrawChip(IRenderContext* ctx, const Rect2Df& rect);
        void DrawFocusRing(IRenderContext* ctx);
        void ToggleFromHeader();                  // what a click on the caption does
        void EndHeaderPress(const Point2Di& windowPos, bool cancelled);

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
