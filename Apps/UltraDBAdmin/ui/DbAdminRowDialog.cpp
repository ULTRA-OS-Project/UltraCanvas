// Apps/UltraDBAdmin/ui/DbAdminRowDialog.cpp
// Version: 0.3.0 - a staged new row opened again with its values
// Version: 0.2.0 - a field per type, NULL / Default / a value function per column, checked before staging
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRowDialog.h"

#include "DbAdminFormat.h"
#include "DbAdminTheme.h"
#include "DbAdminValueTypes.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

constexpr int kDialogWidth = 640;
constexpr int kRowPitch = 38;           // one column's line in the form
constexpr float kRowHeight = 28.0f;

// One column's line. The widgets belong to the dialog; plain pointers here,
// so no callback stored on a widget owns the widget or the dialog.
struct Field {
    std::string column;
    ValueKind kind = ValueKind::Any;
    UltraCanvasTextInput* input = nullptr;      // the value, typed
    UltraCanvasDropdown* truth = nullptr;       // a boolean's value: true / false
    UltraCanvasDropdown* mode = nullptr;        // what the column is set to
    std::vector<std::string> modes;             // per mode item: "value", "null", "default", or a function's SQL
};

struct Form {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasLabel* error = nullptr;
    std::vector<Field> fields;
    SaveRowFn onSave;
};

const char* const kValue   = "value";
const char* const kNull    = "null";
const char* const kDefault = "default";

std::string ModeOf(const Field& field) {
    const int at = field.mode ? field.mode->GetSelectedIndex() : 0;
    return at >= 0 && static_cast<size_t>(at) < field.modes.size() ? field.modes[static_cast<size_t>(at)] : kValue;
}

void SelectMode(const Field& field, const std::string& mode) {
    for (size_t i = 0; i < field.modes.size(); ++i)
        if (field.modes[i] == mode) field.mode->SetSelectedIndex(static_cast<int>(i), false);
}

// The value takes input only while the column is set to a value.
void SyncField(const Field& field) {
    const bool typed = ModeOf(field) == kValue;
    if (field.input) field.input->SetDisabled(!typed);
    if (field.truth) field.truth->SetDisabled(!typed);
}

void Save(Form& form) {
    std::map<std::string, CellValue> values;
    for (const auto& field : form.fields) {
        const std::string mode = ModeOf(field);
        if (mode == kDefault) { values[field.column] = CellValue::DefaultValue(); continue; }
        if (mode == kNull) { values[field.column] = CellValue::NullValue(); continue; }
        if (mode != kValue) { values[field.column] = CellValue::FunctionValue(mode); continue; }
        std::string text = field.truth ? (field.truth->GetSelectedIndex() == 0 ? "true" : "false")
                                       : field.input->GetText();
        // What the column cannot take is said here, before Commit does.
        std::string error;
        if (!(text.empty() && (field.kind == ValueKind::Text || field.kind == ValueKind::Any)) &&
            !CheckValue(field.kind, field.column, text, error)) {
            form.error->SetText(error + (text.empty() ? " Set it to NULL, or to its default, for no value." : ""));
            return;
        }
        values[field.column] = CellValue::Of(text);
    }
    const std::string error = form.onSave ? form.onSave(values) : std::string();
    if (!error.empty()) {
        form.error->SetText(error);
        return;
    }
    form.dialog->CloseDialog(DialogResult::OK);
}

std::string TypeText(const UltraDbColumnSchema& column) {
    std::string text = column.declaredType.empty() ? std::string("any type") : column.declaredType;
    if (!column.nullable) text += ", not null";
    if (column.autoIncrement) text += ", identity";
    if (column.hasDefault) text += ", default " + column.defaultExpression;
    return text;
}

} // namespace

void ShowRowDialog(UltraCanvasWindowBase* parent, const UltraDbTableSchema& table, SqlEngine engine,
                   const UltraDbRow* row, const EditBuffer* pending, SaveRowFn onSave,
                   const std::map<std::string, CellValue>* stagedNew) {
    auto form = std::make_shared<Form>();
    form->onSave = std::move(onSave);
    const bool adding = row == nullptr;

    DialogConfig config;
    config.title      = (adding ? "New row in " : "Edit row of ") + table.name;
    config.width      = kDialogWidth;
    config.height     = std::min(640, 190 + kRowPitch * static_cast<int>(table.columns.size()));
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;   // its own, so a mistake keeps it open
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetFollowTheme(true);
    form->dialog = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto intro = CreateLabel("dbrIntro", 0, 0, 0, 36,
                             adding ? "Each column takes a value, NULL, its default or one of the database's "
                                      "functions. An empty field is empty text."
                                    : "The change is staged: Commit in Browse writes it, Revert drops it.");
    intro->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(intro);

    // The form scrolls when the table has more columns than fit.
    auto scroll = CreateContainer("dbrScroll", 0, 0, 0, 0);
    scroll->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    scroll->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    auto grid = CreateFormGrid("dbrForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);

    int index = 0;
    for (const auto& column : table.columns) {
        const std::string id = "dbrCol" + std::to_string(index++);
        const UltraDbValue original = row && row->Has(column.name) ? (*row)[column.name] : UltraDbValue();
        auto caption = CreateFormCaption(id + "-caption", column.name);
        caption->SetTooltip(TypeText(column));
        const ValueKind kind = KindOfColumn(column);

        auto line = CreateFormCellRow(id + "-line", 8.0f, static_cast<int>(kRowHeight));
        // What the database decides alone is shown, not set.
        const bool blob = kind == ValueKind::Blob || original.Type() == UltraDbType::Blob;
        const bool multiline = !original.IsNull() && original.Type() == UltraDbType::Text &&
                               original.AsString().find('\n') != std::string::npos;
        if (column.generated || blob || multiline) {
            auto shown = CreateTextInput(id + "-input", 0, 0, 0, kRowHeight);
            shown->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            shown->SetText(column.generated ? (adding ? std::string("(computed by the database)") : CellText(original))
                                            : CellText(original));
            shown->SetReadOnly(true);
            shown->SetDisabled(true);
            shown->SetTooltip(column.generated ? "Computed by the database; it cannot be set."
                              : blob ? "Binary data: load a file into the cell in Browse (double-click it)."
                                     : "Text of several lines: change it with UPDATE in the SQL tab.");
            line->AddChild(shown);
            AddFormRow(grid, caption, line);
            continue;
        }

        Field field;
        field.column = column.name;
        field.kind = kind;
        if (kind == ValueKind::Boolean) {
            auto truth = CreateDropdown(id + "-truth", 0, 0, 120, kRowHeight);
            truth->AddItem("true");
            truth->AddItem("false");
            truth->SetSelectedIndex(0, false);
            truth->layoutItem.SetFlexShrink(0);
            field.truth = truth.get();
            line->AddChild(truth);
            line->AddStretchSpacer(1);
        } else {
            auto input = CreateTextInput(id + "-input", 0, 0, 0, kRowHeight);
            input->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            input->SetTooltip(TypeText(column));
            input->SetPlaceholder(KindHint(kind));
            field.input = input.get();
            line->AddChild(input);
        }

        // What the column is set to: a value, NULL, its default (a new row),
        // or one of the database's functions for its kind.
        auto mode = CreateDropdown(id + "-mode", 0, 0, 170, kRowHeight);
        mode->layoutItem.SetFlexShrink(0);
        mode->SetTooltip("What the column is set to");
        auto addMode = [&](const std::string& label, const std::string& key) {
            mode->AddItem(label);
            field.modes.push_back(key);
        };
        addMode("Value", kValue);
        if (column.nullable) addMode("NULL", kNull);
        if (adding && (column.hasDefault || column.autoIncrement || column.nullable))
            addMode(column.autoIncrement ? "Numbered (default)" : column.hasDefault ? "Default" : "Default (NULL)",
                    kDefault);
        for (const auto& function : ValueFunctions(engine, kind)) addMode(function.label, function.sql);
        mode->SetSelectedIndex(0, false);
        field.mode = mode.get();
        line->AddChild(mode);

        // Start from what the row holds, or from what is already staged for it.
        const CellValue* staged = row && pending ? pending->PendingCell(*row, column.name) : nullptr;
        auto setText = [&](const std::string& text) {
            if (field.input) field.input->SetText(text);
            if (field.truth) {
                const std::string v = text.empty() ? std::string() : std::string(1, static_cast<char>(text[0]));
                field.truth->SetSelectedIndex(v == "f" || v == "F" || v == "0" || v == "n" || v == "N" ? 1 : 0, false);
            }
        };
        if (staged) {
            if (staged->kind == CellValue::Kind::Null) SelectMode(field, kNull);
            else if (staged->kind == CellValue::Kind::Function) SelectMode(field, staged->text);
            else setText(staged->text);
        } else if (row) {
            if (original.IsNull()) SelectMode(field, kNull);
            else setText(original.Type() == UltraDbType::Boolean ? (original.AsBool() ? "true" : "false")
                                                                 : original.AsString());
        } else if (stagedNew) {
            const auto it = stagedNew->find(column.name);
            if (it == stagedNew->end()) SelectMode(field, kDefault);
            else if (it->second.kind == CellValue::Kind::Null) SelectMode(field, kNull);
            else if (it->second.kind == CellValue::Kind::Function) SelectMode(field, it->second.text);
            else setText(it->second.text);
        } else if (column.hasDefault || column.autoIncrement) {
            SelectMode(field, kDefault);
        }

        const size_t at = form->fields.size();
        mode->onSelectionChanged = [form, at](int, const DropdownItem&) { SyncField(form->fields[at]); };
        form->fields.push_back(field);
        SyncField(field);
        AddFormRow(grid, caption, line);
    }
    scroll->AddChild(grid);
    dialog->AddChild(scroll);

    auto error = CreateLabel("dbrError", 0, 0, 0, 36, "");
    error->SetTextColor(ErrorColor());
    error->SetWrap(TextWrap::WrapWord);
    error->layoutItem.SetFlexShrink(0);
    dialog->AddChild(error);
    form->error = error.get();

    auto buttons = CreateContainer("dbrButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    buttons->AddStretchSpacer(1);
    auto cancel = MakeDialogButton("dbrCancel", "Cancel", 90);
    cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto save = MakeDialogButton("dbrSave", stagedNew ? "Stage edit" : adding ? "Add row" : "Stage edit", 110,
                                 ButtonRole::Primary);
    save->SetOnClick([form]() { Save(*form); });
    buttons->AddChild(save);
    dialog->AddChild(buttons);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowSqlPreview(UltraCanvasWindowBase* parent, const std::string& title, const std::string& intro,
                    const std::string& sql) {
    DialogConfig config;
    config.title      = title;
    config.width      = 760;
    config.height     = 440;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    dialog->SetFollowTheme(true);
    UltraCanvasModalDialog* raw = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto label = CreateLabel("dbpIntro", 0, 0, 0, 40, intro);
    label->SetWrap(TextWrap::WrapWord);
    label->layoutItem.SetFlexShrink(0);
    dialog->AddChild(label);

    // Text, not Markdown: a statement is shown exactly as it reads.
    auto area = std::make_shared<UltraCanvasTextArea>("dbpSql");
    area->ApplyCodeStyle("SQL");
    area->SetWordWrap(true);
    area->SetText(sql, false);
    area->SetReadOnly(true);
    area->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    dialog->AddChild(area);

    auto buttons = CreateContainer("dbpButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    buttons->AddStretchSpacer(1);
    auto ok = MakeDialogButton("dbpOk", "OK", 90, ButtonRole::Primary);
    ok->SetOnClick([raw]() { raw->CloseDialog(DialogResult::OK); });
    buttons->AddChild(ok);
    dialog->AddChild(buttons);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin
