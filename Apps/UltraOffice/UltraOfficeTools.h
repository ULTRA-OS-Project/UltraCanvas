// Apps/UltraOffice/UltraOfficeTools.h
// The tool table: each kind of tab describes its tools once - what each one
// is called, looks like, does and shows - and both layouts are built from
// that description (Docs/Research/UltraOfficeToolPanelsProposal.md, §4 "One
// source of truth"): the command tabs with their toolbars (ToolBars, below)
// and the tool panels at the left (ToolsByPanel, for UltraCanvasToolPanelStack).
//
// A row of the table (ToolEntry) holds:
//   - a stable command id, which is also the id of the widget a layout makes
//     for it ("doc-bold");
//   - its icon (under media/icons), its label and its tooltip with the
//     shortcut;
//   - its kind: a button, a toggle, an action group (several tools in one
//     slot), a list (paragraph styles, fonts, sizes, number formats, layouts,
//     a percentage), a value (a number or a colour, for the panels) or a
//     custom widget;
//   - the panel it belongs to (proposal §3) and the command tab it is on
//     today, with its group on that tab's bar; a tool on no tab is shown in
//     the panels only (the values: line spacing, indents, a shape's size);
//   - when its panel applies (in a table, a picture selected ...);
//   - what it does (execute, choose, set) and what it shows (isOn, selected,
//     currentFont, get): the state getters the layouts call when the host
//     brings its tools into step.
//
// The view group (navigator, pages, fits, zoom) is in the table too, on the
// view tab (kViewGroupTab) and in no panel (ToolPanel::ViewGroup): it stays
// at the right end of the one top toolbar in both layouts.
//
// Version: 0.1.0 - the tool table, the toolbars built from it, the tools grouped by panel
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeHost.h"

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasFontPicker.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasAppSettings;
class UltraCanvasToolbar;
class UltraCanvasUIElement;
class UltraCanvasWindowBase;
}

namespace UltraOffice {

// The tab key of the view group: the bar at the top bar's right end.
inline constexpr const char* kViewGroupTab = "view";

enum class ToolKind {
    Button,        // does something
    Toggle,        // on or off (isOn)
    ActionGroup,   // several tools in one slot of a bar (members); a row of buttons in a panel
    List,          // a choice from a list (ToolList)
    Value,         // a number or a colour, edited in place (ToolValue); panels only
    Custom         // a widget of its own (addToBar, syncWidget)
};

// The panels of proposal §3, in no particular order (DefaultPanelOrder gives
// a kind's).
enum class ToolPanel {
    // documents
    Clipboard, Style, Font, Paragraph, Lists, Insert, Page, Review, Table, Picture, Chart,
    // spreadsheets (and Clipboard, Insert, Page, Chart)
    Cell, Alignment, Number, Data, Pivot,
    // presentations (and Clipboard, Insert, Picture, Table, Chart)
    Slide, Text, Arrange, Shape, Transitions, Show,
    // not a panel: the view group, at the right end of the top bar
    ViewGroup
};

// When a tool's panel applies. AlwaysShown is a panel that is always there; the
// others are context panels, shown while their selection holds.
enum class ToolContext {
    AlwaysShown,       // not "Always": X11 defines that
    InTable,           // a document's caret in a table
    PictureSelected,   // a picture selected (a document's, a slide's)
    ChartSelected,     // a chart selected (a sheet's, a slide's)
    ShapeSelected,     // one or more shapes of a slide selected
    TextSelected,      // a slide's text being edited, or a shape with text selected
    TableSelected,     // a slide's table selected, or one of its cells edited
    PivotCell          // the active cell is one of a pivot table's
};

// One tool of an action group.
struct ToolGroupMember {
    std::string id;
    std::string icon;       // under media/icons
    std::string label;
    std::string tooltip;
    std::function<void()> execute;
};

enum class ToolListStyle {
    Choice,    // a dropdown of `items`
    Font,      // the framework's font list (UltraCanvasFontPicker, AddFontList)
    Percent    // a percentage, with "Other..." (AddPercentBox)
};

struct ToolList {
    ToolListStyle style = ToolListStyle::Choice;
    // Choice: the items, asked when the list is made and again whenever the
    // host brings its tools into step (a list that grows - the sizes a
    // document uses, a deck's layouts - is made again when they change).
    std::function<std::vector<std::string>()> items;
    std::string placeholder;          // what the list reads while nothing is chosen
    // False for a list of actions (insert this shape, this table, this
    // colour): it goes back to its placeholder after each choice.
    bool keepsChoice = true;
    std::function<void(int index)> choose;
    // The item to show; -1 for none. Asked before `items`, so it may add the
    // value it shows to them.
    std::function<int()> selected;

    // Font: the window's settings (the recent fonts), the families the
    // document uses, the "Default font" row's text (empty: none).
    std::function<UltraCanvas::UltraCanvasAppSettings*()> settings;
    std::function<std::vector<std::string>()> documentFonts;
    std::string defaultFontEntry;
    std::function<void(const UltraCanvas::FontPickerChoice&)> chooseFont;
    // The family to show; "" for the default font, nullopt for several.
    std::function<std::optional<std::string>()> currentFont;

    // Percent: the choices, the range "Other..." takes, its question's title.
    std::vector<int> percents;
    int minimum = 1;
    int maximum = 100;
    std::string askTitle;
    std::function<void(int percent)> choosePercent;
    std::function<int()> currentPercent;
};

enum class ToolValueType { Number, Colour };

// A value a panel shows with its label and changes in place: a number with
// its unit and range, or a colour.
struct ToolValue {
    ToolValueType type = ToolValueType::Number;
    std::string unit;                 // what follows the number ("pt", "cm", "°")
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 1.0;
    int decimals = 0;
    // Number: the value now; nullopt when there is none to show (nothing
    // selected, the selection disagrees, the document's own default).
    std::function<std::optional<double>()> get;
    std::function<void(double)> set;
    // Colour: the same for a colour; nullopt for none or several.
    std::function<std::optional<UltraCanvas::Color>()> getColour;
    std::function<void(const UltraCanvas::Color&)> setColour;
};

struct ToolEntry {
    std::string id;                    // the command's id, and the id of its widget
    ToolKind kind = ToolKind::Button;
    std::string icon;                  // under media/icons; empty for none
    std::string label;                 // its name, in a panel and where a bar shows it
    std::string tooltip;               // with the shortcut ("Bold (Ctrl+B)")
    ToolPanel panel = ToolPanel::ViewGroup;
    ToolContext context = ToolContext::AlwaysShown;

    // The command tab it is on (its key in ToolTable::tabs, or
    // kViewGroupTab); empty: in the panels only. `barGroup` is its group on
    // that bar: a separator stands between two groups.
    std::string tab;
    int barGroup = 0;
    bool labelOnBar = false;           // the bar shows the label beside (or instead of) the icon
    int barWidth = 0;                  // a list's width on the bar in px; 0: its own

    std::function<void()> execute;     // Button, Toggle
    std::function<bool()> isOn;        // Toggle: whether it is on
    std::function<bool()> isEnabled;   // null: always

    std::vector<ToolGroupMember> members;   // ActionGroup
    ToolList list;                          // List
    ToolValue value;                        // Value

    // Custom: the widget, added to `bar` (and returned); brought into step.
    std::function<std::shared_ptr<UltraCanvas::UltraCanvasUIElement>(UltraCanvas::UltraCanvasToolbar& bar)> addToBar;
    std::function<void(UltraCanvas::UltraCanvasUIElement&)> syncWidget;
};

// A command tab: its key (ToolEntry::tab), the name on its button, the id of
// its bar.
struct ToolTab {
    std::string key;
    std::string name;
    std::string barId;
};

// A kind's tools. Written once by the host; the layouts keep pointers to its
// entries, so it is not changed once a layout is built from it.
struct ToolTable {
    OfficeKind kind = OfficeKind::Document;
    std::vector<ToolTab> tabs;         // the command tabs, in order
    std::string viewBarId;             // the view group's bar
    std::vector<ToolEntry> entries;    // in the order of the bars, then of the panels
    // Whether a context holds now (AlwaysShown always does).
    std::function<bool(ToolContext)> inContext;

    const ToolEntry* Find(const std::string& id) const;
    bool Applies(ToolContext context) const {
        return context == ToolContext::AlwaysShown || (inContext && inContext(context));
    }
};

// Writes a table row by row, in the bars' order: Tab / ViewGroup /
// PanelsOnly say where the rows that follow go, NextGroup puts a separator
// before the next one. Each call returns its row to finish (labelOnBar, a
// list's items ...) before the next call; the rows are kept in room reserved
// up front, so that reference holds until then.
class ToolWriter {
public:
    explicit ToolWriter(ToolTable& table) : table_(table) { table_.entries.reserve(kRoom); }

    void Tab(const std::string& key, const std::string& name, const std::string& barId);
    void ViewGroup(const std::string& barId);
    void PanelsOnly();
    void NextGroup() { ++group_; }

    ToolEntry& Button(const std::string& id, const std::string& icon, const std::string& label,
                      const std::string& tooltip, ToolPanel panel, std::function<void()> execute);
    ToolEntry& Toggle(const std::string& id, const std::string& icon, const std::string& label,
                      const std::string& tooltip, ToolPanel panel, std::function<void()> execute,
                      std::function<bool()> isOn);
    ToolEntry& ActionGroup(const std::string& id, const std::string& label, ToolPanel panel,
                           std::vector<ToolGroupMember> members);
    ToolEntry& List(const std::string& id, const std::string& label, const std::string& tooltip, ToolPanel panel,
                    ToolListStyle style = ToolListStyle::Choice);
    ToolEntry& Number(const std::string& id, const std::string& label, const std::string& unit, double minimum,
                      double maximum, double step, int decimals, ToolPanel panel,
                      std::function<std::optional<double>()> get, std::function<void(double)> set);
    ToolEntry& Colour(const std::string& id, const std::string& label, ToolPanel panel,
                      std::function<std::optional<UltraCanvas::Color>()> get,
                      std::function<void(const UltraCanvas::Color&)> set);

private:
    static constexpr size_t kRoom = 256;
    ToolEntry& Row(ToolKind kind, const std::string& id, const std::string& icon, const std::string& label,
                   const std::string& tooltip, ToolPanel panel);
    ToolTable& table_;
    std::string tab_;
    int group_ = 0;
};

// ===== THE PANELS =====

// A stable name for a panel ("clipboard"), for the saved order and the open
// panels; its title ("Clipboard").
std::string PanelKey(ToolPanel panel);
std::string PanelTitle(ToolPanel panel);

// A kind's panels in their default order (proposal §3): the fixed ones and
// the context ones, as listed there. The view group is in none.
std::vector<ToolPanel> DefaultPanelOrder(OfficeKind kind);

// The tools of one panel, in the table's order.
struct ToolPanelTools {
    ToolPanel panel = ToolPanel::Clipboard;
    std::string key;
    std::string title;
    ToolContext context = ToolContext::AlwaysShown;   // the panel's: its tools'
    std::vector<const ToolEntry*> tools;
};

// The table's tools grouped by panel, in `order` (panel keys, as saved; a
// kind's panel the order leaves out follows in its default place, and an
// unknown key is passed over) or, when it is empty, in the default order.
// A panel with no tools is left out, and so is the view group.
std::vector<ToolPanelTools> ToolsByPanel(const ToolTable& table, const std::vector<std::string>& order = {});

// ===== THE TOOLBARS =====

// The command tabs' bars and the view group's bar, built from a table: the
// same tools, order, icons, groups and widths as the bars once built by hand.
class ToolBars {
public:
    // Builds every bar; `window` is the dialogs' parent (a percent list's
    // "Other..."). The table outlives the bars.
    void Build(const ToolTable& table, UltraCanvas::UltraCanvasWindowBase* window);

    const std::vector<CommandTab>& Tabs() const { return tabs_; }
    const std::shared_ptr<UltraCanvas::UltraCanvasToolbar>& ViewBar() const { return viewBar_; }

    // Brings the widgets into step with their entries' getters: every one,
    // or those `which` picks.
    void Sync(const std::function<bool(const ToolEntry&)>& which = nullptr);
    // Those of one tab (a key of ToolTable::tabs, or kViewGroupTab).
    void SyncTab(const std::string& tab);
    // One tool's.
    void SyncTool(const std::string& id);

    // The widget made for a tool; null for one on no bar.
    UltraCanvas::UltraCanvasUIElement* Widget(const std::string& id) const;

private:
    struct Bound {
        const ToolEntry* entry = nullptr;
        UltraCanvas::UltraCanvasUIElement* widget = nullptr;   // raw: its bar owns it, and this owns the bars
        std::vector<std::string> items;                        // a Choice list's, as last shown
    };
    void Add(UltraCanvas::UltraCanvasToolbar& bar, const ToolEntry& entry, UltraCanvas::UltraCanvasWindowBase* window);
    static void SyncBound(Bound& bound);

    std::vector<CommandTab> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> viewBar_;
    std::vector<Bound> bound_;
};

} // namespace UltraOffice
