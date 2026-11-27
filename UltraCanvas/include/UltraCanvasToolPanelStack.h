// include/UltraCanvasToolPanelStack.h
// A column of tool panels at the side of a document: one panel per topic
// (Clipboard, Font, Paragraph, Table ...), each an UltraCanvasGroupBox in its
// Header frame style that opens and closes by itself, stacked top to bottom
// in a column that scrolls. It replaces a row of command tabs and their
// toolbars with every topic on screen at once
// (Docs/Research/UltraOfficeToolPanelsProposal.md).
//
// - Panels are added with their content - any container, usually built from
//   the rows in UltraCanvasToolRows.h - and open or close with a click on
//   their header, SetPanelOpen, or Enter / Space on a focused header.
// - A context panel (ToolPanelOptions::context) is hidden until the host
//   shows it for the selection (SetPanelShown: the caret is in a table, a
//   picture is selected). It comes in at the top of the column, open, with
//   an accent border and header, and the column keeps the panel under the
//   pointer - or, scrolled, the first one in view - where it was.
// - The user reorders the panels by dragging a header (a line shows where it
//   goes), with Move up / Move down in the header's menu (a right-click,
//   Shift+F10), or Ctrl+Up / Ctrl+Down on a focused header. GetOrder and
//   SetOrder read and set the order; SaveState / RestoreState give it, with
//   the open panels, as one string for the host's settings.
// - Rail mode (SetRailMode, or SetAutoRailBelowWidth for a narrow window)
//   folds the column into a strip of icons, one per panel; a click opens the
//   panel as a flyout beside the strip, a popup that Escape or a click
//   outside closes.
// - Keyboard: FocusFirst puts the focus on the first header (the host's F6),
//   Up / Down go from header to header, Tab walks into a panel's controls.
//
// doc-check: std::shared_ptr<UltraCanvasContainer> fontRows; std::shared_ptr<UltraCanvasContainer> tableRows; std::shared_ptr<UltraCanvasWindow> window; std::string savedPanels;
// doc-check: example
//     auto panels = CreateToolPanelStack("tools");
//     panels->AddPanel("font", "Font", fontRows, ToolPanelOptions{"media/icons/textformat/bold.svg"});
//     ToolPanelOptions table;
//     table.context = true;
//     table.chip = "in a table";
//     panels->AddPanel("table", "Table", tableRows, table);
//     panels->RestoreState(ToolPanelStackState::FromString(savedPanels));
//     panels->onOrderChanged = [](const std::vector<std::string>& order) { (void)order; };
//     window->AddChild(panels);
//
// Version: 1.0.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasButton.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasMenu;
    class UltraCanvasToolPanelColumn;
    class UltraCanvasToolPanelFlyout;

    // How a panel is added.
    struct ToolPanelOptions {
        std::string iconPath;              // its icon in the rail
        std::string helpText;              // the header's info icon; empty: none
        std::function<void()> dialogAction;   // the header's "…" button (the topic's dialog); empty: none
        std::string dialogTooltip;         // the "…" button's tooltip, the dialog's name; empty: "Options"
        bool context = false;              // shown only while SetPanelShown(id, true): the selection's panel
        bool open = true;                  // open at the start (a context panel opens whenever it is shown)
        std::string chip;                  // a count or a context at the header's right: "8", "in a table"
    };

    // What the host keeps in its settings between runs: the order of the
    // panels, which are open, and rail or column. ToString / FromString give
    // it as one line ("order=clipboard,font,paragraph;open=font;rail=0"); an
    // id is written with %, comma, semicolon and = escaped, so any id fits.
    struct ToolPanelStackState {
        std::vector<std::string> order;    // every panel's id, top to bottom
        std::vector<std::string> open;     // the ids of the open panels
        bool railMode = false;

        std::string ToString() const;
        // An empty or unreadable string gives an empty state, which
        // RestoreState leaves the stack alone for.
        static ToolPanelStackState FromString(const std::string& text);
        bool operator==(const ToolPanelStackState& o) const {
            return order == o.order && open == o.open && railMode == o.railMode;
        }
    };

    // The stack's look: a column on the panel background, white panels with
    // a light grey header strip, a context panel in the accent.
    struct ToolPanelStackStyle {
        Color backgroundColor       = Color(240, 241, 244, 255);   // the column's ground
        Color panelColor            = Colors::White;
        Color panelBorderColor      = Color(218, 220, 226, 255);
        Color headerColor           = Color(246, 247, 249, 255);
        Color titleColor            = Color(40, 42, 48, 255);
        Color iconColor             = Color(110, 116, 128, 255);    // chevron's neighbours: info, "…"
        Color iconHoverColor        = Color(40, 42, 48, 255);
        Color hoverColor            = Color(0, 0, 0, 20);
        Color chipColor             = Color(228, 230, 235, 255);
        Color chipTextColor         = Color(90, 95, 105, 255);
        Color contextBorderColor    = Color(0, 120, 215, 255);      // a context panel's frame
        Color contextHeaderColor    = Color(229, 241, 251, 255);    // and its header
        Color contextChipColor      = Color(0, 120, 215, 255);
        Color contextChipTextColor  = Colors::White;
        Color dropIndicatorColor    = Color(0, 120, 215, 255);      // where a dragged panel goes
        Color draggedPanelVeil      = Color(240, 241, 244, 150);    // over the panel being dragged
        Color focusColor            = Color(0, 120, 215, 255);
        Color railColor             = Color(240, 241, 244, 255);
        Color railBorderColor       = Color(218, 220, 226, 255);
        Color flyoutBorderColor     = Color(200, 203, 210, 255);
        Color flyoutShadowColor     = Color(0, 0, 0, 56);

        float columnWidth     = 286.0f;   // the stack's width as a column
        float railWidth       = 44.0f;    // and as a rail
        float railButtonSize  = 32.0f;
        float railIconSize    = 20.0f;
        float padding         = 6.0f;     // around the panels
        float panelGap        = 6.0f;     // between them
        float cornerRadius    = 6.0f;
        float contentPadding  = 8.0f;     // inside a panel, around its content
        float flyoutWidth     = 286.0f;
        FontStyle titleFont;              // bold, the body size
        float chipFontSize    = 10.0f;

        static ToolPanelStackStyle Default();
        // The theme's roles: the ground under the column and the rail, the
        // panel surface for the panels with the well for their headers,
        // hairlines, ink and muted ink, the ground for a count's chip; a
        // context panel framed in the accent over the soft accent with an
        // accent chip; the accent for the drop line, the focus colour for
        // the ring. Element roles "tool-panel.header" and
        // "tool-panel.context-header" override the two header strips.
        static ToolPanelStackStyle FromTheme(const UltraCanvasTheme& theme, const ToolPanelStackStyle& base);
        static ToolPanelStackStyle FromTheme(const UltraCanvasTheme& theme) { return FromTheme(theme, Default()); }
        static ToolPanelStackStyle FromTheme() { return FromTheme(UltraCanvasTheme::Current()); }
    };

    class UltraCanvasToolPanelStack : public UltraCanvasContainer {
    public:
        // ===== ACCESSIBILITY =====
        // A toolbar of groups: each panel is a group named by its title.
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Toolbar; }

        UltraCanvasToolPanelStack(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasToolPanelStack(const std::string& identifier, float w, float h)
                : UltraCanvasToolPanelStack(identifier, -1, -1, w, h) {}
        explicit UltraCanvasToolPanelStack(const std::string& identifier)
                : UltraCanvasToolPanelStack(identifier, -1, -1, -1, -1) {}
        ~UltraCanvasToolPanelStack() override;

        // ===== PANELS =====
        // Adds a panel at the bottom (a context panel among the context
        // panels at the top). The content is the panel's body; it is shown
        // inside the group box below the header. Returns the group box, for
        // a host that wants more of its own on the header. An id that is
        // already there returns nullptr.
        std::shared_ptr<UltraCanvasGroupBox> AddPanel(const std::string& id, const std::string& title,
                                                      std::shared_ptr<UltraCanvasContainer> content,
                                                      const ToolPanelOptions& options = ToolPanelOptions());
        bool RemovePanel(const std::string& id);
        bool HasPanel(const std::string& id) const;
        std::shared_ptr<UltraCanvasGroupBox> GetPanel(const std::string& id) const;
        std::shared_ptr<UltraCanvasContainer> GetPanelContent(const std::string& id) const;
        int GetPanelCount() const { return static_cast<int>(panels.size()); }

        void SetPanelTitle(const std::string& id, const std::string& title);
        void SetPanelChip(const std::string& id, const std::string& chip);

        void SetPanelOpen(const std::string& id, bool open);
        bool IsPanelOpen(const std::string& id) const;

        // A context panel comes (at the top, open) and goes with the
        // selection. The panel under the pointer - or, when the column is
        // scrolled, the first panel in view - stays where it is on the
        // screen. A panel that is not a context panel is always shown.
        void SetPanelShown(const std::string& id, bool shown);
        bool IsPanelShown(const std::string& id) const;
        bool IsContextPanel(const std::string& id) const;

        // ===== ORDER =====
        // Every panel's id, top to bottom: the context panels first (shown
        // or not), then the others. SetOrder puts the panels it names in
        // that order; an id it does not know is skipped, and a panel it
        // does not name keeps its default place (after the panel added
        // before it). The context panels stay above the others.
        std::vector<std::string> GetOrder() const;
        void SetOrder(const std::vector<std::string>& order);
        // Moves a panel up (delta < 0) or down past the shown panels of its
        // kind; false when it is at that end already. Move up / Move down
        // and Ctrl+Up / Ctrl+Down call this.
        bool MovePanel(const std::string& id, int delta);

        // ===== SAVED STATE =====
        ToolPanelStackState SaveState() const;
        // Order, open panels (of those the state names) and rail mode; an
        // empty state changes nothing.
        void RestoreState(const ToolPanelStackState& state);

        // ===== RAIL MODE =====
        void SetRailMode(bool rail);
        // The stack folds into the rail while its window is narrower than
        // this; 0 (the default) never.
        void SetAutoRailBelowWidth(float windowWidth);
        float GetAutoRailBelowWidth() const { return autoRailBelowWidth; }
        // Rail mode chosen, or the window narrow enough for it.
        bool IsRailMode() const { return railActive; }
        bool IsRailModeChosen() const { return railChosen; }
        // The flyout of one panel beside the rail (a click on its icon).
        bool OpenFlyout(const std::string& id);
        void CloseFlyout();
        bool IsFlyoutOpen() const;
        std::string GetFlyoutPanel() const { return flyoutPanel; }
        std::shared_ptr<UltraCanvasButton> GetRailButton(const std::string& id) const;

        // ===== KEYBOARD =====
        // The first shown panel's header (in rail mode, its icon) takes the
        // focus: what the host's F6 calls. False when there is none or the
        // window would not give it.
        bool FocusFirst();
        bool FocusPanel(const std::string& id);
        // The panel whose header has the focus; empty for none.
        std::string GetFocusedPanel() const;

        // ===== VALUES =====
        // Every tool row in the panels (UltraCanvasToolRows.h) reads its
        // value again: call it from the host's own Sync after the
        // selection or the document changed.
        void Sync();

        // ===== WIDTH / LOOK =====
        // The stack's width as a column (the rail has its own); the host's
        // splitter sets it, and it is the host's to keep.
        void SetColumnWidth(float width);
        float GetColumnWidth() const { return style.columnWidth; }
        void SetStyle(const ToolPanelStackStyle& newStyle);
        const ToolPanelStackStyle& GetStyle() const { return style; }
        void ApplyTheme(const UltraCanvasTheme& theme) override;

        // ===== DRAG (read by tests and the host) =====
        bool IsDraggingPanel() const { return !dragPanel.empty(); }
        std::string GetDraggedPanel() const { return dragPanel; }
        // Where a drop now would put the dragged panel, as an index of
        // GetOrder; -1 when no drag is under way.
        int GetDropIndex() const { return dropIndex; }

        // ===== CALLBACKS =====
        std::function<void(const std::vector<std::string>& order)> onOrderChanged;
        std::function<void(const std::string& id, bool open)> onPanelOpenChanged;
        std::function<void(bool rail)> onRailModeChanged;

        // ===== FRAMEWORK =====
        bool OnEvent(const UCEvent& event) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        friend class UltraCanvasToolPanelColumn;
        friend class UltraCanvasToolPanelFlyout;

        struct Panel {
            std::string id;
            std::string title;
            ToolPanelOptions options;
            std::shared_ptr<UltraCanvasGroupBox> box;
            std::shared_ptr<UltraCanvasContainer> content;
            std::shared_ptr<UltraCanvasButton> railButton;
            bool shown = true;
            bool open = true;
            int addIndex = 0;
        };

        Panel* Find(const std::string& id);
        const Panel* Find(const std::string& id) const;
        int IndexOf(const std::string& id) const;
        Panel* PanelOfBox(const UltraCanvasUIElement* element);
        void ApplyOrderToChildren();
        void ApplyPanelLook(Panel& panel);
        void ApplyRailButtonLook(Panel& panel, bool selected);
        void ApplyMode();
        void SetRailActive(bool rail);
        void NotifyOrderChanged();
        void ApplyOpen(Panel& panel);
        void OnBoxCollapsedChanged(const std::string& id, bool collapsed);

        // Scroll anchoring for a context panel coming or going.
        void RememberScrollAnchor(const std::string& changing);
        void ApplyScrollAnchor();     // after the column's next layout
        void RevealHeader(const Panel& panel);
        void RenderDragFeedback(IRenderContext* ctx);

        // Header dragging.
        void BeginDrag(const std::string& id);
        void UpdateDrag(const Point2Di& windowPos);
        void EndDrag(const Point2Di& windowPos, bool cancelled);
        // The drop target for the pointer: the panel the dragged one goes
        // above (empty: the end of its kind) and the index it then has.
        int DropTargetAt(const Point2Di& windowPos, std::string& before) const;
        float DropIndicatorY() const;   // in the column's content coordinates
        bool CanMove(const std::string& id, int delta) const;
        int NeighbourIndex(int index, int delta) const;   // the next shown panel of its kind

        // The header's menu.
        void ShowHeaderMenu(const std::string& id, const Point2Di& windowPos);

        // The flyout.
        void FlyoutClosed();
        Point2Di FlyoutPosition(const Panel& panel, const Size2Df& size) const;

        // Keys from a focused header or rail icon.
        bool HandleNavigationKey(const UCEvent& event);

        ToolPanelStackStyle style;
        std::vector<Panel> panels;    // top to bottom: context panels first
        int nextAddIndex = 0;

        std::shared_ptr<UltraCanvasToolPanelColumn> column;
        std::shared_ptr<UltraCanvasContainer> rail;
        std::shared_ptr<UltraCanvasToolPanelFlyout> flyout;
        std::shared_ptr<UltraCanvasMenu> headerMenu;
        std::string flyoutPanel;

        bool railChosen = false;
        bool railActive = false;
        float autoRailBelowWidth = 0.0f;

        std::string anchorPanel;      // empty: none pending
        float anchorOffset = 0.0f;
        std::string revealPanel;      // a header to scroll into view after the next layout
        std::string dropBefore;       // the drag's target: the panel it goes above; empty: the end

        std::string dragPanel;
        int dropIndex = -1;
        bool suppressOpenEvents = false;
    };

    inline std::shared_ptr<UltraCanvasToolPanelStack> CreateToolPanelStack(const std::string& identifier) {
        return std::make_shared<UltraCanvasToolPanelStack>(identifier);
    }
    inline std::shared_ptr<UltraCanvasToolPanelStack> CreateToolPanelStack(const std::string& identifier,
                                                                           float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasToolPanelStack>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
