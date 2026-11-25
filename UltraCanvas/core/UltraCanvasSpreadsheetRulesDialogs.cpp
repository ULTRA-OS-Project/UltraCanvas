// core/UltraCanvasSpreadsheetRulesDialogs.cpp
// The grid's conditional formats, validation rules, comments and links, set
// on the selection: the calls that change them (AddSelectionConditionalFormat,
// SetSelectionValidation, SetCellComment, SetCellHyperlink, OpenHyperlink)
// and the dialogs that edit them, each a form (UltraCanvasFormLayout) of the
// framework's own elements in a modal dialog.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"          // OpenURL
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// THE CALLS
// ============================================================================

void UltraCanvasSpreadsheet::AddSelectionConditionalFormat(const ConditionalFormatRule& rule) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    ConditionalFormat format;
    format.range = GetFormattingRange();
    format.rules.push_back(rule);
    sheet->AddConditionalFormat(format);
    Invalidate();
}

void UltraCanvasSpreadsheet::ClearSelectionConditionalFormats() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    const CellRange range = GetSelection();
    std::vector<ConditionalFormat> kept;
    for (const ConditionalFormat& format : sheet->GetConditionalFormats()) {
        if (!format.range.Intersects(range)) kept.push_back(format);
    }
    sheet->SetConditionalFormats(kept);
    Invalidate();
}

void UltraCanvasSpreadsheet::SetSelectionValidation(const DataValidation& rule) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    const CellRange range = GetFormattingRange();
    sheet->RemoveValidation(range);
    for (int r = range.start.row; r <= range.end.row; ++r) {
        for (int c = range.start.col; c <= range.end.col; ++c) {
            if (SpreadsheetCell* cell = sheet->GetCellIfExists(r, c)) cell->RemoveValidation();
        }
    }
    if (rule.IsValid()) sheet->SetValidation(range, rule);
    Invalidate();
}

void UltraCanvasSpreadsheet::SetCellComment(int row, int col, const std::string& text, const std::string& author) {
    if (text.empty()) {
        RemoveCellComment(row, col);
        return;
    }
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    RecordCellChange(row, col);
    sheet->GetCell(row, col)->SetComment(CellComment(text, author));
    if (onCellChange) onCellChange(row, col);
    Invalidate();
}

void UltraCanvasSpreadsheet::RemoveCellComment(int row, int col) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    SpreadsheetCell* cell = sheet ? sheet->GetCellIfExists(row, col) : nullptr;
    if (!cell || !cell->HasComment()) return;
    RecordCellChange(row, col);
    cell->RemoveComment();
    if (onCellChange) onCellChange(row, col);
    Invalidate();
}

void UltraCanvasSpreadsheet::SetCellHyperlink(int row, int col, const CellHyperlink& link) {
    if (link.target.empty()) {
        RemoveCellHyperlink(row, col);
        return;
    }
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    RecordCellChange(row, col);
    SpreadsheetCell* cell = sheet->GetCell(row, col);
    cell->SetHyperlink(link);
    // An empty cell shows the link's text, or where it goes.
    if (cell->IsEmpty()) cell->SetText(link.displayText.empty() ? link.target : link.displayText);
    if (onCellChange) onCellChange(row, col);
    Invalidate();
}

void UltraCanvasSpreadsheet::RemoveCellHyperlink(int row, int col) {
    SpreadsheetSheet* sheet = GetActiveSheet();
    SpreadsheetCell* cell = sheet ? sheet->GetCellIfExists(row, col) : nullptr;
    if (!cell || !cell->HasHyperlink()) return;
    RecordCellChange(row, col);
    cell->RemoveHyperlink();
    if (onCellChange) onCellChange(row, col);
    Invalidate();
}

bool UltraCanvasSpreadsheet::OpenHyperlink(int row, int col) {
    const SpreadsheetSheet* sheet = GetActiveSheet();
    const SpreadsheetCell* cell = sheet ? sheet->GetCellIfExists(row, col) : nullptr;
    if (!cell || !cell->HasHyperlink() || cell->GetHyperlink()->target.empty()) return false;
    CellHyperlink link = *cell->GetHyperlink();
    if (onOpenHyperlink) {
        onOpenHyperlink(link);
        return true;
    }
    const bool internal = link.type == HyperlinkType::InternalCell || link.type == HyperlinkType::InternalRange ||
                          link.type == HyperlinkType::InternalNamedRange || link.target[0] == '#';
    if (!internal) {
        std::string url = link.target;
        if (link.type == HyperlinkType::Email && url.rfind("mailto:", 0) != 0) url = "mailto:" + url;
        OpenURL(url);
        return true;
    }
    // A place in the workbook: "Sheet2.B4", "'My sheet'.B4:C9", Excel's
    // "Sheet2!B4", or a cell of this sheet.
    std::string place = link.target[0] == '#' ? link.target.substr(1) : link.target;
    const size_t bang = place.rfind('!');
    if (bang != std::string::npos) place[bang] = '.';
    const CellRange range = CellRange::FromString(place);
    if (!range.start.IsValid()) return true;
    if (!range.start.sheetName.empty()) {
        for (int i = 0; i < GetSheetCount(); ++i) {
            if (GetSheet(i) && GetSheet(i)->GetName() == range.start.sheetName) {
                SetActiveSheet(i);
                break;
            }
        }
    }
    SelectCell(range.start.row, range.start.col);
    EnsureCellVisible(range.start.row, range.start.col);
    Invalidate();
    return true;
}

// ============================================================================
// THE DIALOGS
// ============================================================================

namespace {

constexpr int kRowHeight = 28;
constexpr float kRowGap = 8.0f;
constexpr float kColumnGap = 12.0f;
constexpr int kFieldWidth = 300;

// A dialog holding a form of `rows` rows, OK and Cancel, no message.
struct RulesForm {
    std::shared_ptr<UltraCanvasModalDialog> dialog;
    std::shared_ptr<UltraCanvasContainer> form;
};

RulesForm MakeRulesForm(const std::string& id, const std::string& title, int rows) {
    DialogConfig config;
    config.title = title;
    config.dialogType = DialogType::Information;
    config.buttons = DialogButtons::OKCancel;
    config.defaultButton = DialogButton::OK;
    config.showIcon = false;
    config.width = 560;
    config.height = static_cast<int>(rows * kRowHeight + (rows - 1) * kRowGap) + 110;
    RulesForm out;
    out.dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!out.dialog) return out;
    out.dialog->SetMessageVisible(false);
    out.form = CreateFormGrid(id, kRowGap, kColumnGap);
    out.form->size.height = CSSLayout::Dimension::Px(static_cast<float>(rows * kRowHeight) + kRowGap * (rows - 1));
    return out;
}

std::shared_ptr<UltraCanvasDropdown> MakeChoice(const std::string& id, const std::vector<std::string>& items,
                                                int selected) {
    auto list = std::make_shared<UltraCanvasDropdown>(id, 0, 0, kFieldWidth, kRowHeight);
    list->size.width = CSSLayout::Dimension::Px(kFieldWidth);
    list->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    for (const std::string& item : items) list->AddItem(item);
    list->SetSelectedIndex(std::clamp(selected, 0, static_cast<int>(items.size()) - 1), false);
    return list;
}

std::shared_ptr<UltraCanvasTextInput> MakeField(const std::string& id, const std::string& text,
                                                std::string* target) {
    auto field = std::make_shared<UltraCanvasTextInput>(id, 0, 0, kFieldWidth, kRowHeight);
    field->size.width = CSSLayout::Dimension::Px(kFieldWidth);
    field->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    field->SetText(text);
    field->onTextChanged = [target](const std::string& now) { *target = now; };
    return field;
}

// A form row that can be shown or hidden: its caption and its control.
struct FormRow {
    UltraCanvasLabel* caption = nullptr;
    UltraCanvasUIElement* control = nullptr;
    void Show(bool visible) const {
        if (caption) caption->SetVisible(visible);
        if (control) control->SetVisible(visible);
    }
};

FormRow AddRow(const std::shared_ptr<UltraCanvasContainer>& form, const std::string& id, const std::string& caption,
               const std::shared_ptr<UltraCanvasUIElement>& control) {
    FormRow row;
    row.caption = AddFormRow(form, id, caption, control).get();
    row.control = control.get();
    return row;
}

const std::vector<ValidationOperator> kOperators = {
    ValidationOperator::Between, ValidationOperator::NotBetween, ValidationOperator::Equal,
    ValidationOperator::NotEqual, ValidationOperator::GreaterThan, ValidationOperator::LessThan,
    ValidationOperator::GreaterOrEqual, ValidationOperator::LessOrEqual};

std::vector<std::string> OperatorNames() {
    return {Tr("between"), Tr("not between"), Tr("equal to"), Tr("not equal to"), Tr("greater than"),
            Tr("less than"), Tr("greater than or equal to"), Tr("less than or equal to")};
}

int OperatorIndex(ValidationOperator op) {
    const auto it = std::find(kOperators.begin(), kOperators.end(), op);
    return it == kOperators.end() ? 0 : static_cast<int>(it - kOperators.begin());
}

bool NeedsSecondValue(ValidationOperator op) {
    return op == ValidationOperator::Between || op == ValidationOperator::NotBetween;
}

// ---- Conditional formats ----

// What the dialog offers, in its order.
enum class RuleKind {
    CellValue, Formula, TextContains, TextBegins, TextEnds, Blank, Errors,
    TopValues, BottomValues, AboveAverage, BelowAverage, Duplicates, UniqueValues,
    ColourScale2, ColourScale3, DataBar
};

std::vector<std::string> RuleKindNames() {
    return {Tr("Cell value"), Tr("Formula is true"), Tr("Text contains"), Tr("Text begins with"),
            Tr("Text ends with"), Tr("Cell is blank"), Tr("Cell has an error"), Tr("Top values"),
            Tr("Bottom values"), Tr("Above average"), Tr("Below average"), Tr("Duplicate values"),
            Tr("Unique values"), Tr("Two-colour scale"), Tr("Three-colour scale"), Tr("Data bar")};
}

RuleKind KindOfRule(const ConditionalFormatRule& rule) {
    switch (rule.type) {
        case ConditionalFormatType::Expression: return RuleKind::Formula;
        case ConditionalFormatType::ContainsText: return RuleKind::TextContains;
        case ConditionalFormatType::BeginsWithText: return RuleKind::TextBegins;
        case ConditionalFormatType::EndsWithText: return RuleKind::TextEnds;
        case ConditionalFormatType::ContainsBlanks: return RuleKind::Blank;
        case ConditionalFormatType::ContainsErrors: return RuleKind::Errors;
        case ConditionalFormatType::Top10: return rule.bottom ? RuleKind::BottomValues : RuleKind::TopValues;
        case ConditionalFormatType::AboveAverage:
            return rule.aboveAverage ? RuleKind::AboveAverage : RuleKind::BelowAverage;
        case ConditionalFormatType::DuplicateValues: return RuleKind::Duplicates;
        case ConditionalFormatType::UniqueValues: return RuleKind::UniqueValues;
        case ConditionalFormatType::ColorScale:
            return rule.colorScale.size() >= 3 ? RuleKind::ColourScale3 : RuleKind::ColourScale2;
        case ConditionalFormatType::DataBar: return RuleKind::DataBar;
        default: return RuleKind::CellValue;
    }
}

// The looks a matching cell can take.
struct StylePreset { Color fill; Color text; bool bold; };
const std::vector<StylePreset>& StylePresets() {
    static const std::vector<StylePreset> presets = {
        {Color(255, 199, 206), Color(156, 0, 6), false},
        {Color(255, 235, 156), Color(156, 87, 0), false},
        {Color(198, 239, 206), Color(0, 97, 0), false},
        {Color(189, 215, 238), Colors::Black, false},
        {Colors::Transparent, Color(192, 0, 0), false},
        {Colors::Transparent, Colors::Black, true}};
    return presets;
}

// StylePresets' names, in their order.
std::vector<std::string> StylePresetNames() {
    return {Tr("Light red fill, dark red text"), Tr("Yellow fill, dark yellow text"),
            Tr("Green fill, dark green text"), Tr("Light blue fill"), Tr("Red text"), Tr("Bold text")};
}

CellStyle StyleOfPreset(const StylePreset& preset) {
    CellStyle style;
    if (preset.fill.a > 0) style.fill = CellFill::Solid(preset.fill);
    style.font.color = preset.text;
    style.font.bold = preset.bold;
    return style;
}

int PresetOfStyle(const CellStyle& style) {
    const auto& presets = StylePresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        const CellStyle candidate = StyleOfPreset(presets[i]);
        if (candidate.fill.HasFill() == style.fill.HasFill() &&
            (!style.fill.HasFill() || candidate.fill.foregroundColor == style.fill.foregroundColor) &&
            candidate.font.color == style.font.color && candidate.font.bold == style.font.bold) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// The colours of a scale or a bar.
struct ColourPreset { std::vector<Color> colours; };
const std::vector<ColourPreset>& ColourPresets() {
    static const std::vector<ColourPreset> presets = {
        {{Color(248, 105, 107), Color(255, 235, 132), Color(99, 190, 123)}},
        {{Color(99, 190, 123), Color(255, 235, 132), Color(248, 105, 107)}},
        {{Color(252, 252, 255), Color(156, 195, 230), Color(90, 138, 198)}},
        {{Color(99, 142, 198), Color(99, 142, 198), Color(99, 142, 198)}},
        {{Color(99, 190, 123), Color(99, 190, 123), Color(99, 190, 123)}},
        {{Color(255, 182, 40), Color(255, 182, 40), Color(255, 182, 40)}}};
    return presets;
}

// ColourPresets' names, in their order.
std::vector<std::string> ColourPresetNames() {
    return {Tr("Red, yellow, green"), Tr("Green, yellow, red"), Tr("White, blue"), Tr("Blue"), Tr("Green"),
            Tr("Orange")};
}

int PresetOfColours(const ConditionalFormatRule& rule) {
    const auto& presets = ColourPresets();
    const Color first = rule.type == ConditionalFormatType::DataBar
                            ? rule.dataBarColor
                            : (rule.colorScale.empty() ? Colors::White : rule.colorScale.front());
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].colours.front() == first) return static_cast<int>(i);
    }
    return rule.type == ConditionalFormatType::DataBar ? 3 : 0;
}

// One line naming a rule, for the list of those over the selection.
std::string DescribeRule(const ConditionalFormat& format, const ConditionalFormatRule& rule) {
    const std::vector<std::string> kinds = RuleKindNames();
    std::string text = format.range.ToString() + ": " + kinds[static_cast<size_t>(KindOfRule(rule))];
    if (rule.type == ConditionalFormatType::CellValue) {
        text += " " + OperatorNames()[static_cast<size_t>(OperatorIndex(rule.op))] + " " + rule.formula1;
        if (NeedsSecondValue(rule.op)) text += " / " + rule.formula2;
    } else if (!rule.formula1.empty() && rule.type != ConditionalFormatType::AboveAverage) {
        text += " " + rule.formula1;
    }
    return text;
}

struct ConditionalDialogState {
    // The rules over the selection: (format, rule) positions in the sheet's list.
    std::vector<std::pair<size_t, size_t>> existing;
    int picked = 0;                       // 0: a new rule; i: existing[i - 1]
    int kind = 0;
    int op = 4;                           // greater than
    std::string value1, value2;
    int style = 0;
    int colours = 0;
};

ConditionalFormatRule RuleFromState(const ConditionalDialogState& state) {
    ConditionalFormatRule rule;
    const RuleKind kind = static_cast<RuleKind>(state.kind);
    rule.style = StyleOfPreset(StylePresets()[static_cast<size_t>(std::max(0, state.style))]);
    rule.formula1 = state.value1;
    switch (kind) {
        case RuleKind::CellValue:
            rule.type = ConditionalFormatType::CellValue;
            rule.op = kOperators[static_cast<size_t>(std::max(0, state.op))];
            if (NeedsSecondValue(rule.op)) rule.formula2 = state.value2;
            break;
        case RuleKind::Formula:
            rule.type = ConditionalFormatType::Expression;
            if (!rule.formula1.empty() && rule.formula1[0] != '=') rule.formula1 = "=" + rule.formula1;
            break;
        case RuleKind::TextContains: rule.type = ConditionalFormatType::ContainsText; break;
        case RuleKind::TextBegins: rule.type = ConditionalFormatType::BeginsWithText; break;
        case RuleKind::TextEnds: rule.type = ConditionalFormatType::EndsWithText; break;
        case RuleKind::Blank: rule.type = ConditionalFormatType::ContainsBlanks; rule.formula1.clear(); break;
        case RuleKind::Errors: rule.type = ConditionalFormatType::ContainsErrors; rule.formula1.clear(); break;
        case RuleKind::TopValues:
        case RuleKind::BottomValues:
            rule.type = ConditionalFormatType::Top10;
            rule.bottom = kind == RuleKind::BottomValues;
            if (rule.formula1.empty()) rule.formula1 = "10";
            break;
        case RuleKind::AboveAverage:
        case RuleKind::BelowAverage:
            rule.type = ConditionalFormatType::AboveAverage;
            rule.aboveAverage = kind == RuleKind::AboveAverage;
            rule.formula1.clear();
            break;
        case RuleKind::Duplicates: rule.type = ConditionalFormatType::DuplicateValues; rule.formula1.clear(); break;
        case RuleKind::UniqueValues: rule.type = ConditionalFormatType::UniqueValues; rule.formula1.clear(); break;
        case RuleKind::ColourScale2:
        case RuleKind::ColourScale3: {
            rule.type = ConditionalFormatType::ColorScale;
            rule.formula1.clear();
            const auto& colours = ColourPresets()[static_cast<size_t>(std::max(0, state.colours))].colours;
            if (kind == RuleKind::ColourScale3) rule.colorScale = colours;
            else rule.colorScale = {colours.front(), colours.back()};
            break;
        }
        case RuleKind::DataBar:
            rule.type = ConditionalFormatType::DataBar;
            rule.formula1.clear();
            rule.dataBarColor = ColourPresets()[static_cast<size_t>(std::max(0, state.colours))].colours.front();
            break;
    }
    return rule;
}

} // namespace

void UltraCanvasSpreadsheet::ShowConditionalFormatDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    const CellRange selection = GetSelection();

    auto state = std::make_shared<ConditionalDialogState>();
    std::vector<std::string> existingNames = {Tr("New rule")};
    const auto& formats = sheet->GetConditionalFormats();
    for (size_t f = 0; f < formats.size(); ++f) {
        if (!formats[f].range.Intersects(selection)) continue;
        for (size_t r = 0; r < formats[f].rules.size(); ++r) {
            state->existing.emplace_back(f, r);
            existingNames.push_back(DescribeRule(formats[f], formats[f].rules[r]));
        }
    }

    RulesForm built = MakeRulesForm("ConditionalFormatForm", Tr("Conditional formatting"), 7);
    if (!built.dialog) return;
    auto& form = built.form;

    auto rules = MakeChoice("ConditionalRuleList", existingNames, 0);
    auto kinds = MakeChoice("ConditionalRuleKind", RuleKindNames(), 0);
    auto ops = MakeChoice("ConditionalOperator", OperatorNames(), state->op);
    auto value1 = MakeField("ConditionalValue1", "", &state->value1);
    auto value2 = MakeField("ConditionalValue2", "", &state->value2);
    auto styles = MakeChoice("ConditionalStyle", StylePresetNames(), 0);
    auto colours = MakeChoice("ConditionalColours", ColourPresetNames(), 0);

    AddRow(form, "ConditionalRuleList", Tr("Rule") + ":", rules);
    auto rowKind = AddRow(form, "ConditionalRuleKind", Tr("Format cells where") + ":", kinds);
    auto rowOp = AddRow(form, "ConditionalOperator", Tr("Condition") + ":", ops);
    auto rowValue1 = AddRow(form, "ConditionalValue1", Tr("Value") + ":", value1);
    auto rowValue2 = AddRow(form, "ConditionalValue2", Tr("and") + ":", value2);
    auto rowStyle = AddRow(form, "ConditionalStyle", Tr("Style") + ":", styles);
    auto rowColours = AddRow(form, "ConditionalColours", Tr("Colours") + ":", colours);
    built.dialog->AddDialogElement(form);

    // Which rows the kind of rule needs. Raw pointers: the dialog owns them all.
    UltraCanvasTextInput* value1Raw = value1.get();
    UltraCanvasTextInput* value2Raw = value2.get();
    UltraCanvasDropdown* kindsRaw = kinds.get();
    UltraCanvasDropdown* opsRaw = ops.get();
    UltraCanvasDropdown* stylesRaw = styles.get();
    UltraCanvasDropdown* coloursRaw = colours.get();
    auto refresh = [state, rowOp, rowValue1, rowValue2, rowStyle, rowColours]() {
        const RuleKind kind = static_cast<RuleKind>(state->kind);
        const bool cellValue = kind == RuleKind::CellValue;
        const bool scale = kind == RuleKind::ColourScale2 || kind == RuleKind::ColourScale3 || kind == RuleKind::DataBar;
        const bool needsValue = cellValue || kind == RuleKind::Formula || kind == RuleKind::TextContains ||
                                kind == RuleKind::TextBegins || kind == RuleKind::TextEnds ||
                                kind == RuleKind::TopValues || kind == RuleKind::BottomValues;
        rowOp.Show(cellValue);
        rowValue1.Show(needsValue);
        rowValue2.Show(cellValue && NeedsSecondValue(kOperators[static_cast<size_t>(std::max(0, state->op))]));
        rowStyle.Show(!scale);
        rowColours.Show(scale);
    };
    kinds->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->kind = index;
        refresh();
    };
    ops->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->op = index;
        refresh();
    };
    styles->onSelectionChanged = [state](int index, const DropdownItem&) { state->style = index; };
    colours->onSelectionChanged = [state](int index, const DropdownItem&) { state->colours = index; };
    // Picking a rule over the selection loads it into the fields.
    SpreadsheetSheet* sheetRaw = sheet;
    rules->onSelectionChanged = [state, refresh, sheetRaw, value1Raw, value2Raw, kindsRaw, opsRaw, stylesRaw,
                                 coloursRaw](int index, const DropdownItem&) {
        state->picked = index;
        if (index <= 0 || index > static_cast<int>(state->existing.size())) return;
        const auto [f, r] = state->existing[static_cast<size_t>(index - 1)];
        const auto& all = sheetRaw->GetConditionalFormats();
        if (f >= all.size() || r >= all[f].rules.size()) return;
        const ConditionalFormatRule& rule = all[f].rules[r];
        state->kind = static_cast<int>(KindOfRule(rule));
        state->op = OperatorIndex(rule.op);
        state->style = PresetOfStyle(rule.style);
        state->colours = PresetOfColours(rule);
        kindsRaw->SetSelectedIndex(state->kind, false);
        opsRaw->SetSelectedIndex(state->op, false);
        stylesRaw->SetSelectedIndex(state->style, false);
        coloursRaw->SetSelectedIndex(state->colours, false);
        value1Raw->SetText(rule.formula1);
        value2Raw->SetText(rule.formula2);
        state->value1 = rule.formula1;
        state->value2 = rule.formula2;
        refresh();
    };
    refresh();

    built.dialog->AddCustomButton(Tr("Delete rule"), DialogResult::No, DialogButtonRole::Destructive);

    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weakSelf, state, selection](DialogResult result) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        SpreadsheetSheet* target = self->GetActiveSheet();
        if (!target) return;
        const bool existing = state->picked > 0 && state->picked <= static_cast<int>(state->existing.size());
        std::vector<ConditionalFormat> all = target->GetConditionalFormats();
        if (result == DialogResult::No) {
            if (!existing) return;
            const auto [f, r] = state->existing[static_cast<size_t>(state->picked - 1)];
            if (f >= all.size() || r >= all[f].rules.size()) return;
            all[f].rules.erase(all[f].rules.begin() + static_cast<long>(r));
            if (all[f].rules.empty()) all.erase(all.begin() + static_cast<long>(f));
            target->SetConditionalFormats(all);
        } else if (result == DialogResult::OK) {
            ConditionalFormatRule rule = RuleFromState(*state);
            if (existing) {
                const auto [f, r] = state->existing[static_cast<size_t>(state->picked - 1)];
                if (f >= all.size() || r >= all[f].rules.size()) return;
                rule.priority = all[f].rules[r].priority;
                rule.stopIfTrue = all[f].rules[r].stopIfTrue;
                all[f].rules[r] = rule;
                target->SetConditionalFormats(all);
            } else {
                ConditionalFormat format;
                format.range = selection;
                format.rules.push_back(rule);
                target->AddConditionalFormat(format);
            }
        } else {
            return;
        }
        if (self->onCellFormatChange) self->onCellFormatChange(selection.start.row, selection.start.col);
        self->Invalidate();
    }, GetWindow());
}

// ---- Data validation ----

namespace {

const std::vector<ValidationType> kValidationTypes = {
    ValidationType::Any, ValidationType::WholeNumber, ValidationType::Decimal, ValidationType::List,
    ValidationType::Date, ValidationType::Time, ValidationType::TextLength, ValidationType::Custom};

struct ValidationDialogState {
    int type = 0;
    int op = 0;
    std::string value1, value2;
    bool allowBlank = true;
    bool dropdown = true;
    int alert = 0;
    std::string message;
};

} // namespace

void UltraCanvasSpreadsheet::ShowDataValidationDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    const CellAddress active = GetActiveCell();

    auto state = std::make_shared<ValidationDialogState>();
    if (const DataValidation* rule = sheet->GetEffectiveValidation(active.row, active.col)) {
        const auto it = std::find(kValidationTypes.begin(), kValidationTypes.end(), rule->type);
        state->type = it == kValidationTypes.end() ? 0 : static_cast<int>(it - kValidationTypes.begin());
        state->op = OperatorIndex(rule->op);
        state->value1 = rule->formula1;
        state->value2 = rule->formula2;
        state->allowBlank = rule->allowBlank;
        state->dropdown = rule->showDropdown;
        state->alert = static_cast<int>(rule->alertStyle);
        state->message = rule->errorMessage;
    }

    RulesForm built = MakeRulesForm("ValidationForm", Tr("Data validation"), 8);
    if (!built.dialog) return;
    auto& form = built.form;
    auto types = MakeChoice("ValidationType", {Tr("Any value"), Tr("Whole number"), Tr("Decimal"), Tr("List"),
                                               Tr("Date"), Tr("Time"), Tr("Text length"), Tr("Custom formula")},
                            state->type);
    auto ops = MakeChoice("ValidationOperator", OperatorNames(), state->op);
    auto value1 = MakeField("ValidationValue1", state->value1, &state->value1);
    auto value2 = MakeField("ValidationValue2", state->value2, &state->value2);
    auto blank = UltraCanvasCheckbox::CreateCheckbox("ValidationAllowBlank", 0, 0, -1, kRowHeight,
                                                     Tr("Allow blank cells"), state->allowBlank);
    blank->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    blank->onStateChanged = [state](CheckedState, CheckedState now) { state->allowBlank = now == CheckedState::Checked; };
    auto arrow = UltraCanvasCheckbox::CreateCheckbox("ValidationDropdown", 0, 0, -1, kRowHeight,
                                                     Tr("Show a list arrow beside the cell"), state->dropdown);
    arrow->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::Start);
    arrow->onStateChanged = [state](CheckedState, CheckedState now) { state->dropdown = now == CheckedState::Checked; };
    auto alert = MakeChoice("ValidationAlert", {Tr("Stop"), Tr("Warning"), Tr("Information")}, state->alert);
    alert->onSelectionChanged = [state](int index, const DropdownItem&) { state->alert = index; };
    auto message = MakeField("ValidationMessage", state->message, &state->message);

    AddRow(form, "ValidationType", Tr("Allow") + ":", types);
    auto rowOp = AddRow(form, "ValidationOperator", Tr("Condition") + ":", ops);
    auto rowValue1 = AddRow(form, "ValidationValue1", Tr("Value") + ":", value1);
    auto rowValue2 = AddRow(form, "ValidationValue2", Tr("and") + ":", value2);
    auto rowBlank = AddRow(form, "ValidationAllowBlank", "", blank);
    auto rowArrow = AddRow(form, "ValidationDropdown", "", arrow);
    auto rowAlert = AddRow(form, "ValidationAlert", Tr("On a wrong entry") + ":", alert);
    auto rowMessage = AddRow(form, "ValidationMessage", Tr("Message") + ":", message);
    built.dialog->AddDialogElement(form);

    auto refresh = [state, rowOp, rowValue1, rowValue2, rowBlank, rowArrow, rowAlert, rowMessage]() {
        const ValidationType type = kValidationTypes[static_cast<size_t>(std::max(0, state->type))];
        const bool any = type == ValidationType::Any;
        const bool compared = !any && type != ValidationType::List && type != ValidationType::Custom;
        rowOp.Show(compared);
        rowValue1.Show(!any);
        rowValue2.Show(compared && NeedsSecondValue(kOperators[static_cast<size_t>(std::max(0, state->op))]));
        rowBlank.Show(!any);
        rowArrow.Show(type == ValidationType::List);
        rowAlert.Show(!any);
        rowMessage.Show(!any);
    };
    types->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->type = index;
        refresh();
    };
    ops->onSelectionChanged = [state, refresh](int index, const DropdownItem&) {
        state->op = index;
        refresh();
    };
    value1->SetPlaceholder(Tr("A number, a list such as a,b,c, or a formula such as =$D$1:$D$9"));
    refresh();

    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weakSelf, state](DialogResult result) {
        if (result != DialogResult::OK) return;
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        DataValidation rule;
        rule.type = kValidationTypes[static_cast<size_t>(std::max(0, state->type))];
        rule.op = kOperators[static_cast<size_t>(std::max(0, state->op))];
        rule.formula1 = state->value1;
        if (NeedsSecondValue(rule.op)) rule.formula2 = state->value2;
        if (rule.type == ValidationType::Custom && !rule.formula1.empty() && rule.formula1[0] != '=') {
            rule.formula1 = "=" + rule.formula1;
        }
        rule.allowBlank = state->allowBlank;
        rule.showDropdown = state->dropdown;
        rule.alertStyle = static_cast<ValidationAlertStyle>(std::clamp(state->alert, 0, 2));
        rule.errorMessage = state->message;
        self->SetSelectionValidation(rule);
        const CellAddress at = self->GetActiveCell();
        if (self->onCellFormatChange) self->onCellFormatChange(at.row, at.col);
    }, GetWindow());
}

// ---- Comment ----

void UltraCanvasSpreadsheet::ShowCommentDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    const CellAddress at = GetActiveCell();
    const SpreadsheetCell* cell = sheet->GetCellIfExists(at.row, at.col);
    auto text = std::make_shared<std::string>(cell && cell->HasComment() ? cell->GetComment()->text : "");
    const std::string author = cell && cell->HasComment() ? cell->GetComment()->author : std::string();

    RulesForm built = MakeRulesForm("CommentForm", Tr("Comment"), 4);
    if (!built.dialog) return;
    auto area = std::make_shared<UltraCanvasTextArea>("CommentText", 0, 0, kFieldWidth + 120,
                                                      4 * kRowHeight + 3 * static_cast<int>(kRowGap));
    area->size.width = CSSLayout::Dimension::Px(static_cast<float>(kFieldWidth + 120));
    area->SetText(*text, false);
    area->SetOnTextChanged([text](const std::string& now) { *text = now; });
    AddFormWideRow(built.form, area);
    built.dialog->AddDialogElement(built.form);
    if (cell && cell->HasComment()) {
        built.dialog->AddCustomButton(Tr("Delete comment"), DialogResult::No, DialogButtonRole::Destructive);
    }

    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weakSelf, text, at, author](DialogResult result) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        if (result == DialogResult::No) self->RemoveCellComment(at.row, at.col);
        else if (result == DialogResult::OK) self->SetCellComment(at.row, at.col, *text, author);
    }, GetWindow());
}

// ---- Link ----

void UltraCanvasSpreadsheet::ShowHyperlinkDialog() {
    SpreadsheetSheet* sheet = GetActiveSheet();
    if (!sheet) return;
    if (IsEditing()) StopEditing(true);
    const CellAddress at = GetActiveCell();
    const SpreadsheetCell* cell = sheet->GetCellIfExists(at.row, at.col);
    const CellHyperlink* current = cell ? cell->GetHyperlink() : nullptr;
    auto address = std::make_shared<std::string>(current ? current->target : std::string("https://"));
    auto shown = std::make_shared<std::string>(cell && !cell->IsEmpty() ? cell->GetDisplayValue() : std::string());

    RulesForm built = MakeRulesForm("LinkForm", Tr("Insert link"), 2);
    if (!built.dialog) return;
    auto field = MakeField("LinkAddress", *address, address.get());
    field->SetPlaceholder(Tr("A web or mail address, or a cell such as Sheet2.A1"));
    auto text = MakeField("LinkText", *shown, shown.get());
    AddRow(built.form, "LinkAddress", Tr("Address") + ":", field);
    AddRow(built.form, "LinkText", Tr("Text") + ":", text);
    built.dialog->AddDialogElement(built.form);
    if (current) built.dialog->AddCustomButton(Tr("Remove link"), DialogResult::No, DialogButtonRole::Destructive);

    std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
    UltraCanvasDialogManager::ShowDialog(built.dialog, [weakSelf, address, shown, at](DialogResult result) {
        auto self = std::static_pointer_cast<UltraCanvasSpreadsheet>(weakSelf.lock());
        if (!self) return;
        if (result == DialogResult::No) {
            self->RemoveCellHyperlink(at.row, at.col);
            return;
        }
        if (result != DialogResult::OK) return;
        std::string target = *address;
        const size_t a = target.find_first_not_of(" \t");
        target = a == std::string::npos ? std::string() : target.substr(a, target.find_last_not_of(" \t") - a + 1);
        if (target.empty() || target == "https://") {
            self->RemoveCellHyperlink(at.row, at.col);
            return;
        }
        CellHyperlink link;
        link.target = target;
        const bool hasScheme = target.find("://") != std::string::npos || target.rfind("mailto:", 0) == 0 ||
                               target.rfind("file:", 0) == 0;
        // A cell of the workbook: "#Sheet2.A1", "Sheet2.A1", "Sheet2!A1", "B4".
        auto namesCell = [](const std::string& place) {
            const size_t cut = place.find_last_of(".!");
            const std::string cellPart = cut == std::string::npos ? place : place.substr(cut + 1);
            return CellRange::FromString(cellPart).start.IsValid();
        };
        if (target[0] == '#') {
            link.type = HyperlinkType::InternalCell;
            link.target = target.substr(1);
        } else if (!hasScheme && target.find('@') != std::string::npos) {
            link.type = HyperlinkType::Email;
            link.target = "mailto:" + target;
        } else if (!hasScheme && namesCell(target)) {
            link.type = HyperlinkType::InternalCell;
        } else if (target.rfind("mailto:", 0) == 0) {
            link.type = HyperlinkType::Email;
        } else if (target.rfind("file:", 0) == 0) {
            link.type = HyperlinkType::File;
        } else if (!hasScheme) {
            link.target = "https://" + target;
        }
        self->SetCellHyperlink(at.row, at.col, link);
        // The text the cell shows, when one was typed.
        if (!shown->empty()) {
            if (SpreadsheetSheet* sheetNow = self->GetActiveSheet()) {
                SpreadsheetCell* target2 = sheetNow->GetCell(at.row, at.col);
                if (target2->GetDisplayValue() != *shown) target2->SetText(*shown);
            }
            if (self->onCellChange) self->onCellChange(at.row, at.col);
            self->Invalidate();
        }
    }, GetWindow());
}

} // namespace UltraCanvas
