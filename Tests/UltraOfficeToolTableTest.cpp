// Tests/UltraOfficeToolTableTest.cpp
// UltraOffice's tool tables (Apps/UltraOffice/UltraOfficeTools.h), for each
// kind of tab - documents, spreadsheets, presentations:
//   - every tool of today's toolbars (each command tab's bar and the view
//     group, listed here as they were built by hand, separators and all) is
//     on the bars built from the table, in that order, and is in the table;
//     every action group's tools too;
//   - every entry of the table has a panel of its kind (the view group's
//     none), every id is unique, and grouped by panel each tool comes once,
//     the panels in their default order or in a saved one;
//   - the tools of a panel share its context, and the contexts follow the
//     selection (a document's table, a slide's shape);
//   - the value entries' getters and setters round-trip on a sample
//     document, sheet and deck: line spacing, spacing, indents, colours, a
//     table's borders; a cell's font, size, colours and borders; a shape's
//     place, size, turn, fill and outline, a transition's duration;
//   - a toggle's state shows on its bar (Sync).
//
// Opens a window (the hosts' elements are laid out in it): skips without a
// display, fails instead when ULTRACANVAS_TEST_DISPLAY_REQUIRED is set
// (build.yml's framework window tests). The settings folder is a temporary
// one (XDG_CONFIG_HOME).
//
// Usage: UltraOfficeToolTableTest
// Exit code is the number of failed checks.

#include "DisplayTestSupport.h"
#include "UltraOfficeHost.h"
#include "UltraOfficeTools.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasToolGroupButton.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasWindow.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraOffice;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

void Pump(UltraCanvasApplication& app, UltraCanvasWindow& window, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        app.RunOnce();
        window.UpdateAndRender();
    }
    window.UpdateAndRender();
}

// A bar's items as "a b | c d": ids, a separator as "|".
std::string BarLayout(const UltraCanvasToolbar& bar) {
    std::string layout;
    for (const auto& item : bar.GetItems()) {
        const bool separator = dynamic_cast<UltraCanvasSeparator*>(item.get()) != nullptr;
        layout += (layout.empty() ? "" : " ") + (separator ? std::string("|") : item->GetIdentifier());
    }
    return layout;
}

std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream in(text);
    for (std::string word; in >> word;) words.push_back(word);
    return words;
}

// Today's bars, as they were built by hand before the table: a command
// tab's name and its bar, then the view group (its leading separator stands
// it apart from the command tab's tools).
struct ExpectedBars {
    std::vector<std::pair<std::string, std::string>> tabs;
    std::string view;
    std::map<std::string, std::string> groups;   // an action group's tools
};

ExpectedBars DocumentBars() {
    return {{{"Home", "doc-cut doc-copy doc-paste | doc-style doc-font doc-size | doc-bold doc-italic doc-underline doc-strike | "
                      "doc-lists doc-align | doc-paragraph doc-styles"},
             {"Insert", "doc-table doc-picture doc-link | doc-pagebreak | doc-footnote doc-toc doc-caption doc-crossref"},
             {"Layout", "doc-pagesetup | doc-tableprops doc-pictureprops"},
             {"Review", "doc-comment doc-prevcomment doc-nextcomment doc-comments doc-showcomments | doc-track doc-prevchange "
                        "doc-nextchange doc-accept doc-reject doc-acceptall doc-rejectall"}},
            "| doc-navigator doc-pageview | doc-fitwidth doc-fitpage doc-fitshare doc-share | doc-zoomout doc-zoom doc-zoomin",
            {{"doc-lists", "doc-bullets doc-numbers doc-outdent doc-indent"},
             {"doc-align", "doc-left doc-centre doc-right doc-justify"}}};
}

ExpectedBars SpreadsheetBars() {
    return {{{"Home", "sheet-cut sheet-copy sheet-paste | sheet-bold sheet-italic | sheet-left sheet-centre sheet-right sheet-wrap | "
                      "sheet-format sheet-moredec sheet-lessdec"},
             {"Insert", "sheet-chart sheet-chart-type sheet-chart-properties sheet-chart-delete | sheet-pivot"},
             {"Data", "sheet-sortup sheet-sortdown sheet-filter | sheet-filldown sheet-fillright | sheet-sum | sheet-conditional "
                      "sheet-validation | sheet-pivot-refresh sheet-pivot-source sheet-pivot-fields sheet-pivot-delete"},
             {"Review", "sheet-comment sheet-comment-delete | sheet-link sheet-link-open sheet-link-remove"},
             {"Page", "sheet-print-area sheet-print-area-clear | sheet-landscape sheet-fit-width sheet-print-gridlines "
                      "sheet-print-headings"}},
            "| sheet-freeze sheet-unfreeze sheet-gridlines | sheet-zoomout sheet-zoom sheet-zoomin",
            {}};
}

ExpectedBars PresentationBars() {
    return {{{"Home", "slides-cut slides-copy slides-paste | slides-new slides-layout slides-duplicate slides-delete slides-hide | "
                      "slides-present"},
             {"Insert", "slides-textbox slides-shape slides-picture"},
             {"Text", "slides-bold slides-italic slides-underline slides-strike | slides-font slides-size slides-grow slides-shrink "
                      "slides-colour | slides-left slides-centre slides-right slides-justify | slides-bullets slides-numbers "
                      "slides-outdent slides-indent"},
             {"Table", "slides-table | slides-row-above slides-row-below slides-col-left slides-col-right | slides-row-delete "
                       "slides-col-delete"},
             {"Arrange", "slides-group slides-ungroup | slides-align | slides-front slides-forward slides-backward slides-back | "
                         "slides-shape-duplicate slides-shape-delete"},
             {"Format", "slides-crop slides-crop-reset | slides-chart-data"},
             {"Slide show", "slides-show-first slides-show-here | slides-transition slides-transition-all"}},
            "| slides-view-normal slides-view-sorter slides-view-notes | slides-fit slides-zoomout slides-zoom slides-zoomin",
            {{"slides-align", "slides-align-left slides-align-centre slides-align-right slides-align-top slides-align-middle "
                              "slides-align-bottom slides-distribute-h slides-distribute-v"}}};
}

// Every tool of today's bars is on the bars built from the table, in order,
// and is the table's - on that tab.
void CheckBars(OfficeHost& host, const std::string& kind, const ExpectedBars& expected) {
    const ToolTable& table = host.GetTools();
    const auto& tabs = host.GetCommandTabs();
    Check(tabs.size() == expected.tabs.size(), kind + ": " + std::to_string(expected.tabs.size()) + " command tabs (" +
                                                   std::to_string(tabs.size()) + ")");
    size_t onBars = 0;
    auto checkBar = [&](const UltraCanvasToolbar* bar, const std::string& name, const std::string& key, const std::string& want) {
        const std::string got = bar ? BarLayout(*bar) : std::string("(no bar)");
        Check(got == want, kind + ", " + name + ": the bar's tools as before" + (got == want ? "" : "\n      got:  " + got + "\n      want: " + want));
        bool all = true;
        std::string missing;
        for (const std::string& id : Words(want)) {
            if (id == "|") continue;
            ++onBars;
            const ToolEntry* entry = table.Find(id);
            if (!entry || entry->id != id || entry->tab != key) { all = false; missing += " " + id; }
        }
        Check(all, kind + ", " + name + ": every tool is in the table, on this tab" + (all ? "" : " (not:" + missing + ")"));
    };
    for (size_t i = 0; i < expected.tabs.size() && i < tabs.size(); ++i) {
        Check(tabs[i].name == expected.tabs[i].first, kind + ": command tab " + std::to_string(i) + " is " + expected.tabs[i].first);
        const std::string key = i < table.tabs.size() ? table.tabs[i].key : std::string();
        checkBar(tabs[i].toolbar.get(), expected.tabs[i].first, key, expected.tabs[i].second);
    }
    checkBar(host.GetViewToolbar().get(), "the view group", kViewGroupTab, expected.view);

    // The action groups' tools: the table's members, the slot's items.
    for (const auto& [groupId, members] : expected.groups) {
        const ToolEntry* entry = table.Find(groupId);
        std::string got;
        for (const ToolGroupMember& member : entry ? entry->members : std::vector<ToolGroupMember>())
            got += (got.empty() ? "" : " ") + member.id;
        Check(entry && entry->kind == ToolKind::ActionGroup && got == members, kind + ": the group " + groupId + " holds " + members);
        bool found = true;
        for (const std::string& id : Words(members)) found = found && table.Find(id) == entry;
        Check(found, kind + ": ...each found in the table by its id");
    }

    // The other way round: a tool the table puts on a tab is on its bar.
    size_t onTabs = 0;
    for (const ToolEntry& entry : table.entries)
        if (!entry.tab.empty() && entry.kind != ToolKind::Value) ++onTabs;
    Check(onTabs == onBars, kind + ": the table puts as many tools on the bars as they hold (" + std::to_string(onTabs) + " and " +
                                std::to_string(onBars) + ")");
}

// Every entry has a panel of its kind; ids are unique; grouped by panel,
// each tool comes once, in the default order or a saved one.
void CheckPanels(const ToolTable& table, const std::string& kind, std::set<std::string>& everyId) {
    const std::vector<ToolPanel> order = DefaultPanelOrder(table.kind);
    bool panels = true, unique = true;
    std::string wrong, twice;
    for (const ToolEntry& entry : table.entries) {
        const bool view = entry.tab == kViewGroupTab;
        const bool known = std::find(order.begin(), order.end(), entry.panel) != order.end();
        if (view ? entry.panel != ToolPanel::ViewGroup : !known) { panels = false; wrong += " " + entry.id; }
        std::vector<std::string> ids = {entry.id};
        for (const ToolGroupMember& member : entry.members) ids.push_back(member.id);
        for (const std::string& id : ids)
            if (id.empty() || !everyId.insert(id).second) { unique = false; twice += " " + id; }
        if (entry.label.empty() || entry.tooltip.empty()) { panels = false; wrong += " " + entry.id + "(no label)"; }
    }
    Check(panels, kind + ": every entry has a label, a tooltip and a panel of its kind; the view group's none" +
                      (panels ? "" : " (not:" + wrong + ")"));
    Check(unique, kind + ": every id is unique" + (unique ? "" : " (twice:" + twice + ")"));

    const std::vector<ToolPanelTools> grouped = ToolsByPanel(table);
    size_t tools = 0, lastPlace = 0;
    bool inOrder = true, shared = true;
    std::string titles;
    for (const ToolPanelTools& group : grouped) {
        const size_t place = static_cast<size_t>(std::find(order.begin(), order.end(), group.panel) - order.begin());
        inOrder = inOrder && place < order.size() && (tools == 0 || place > lastPlace);
        lastPlace = place;
        tools += group.tools.size();
        titles += (titles.empty() ? "" : ", ") + group.title + " " + std::to_string(group.tools.size());
        for (const ToolEntry* entry : group.tools) shared = shared && entry->context == group.context && entry->panel == group.panel;
        Check(!group.key.empty() && !group.title.empty(), kind + ": the panel " + group.title + " has a key (" + group.key + ")");
    }
    size_t inPanels = 0;
    for (const ToolEntry& entry : table.entries)
        if (entry.panel != ToolPanel::ViewGroup) ++inPanels;
    Check(tools == inPanels, kind + ": grouped by panel, every tool but the view group's once (" + std::to_string(tools) + " of " +
                                 std::to_string(inPanels) + "): " + titles);
    Check(inOrder, kind + ": the panels in their default order");
    Check(shared, kind + ": a panel's tools share its context");

    // A saved order: its panels first, the others after in their order.
    if (grouped.size() >= 3) {
        const std::string last = grouped.back().key, second = grouped[1].key;
        const std::vector<ToolPanelTools> moved = ToolsByPanel(table, {last, "no-such-panel", second});
        Check(moved.size() == grouped.size() && moved[0].key == last && moved[1].key == second && moved[2].key == grouped[0].key,
              kind + ": a saved order puts its panels first (" + last + ", " + second + "), the rest after");
    }
}

const ToolEntry* Entry(OfficeHost& host, const std::string& id) {
    const ToolEntry* entry = host.GetTools().Find(id);
    if (!entry || entry->id != id) Check(false, "the entry " + id);
    return entry && entry->id == id ? entry : nullptr;
}

// A number set through the entry reads back.
void RoundTrip(OfficeHost& host, const std::string& id, double value, double tolerance = 1e-3) {
    const ToolEntry* entry = Entry(host, id);
    if (!entry) return;
    if (entry->kind != ToolKind::Value || entry->value.type != ToolValueType::Number || !entry->value.get || !entry->value.set) {
        Check(false, id + " is a number value with a getter and a setter");
        return;
    }
    entry->value.set(value);
    const std::optional<double> back = entry->value.get();
    Check(back && std::fabs(*back - value) <= tolerance,
          id + ": " + std::to_string(value) + " set, " + (back ? std::to_string(*back) : std::string("none")) + " read back");
}

void RoundTripColour(OfficeHost& host, const std::string& id, const Color& colour) {
    const ToolEntry* entry = Entry(host, id);
    if (!entry) return;
    if (entry->kind != ToolKind::Value || entry->value.type != ToolValueType::Colour || !entry->value.getColour ||
        !entry->value.setColour) {
        Check(false, id + " is a colour value with a getter and a setter");
        return;
    }
    entry->value.setColour(colour);
    const std::optional<Color> back = entry->value.getColour();
    Check(back && back->r == colour.r && back->g == colour.g && back->b == colour.b,
          id + ": " + colour.ToHexString() + " set, " + (back ? back->ToHexString() : std::string("none")) + " read back");
}

bool IsOn(OfficeHost& host, const std::string& id) {
    const ToolEntry* entry = Entry(host, id);
    return entry && entry->isOn && entry->isOn();
}

UltraCanvasButton* BarButton(OfficeHost& host, const std::string& id) {
    for (const auto& tab : host.GetCommandTabs())
        if (auto* button = dynamic_cast<UltraCanvasButton*>(tab.toolbar->FindChildById(id))) return button;
    return nullptr;
}

} // namespace

int main() {
    if (!std::getenv("DISPLAY") || !*std::getenv("DISPLAY")) {
        if (DisplayTest::DisplayRequired()) {
            std::printf("FAIL: no DISPLAY, and ULTRACANVAS_TEST_DISPLAY_REQUIRED is set\n");
            return 1;
        }
        std::printf("SKIP: no DISPLAY\n");
        return 0;
    }
    std::error_code ec;
    const fs::path settingsFolder = fs::temp_directory_path() / "uc-ultraoffice-tool-table-test";
    fs::remove_all(settingsFolder, ec);
    fs::create_directories(settingsFolder, ec);
    setenv("XDG_CONFIG_HOME", PathToUtf8(settingsFolder).c_str(), 1);   // path-string-ok: test's own ASCII temp folder

    UltraCanvasApplication app;
    if (!app.Initialize("UltraOfficeToolTableTest")) {
        std::printf("%s: the application could not be initialised\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    auto window = std::make_shared<UltraCanvasWindow>();
    WindowConfig config;
    config.title = "UltraOfficeToolTableTest";
    config.width = 1100;
    config.height = 760;
    if (!window->Create(config)) {
        std::printf("%s: the window could not be created\n", DisplayTest::DisplayRequired() ? "FAIL" : "SKIP");
        return DisplayTest::DisplayRequired() ? 1 : 0;
    }
    window->Show();
    const TimerId tick = app.StartTimer(40, true, [](TimerId) {});
    // The host's element fills the window while its values are tried.
    auto show = [&](OfficeHost& host) {
        window->ClearChildren();
        auto content = host.GetContent();
        content->SetElementSize(CSSLayout::Dimension::Pct(100), CSSLayout::Dimension::Pct(100));
        window->AddChild(content);
        Pump(app, *window, 200);
    };
    std::set<std::string> everyId;
    std::map<std::string, size_t> counts;

    std::printf("=== Documents ===\n");
    auto document = CreateDocumentHost(window.get());
    show(*document);
    CheckBars(*document, "document", DocumentBars());
    CheckPanels(document->GetTools(), "document", everyId);
    if (auto* edit = dynamic_cast<UltraCanvasRichTextEdit*>(
            dynamic_cast<UltraCanvasContainer*>(document->GetContent().get())->FindChildById("office-document"))) {
        edit->InsertText("The quick brown fox jumps over the lazy dog.");
        edit->SelectAll();
        RoundTrip(*document, "doc-linespacing", 1.5);
        RoundTrip(*document, "doc-spacebefore", 6.0);
        RoundTrip(*document, "doc-spaceafter", 12.0);
        RoundTrip(*document, "doc-indentleft", 18.0);
        RoundTrip(*document, "doc-indentright", 9.0);
        RoundTrip(*document, "doc-indentfirst", -12.0);
        RoundTrip(*document, "doc-linespacing", 1.0);
        RoundTripColour(*document, "doc-textcolour", Color(0xC0, 0x00, 0x00));
        RoundTripColour(*document, "doc-highlight", Color(0xFF, 0xEB, 0x3B));
        const bool superBefore = IsOn(*document, "doc-superscript");
        if (const ToolEntry* super = Entry(*document, "doc-superscript")) super->execute();
        Check(IsOn(*document, "doc-superscript") != superBefore, "doc-superscript: its toggle turns it on");
        if (const ToolEntry* super = Entry(*document, "doc-superscript")) super->execute();
        // A toggle's state on its bar.
        edit->SelectAll();
        if (const ToolEntry* bold = Entry(*document, "doc-bold")) bold->execute();
        auto* boldButton = BarButton(*document, "doc-bold");
        Check(IsOn(*document, "doc-bold") && boldButton && boldButton->IsPressed(), "doc-bold: on, and pressed on the Home bar");
        // A table: its context and its values.
        const ToolTable& tools = document->GetTools();
        Check(!tools.Applies(ToolContext::InTable), "the Table panel does not apply outside a table");
        edit->SelectAll();
        if (const ToolEntry* table = Entry(*document, "doc-table")) table->execute();
        Check(tools.Applies(ToolContext::InTable), "...and applies with the caret in the table Insert made");
        RoundTrip(*document, "doc-table-border", 1.5);
        const bool header = IsOn(*document, "doc-table-header");
        if (const ToolEntry* toggle = Entry(*document, "doc-table-header")) toggle->execute();
        Check(IsOn(*document, "doc-table-header") != header, "doc-table-header: its toggle changes the header row");
        Check(!tools.Applies(ToolContext::PictureSelected), "the Picture panel does not apply with no picture selected");
    } else {
        Check(false, "the document's editor");
    }
    for (const ToolPanelTools& group : ToolsByPanel(document->GetTools())) counts["document " + group.title] = group.tools.size();

    std::printf("\n=== Spreadsheets ===\n");
    auto spreadsheet = CreateSpreadsheetHost(window.get());
    spreadsheet->NewBlank();
    show(*spreadsheet);
    CheckBars(*spreadsheet, "spreadsheet", SpreadsheetBars());
    CheckPanels(spreadsheet->GetTools(), "spreadsheet", everyId);
    if (auto* sheet = dynamic_cast<UltraCanvasSpreadsheet*>(
            dynamic_cast<UltraCanvasContainer*>(spreadsheet->GetContent().get())->FindChildById("office-spreadsheet"))) {
        sheet->Select(CellRange(CellAddress(1, 1)));   // B2: the selection the formats go to, and the active cell
        sheet->SetCellValue(1, 1, 42.0);
        RoundTrip(*spreadsheet, "sheet-fontsize", 15.0);
        RoundTripColour(*spreadsheet, "sheet-textcolour", Color(0x44, 0x72, 0xC4));
        RoundTripColour(*spreadsheet, "sheet-fillcolour", Color(0xFF, 0xF2, 0xCC));
        if (const ToolEntry* borders = Entry(*spreadsheet, "sheet-borders")) {
            bool all = borders->list.choose && borders->list.selected;
            for (int choice : {1, 2, 0}) {
                if (!all) break;
                borders->list.choose(choice);
                all = borders->list.selected() == choice;
            }
            Check(all, "sheet-borders: all, bottom and none chosen read back");
        }
        if (const ToolEntry* font = Entry(*spreadsheet, "sheet-font")) {
            FontPickerChoice choice;
            choice.family = "DejaVu Sans";
            if (font->list.chooseFont) font->list.chooseFont(choice);
            const std::optional<std::string> back = font->list.currentFont ? font->list.currentFont() : std::nullopt;
            Check(back && *back == "DejaVu Sans", "sheet-font: DejaVu Sans chosen, " + back.value_or("none") + " read back");
        }
        if (const ToolEntry* bold = Entry(*spreadsheet, "sheet-bold")) bold->execute();
        auto* boldButton = BarButton(*spreadsheet, "sheet-bold");
        Check(IsOn(*spreadsheet, "sheet-bold") && boldButton && boldButton->IsPressed(), "sheet-bold: on, and pressed on the Home bar");
        Check(!spreadsheet->GetTools().Applies(ToolContext::ChartSelected), "the Chart panel does not apply with no chart selected");
        Check(!spreadsheet->GetTools().Applies(ToolContext::PivotCell), "the Pivot panel does not apply outside a pivot table");
    } else {
        Check(false, "the spreadsheet's grid");
    }
    for (const ToolPanelTools& group : ToolsByPanel(spreadsheet->GetTools())) counts["spreadsheet " + group.title] = group.tools.size();

    std::printf("\n=== Presentations ===\n");
    auto presentation = CreatePresentationHost(window.get());
    presentation->NewBlank();
    show(*presentation);
    CheckBars(*presentation, "presentation", PresentationBars());
    CheckPanels(presentation->GetTools(), "presentation", everyId);
    if (auto* slides = dynamic_cast<UltraCanvasSlideEdit*>(
            dynamic_cast<UltraCanvasContainer*>(presentation->GetContent().get())->FindChildById("office-presentation"))) {
        const ToolTable& tools = presentation->GetTools();
        slides->Editor().ClearSelection();
        Check(!tools.Applies(ToolContext::ShapeSelected), "the Shape panel does not apply with nothing selected");
        const ToolEntry* x = Entry(*presentation, "slides-x");
        Check(x && !x->value.get(), "...and slides-x reads none");
        slides->Editor().SetShapeText(0, "Quarterly results");
        slides->Editor().Select({0});
        Check(tools.Applies(ToolContext::ShapeSelected) && tools.Applies(ToolContext::TextSelected),
              "the Shape and Text panels apply with the title selected");
        RoundTrip(*presentation, "slides-x", 2.5);
        RoundTrip(*presentation, "slides-y", 1.25);
        RoundTrip(*presentation, "slides-width", 20.0);
        RoundTrip(*presentation, "slides-height", 4.0);
        RoundTrip(*presentation, "slides-rotation", 30.0);
        RoundTrip(*presentation, "slides-rotation", 0.0);
        RoundTripColour(*presentation, "slides-fill", Color(0x70, 0xAD, 0x47));
        RoundTripColour(*presentation, "slides-line", Color(0x40, 0x40, 0x40));
        RoundTrip(*presentation, "slides-line-width", 2.25);
        Check(slides->Editor().GetSelection() == std::vector<int>{0}, "the title stays selected through each change");
        if (const ToolEntry* transition = Entry(*presentation, "slides-transition")) {
            if (transition->list.choose) transition->list.choose(1);   // Fade
            Check(transition->list.selected && transition->list.selected() == 1, "slides-transition: Fade chosen reads back");
        }
        RoundTrip(*presentation, "slides-transition-duration", 1.5);
        if (const ToolEntry* bold = Entry(*presentation, "slides-bold")) bold->execute();
        Pump(app, *window, 100);
        auto* boldButton = BarButton(*presentation, "slides-bold");
        Check(IsOn(*presentation, "slides-bold") && boldButton && boldButton->IsPressed(),
              "slides-bold: the title's text bold, and pressed on the Text bar");
        Check(!tools.Applies(ToolContext::PictureSelected) && !tools.Applies(ToolContext::TableSelected),
              "the Picture and Table panels do not apply to a title");
    } else {
        Check(false, "the presentation's slide editor");
    }
    for (const ToolPanelTools& group : ToolsByPanel(presentation->GetTools())) counts["presentation " + group.title] = group.tools.size();

    std::printf("\n=== The tools per panel ===\n");
    for (const auto& [panel, count] : counts) std::printf("  %s: %zu\n", panel.c_str(), count);
    std::printf("  entries: document %zu, spreadsheet %zu, presentation %zu\n", document->GetTools().entries.size(),
                spreadsheet->GetTools().entries.size(), presentation->GetTools().entries.size());

    window->ClearChildren();
    presentation.reset();
    spreadsheet.reset();
    document.reset();
    app.StopTimer(tick);
    window->Close();
    fs::remove_all(settingsFolder, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
