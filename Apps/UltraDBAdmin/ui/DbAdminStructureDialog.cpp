// Apps/UltraDBAdmin/ui/DbAdminStructureDialog.cpp
// Version: 0.3.0 - how a form works under its buttons
// Version: 0.2.0 - MySQL / MariaDB types
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminStructureDialog.h"

#include "DbAdminTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

constexpr float kRowHeight = 28.0f;

const std::vector<std::string>& CommonTypes(SqlEngine engine) {
    // SQLite's five storage classes, and the names its affinity rules read.
    static const std::vector<std::string> sqlite = {"INTEGER", "REAL", "TEXT", "BLOB", "NUMERIC"};
    static const std::vector<std::string> postgres = {
        "integer", "bigint", "numeric(12,2)", "double precision", "text", "varchar(255)", "boolean",
        "date", "timestamptz", "jsonb", "uuid", "bytea"};
    static const std::vector<std::string> mysql = {
        "int", "bigint", "decimal(12,2)", "double", "varchar(255)", "text", "tinyint(1)",
        "date", "datetime", "timestamp", "json", "blob"};
    return engine == SqlEngine::Sqlite ? sqlite : engine == SqlEngine::MySql ? mysql : postgres;
}

std::shared_ptr<UltraCanvasModalDialog> MakeDialog(const std::string& title, int height) {
    DialogConfig config;
    config.title      = title;
    config.width      = 560;
    config.height     = height;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;   // its own, so a mistake keeps it open
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return nullptr;
    dialog->SetFollowTheme(true);
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);
    return dialog;
}

// How a form works, put under its buttons: the fields come first.
std::shared_ptr<UltraCanvasLabel> MakeIntro(const std::string& id, const std::string& text) {
    auto intro = CreateLabel(id, 0, 0, 0, 36, text);
    intro->SetWrap(TextWrap::WrapWord);
    intro->SetTextColor(StatusColor());
    intro->layoutItem.SetFlexShrink(0);
    return intro;
}

std::shared_ptr<UltraCanvasLabel> MakeError(const std::string& id) {
    auto error = CreateLabel(id, 0, 0, 0, 36, "");
    error->SetTextColor(ErrorColor());
    error->SetWrap(TextWrap::WrapWord);
    error->layoutItem.SetFlexShrink(0);
    return error;
}

// Cancel and the primary button, right-aligned; the dialog closes on Cancel.
void AddButtons(UltraCanvasModalDialog* dialog, const std::string& id, const std::string& primary,
                std::function<void()> onPrimary) {
    auto buttons = CreateContainer(id, 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    buttons->AddStretchSpacer(1);
    auto cancel = MakeDialogButton(id + "-cancel", "Cancel", 90);
    cancel->SetOnClick([dialog]() { dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto save = MakeDialogButton(id + "-save", primary, 120, ButtonRole::Primary);
    save->SetOnClick(std::move(onPrimary));
    buttons->AddChild(save);
    dialog->AddChild(buttons);
}

// The column form's widgets. Plain pointers: the widgets belong to the
// dialog, so no callback stored on a widget owns the widget or the dialog.
struct ColumnForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasLabel* error = nullptr;
    UltraCanvasTextInput* name = nullptr;
    UltraCanvasTextInput* type = nullptr;
    UltraCanvasCheckbox* notNull = nullptr;
    UltraCanvasCheckbox* hasDefault = nullptr;
    UltraCanvasTextInput* defaultExpression = nullptr;
    bool generated = false;
    SaveColumnFn onSave;
};

void SyncDefault(const ColumnForm& form) {
    form.defaultExpression->SetDisabled(form.generated || !form.hasDefault->IsChecked());
}

void SaveColumn(ColumnForm& form) {
    ColumnDraft draft;
    draft.name = form.name->GetText();
    draft.type = form.type->GetText();
    draft.notNull = form.notNull->IsChecked();
    draft.hasDefault = form.hasDefault->IsChecked();
    draft.defaultExpression = draft.hasDefault ? form.defaultExpression->GetText() : std::string();
    const std::string error = form.onSave ? form.onSave(draft) : std::string();
    if (!error.empty()) { form.error->SetText(error); return; }
    form.dialog->CloseDialog(DialogResult::OK);
}

struct IndexForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasLabel* error = nullptr;
    UltraCanvasLabel* order = nullptr;
    UltraCanvasTextInput* name = nullptr;
    UltraCanvasCheckbox* unique = nullptr;
    std::string table;
    std::string autoName;               // the name the form made up, while the user keeps it
    std::vector<std::string> columns;   // ticked, in the order they were ticked
    SaveIndexFn onSave;
};

std::string MadeUpName(const IndexForm& form) {
    std::string name = form.table;
    for (const auto& column : form.columns) name += "_" + column;
    return name + "_idx";
}

void SyncIndex(IndexForm& form) {
    std::string key;
    for (const auto& column : form.columns) key += (key.empty() ? "" : ", ") + column;
    form.order->SetText(key.empty() ? std::string("Key: none yet - tick the columns, first column first.")
                                    : "Key: " + key);
    if (form.name->GetText() == form.autoName) {
        form.autoName = MadeUpName(form);
        form.name->SetText(form.autoName);
    }
}

void SaveIndex(IndexForm& form) {
    IndexDraft draft;
    draft.name = form.name->GetText();
    draft.columns = form.columns;
    draft.unique = form.unique->IsChecked();
    const std::string error = form.onSave ? form.onSave(draft) : std::string();
    if (!error.empty()) { form.error->SetText(error); return; }
    form.dialog->CloseDialog(DialogResult::OK);
}

} // namespace

void ShowColumnDialog(UltraCanvasWindowBase* parent, const StructureEdit& edit, int at, SaveColumnFn onSave) {
    const bool adding = at < 0 || static_cast<size_t>(at) >= edit.Columns().size();
    const ColumnDraft current = adding ? ColumnDraft() : edit.Columns()[static_cast<size_t>(at)];
    const UltraDbColumnSchema* original = adding ? nullptr : edit.Original(static_cast<size_t>(at));
    const bool sqlite = edit.Engine() == SqlEngine::Sqlite;

    auto form = std::make_shared<ColumnForm>();
    form->onSave = std::move(onSave);
    form->generated = original && original->generated;
    auto dialog = MakeDialog(adding ? "New column in " + edit.Table().name
                                    : "Column " + current.name + " of " + edit.Table().name,
                             400);
    if (!dialog) return;
    form->dialog = dialog.get();

    std::string intro = "The change is staged: Apply in Structure writes it, Revert drops it.";
    if (form->generated) intro = "Computed by the database: only its name can change here.";
    else if (!adding && sqlite) intro = "SQLite changes a type, NOT NULL or default by rebuilding the table, which "
                                        "Apply does for you, keeping its rows, indexes and triggers.";

    auto grid = CreateFormGrid("dbcForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);

    auto name = CreateTextInput("dbcName", 0, 0, 0, kRowHeight);
    name->SetText(current.name);
    form->name = name.get();
    AddFormRow(grid, CreateFormCaption("dbcName-caption", "Name"), name);

    auto typeLine = CreateFormCellRow("dbcType-line", 8.0f, static_cast<int>(kRowHeight));
    auto type = CreateTextInput("dbcType", 0, 0, 0, kRowHeight);
    type->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    type->SetText(current.type);
    const bool mysql = edit.Engine() == SqlEngine::MySql;
    type->SetPlaceholder(sqlite  ? "none: SQLite takes any value"
                         : mysql ? "varchar(80), int, decimal(12,2)…"
                                 : "text, integer, numeric(12,2)…");
    type->SetTooltip("As the database spells it. A changed type converts the values: " +
                     std::string(sqlite  ? "by SQLite's type affinity."
                                 : mysql ? "MySQL's MODIFY COLUMN converts them itself."
                                         : "with CAST(column AS type)."));
    form->type = type.get();
    typeLine->AddChild(type);
    auto common = CreateDropdown("dbcCommon", 0, 0, 170, kRowHeight);
    common->layoutItem.SetFlexShrink(0);
    common->AddItem("Common types…");
    for (const auto& typeName : CommonTypes(edit.Engine())) common->AddItem(typeName);
    common->SetSelectedIndex(0, false);
    UltraCanvasTextInput* rawType = type.get();
    common->onSelectionChanged = [rawType, engine = edit.Engine()](int index, const DropdownItem&) {
        const auto& types = CommonTypes(engine);
        if (index >= 1 && static_cast<size_t>(index) <= types.size()) rawType->SetText(types[static_cast<size_t>(index - 1)]);
    };
    typeLine->AddChild(common);
    AddFormRow(grid, CreateFormCaption("dbcType-caption", "Type"), typeLine);

    auto notNull = UltraCanvasCheckbox::CreateCheckbox("dbcNotNull", 0, 0, 300, 24,
                                                       "NOT NULL - every row has a value", current.notNull);
    form->notNull = notNull.get();
    AddFormRow(grid, CreateFormCaption("dbcNotNull-caption", "Null"), notNull);

    auto defaultLine = CreateFormCellRow("dbcDefault-line", 8.0f, static_cast<int>(kRowHeight));
    auto hasDefault = UltraCanvasCheckbox::CreateCheckbox("dbcHasDefault", 0, 0, 86, 24, "Default", current.hasDefault);
    hasDefault->layoutItem.SetFlexShrink(0);
    form->hasDefault = hasDefault.get();
    defaultLine->AddChild(hasDefault);
    auto expression = CreateTextInput("dbcDefault", 0, 0, 0, kRowHeight);
    expression->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    expression->SetText(current.defaultExpression);
    expression->SetPlaceholder(edit.Engine() == SqlEngine::Postgres ? "0, 'text', now()" : "0, 'text', CURRENT_TIMESTAMP");
    expression->SetTooltip("SQL, as written after DEFAULT: a string needs its quotes.");
    form->defaultExpression = expression.get();
    defaultLine->AddChild(expression);
    AddFormRow(grid, CreateFormCaption("dbcDefault-caption", "Default"), defaultLine);
    dialog->AddChild(grid);

    if (form->generated) {
        type->SetDisabled(true);
        common->SetDisabled(true);
        notNull->SetDisabled(true);
        hasDefault->SetDisabled(true);
    }
    hasDefault->onChecked   = [form]() { SyncDefault(*form); };
    hasDefault->onUnchecked = [form]() { SyncDefault(*form); };
    SyncDefault(*form);

    auto error = MakeError("dbcError");
    form->error = error.get();
    dialog->AddChild(error);
    AddButtons(dialog.get(), "dbcButtons", adding ? "Add column" : "Stage change", [form]() { SaveColumn(*form); });
    dialog->AddChild(MakeIntro("dbcIntro", intro));
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowIndexDialog(UltraCanvasWindowBase* parent, const StructureEdit& edit, const std::string& firstColumn,
                     SaveIndexFn onSave) {
    auto form = std::make_shared<IndexForm>();
    form->onSave = std::move(onSave);
    form->table = edit.NewName().empty() ? edit.Table().name : edit.NewName();

    std::vector<std::string> columns;
    for (const auto& draft : edit.Columns())
        if (!draft.dropped) columns.push_back(draft.name);
    if (std::find(columns.begin(), columns.end(), firstColumn) != columns.end()) form->columns.push_back(firstColumn);

    const int listHeight = std::min(260, 26 * static_cast<int>(columns.size()));
    auto dialog = MakeDialog("New index on " + edit.Table().name, 300 + listHeight);
    if (!dialog) return;
    form->dialog = dialog.get();

    auto grid = CreateFormGrid("dbiForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);
    auto name = CreateTextInput("dbiName", 0, 0, 0, kRowHeight);
    form->name = name.get();
    AddFormRow(grid, CreateFormCaption("dbiName-caption", "Name"), name);
    auto unique = UltraCanvasCheckbox::CreateCheckbox("dbiUnique", 0, 0, 340, 24,
                                                      "UNIQUE - no two rows share a key", false);
    form->unique = unique.get();
    AddFormRow(grid, CreateFormCaption("dbiUnique-caption", "Kind"), unique);
    dialog->AddChild(grid);

    // The columns, one box each; they scroll when there are many.
    auto list = CreateContainer("dbiColumns", 0, 0, 0, static_cast<float>(listHeight));
    list->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    int index = 0;
    for (const auto& column : columns) {
        const bool ticked = !form->columns.empty() && form->columns.front() == column;
        auto box = UltraCanvasCheckbox::CreateCheckbox("dbiCol" + std::to_string(index++), 0, 0, 0, 24, column, ticked);
        box->layoutItem.SetFlexShrink(0);
        box->onChecked = [form, column]() {
            if (std::find(form->columns.begin(), form->columns.end(), column) == form->columns.end())
                form->columns.push_back(column);
            SyncIndex(*form);
        };
        box->onUnchecked = [form, column]() {
            form->columns.erase(std::remove(form->columns.begin(), form->columns.end(), column), form->columns.end());
            SyncIndex(*form);
        };
        list->AddChild(box);
    }
    dialog->AddChild(list);

    auto order = CreateLabel("dbiOrder", 0, 0, 0, 24, "");
    order->layoutItem.SetFlexShrink(0);
    form->order = order.get();
    dialog->AddChild(order);
    // The made-up name follows the columns until the user writes another.
    form->autoName = name->GetText();
    SyncIndex(*form);

    auto error = MakeError("dbiError");
    form->error = error.get();
    dialog->AddChild(error);
    AddButtons(dialog.get(), "dbiButtons", "Add index", [form]() { SaveIndex(*form); });
    dialog->AddChild(MakeIntro("dbiIntro", "The index is staged: Apply in Structure creates it. The key is in the "
                                           "order you tick the columns."));
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin
