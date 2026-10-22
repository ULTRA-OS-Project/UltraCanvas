// Apps/UltraDBAdmin/ui/DbAdminRowDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminRowDialog.h"

#include "DbAdminFormat.h"
#include "DbAdminTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
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
    UltraCanvasTextInput* input = nullptr;
    UltraCanvasCheckbox* null = nullptr;
    UltraCanvasCheckbox* useDefault = nullptr;
};

struct Form {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasLabel* error = nullptr;
    std::vector<Field> fields;
    SaveRowFn onSave;
};

// The input takes text only while neither box is ticked.
void SyncField(const Field& field) {
    const bool locked = (field.null && field.null->IsChecked()) ||
                        (field.useDefault && field.useDefault->IsChecked());
    field.input->SetDisabled(locked);
}

void Save(Form& form) {
    std::map<std::string, CellValue> values;
    for (const auto& field : form.fields) {
        if (field.useDefault && field.useDefault->IsChecked()) values[field.column] = CellValue::DefaultValue();
        else if (field.null && field.null->IsChecked()) values[field.column] = CellValue::NullValue();
        else values[field.column] = CellValue::Of(field.input->GetText());
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

void ShowRowDialog(UltraCanvasWindowBase* parent, const UltraDbTableSchema& table, const UltraDbRow* row,
                   const EditBuffer* pending, SaveRowFn onSave) {
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
                             adding ? "Empty fields are empty text. Tick NULL for no value, or Default to "
                                      "leave the column to the database."
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

        auto line = CreateFormCellRow(id + "-line", 8.0f, static_cast<int>(kRowHeight));
        auto input = CreateTextInput(id + "-input", 0, 0, 0, kRowHeight);
        input->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        input->SetTooltip(TypeText(column));
        line->AddChild(input);

        // What the database decides alone is shown, not set.
        const bool blob = column.valueType == UltraDbType::Blob || original.Type() == UltraDbType::Blob;
        const bool multiline = !original.IsNull() && original.Type() == UltraDbType::Text &&
                               original.AsString().find('\n') != std::string::npos;
        if (column.generated || blob || multiline) {
            input->SetText(column.generated ? (adding ? std::string("(computed by the database)") : CellText(original))
                                            : CellText(original));
            input->SetReadOnly(true);
            input->SetDisabled(true);
            input->SetTooltip(column.generated ? "Computed by the database; it cannot be set."
                              : blob ? "Binary data is not edited here."
                                     : "Text of several lines: change it with UPDATE in the SQL tab.");
            AddFormRow(grid, caption, line);
            continue;
        }

        Field field;
        field.column = column.name;
        field.input = input.get();
        auto null = UltraCanvasCheckbox::CreateCheckbox(id + "-null", 0, 0, 70, 24, "NULL", false);
        null->layoutItem.SetFlexShrink(0);
        null->SetDisabled(!column.nullable);
        if (!column.nullable) null->SetTooltip("This column must have a value.");
        line->AddChild(null);
        field.null = null.get();
        if (adding) {
            const bool defaulted = column.hasDefault || column.autoIncrement;
            auto useDefault = UltraCanvasCheckbox::CreateCheckbox(id + "-default", 0, 0, 86, 24, "Default", defaulted);
            useDefault->layoutItem.SetFlexShrink(0);
            useDefault->SetTooltip(column.autoIncrement ? "Numbered by the database."
                                   : column.hasDefault ? "Default: " + column.defaultExpression
                                                       : "No default: the column is NULL, or refuses to be.");
            line->AddChild(useDefault);
            field.useDefault = useDefault.get();
        }

        // Start from what the row holds, or from what is already staged for it.
        const CellValue* staged = row && pending ? pending->PendingCell(*row, column.name) : nullptr;
        if (staged) {
            if (staged->kind == CellValue::Kind::Null) null->SetChecked(true);
            else input->SetText(staged->text);
        } else if (row) {
            if (original.IsNull()) null->SetChecked(true);
            else input->SetText(original.AsString());
        }

        const size_t at = form->fields.size();
        null->onChecked   = [form, at]() { SyncField(form->fields[at]); };
        null->onUnchecked = [form, at]() { SyncField(form->fields[at]); };
        if (field.useDefault) {
            field.useDefault->onChecked   = [form, at]() { SyncField(form->fields[at]); };
            field.useDefault->onUnchecked = [form, at]() { SyncField(form->fields[at]); };
        }
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
    auto save = MakeDialogButton("dbrSave", adding ? "Add row" : "Stage edit", 110, ButtonRole::Primary);
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
