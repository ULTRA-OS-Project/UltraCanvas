// Apps/UltraOffice/UltraOfficePanels.cpp
// The tool panels built from a tool table (UltraOfficePanels.h).
//
// Version: 0.1.0
// Last Modified: 2026-11-27
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficePanels.h"
#include "UltraOfficeToolbar.h"   // FontListSettings

#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFontPicker.h"
#include "UltraCanvasFormLayout.h"   // DisableScrollbars
#include "UltraCanvasSpinner.h"
#include "UltraCanvasToolRows.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

constexpr float kRowHeight = 24.0f;
constexpr float kFontRowHeight = 26.0f;
constexpr float kSwatchHeight = 24.0f;
constexpr float kSizeBoxWidth = 64.0f;     // the font size beside the font
constexpr float kSizeSpinnerWidth = 84.0f;

// The tool that is a panel's dialog, shown as the header's "…" button
// (proposal §2): Paragraph…, Styles…, Page setup…, Table properties…,
// Picture…, Chart properties…, the comments list.
const char* const kDialogTools[] = {"doc-paragraph", "doc-styles", "doc-pagesetup", "doc-tableprops",
                                    "doc-pictureprops", "doc-comments", "sheet-chart-properties"};

// A panel's rail icon: the main tool's where the first one's would mislead
// (the clipboard's is Paste, the review's a comment), and where none of its
// tools has one.
struct PanelIcon {
    const char* key;
    const char* icon;
};
const PanelIcon kMainToolIcons[] = {{"clipboard", "menu/paste.svg"}, {"review", "clipboard/edit.svg"}};
const PanelIcon kFallbackIcons[] = {
    {"number", "menu/format.svg"},     {"page", "menu/print.svg"},
    {"slide", "office/presentation.svg"}, {"transitions", "menu/wand.svg"}, {"shape", "curves.svg"},
    {"table", "table.svg"},           {"pivot", "table.svg"},            {"chart", "chart.svg"},
};

// The colours a panel offers: the inks, then the colours of a palette.
std::vector<Color> PanelColours() {
    return {Color(0, 0, 0, 255),       Color(64, 64, 64, 255),    Color(128, 128, 128, 255), Color(255, 255, 255, 255),
            Color(192, 0, 0, 255),     Color(237, 125, 49, 255),  Color(255, 192, 0, 255),   Color(255, 255, 0, 255),
            Color(112, 173, 71, 255),  Color(68, 114, 196, 255),  Color(0, 176, 240, 255),   Color(112, 48, 160, 255)};
}

// What a context panel's chip says.
std::string ContextChip(ToolContext context) {
    switch (context) {
        case ToolContext::InTable: return Tr("in a table");
        case ToolContext::PivotCell: return Tr("pivot cell");
        case ToolContext::AlwaysShown: return std::string();
        default: return Tr("selected");   // a picture, a chart, shapes, text, a table
    }
}

bool IsDialogTool(const std::string& id) {
    for (const char* dialog : kDialogTools)
        if (id == dialog) return true;
    return false;
}

// "Ctrl+B" out of "Bold (Ctrl+B)"; empty where the tooltip names no key.
std::string ShortcutOf(const std::string& tooltip) {
    if (tooltip.empty() || tooltip.back() != ')') return std::string();
    const size_t open = tooltip.rfind('(');
    if (open == std::string::npos) return std::string();
    const std::string inside = tooltip.substr(open + 1, tooltip.size() - open - 2);
    if (inside.empty() || inside.find(' ') != std::string::npos) return std::string();
    return inside;
}

bool IsSizeTool(const ToolEntry* entry) {
    if (!entry) return false;
    const bool list = entry->kind == ToolKind::List && entry->list.style == ToolListStyle::Choice;
    const bool number = entry->kind == ToolKind::Value && entry->value.type == ToolValueType::Number;
    return list || number;
}

// Shows a number, or nothing (the spinner blank) when there is none.
void ShowNumber(UltraCanvasSpinner& spinner, const std::optional<double>& value) {
    if (value) {
        spinner.SetFormatter(nullptr);
        spinner.SetValue(*value);
    } else {
        // Blank, and stepping from the bottom of the range: any value typed
        // or stepped to is a change.
        spinner.SetFormatter([](double) { return std::string(); });
        spinner.SetValue(spinner.GetMinValue());
    }
    spinner.RequestRedraw();
}

void ShowColour(UltraCanvasColorSwatchBar& bar, const std::optional<Color>& colour) {
    if (colour) bar.SelectColor(*colour, false);
    else bar.ClearSelection();
}

std::shared_ptr<UltraCanvasSpinner> MakeSpinner(const ToolEntry& entry, float width) {
    const ToolValue& v = entry.value;
    auto spinner = v.decimals > 0
        ? CreateDecimalSpinner(entry.id, 0, 0, width, kRowHeight, v.minimum, v.maximum, v.minimum, v.step, v.decimals)
        : CreateIntSpinner(entry.id, 0, 0, width, kRowHeight, static_cast<int>(v.minimum), static_cast<int>(v.maximum),
                           static_cast<int>(v.minimum), static_cast<int>(std::max(1.0, v.step)));
    spinner->SetSnapToStep(false);   // the document's own value, not the nearest step
    if (!v.unit.empty()) spinner->SetSuffix(" " + v.unit);
    spinner->SetPlaceholder("-");
    spinner->SetTooltip(entry.tooltip);
    spinner->SetAccessibleName(entry.label);
    if (width > 0.0f) {
        spinner->size.width = CSSLayout::Dimension::Px(width);
        spinner->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    }
    return spinner;
}

// A choice list whose items may change (the sizes a document uses, a deck's
// layouts): read again on every Sync.
struct ChoiceList {
    std::shared_ptr<UltraCanvasDropdown> box;
    std::function<void()> sync;
};

ChoiceList MakeChoice(const ToolEntry& entry, UltraCanvasToolValueRows* rows, float width) {
    ChoiceList made;
    made.box = CreateDropdown(entry.id, 0, 0, width, kRowHeight);
    auto shown = std::make_shared<std::vector<std::string>>();
    if (entry.list.items) *shown = entry.list.items();
    for (const std::string& item : *shown) made.box->AddItem(item);
    if (!entry.list.placeholder.empty()) made.box->SetPlaceholderText(entry.list.placeholder);
    made.box->SetTooltip(entry.tooltip);
    made.box->SetAccessibleName(entry.label);
    if (width > 0.0f) {
        made.box->size.width = CSSLayout::Dimension::Px(width);
        made.box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    }
    const ToolEntry* row = &entry;   // the table outlives the panels
    UltraCanvasDropdown* raw = made.box.get();
    made.box->onSelectionChanged = [row, raw, rows](int index, const DropdownItem&) {
        if (index < 0 || (rows && rows->IsSyncing())) return;
        if (row->list.choose) row->list.choose(index);
        if (!row->list.keepsChoice) raw->SetSelectedIndex(-1, false);
    };
    made.sync = [row, raw, shown]() {
        const int index = row->list.selected ? row->list.selected() : -1;   // asked first: it may add its value
        if (row->list.items) {
            std::vector<std::string> items = row->list.items();
            if (items != *shown) {
                *shown = std::move(items);
                raw->ClearItems();
                for (const std::string& item : *shown) raw->AddItem(item);
                if (!row->list.placeholder.empty()) raw->SetPlaceholderText(row->list.placeholder);
            }
        }
        if (row->list.selected) raw->SetSelectedIndex(index, false);
    };
    made.sync();
    return made;
}

} // namespace

// ===== BUILDING =====

ToolPanels::ToolPanels(const ToolTable& table, const std::string& id) : table_(table) {
    stack_ = CreateToolPanelStack(id);
    stack_->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    const std::vector<std::string> open = DefaultOpen(table.kind);
    for (const ToolPanelTools& group : ToolsByPanel(table)) BuildPanel(group);
    for (const Panel& panel : panels_)
        if (!stack_->IsContextPanel(panel.key))
            stack_->SetPanelOpen(panel.key, std::find(open.begin(), open.end(), panel.key) != open.end());
    // The panels follow whenever the host brings its tools into step.
    table_.onSynced = [this, alive = std::weak_ptr<bool>(alive_)]() {
        if (!alive.expired()) Sync();
    };
    Sync();
}

ToolPanels::~ToolPanels() {
    table_.onSynced = nullptr;
    // The host's own elements go back where it keeps them.
    for (const ToolEntry* entry : handedOver_) {
        for (const Placed& placed : placed_) {
            if (placed.id != entry->id || !placed.widget) continue;
            if (auto* parent = placed.widget->GetParentContainer()) {
                for (const auto& child : parent->GetChildren()) {
                    if (child.get() != placed.widget) continue;
                    parent->RemoveChild(child);
                    break;
                }
            }
        }
        if (entry->panelElement) entry->panelElement(false);
    }
}

std::vector<std::string> ToolPanels::DefaultOpen(OfficeKind kind) {
    switch (kind) {
        case OfficeKind::Spreadsheet: return {PanelKey(ToolPanel::Cell), PanelKey(ToolPanel::Number)};
        case OfficeKind::Presentation: return {PanelKey(ToolPanel::Slide), PanelKey(ToolPanel::Text)};
        default: return {PanelKey(ToolPanel::Font), PanelKey(ToolPanel::Paragraph)};
    }
}

void ToolPanels::Place(const std::string& id, const std::string& panel, UltraCanvasUIElement* widget) {
    placed_.push_back({id, panel, widget});
}

void ToolPanels::BuildPanel(const ToolPanelTools& group) {
    const std::string prefix = stack_->GetIdentifier() + "." + group.key;
    auto content = CreateToolPanelContent(prefix + ".content");

    // The panel's dialog: its "…" button.
    const ToolEntry* dialog = nullptr;
    for (const ToolEntry* entry : group.tools)
        if (IsDialogTool(entry->id)) { dialog = entry; break; }
    const bool dialogAlone = dialog && group.tools.size() == 1;   // nothing else to show: a button too

    std::shared_ptr<UltraCanvasToolButtonRow> buttons;
    std::shared_ptr<UltraCanvasToolValueRows> values;
    int rowCount = 0;
    auto buttonRow = [&]() -> UltraCanvasToolButtonRow& {
        values.reset();
        if (!buttons) {
            buttons = CreateToolButtonRow(prefix + ".buttons" + std::to_string(rowCount++));
            content->AddChild(buttons);
        }
        return *buttons;
    };
    auto valueRows = [&]() -> UltraCanvasToolValueRows& {
        buttons.reset();
        if (!values) {
            values = CreateToolValueRows(prefix + ".values" + std::to_string(rowCount++));
            content->AddChild(values);
        }
        return *values;
    };

    // A button or a toggle of the row; its tooltip the table's, with the key.
    auto addButton = [&](const std::string& id, const std::string& icon, const std::string& label,
                         const std::string& tooltip, std::function<void()> execute, const ToolEntry* toggle,
                         std::function<bool()> enabled) {
        ToolRowButton spec;
        spec.id = id;
        spec.iconPath = icon.empty() ? std::string() : IconPath(icon);
        spec.name = label;
        spec.text = icon.empty() ? label : std::string();
        spec.isEnabled = std::move(enabled);
        if (toggle) {
            spec.isChecked = toggle->isOn;
            spec.setChecked = [execute](bool) { if (execute) execute(); };
        } else {
            spec.onClick = execute;
        }
        auto button = buttonRow().AddButton(spec);
        button->SetTooltip(tooltip);
        Place(id, group.key, button.get());
    };

    const std::vector<const ToolEntry*>& tools = group.tools;
    bool first = true;   // the panel's first control: a list there goes across the panel
    for (size_t i = 0; i < tools.size(); ++i) {
        const ToolEntry& entry = *tools[i];
        const ToolEntry* row = &entry;
        if (&entry == dialog && !dialogAlone) continue;
        // Each tool's action, then the panels as the document now is (an
        // action that raises no change of its own still shows).
        auto act = [this, alive = std::weak_ptr<bool>(alive_)](std::function<void()> action) {
            return [this, alive, action]() {
                if (action) action();
                if (!alive.expired()) Sync();
            };
        };
        switch (entry.kind) {
            case ToolKind::Button:
                addButton(entry.id, entry.icon, entry.label, entry.tooltip, act(entry.execute), nullptr, entry.isEnabled);
                break;
            case ToolKind::Toggle:
                addButton(entry.id, entry.icon, entry.label, entry.tooltip, act(entry.execute), row, entry.isEnabled);
                break;
            case ToolKind::ActionGroup:
                // Every member as a button: a panel has the room.
                for (const ToolGroupMember& member : entry.members)
                    addButton(member.id, member.icon, member.label, member.tooltip, act(member.execute), nullptr,
                              entry.isEnabled);
                break;
            case ToolKind::List: {
                UltraCanvasToolValueRows& rows = valueRows();
                UltraCanvasToolValueRows* owner = &rows;   // the rows are above their controls: held raw
                if (entry.list.style == ToolListStyle::Font) {
                    // The font picker across the panel, the size beside it
                    // when the size is the next tool.
                    auto line = CreateContainer(entry.id + "-row");
                    line->layout.SetFlexRow().SetFlexGap(4.0f).SetFlexAlignItems(CSSLayout::AlignItems::Center);
                    line->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kFontRowHeight));
                    line->SetBackgroundColor(Colors::Transparent);
                    DisableScrollbars(line);
                    auto picker = CreateFontPicker(entry.id, 0, 0, 0, kFontRowHeight);
                    picker->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
                    picker->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                    picker->SetListWidth(300);
                    picker->SetTooltip(entry.tooltip);
                    picker->SetAccessibleName(entry.label);
                    UltraCanvasFontPicker* rawPicker = picker.get();
                    picker->onListOpening = [rawPicker, row]() {
                        if (row->list.documentFonts) rawPicker->SetDocumentFonts(row->list.documentFonts());
                    };
                    FontListSettings(*picker, entry.list.settings);
                    if (!entry.list.defaultFontEntry.empty()) picker->SetDefaultFontEntry(entry.list.defaultFontEntry);
                    picker->onFontChosen = [row, owner](const FontPickerChoice& choice) {
                        if (owner->IsSyncing()) return;
                        if (row->list.chooseFont) row->list.chooseFont(choice);
                    };
                    line->AddChild(picker);
                    Place(entry.id, group.key, picker.get());
                    std::vector<std::function<void()>> syncs;
                    syncs.push_back([rawPicker, row]() {
                        if (!row->list.currentFont) return;
                        if (const std::optional<std::string> family = row->list.currentFont()) rawPicker->SetCurrentFont(*family);
                        else rawPicker->SetMixed();
                    });
                    const ToolEntry* size = i + 1 < tools.size() && IsSizeTool(tools[i + 1]) ? tools[i + 1] : nullptr;
                    if (size && size->kind == ToolKind::List) {
                        ChoiceList box = MakeChoice(*size, owner, kSizeBoxWidth);
                        line->AddChild(box.box);
                        Place(size->id, group.key, box.box.get());
                        syncs.push_back(box.sync);
                        ++i;
                    } else if (size) {
                        auto spinner = MakeSpinner(*size, kSizeSpinnerWidth);
                        UltraCanvasSpinner* rawSpinner = spinner.get();
                        spinner->onValueChanged = [size, owner](double to) {
                            if (!owner->IsSyncing() && size->value.set) size->value.set(to);
                        };
                        line->AddChild(spinner);
                        Place(size->id, group.key, spinner.get());
                        syncs.push_back([rawSpinner, size]() {
                            ShowNumber(*rawSpinner, size->value.get ? size->value.get() : std::nullopt);
                        });
                        ++i;
                    }
                    rows.AddWideRow(line, [syncs]() { for (const auto& sync : syncs) sync(); });
                } else {
                    // A choice (a percentage too: the view group's, never in a panel).
                    ChoiceList box = MakeChoice(entry, owner, 0.0f);
                    Place(entry.id, group.key, box.box.get());
                    if (first) rows.AddWideRow(box.box, box.sync);
                    else rows.AddRow(entry.id, entry.label, box.box, box.sync);
                }
                break;
            }
            case ToolKind::Value: {
                UltraCanvasToolValueRows& rows = valueRows();
                UltraCanvasToolValueRows* owner = &rows;
                if (entry.value.type == ToolValueType::Colour) {
                    auto bar = std::make_shared<UltraCanvasColorSwatchBar>(entry.id, 0, 0, 0, kSwatchHeight);
                    bar->SetColors(PanelColours());
                    bar->SetTooltip(entry.tooltip);
                    bar->SetAccessibleName(entry.label);
                    bar->size.height = CSSLayout::Dimension::Px(kSwatchHeight);
                    bar->onColorSelected = [row, owner](const Color& colour) {
                        if (!owner->IsSyncing() && row->value.setColour) row->value.setColour(colour);
                    };
                    UltraCanvasColorSwatchBar* raw = bar.get();
                    // The caption over the swatches, which take the panel's
                    // width: a long caption beside them left them a few
                    // pixels each.
                    auto block = CreateContainer(entry.id + "-row");
                    block->layout.SetFlexColumn().SetFlexGap(2.0f).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
                    block->SetBackgroundColor(Colors::Transparent);
                    DisableScrollbars(block);
                    block->AddChild(CreateFormCaption(entry.id + "-caption", entry.label));
                    block->AddChild(bar);
                    rows.AddWideRow(block, [raw, row]() {
                        ShowColour(*raw, row->value.getColour ? row->value.getColour() : std::nullopt);
                    });
                    Place(entry.id, group.key, bar.get());
                } else {
                    auto spinner = MakeSpinner(entry, 0.0f);
                    spinner->onValueChanged = [row, owner](double to) {
                        if (!owner->IsSyncing() && row->value.set) row->value.set(to);
                    };
                    UltraCanvasSpinner* raw = spinner.get();
                    rows.AddRow(entry.id, entry.label, spinner, [raw, row]() {
                        ShowNumber(*raw, row->value.get ? row->value.get() : std::nullopt);
                    });
                    Place(entry.id, group.key, spinner.get());
                }
                break;
            }
            case ToolKind::Custom: {
                if (!entry.panelElement) break;
                std::shared_ptr<UltraCanvasUIElement> element = entry.panelElement(true);
                if (!element) break;
                element->layoutItem.SetFlexShrink(0);   // its own height: the column scrolls
                valueRows().AddWideRow(element);
                handedOver_.push_back(row);
                Place(entry.id, group.key, element.get());
                break;
            }
        }
        first = false;
    }

    ToolPanelOptions options;
    // The rail's icon: the main tool's - the dialog's, else the first one with an icon.
    std::string icon = dialog ? dialog->icon : std::string();
    for (const PanelIcon& main : kMainToolIcons)
        if (group.key == main.key) icon = main.icon;
    for (const ToolEntry* entry : tools) {
        if (!icon.empty()) break;
        if (!entry->icon.empty()) icon = entry->icon;
        for (const ToolGroupMember& member : entry->members)
            if (icon.empty() && !member.icon.empty()) icon = member.icon;
    }
    for (const PanelIcon& fallback : kFallbackIcons)
        if (icon.empty() && group.key == fallback.key) icon = fallback.icon;
    if (!icon.empty()) options.iconPath = IconPath(icon);

    // Help: the dialog behind "…" and the panel's keys.
    std::string help;
    if (dialog) {
        options.dialogAction = [row = dialog, this, alive = std::weak_ptr<bool>(alive_)]() {
            if (row->execute) row->execute();
            if (!alive.expired()) Sync();
        };
        options.dialogTooltip = dialog->tooltip;
        help = TrF("More: {0}, with the … button.", dialog->label);
    }
    std::string keys;
    auto addKey = [&keys](const std::string& label, const std::string& tooltip) {
        const std::string key = ShortcutOf(tooltip);
        if (!key.empty()) keys += (keys.empty() ? "" : ", ") + label + " " + key;
    };
    for (const ToolEntry* entry : tools) {
        addKey(entry->label, entry->tooltip);
        for (const ToolGroupMember& member : entry->members) addKey(member.label, member.tooltip);
    }
    if (!keys.empty()) help += (help.empty() ? "" : "\n") + TrF("Keys: {0}", keys);
    options.helpText = help;
    options.context = group.context != ToolContext::AlwaysShown;
    if (options.context) options.chip = ContextChip(group.context);

    stack_->AddPanel(group.key, group.title, content, options);
    panels_.push_back({group.key, group.context, dialog ? dialog->id : std::string()});
}

// ===== IN STEP =====

void ToolPanels::Sync() {
    for (const Panel& panel : panels_)
        if (panel.context != ToolContext::AlwaysShown) stack_->SetPanelShown(panel.key, table_.Applies(panel.context));
    stack_->Sync();
}

UltraCanvasUIElement* ToolPanels::Widget(const std::string& id) const {
    for (const Placed& placed : placed_)
        if (placed.id == id) return placed.widget;
    return nullptr;
}

std::string ToolPanels::DialogOf(const std::string& panelKey) const {
    for (const Panel& panel : panels_)
        if (panel.key == panelKey) return panel.dialog;
    return std::string();
}

std::string ToolPanels::PanelOf(const std::string& id) const {
    for (const Placed& placed : placed_)
        if (placed.id == id) return placed.panel;
    for (const Panel& panel : panels_)
        if (panel.dialog == id) return panel.key;
    return std::string();
}

} // namespace UltraOffice
