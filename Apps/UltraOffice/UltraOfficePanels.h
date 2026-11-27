// Apps/UltraOffice/UltraOfficePanels.h
// The tool panels at the left of a tab (Docs/Research/UltraOfficeToolPanelsProposal.md,
// phases 3 to 5): an UltraCanvasToolPanelStack filled from the kind's tool
// table (UltraOfficeTools.h), panel by panel in ToolsByPanel's order, each
// tool by its kind:
//   - buttons and toggles in a wrapping row of icon tools
//     (UltraCanvasToolButtonRow); an action group's members all as buttons,
//     since a panel has the room the bar's one slot did not;
//   - a list as a dropdown - across the panel when it is the panel's first
//     tool (the paragraph style, the number format), else with its label -
//     and the font list as the font picker across the panel, with the size
//     beside it when the size follows it;
//   - a number as a spinner with its unit, a colour as a row of swatches,
//     both with their labels, edited in place;
//   - an element of the host's own (the pivot field list) across the panel.
// Each panel has its title, an icon for the rail (its main tool's), its
// dialog as the header's "…" button where the topic has one, and help text
// naming the dialog and the panel's shortcuts. A context panel (the table
// the caret is in, the selected picture, chart, shape, text or table, a
// pivot cell) is shown while its context holds.
//
// Sync reads every getter again and shows or hides the context panels; the
// table's onSynced calls it, so the panels follow the caret and the
// selection whenever the host brings its toolbars into step.
//
// Version: 0.1.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeTools.h"

#include "UltraCanvasToolPanelStack.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraOffice {

class ToolPanels {
public:
    // The stack `id`, filled from `table` (which outlives it).
    ToolPanels(const ToolTable& table, const std::string& id);
    ~ToolPanels();
    ToolPanels(const ToolPanels&) = delete;
    ToolPanels& operator=(const ToolPanels&) = delete;

    const std::shared_ptr<UltraCanvas::UltraCanvasToolPanelStack>& Stack() const { return stack_; }

    // The context panels shown as their contexts hold now, every control
    // showing its getter's value (no setter runs).
    void Sync();

    // The panels open when nothing was saved: documents Font and Paragraph,
    // spreadsheets Cell and Number, presentations Slide and Text.
    static std::vector<std::string> DefaultOpen(OfficeKind kind);

    // ===== FOR TESTS AND THE WINDOW =====
    // The control made for a tool (an entry's id, or an action group
    // member's); null for one shown as its panel's "…" button, or none.
    UltraCanvas::UltraCanvasUIElement* Widget(const std::string& id) const;
    // The tool behind a panel's "…" button; empty for none.
    std::string DialogOf(const std::string& panelKey) const;
    // The panel a tool is in; empty for none.
    std::string PanelOf(const std::string& id) const;

private:
    struct Placed {
        std::string id;
        std::string panel;
        UltraCanvas::UltraCanvasUIElement* widget = nullptr;   // raw: the stack owns it, and this the stack
    };
    struct Panel {
        std::string key;
        ToolContext context = ToolContext::AlwaysShown;
        std::string dialog;   // the entry behind "…"
    };

    void BuildPanel(const ToolPanelTools& group);
    void Place(const std::string& id, const std::string& panel, UltraCanvas::UltraCanvasUIElement* widget);

    const ToolTable& table_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolPanelStack> stack_;
    std::vector<Panel> panels_;
    std::vector<Placed> placed_;
    std::vector<const ToolEntry*> handedOver_;   // entries whose panelElement the panels hold
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace UltraOffice
