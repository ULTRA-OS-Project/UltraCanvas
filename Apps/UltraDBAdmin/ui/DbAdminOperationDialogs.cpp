// Apps/UltraDBAdmin/ui/DbAdminOperationDialogs.cpp
// Version: 0.3.0 - the data compare dialog
// Version: 0.2.0 - MySQL / MariaDB types
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminOperationDialogs.h"

#include "DbAdminModels.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"

#include <algorithm>
#include <memory>
#include <utility>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

std::shared_ptr<UltraCanvasModalDialog> MakeDialog(const std::string& title, int width, int height) {
    DialogConfig config;
    config.title      = title;
    config.width      = width;
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

std::shared_ptr<UltraCanvasLabel> Intro(const std::string& id, const std::string& text, float height = 40) {
    auto label = CreateLabel(id, 0, 0, 0, height, text);
    label->SetWrap(TextWrap::WrapWord);
    label->layoutItem.SetFlexShrink(0);
    return label;
}

std::shared_ptr<UltraCanvasLabel> ErrorLine(const std::string& id) {
    auto error = CreateLabel(id, 0, 0, 0, 36, "");
    error->SetTextColor(ErrorColor());
    error->SetWrap(TextWrap::WrapWord);
    error->layoutItem.SetFlexShrink(0);
    return error;
}

std::shared_ptr<UltraCanvasContainer> ButtonRow(const std::string& id) {
    auto buttons = CreateContainer(id, 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->layoutItem.SetFlexShrink(0);
    return buttons;
}

std::shared_ptr<UltraCanvasTextArea> SqlView(const std::string& id, const std::string& sql) {
    // Text, not Markdown: a statement is shown exactly as it reads.
    auto area = std::make_shared<UltraCanvasTextArea>(id);
    area->ApplyCodeStyle("SQL");
    area->SetWordWrap(true);
    area->SetText(sql, false);
    area->SetReadOnly(true);
    area->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    return area;
}

// ----- the options dialog -----

struct OperationForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasTextInput* name = nullptr;
    std::vector<UltraCanvasCheckbox*> options;
    UltraCanvasLabel* error = nullptr;
    UltraCanvasTextArea* preview = nullptr;
    std::function<bool(const OperationChoice&, std::string&)> onPreview;
    std::function<std::string(const OperationChoice&)> onRun;

    OperationChoice Choice() const {
        OperationChoice choice;
        if (name) choice.name = name->GetText();
        for (const auto* box : options) choice.options.push_back(box->IsChecked());
        return choice;
    }
};

void Preview(OperationForm& form) {
    std::string text;
    const bool ok = form.onPreview && form.onPreview(form.Choice(), text);
    form.error->SetText(ok ? std::string() : text);
    form.preview->SetText(ok ? text : std::string(), false);
}

// ----- the new-table dialog -----

struct ColumnLine {
    std::shared_ptr<UltraCanvasContainer> row;
    UltraCanvasTextInput* name = nullptr;
    UltraCanvasTextInput* type = nullptr;
    UltraCanvasCheckbox* notNull = nullptr;
    UltraCanvasCheckbox* key = nullptr;
    UltraCanvasCheckbox* counted = nullptr;
    UltraCanvasTextInput* defaultValue = nullptr;
};

struct NewTableForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasContainer* list = nullptr;
    UltraCanvasTextInput* name = nullptr;
    UltraCanvasLabel* error = nullptr;
    UltraCanvasTextArea* preview = nullptr;
    std::vector<ColumnLine> lines;
    SqlEngine engine = SqlEngine::Sqlite;
    int next = 0;
    std::function<bool(const std::string&, const std::vector<NewColumn>&, std::string&)> onPreview;
    std::function<std::string(const std::string&, const std::vector<NewColumn>&)> onCreate;

    // The lines that say anything, as columns; an empty line is left out.
    std::vector<NewColumn> Columns() const {
        std::vector<NewColumn> columns;
        for (const auto& line : lines) {
            NewColumn column;
            column.name = line.name->GetText();
            column.type = line.type->GetText();
            if (column.name.empty() && column.type.empty()) continue;
            column.notNull = line.notNull->IsChecked();
            column.primaryKey = line.key->IsChecked();
            column.autoIncrement = line.counted->IsChecked();
            column.defaultExpression = line.defaultValue->GetText();
            column.hasDefault = !column.defaultExpression.empty();
            columns.push_back(std::move(column));
        }
        return columns;
    }
};

constexpr float kLineHeight = 28.0f;

std::shared_ptr<UltraCanvasCheckbox> LineBox(const std::string& id, const std::string& label, float width,
                                             const std::string& tip, bool checked) {
    auto box = UltraCanvasCheckbox::CreateCheckbox(id, 0, 0, width, 24, label, checked);
    box->layoutItem.SetFlexShrink(0);
    box->SetTooltip(tip);
    return box;
}

void AddColumnLine(const std::shared_ptr<NewTableForm>& form, const NewColumn& column) {
    const std::string id = "dbnLine" + std::to_string(form->next++);
    ColumnLine line;
    line.row = CreateContainer(id, 0, 0, 0, kLineHeight);
    line.row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    line.row->layoutItem.SetFlexShrink(0);
    const bool sqlite = form->engine == SqlEngine::Sqlite;

    auto name = CreateTextInput(id + "-name", 0, 0, 140, kLineHeight);
    name->SetPlaceholder("name");
    name->SetText(column.name);
    name->layoutItem.SetFlexShrink(0);
    line.name = name.get();
    line.row->AddChild(name);

    auto type = CreateTextInput(id + "-type", 0, 0, 130, kLineHeight);
    const bool mysql = form->engine == SqlEngine::MySql;
    type->SetPlaceholder(sqlite ? "TEXT" : mysql ? "varchar(80)" : "text");
    type->SetText(column.type);
    type->SetTooltip(sqlite  ? "INTEGER, TEXT, REAL, BLOB, NUMERIC - or none at all"
                     : mysql ? "int, bigint, varchar(80), text, decimal(10,2), tinyint(1), date, datetime, "
                               "timestamp, json, blob ..."
                             : "integer, bigint, text, varchar(80), numeric(10,2), boolean, date, timestamptz, "
                               "uuid, jsonb, bytea ...");
    type->layoutItem.SetFlexShrink(0);
    line.type = type.get();
    line.row->AddChild(type);

    auto notNull = LineBox(id + "-notnull", "NOT NULL", 106, "The column must hold a value", column.notNull);
    line.notNull = notNull.get();
    line.row->AddChild(notNull);
    auto key = LineBox(id + "-key", "Key", 56, "Part of the primary key: Browse edits rows by it", column.primaryKey);
    line.key = key.get();
    line.row->AddChild(key);
    auto counted = LineBox(id + "-counted", "Numbered", 100,
                           sqlite ? "INTEGER PRIMARY KEY AUTOINCREMENT: the database numbers new rows"
                                  : "An identity column: the database numbers new rows",
                           column.autoIncrement);
    line.counted = counted.get();
    line.row->AddChild(counted);

    auto defaultValue = CreateTextInput(id + "-default", 0, 0, 0, kLineHeight);
    defaultValue->SetPlaceholder("default (SQL)");
    defaultValue->SetTooltip("Written after DEFAULT, as SQL: 0, 'none', CURRENT_TIMESTAMP");
    defaultValue->SetText(column.defaultExpression);
    defaultValue->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    line.defaultValue = defaultValue.get();
    line.row->AddChild(defaultValue);

    auto remove = MakeActionButton(id + "-remove", "", "delete", "Take this column out", ButtonRole::Danger);
    UltraCanvasContainer* row = line.row.get();
    std::weak_ptr<NewTableForm> weak = form;
    remove->SetOnClick([weak, row]() {
        auto form = weak.lock();
        if (!form) return;
        for (auto it = form->lines.begin(); it != form->lines.end(); ++it) {
            if (it->row.get() != row) continue;
            auto keep = it->row;                // removed from the list after it is out of the dialog
            form->lines.erase(it);
            form->list->RemoveChild(keep);
            break;
        }
    });
    line.row->AddChild(remove);
    form->list->AddChild(line.row);
    form->lines.push_back(std::move(line));
}

} // namespace

void ShowPlanDialog(UltraCanvasWindowBase* parent, const std::string& title, const std::string& intro,
                    const std::string& sql, const std::string& action, ButtonRole role, std::function<void()> onRun) {
    auto dialog = MakeDialog(title, 760, 460);
    if (!dialog) return;
    UltraCanvasModalDialog* raw = dialog.get();
    dialog->AddChild(Intro("dbpIntro", intro, 54));
    dialog->AddChild(SqlView("dbpSql", sql));
    auto buttons = ButtonRow("dbpButtons");
    buttons->AddStretchSpacer(1);
    auto cancel = MakeDialogButton("dbpCancel", "Cancel", 90);
    cancel->SetOnClick([raw]() { raw->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto run = MakeDialogButton("dbpRun", action, 140, role);
    run->SetOnClick([raw, onRun = std::move(onRun)]() {
        const std::function<void()> then = onRun;   // the button belongs to the dialog
        raw->CloseDialog(DialogResult::OK);
        if (then) then();
    });
    buttons->AddChild(run);
    dialog->AddChild(buttons);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowOperationDialog(UltraCanvasWindowBase* parent, const OperationDialogSpec& spec,
                         std::function<bool(const OperationChoice&, std::string&)> onPreview,
                         std::function<std::string(const OperationChoice&)> onRun) {
    auto form = std::make_shared<OperationForm>();
    form->onPreview = std::move(onPreview);
    form->onRun = std::move(onRun);
    auto dialog = MakeDialog(spec.title, 680, 470);
    if (!dialog) return;
    form->dialog = dialog.get();
    dialog->AddChild(Intro("dboIntro", spec.intro, 44));

    auto grid = CreateFormGrid("dboForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);
    if (!spec.nameLabel.empty()) {
        auto name = CreateTextInput("dboName", 0, 0, 0, kLineHeight);
        name->SetText(spec.name);
        name->onEnterPressed = [form](const std::string&) { Preview(*form); return true; };
        form->name = name.get();
        AddFormRow(grid, "dboNameRow", spec.nameLabel, name);
    }
    for (size_t i = 0; i < spec.options.size(); ++i) {
        auto box = UltraCanvasCheckbox::CreateCheckbox("dboOption" + std::to_string(i), 0, 0, 0, 24,
                                                       spec.options[i].first, spec.options[i].second);
        if (i < spec.optionTips.size()) box->SetTooltip(spec.optionTips[i]);
        box->onChecked = [form]() { Preview(*form); };
        box->onUnchecked = [form]() { Preview(*form); };
        form->options.push_back(box.get());
        AddFormRow(grid, "dboOptionRow" + std::to_string(i), i == 0 ? std::string("Options") : std::string(), box);
    }
    dialog->AddChild(grid);

    auto preview = SqlView("dboSql", "");
    form->preview = preview.get();
    dialog->AddChild(preview);
    auto error = ErrorLine("dboError");
    form->error = error.get();
    dialog->AddChild(error);

    auto buttons = ButtonRow("dboButtons");
    auto show = MakeDialogButton("dboPreview", "Preview SQL", 120);
    show->SetOnClick([form]() { Preview(*form); });
    buttons->AddChild(show);
    buttons->AddStretchSpacer(1);
    auto cancel = MakeDialogButton("dboCancel", "Cancel", 90);
    cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto run = MakeDialogButton("dboRun", spec.action, 140, spec.role);
    run->SetOnClick([form]() {
        const std::string failed = form->onRun ? form->onRun(form->Choice()) : std::string();
        if (!failed.empty()) { form->error->SetText(failed); return; }
        form->dialog->CloseDialog(DialogResult::OK);
    });
    buttons->AddChild(run);
    dialog->AddChild(buttons);
    Preview(*form);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowNewTableDialog(UltraCanvasWindowBase* parent, SqlEngine engine, const std::string& schema,
                        std::function<bool(const std::string&, const std::vector<NewColumn>&, std::string&)> onPreview,
                        std::function<std::string(const std::string&, const std::vector<NewColumn>&)> onCreate) {
    auto form = std::make_shared<NewTableForm>();
    form->engine = engine;
    form->onPreview = std::move(onPreview);
    form->onCreate = std::move(onCreate);
    auto dialog = MakeDialog("New table in " + schema, 820, 600);
    if (!dialog) return;
    form->dialog = dialog.get();
    dialog->AddChild(Intro("dbnIntro",
                           "A line per column; an empty line is left out. A type and a default are SQL, as the "
                           "database writes them. More columns, indexes and keys can follow in Structure.",
                           40));

    auto nameRow = ButtonRow("dbnNameRow");
    auto nameLabel = CreateLabel("dbnNameLabel", 0, 0, 60, 24, "Name");
    nameLabel->layoutItem.SetFlexShrink(0);
    nameRow->AddChild(nameLabel);
    auto name = CreateTextInput("dbnName", 0, 0, 260, kLineHeight);
    name->SetPlaceholder("the table's name");
    name->layoutItem.SetFlexShrink(0);
    form->name = name.get();
    nameRow->AddChild(name);
    dialog->AddChild(nameRow);

    // The column lines scroll when there are more than fit.
    auto list = CreateContainer("dbnColumns", 0, 0, 0, 0);
    list->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    form->list = list.get();
    dialog->AddChild(list);
    const bool sqlite = engine == SqlEngine::Sqlite;
    NewColumn id;
    id.name = "id";
    const bool mysql = engine == SqlEngine::MySql;
    id.type = sqlite ? "INTEGER" : mysql ? "int" : "integer";
    id.primaryKey = id.autoIncrement = true;
    AddColumnLine(form, id);
    NewColumn text;
    text.name = "name";
    text.type = sqlite ? "TEXT" : mysql ? "varchar(255)" : "text";
    AddColumnLine(form, text);
    AddColumnLine(form, NewColumn());

    auto preview = SqlView("dbnSql", "");
    preview->layoutItem.SetFlexGrow(0.6f);
    form->preview = preview.get();
    dialog->AddChild(preview);
    auto error = ErrorLine("dbnError");
    form->error = error.get();
    dialog->AddChild(error);

    auto buttons = ButtonRow("dbnButtons");
    auto add = MakeDialogButton("dbnAdd", "Add column", 120);
    add->SetIcon(IconPath("add"));
    add->SetIconSize(16, 16);
    std::weak_ptr<NewTableForm> weak = form;
    add->SetOnClick([weak]() {
        if (auto form = weak.lock()) AddColumnLine(form, NewColumn());
    });
    buttons->AddChild(add);
    auto show = MakeDialogButton("dbnPreview", "Preview SQL", 120);
    show->SetOnClick([form]() {
        std::string text;
        const bool ok = form->onPreview && form->onPreview(form->name->GetText(), form->Columns(), text);
        form->error->SetText(ok ? std::string() : text);
        form->preview->SetText(ok ? text : std::string(), false);
    });
    buttons->AddChild(show);
    buttons->AddStretchSpacer(1);
    auto cancel = MakeDialogButton("dbnCancel", "Cancel", 90);
    cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto create = MakeDialogButton("dbnCreate", "Create table", 130, ButtonRole::Primary);
    create->SetOnClick([form]() {
        const std::string failed = form->onCreate ? form->onCreate(form->name->GetText(), form->Columns()) : std::string();
        if (!failed.empty()) { form->error->SetText(failed); return; }
        form->dialog->CloseDialog(DialogResult::OK);
    });
    buttons->AddChild(create);
    dialog->AddChild(buttons);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowTextDialog(UltraCanvasWindowBase* parent, const std::string& title, const std::string& intro,
                    const std::string& text) {
    auto dialog = MakeDialog(title, 640, 420);
    if (!dialog) return;
    UltraCanvasModalDialog* raw = dialog.get();
    dialog->AddChild(Intro("dbtIntro", intro));
    auto area = std::make_shared<UltraCanvasTextArea>("dbtText");
    area->ApplyPlainTextStyle();
    area->SetFont("monospace", 11.0f);
    area->SetShowLineNumbers(false);
    area->SetWordWrap(true);
    area->SetText(text, false);
    area->SetReadOnly(true);
    area->SetBorders(1.0f, UltraCanvasTheme::Current().roles.line, 6.0f);
    area->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    dialog->AddChild(area);
    auto buttons = ButtonRow("dbtButtons");
    buttons->AddStretchSpacer(1);
    auto ok = MakeDialogButton("dbtOk", "OK", 90, ButtonRole::Primary);
    ok->SetOnClick([raw]() { raw->CloseDialog(DialogResult::OK); });
    buttons->AddChild(ok);
    dialog->AddChild(buttons);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

namespace {

struct CompareForm {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasDropdown* connection = nullptr;
    UltraCanvasTextInput* schema = nullptr;
    UltraCanvasTextInput* table = nullptr;
    UltraCanvasLabel* summary = nullptr;
    UltraCanvasButton* compare = nullptr;
    UltraCanvasButton* sync = nullptr;
    std::shared_ptr<ResultSetModel> model;
    UltraCanvasListView* grid = nullptr;
    bool busy = false;
    std::function<void(const CompareChoice&, CompareShown)> onCompare;
    std::function<void(const CompareChoice&, std::function<void()>)> onSync;

    CompareChoice Choice() const {
        CompareChoice choice;
        choice.connection = connection->GetSelectedIndex();
        choice.schema = schema->GetText();
        choice.table = table->GetText();
        return choice;
    }
};

void RunCompare(const std::shared_ptr<CompareForm>& form) {
    if (form->busy || !form->onCompare) return;
    form->busy = true;
    form->compare->SetDisabled(true);
    form->sync->SetDisabled(true);
    form->summary->SetText("Comparing…");
    form->summary->SetTextColor(StatusColor());
    // The dialog may be closed before the answer comes.
    std::weak_ptr<CompareForm> weak = form;
    form->onCompare(form->Choice(), [weak](const std::string& error, const std::string& summary,
                                           const UltraDbResultSet& rows, bool canSync) {
        auto form = weak.lock();
        if (!form) return;
        form->busy = false;
        form->compare->SetDisabled(false);
        form->sync->SetDisabled(!error.empty() || !canSync);
        form->summary->SetText(error.empty() ? summary : error);
        form->summary->SetTextColor(error.empty() ? StatusColor() : ErrorColor());
        form->model->Replace(rows);
        FitResultColumns(*form->grid);
    });
}

} // namespace

void ShowCompareDialog(UltraCanvasWindowBase* parent, const CompareDialogSpec& spec,
                       std::function<void(const CompareChoice&, CompareShown)> onCompare,
                       std::function<void(const CompareChoice&, std::function<void()>)> onSync) {
    auto form = std::make_shared<CompareForm>();
    form->onCompare = std::move(onCompare);
    form->onSync = std::move(onSync);
    auto dialog = MakeDialog(spec.title, 860, 600);
    if (!dialog) return;
    form->dialog = dialog.get();
    dialog->AddChild(Intro("dbdIntro", spec.intro, 40));

    auto grid = CreateFormGrid("dbdForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);
    auto connection = CreateDropdown("dbdConnection", 0, 0, 0, kLineHeight);
    for (const std::string& name : spec.connections) connection->AddItem(name);
    if (!spec.connections.empty())
        connection->SetSelectedIndex(std::clamp(spec.connection, 0, static_cast<int>(spec.connections.size()) - 1), false);
    form->connection = connection.get();
    AddFormRow(grid, "dbdConnectionRow", "Compare with", connection);
    auto schema = CreateTextInput("dbdSchema", 0, 0, 0, kLineHeight);
    schema->SetText(spec.schema);
    schema->SetPlaceholder("the schema (\"main\" for a SQLite file)");
    form->schema = schema.get();
    AddFormRow(grid, "dbdSchemaRow", "Schema", schema);
    auto table = CreateTextInput("dbdTable", 0, 0, 0, kLineHeight);
    table->SetText(spec.table);
    form->table = table.get();
    AddFormRow(grid, "dbdTableRow", "Table", table);
    dialog->AddChild(grid);

    auto summary = CreateLabel("dbdSummary", 0, 0, 0, 40, "Pick the other table, then Compare.");
    summary->SetWrap(TextWrap::WrapWord);
    summary->SetTextColor(StatusColor());
    summary->layoutItem.SetFlexShrink(0);
    form->summary = summary.get();
    dialog->AddChild(summary);

    form->model = std::make_shared<ResultSetModel>();
    auto rows = std::make_shared<UltraCanvasListView>("dbdRows", -1, -1, 400, 200);
    rows->SetModel(form->model);
    rows->SetShowHeader(true);
    rows->SetShowItemTooltips(true);
    StyleList(*rows);
    FitResultColumns(*rows);
    rows->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexGrow(1).SetFlexShrink(1);
    rows->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    form->grid = rows.get();
    dialog->AddChild(rows);

    auto buttons = ButtonRow("dbdButtons");
    auto sync = MakeDialogButton("dbdSync", "Make the other the same…", 200);
    sync->SetTooltip("Insert, update and delete rows of the other table until it holds what this one holds - "
                     "the SQL is shown first");
    sync->SetDisabled(true);
    form->sync = sync.get();
    // The buttons hold the form, so it lives as long as the dialog; what
    // comes back later holds it weakly.
    std::weak_ptr<CompareForm> weak = form;
    sync->SetOnClick([form, weak]() {
        if (!form->onSync || form->busy) return;
        form->onSync(form->Choice(), [weak]() {
            if (auto again = weak.lock()) RunCompare(again);
        });
    });
    buttons->AddChild(sync);
    buttons->AddStretchSpacer(1);
    auto close = MakeDialogButton("dbdClose", "Close", 90);
    close->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(close);
    auto compare = MakeDialogButton("dbdCompare", "Compare", 110, ButtonRole::Primary);
    form->compare = compare.get();
    compare->SetOnClick([form]() { RunCompare(form); });
    buttons->AddChild(compare);
    dialog->AddChild(buttons);
    table->onEnterPressed = [form](const std::string&) {
        RunCompare(form);
        return true;
    };
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

void ShowRelayDialog(UltraCanvasWindowBase* parent, const std::string& url, bool cancellable,
                     std::function<void(const std::string&, const std::string&,
                                        std::function<void(const std::string&)>)> onConnect) {
    struct RelayForm {
        UltraCanvasModalDialog* dialog = nullptr;
        UltraCanvasTextInput* url = nullptr;
        UltraCanvasTextInput* token = nullptr;
        UltraCanvasLabel* error = nullptr;
        UltraCanvasButton* connect = nullptr;
        bool busy = false;
        std::function<void(const std::string&, const std::string&, std::function<void(const std::string&)>)> onConnect;
    };
    auto form = std::make_shared<RelayForm>();
    form->onConnect = std::move(onConnect);
    auto dialog = MakeDialog("Connect to a relay", 560, 300);
    if (!dialog) return;
    form->dialog = dialog.get();
    dialog->AddChild(Intro("dbrIntro",
                           "An UltraDBAdmin relay runs your queries on the databases its administrator set up, "
                           "with passwords that stay on the relay. Your token says who you are and what you may "
                           "read or change.",
                           56));
    auto grid = CreateFormGrid("dbrForm", 8.0f, 10.0f);
    grid->layoutItem.SetFlexShrink(0);
    auto address = CreateTextInput("dbrUrl", 0, 0, 0, kLineHeight);
    address->SetText(url);
    address->SetPlaceholder("wss://db.example.com/relay");
    form->url = address.get();
    AddFormRow(grid, "dbrUrlRow", "Relay", address);
    auto token = CreatePasswordInput("dbrToken", 0, 0, 0, kLineHeight);
    token->SetPlaceholder("the token the relay's administrator gave you");
    form->token = token.get();
    AddFormRow(grid, "dbrTokenRow", "Token", token);
    dialog->AddChild(grid);
    auto error = ErrorLine("dbrError");
    form->error = error.get();
    dialog->AddChild(error);
    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    auto buttons = ButtonRow("dbrButtons");
    buttons->AddStretchSpacer(1);
    if (cancellable) {
        auto cancel = MakeDialogButton("dbrCancel", "Cancel", 90);
        cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
        buttons->AddChild(cancel);
    }
    auto connect = MakeDialogButton("dbrConnect", "Connect", 110, ButtonRole::Primary);
    form->connect = connect.get();
    std::weak_ptr<RelayForm> weak = form;
    auto run = [form, weak]() {
        if (form->busy || !form->onConnect) return;
        const std::string address = form->url->GetText(), secret = form->token->GetText();
        if (address.rfind("ws://", 0) != 0 && address.rfind("wss://", 0) != 0) {
            form->error->SetText("The relay's address begins wss:// (or ws:// on this computer).");
            return;
        }
        if (secret.empty()) {
            form->error->SetText("Type your token.");
            return;
        }
        form->busy = true;
        form->connect->SetDisabled(true);
        form->error->SetTextColor(StatusColor());
        form->error->SetText("Asking the relay…");
        form->onConnect(address, secret, [weak](const std::string& failed) {
            auto form = weak.lock();
            if (!form) return;
            form->busy = false;
            form->connect->SetDisabled(false);
            if (failed.empty()) {
                form->dialog->CloseDialog(DialogResult::OK);
                return;
            }
            form->error->SetTextColor(ErrorColor());
            form->error->SetText(failed);
        });
    };
    connect->SetOnClick(run);
    token->onEnterPressed = [run](const std::string&) {
        run();
        return true;
    };
    buttons->AddChild(connect);
    dialog->AddChild(buttons);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin
