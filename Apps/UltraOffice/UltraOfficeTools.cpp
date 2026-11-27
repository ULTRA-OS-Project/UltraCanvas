// Apps/UltraOffice/UltraOfficeTools.cpp
// The tool table's panels and the toolbars built from it.
//
// Version: 0.1.0 - ToolBars (the command tabs' bars and the view group's), ToolsByPanel, the panels' order
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeTools.h"
#include "UltraOfficeToolbar.h"

#include "UltraCanvasDropdown.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasToolbar.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraOffice {

using namespace UltraCanvas;

const ToolEntry* ToolTable::Find(const std::string& id) const {
    for (const ToolEntry& entry : entries) {
        if (entry.id == id) return &entry;
        for (const ToolGroupMember& member : entry.members)
            if (member.id == id) return &entry;
    }
    return nullptr;
}

// ===== WRITING A TABLE =====

void ToolWriter::Tab(const std::string& key, const std::string& name, const std::string& barId) {
    table_.tabs.push_back({key, name, barId});
    tab_ = key;
    ++group_;
}

void ToolWriter::ViewGroup(const std::string& barId) {
    table_.viewBarId = barId;
    tab_ = kViewGroupTab;
    ++group_;
}

void ToolWriter::PanelsOnly() {
    tab_.clear();
    ++group_;
}

ToolEntry& ToolWriter::Row(ToolKind kind, const std::string& id, const std::string& icon, const std::string& label,
                           const std::string& tooltip, ToolPanel panel) {
    // The rows handed out stay where they are while the table is written.
    if (table_.entries.size() == table_.entries.capacity()) table_.entries.reserve(table_.entries.size() * 2);
    ToolEntry& entry = table_.entries.emplace_back();
    entry.id = id;
    entry.kind = kind;
    entry.icon = icon;
    entry.label = label;
    entry.tooltip = tooltip;
    entry.panel = panel;
    entry.tab = tab_;
    entry.barGroup = group_;
    return entry;
}

ToolEntry& ToolWriter::Button(const std::string& id, const std::string& icon, const std::string& label,
                              const std::string& tooltip, ToolPanel panel, std::function<void()> execute) {
    ToolEntry& entry = Row(ToolKind::Button, id, icon, label, tooltip, panel);
    entry.execute = std::move(execute);
    return entry;
}

ToolEntry& ToolWriter::Toggle(const std::string& id, const std::string& icon, const std::string& label,
                              const std::string& tooltip, ToolPanel panel, std::function<void()> execute,
                              std::function<bool()> isOn) {
    ToolEntry& entry = Row(ToolKind::Toggle, id, icon, label, tooltip, panel);
    entry.execute = std::move(execute);
    entry.isOn = std::move(isOn);
    return entry;
}

ToolEntry& ToolWriter::ActionGroup(const std::string& id, const std::string& label, ToolPanel panel,
                                   std::vector<ToolGroupMember> members) {
    ToolEntry& entry = Row(ToolKind::ActionGroup, id, members.empty() ? std::string() : members.front().icon, label,
                           label, panel);
    entry.members = std::move(members);
    return entry;
}

ToolEntry& ToolWriter::List(const std::string& id, const std::string& label, const std::string& tooltip,
                            ToolPanel panel, ToolListStyle style) {
    ToolEntry& entry = Row(ToolKind::List, id, std::string(), label, tooltip, panel);
    entry.list.style = style;
    return entry;
}

ToolEntry& ToolWriter::Number(const std::string& id, const std::string& label, const std::string& unit,
                              double minimum, double maximum, double step, int decimals, ToolPanel panel,
                              std::function<std::optional<double>()> get, std::function<void(double)> set) {
    ToolEntry& entry = Row(ToolKind::Value, id, std::string(), label, label, panel);
    entry.value.type = ToolValueType::Number;
    entry.value.unit = unit;
    entry.value.minimum = minimum;
    entry.value.maximum = maximum;
    entry.value.step = step;
    entry.value.decimals = decimals;
    entry.value.get = std::move(get);
    entry.value.set = std::move(set);
    return entry;
}

ToolEntry& ToolWriter::Colour(const std::string& id, const std::string& label, ToolPanel panel,
                              std::function<std::optional<Color>()> get, std::function<void(const Color&)> set) {
    ToolEntry& entry = Row(ToolKind::Value, id, std::string(), label, label, panel);
    entry.value.type = ToolValueType::Colour;
    entry.value.getColour = std::move(get);
    entry.value.setColour = std::move(set);
    return entry;
}

// ===== THE PANELS =====

namespace {

struct PanelName {
    ToolPanel panel;
    const char* key;
};

constexpr PanelName kPanelKeys[] = {
    {ToolPanel::Clipboard, "clipboard"}, {ToolPanel::Style, "style"},         {ToolPanel::Font, "font"},
    {ToolPanel::Paragraph, "paragraph"}, {ToolPanel::Lists, "lists"},         {ToolPanel::Insert, "insert"},
    {ToolPanel::Page, "page"},           {ToolPanel::Review, "review"},       {ToolPanel::Table, "table"},
    {ToolPanel::Picture, "picture"},     {ToolPanel::Chart, "chart"},         {ToolPanel::Cell, "cell"},
    {ToolPanel::Alignment, "alignment"}, {ToolPanel::Number, "number"},       {ToolPanel::Data, "data"},
    {ToolPanel::Pivot, "pivot"},         {ToolPanel::Slide, "slide"},         {ToolPanel::Text, "text"},
    {ToolPanel::Arrange, "arrange"},     {ToolPanel::Shape, "shape"},         {ToolPanel::Transitions, "transitions"},
    {ToolPanel::Show, "show"},           {ToolPanel::ViewGroup, "view"},
};

} // namespace

std::string PanelKey(ToolPanel panel) {
    for (const PanelName& name : kPanelKeys)
        if (name.panel == panel) return name.key;
    return std::string();
}

std::string PanelTitle(ToolPanel panel) {
    switch (panel) {
        case ToolPanel::Clipboard: return Tr("Clipboard");
        case ToolPanel::Style: return Tr("Style");
        case ToolPanel::Font: return Tr("Font");
        case ToolPanel::Paragraph: return Tr("Paragraph");
        case ToolPanel::Lists: return Tr("Lists");
        case ToolPanel::Insert: return Tr("Insert");
        case ToolPanel::Page: return Tr("Page");
        case ToolPanel::Review: return Tr("Review");
        case ToolPanel::Table: return Tr("Table");
        case ToolPanel::Picture: return Tr("Picture");
        case ToolPanel::Chart: return Tr("Chart");
        case ToolPanel::Cell: return Tr("Cell");
        case ToolPanel::Alignment: return Tr("Alignment");
        case ToolPanel::Number: return Tr("Number");
        case ToolPanel::Data: return Tr("Data");
        case ToolPanel::Pivot: return Tr("Pivot table");
        case ToolPanel::Slide: return Tr("Slide");
        case ToolPanel::Text: return Tr("Text");
        case ToolPanel::Arrange: return Tr("Arrange");
        case ToolPanel::Shape: return Tr("Shape");
        case ToolPanel::Transitions: return Tr("Transitions");
        case ToolPanel::Show: return Tr("Slide show");
        case ToolPanel::ViewGroup: return Tr("View");
    }
    return std::string();
}

std::vector<ToolPanel> DefaultPanelOrder(OfficeKind kind) {
    using P = ToolPanel;
    switch (kind) {
        case OfficeKind::Spreadsheet:
            return {P::Clipboard, P::Cell, P::Alignment, P::Number, P::Data, P::Insert, P::Page, P::Chart, P::Pivot};
        case OfficeKind::Presentation:
            return {P::Clipboard, P::Slide, P::Insert, P::Text, P::Arrange, P::Shape, P::Picture, P::Table, P::Chart,
                    P::Transitions, P::Show};
        default:
            return {P::Clipboard, P::Style, P::Font, P::Paragraph, P::Lists, P::Insert, P::Page, P::Review,
                    P::Table, P::Picture, P::Chart};
    }
}

std::vector<ToolPanelTools> ToolsByPanel(const ToolTable& table, const std::vector<std::string>& order) {
    const std::vector<ToolPanel> defaults = DefaultPanelOrder(table.kind);
    // The saved order first, then the kind's panels it leaves out, each in
    // its default place among those.
    std::vector<ToolPanel> panels;
    for (const std::string& key : order)
        for (ToolPanel panel : defaults)
            if (PanelKey(panel) == key && std::find(panels.begin(), panels.end(), panel) == panels.end())
                panels.push_back(panel);
    for (ToolPanel panel : defaults)
        if (std::find(panels.begin(), panels.end(), panel) == panels.end()) panels.push_back(panel);

    std::vector<ToolPanelTools> grouped;
    for (ToolPanel panel : panels) {
        ToolPanelTools group;
        group.panel = panel;
        group.key = PanelKey(panel);
        group.title = PanelTitle(panel);
        for (const ToolEntry& entry : table.entries)
            if (entry.panel == panel) group.tools.push_back(&entry);
        if (group.tools.empty()) continue;
        group.context = group.tools.front()->context;
        grouped.push_back(std::move(group));
    }
    return grouped;
}

// ===== THE TOOLBARS =====

void ToolBars::Add(UltraCanvasToolbar& bar, const ToolEntry& entry, UltraCanvasWindowBase* window) {
    const ToolEntry* row = &entry;   // the table outlives the bars
    const std::string text = entry.labelOnBar ? entry.label : std::string();
    Bound bound;
    bound.entry = row;
    switch (entry.kind) {
        case ToolKind::Button:
            bound.widget = AddTool(bar, entry.id, text, entry.icon, entry.tooltip, entry.execute).get();
            break;
        case ToolKind::Toggle:
            bound.widget = AddToggleTool(bar, entry.id, text, entry.icon, entry.tooltip, entry.execute).get();
            break;
        case ToolKind::ActionGroup: {
            std::vector<GroupTool> tools;
            for (const ToolGroupMember& member : entry.members)
                tools.push_back({member.id, member.icon, member.tooltip, member.execute});
            bound.widget = AddActionGroup(bar, entry.id, tools).get();
            break;
        }
        case ToolKind::List:
            switch (entry.list.style) {
                case ToolListStyle::Font: {
                    auto font = AddFontList(bar, entry.id, entry.list.settings, entry.list.documentFonts);
                    if (!entry.list.defaultFontEntry.empty()) font->SetDefaultFontEntry(entry.list.defaultFontEntry);
                    font->onFontChosen = [row](const FontPickerChoice& choice) {
                        if (row->list.chooseFont) row->list.chooseFont(choice);
                    };
                    bound.widget = font.get();
                    break;
                }
                case ToolListStyle::Percent:
                    bound.widget = AddPercentBox(bar, entry.id, entry.list.percents, entry.list.minimum, entry.list.maximum,
                                                 entry.tooltip, entry.list.askTitle, window, entry.list.choosePercent).get();
                    break;
                case ToolListStyle::Choice: {
                    if (entry.list.items) bound.items = entry.list.items();
                    auto box = bar.AddDropdownButton(entry.id, "", bound.items, nullptr);
                    box->SetTooltip(entry.tooltip);
                    if (!entry.list.placeholder.empty()) box->SetPlaceholderText(entry.list.placeholder);
                    if (entry.barWidth > 0) box->size.width = CSSLayout::Dimension::Px(static_cast<float>(entry.barWidth));
                    UltraCanvasDropdown* raw = box.get();
                    box->onSelectionChanged = [row, raw](int index, const DropdownItem&) {
                        if (index < 0) return;
                        if (row->list.choose) row->list.choose(index);
                        if (!row->list.keepsChoice) raw->SetSelectedIndex(-1, false);
                    };
                    bound.widget = raw;
                    break;
                }
            }
            break;
        case ToolKind::Custom:
            if (entry.addToBar) bound.widget = entry.addToBar(bar).get();
            break;
        case ToolKind::Value:
            return;   // the panels' alone
    }
    bound_.push_back(std::move(bound));
}

void ToolBars::Build(const ToolTable& table, UltraCanvasWindowBase* window) {
    tabs_.clear();
    bound_.clear();
    // Each bar's tools in the table's order, a separator between two groups.
    auto fill = [this, &table, window](UltraCanvasToolbar& bar, const std::string& tab) {
        bool first = true;
        int group = 0;
        for (const ToolEntry& entry : table.entries) {
            if (entry.tab != tab) continue;
            if (!first && entry.barGroup != group) bar.AddSeparator();
            first = false;
            group = entry.barGroup;
            Add(bar, entry, window);
        }
    };
    for (const ToolTab& tab : table.tabs) {
        auto bar = MakeToolbar(tab.barId);
        fill(*bar, tab.key);
        tabs_.push_back({tab.name, bar});
    }
    viewBar_.reset();
    if (!table.viewBarId.empty()) {
        // The view group stands apart from the command tab's tools beside it.
        viewBar_ = MakeViewToolbar(table.viewBarId);
        viewBar_->AddSeparator();
        fill(*viewBar_, kViewGroupTab);
    }
}

void ToolBars::SyncBound(Bound& bound) {
    const ToolEntry& entry = *bound.entry;
    UltraCanvasUIElement* widget = bound.widget;
    if (!widget) return;
    if (entry.isEnabled) widget->SetDisabled(!entry.isEnabled());
    switch (entry.kind) {
        case ToolKind::Toggle:
            if (entry.isOn)
                if (auto* button = dynamic_cast<UltraCanvasButton*>(widget)) button->SetPressed(entry.isOn());
            break;
        case ToolKind::List:
            if (entry.list.style == ToolListStyle::Font) {
                auto* font = dynamic_cast<UltraCanvasFontPicker*>(widget);
                if (!font || !entry.list.currentFont) break;
                if (const std::optional<std::string> family = entry.list.currentFont()) font->SetCurrentFont(*family);
                else font->SetMixed();
            } else if (entry.list.style == ToolListStyle::Percent) {
                auto* box = dynamic_cast<UltraCanvasDropdown*>(widget);
                if (box && entry.list.currentPercent) ShowPercent(*box, entry.list.percents, entry.list.currentPercent());
            } else if (auto* box = dynamic_cast<UltraCanvasDropdown*>(widget)) {
                const int index = entry.list.selected ? entry.list.selected() : -1;
                if (entry.list.items) {
                    std::vector<std::string> items = entry.list.items();
                    if (items != bound.items) {
                        bound.items = std::move(items);
                        box->ClearItems();
                        for (const std::string& item : bound.items) box->AddItem(item);
                        if (!entry.list.placeholder.empty()) box->SetPlaceholderText(entry.list.placeholder);
                    }
                }
                if (entry.list.selected) box->SetSelectedIndex(index, false);   // read out, not applied again
            }
            break;
        case ToolKind::Custom:
            if (entry.syncWidget) entry.syncWidget(*widget);
            break;
        default:
            break;
    }
}

void ToolBars::Sync(const std::function<bool(const ToolEntry&)>& which) {
    for (Bound& bound : bound_)
        if (!which || which(*bound.entry)) SyncBound(bound);
}

void ToolBars::SyncTab(const std::string& tab) {
    Sync([&tab](const ToolEntry& entry) { return entry.tab == tab; });
}

void ToolBars::SyncTool(const std::string& id) {
    Sync([&id](const ToolEntry& entry) { return entry.id == id; });
}

UltraCanvasUIElement* ToolBars::Widget(const std::string& id) const {
    for (const Bound& bound : bound_)
        if (bound.entry->id == id) return bound.widget;
    return nullptr;
}

} // namespace UltraOffice
