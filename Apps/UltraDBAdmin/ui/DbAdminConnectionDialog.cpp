// Apps/UltraDBAdmin/ui/DbAdminConnectionDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnectionDialog.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextInput.h"

#include <memory>
#include <utility>
#include <vector>

using namespace UltraCanvas;

namespace UltraDBAdmin {
namespace {

constexpr int kDialogWidth  = 520;
constexpr int kDialogHeight = 470;
constexpr float kRowHeight  = 28.0f;

// The TLS dropdown, in UltraDbTls order.
const char* const kTlsLabels[] = {
    "Off - never encrypt",
    "Prefer - encrypt when the server offers it",
    "Require - encrypt, do not check the certificate",
    "Verify - encrypt and check certificate and host name",
};

std::string Trim(const std::string& text) {
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// 0 for an empty field (the driver's default port), -1 for anything that is
// not a port number.
int ParsePort(const std::string& text) {
    const std::string t = Trim(text);
    if (t.empty()) return 0;
    if (t.size() > 5) return -1;
    int value = 0;
    for (const char c : t) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value >= 1 && value <= 65535 ? value : -1;
}

// What the form's callbacks share. The widgets belong to the dialog; this
// holds plain pointers to them, so no callback stored on a widget owns the
// widget or the dialog above it.
struct Form {
    UltraCanvasModalDialog* dialog = nullptr;
    UltraCanvasTextInput* name = nullptr;
    UltraCanvasDropdown* driver = nullptr;
    UltraCanvasTextInput* file = nullptr;
    UltraCanvasTextInput* host = nullptr;
    UltraCanvasTextInput* port = nullptr;
    UltraCanvasTextInput* database = nullptr;
    UltraCanvasTextInput* user = nullptr;
    UltraCanvasTextInput* password = nullptr;
    UltraCanvasCheckbox* forget = nullptr;
    UltraCanvasDropdown* tls = nullptr;
    UltraCanvasCheckbox* readOnly = nullptr;
    UltraCanvasCheckbox* production = nullptr;
    UltraCanvasLabel* error = nullptr;
    // The rows only one type has: caption and control, hidden together.
    std::vector<UltraCanvasUIElement*> sqliteRows;
    std::vector<UltraCanvasUIElement*> serverRows;
    ConnectionSpec original;
    SaveConnectionFn onSave;
};

bool IsSqliteChoice(const Form& form) { return form.driver->GetSelectedIndex() != 1; }

void ShowRowsFor(Form& form) {
    const bool sqlite = IsSqliteChoice(form);
    for (auto* element : form.sqliteRows) element->SetVisible(sqlite);
    for (auto* element : form.serverRows) element->SetVisible(!sqlite);
    form.error->SetText("");
}

void Save(Form& form) {
    ConnectionSpec spec = form.original;
    spec.name = Trim(form.name->GetText());
    spec.driver = IsSqliteChoice(form) ? "sqlite" : "postgresql";
    spec.readOnly = form.readOnly->IsChecked();
    spec.production = form.production->IsChecked();
    std::string password;
    bool clearPassword = false;
    if (spec.IsSqlite()) {
        spec.database = Trim(form.file->GetText());
        spec.host.clear();
        spec.port = 0;
        spec.user.clear();
        // A SQLite file has no password; one stored while this was a server
        // connection goes.
        clearPassword = spec.hasPassword;
        if (spec.database.empty()) {
            form.error->SetText("Choose the database file, or type the path of a new one.");
            return;
        }
    } else {
        spec.host = Trim(form.host->GetText());
        spec.database = Trim(form.database->GetText());
        spec.user = Trim(form.user->GetText());
        const int port = ParsePort(form.port->GetText());
        if (port < 0) {
            form.error->SetText("The port is a number from 1 to 65535, or empty for the default (5432).");
            return;
        }
        spec.port = port;
        const int tls = form.tls->GetSelectedIndex();
        spec.tls = tls >= 0 && tls <= 3 ? static_cast<UltraDbTls>(tls) : UltraDbTls::VerifyFull;
        if (spec.database.empty()) {
            form.error->SetText("Name the database to connect to.");
            return;
        }
        password = form.password->GetText();
        clearPassword = password.empty() && form.forget && form.forget->IsChecked();
    }
    if (spec.name.empty()) spec.name = Describe(spec);
    const std::string error = form.onSave ? form.onSave(spec, password, clearPassword) : std::string();
    if (!error.empty()) {
        form.error->SetText(error);
        return;
    }
    form.dialog->CloseDialog(DialogResult::OK);
}

void ChooseFile(Form& form) {
    FileDialogOptions options;
    options.SetTitle("SQLite database")
           .AddFilter("SQLite databases", std::vector<std::string>{ "db", "sqlite", "sqlite3", "db3" })
           .AddFilter("All files", "*");
    UltraCanvasTextInput* file = form.file;
    // The dialog outlives the file chooser it opened: the chooser is modal
    // over it, so `file` is still there when the answer comes.
    UltraCanvasFileLoader::OpenFileDialog(options, [file](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) file->SetText(path);
    });
}

// A caption and its control as one grid row, remembered as one of `rows`.
template <typename T>
void AddTypedRow(const std::shared_ptr<UltraCanvasContainer>& grid, const std::string& id,
                 const std::string& caption, const std::shared_ptr<T>& control,
                 std::vector<UltraCanvasUIElement*>& rows) {
    auto label = AddFormRow(grid, id, caption, control);
    rows.push_back(label.get());
    rows.push_back(control.get());
}

} // namespace

void ShowConnectionDialog(UltraCanvasWindowBase* parent, const ConnectionSpec* existing,
                          SaveConnectionFn onSave) {
    auto form = std::make_shared<Form>();
    if (existing) form->original = *existing;
    form->onSave = std::move(onSave);
    const ConnectionSpec& spec = form->original;

    DialogConfig config;
    config.title      = existing ? "Edit connection" : "Add connection";
    config.width      = kDialogWidth;
    config.height     = kDialogHeight;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;   // its own, so a mistake keeps it open
    config.position   = DialogPosition::CenterParent;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    if (!dialog) return;
    form->dialog = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto grid = CreateFormGrid("dbcForm", 8.0f, 10.0f);

    auto name = CreateTextInput("dbcName", 0, 0, 0, kRowHeight);
    name->SetText(spec.name);
    name->SetPlaceholder("What the tree calls it (default: the database)");
    AddFormRow(grid, "dbcName", "Name:", name);
    form->name = name.get();

    auto driver = CreateDropdown("dbcDriver", 0, 0, 0, kRowHeight);
    driver->AddItem("SQLite file", "sqlite");
    driver->AddItem("PostgreSQL server", "postgresql");
    driver->SetSelectedIndex(spec.IsSqlite() ? 0 : 1, /*runNotifications=*/false);
    AddFormRow(grid, "dbcDriver", "Type:", driver);
    form->driver = driver.get();

    // SQLite: the file, typed or chosen.
    auto fileRow = CreateFormCellRow("dbcFileRow", 8.0f, static_cast<int>(kRowHeight));
    auto file = CreateTextInput("dbcFile", 0, 0, 0, kRowHeight);
    file->SetText(spec.IsSqlite() ? spec.database : std::string());
    file->SetPlaceholder("/path/to/database.db");
    file->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    fileRow->AddChild(file);
    auto browse = CreateButton("dbcBrowse", 0, 0, 0, kRowHeight, "Choose…");
    browse->layoutItem.SetFlexShrink(0);
    fileRow->AddChild(browse);
    AddTypedRow(grid, "dbcFile", "File:", fileRow, form->sqliteRows);
    form->file = file.get();
    browse->SetOnClick([form]() { ChooseFile(*form); });

    // PostgreSQL: where, who, how.
    const bool server = !spec.IsSqlite();
    auto host = CreateTextInput("dbcHost", 0, 0, 0, kRowHeight);
    host->SetText(server ? spec.host : std::string());
    host->SetPlaceholder("localhost, a name, an address or a socket directory");
    AddTypedRow(grid, "dbcHost", "Host:", host, form->serverRows);
    form->host = host.get();

    auto port = CreateTextInput("dbcPort", 0, 0, 0, kRowHeight);
    port->SetText(server && spec.port > 0 ? std::to_string(spec.port) : std::string());
    port->SetPlaceholder("5432");
    AddTypedRow(grid, "dbcPort", "Port:", port, form->serverRows);
    form->port = port.get();

    auto database = CreateTextInput("dbcDatabase", 0, 0, 0, kRowHeight);
    database->SetText(server ? spec.database : std::string());
    AddTypedRow(grid, "dbcDatabase", "Database:", database, form->serverRows);
    form->database = database.get();

    auto user = CreateTextInput("dbcUser", 0, 0, 0, kRowHeight);
    user->SetText(server ? spec.user : std::string());
    AddTypedRow(grid, "dbcUser", "User:", user, form->serverRows);
    form->user = user.get();

    auto password = CreatePasswordInput("dbcPassword", 0, 0, 0, kRowHeight);
    password->SetPlaceholder(spec.hasPassword ? "stored in the vault - type to replace it"
                                              : "kept in the app's encrypted vault");
    AddTypedRow(grid, "dbcPassword", "Password:", password, form->serverRows);
    form->password = password.get();

    if (spec.hasPassword) {
        auto forget = UltraCanvasCheckbox::CreateCheckbox("dbcForget", 0, 0, 0, 24,
                                                          "Forget the stored password", false);
        AddFormWideRow(grid, forget);
        form->serverRows.push_back(forget.get());
        form->forget = forget.get();
    }

    auto tls = CreateDropdown("dbcTls", 0, 0, 0, kRowHeight);
    for (const char* label : kTlsLabels) tls->AddItem(label);
    tls->SetSelectedIndex(static_cast<int>(spec.tls), /*runNotifications=*/false);
    AddTypedRow(grid, "dbcTls", "Encryption:", tls, form->serverRows);
    form->tls = tls.get();

    auto readOnly = UltraCanvasCheckbox::CreateCheckbox("dbcReadOnly", 0, 0, 0, 24,
                                                        "Read only - the database refuses every change",
                                                        spec.readOnly);
    AddFormWideRow(grid, readOnly);
    form->readOnly = readOnly.get();

    auto production = UltraCanvasCheckbox::CreateCheckbox("dbcProduction", 0, 0, 0, 24,
                                                          "Production - shown in red, so a slip is visible",
                                                          spec.production);
    AddFormWideRow(grid, production);
    form->production = production.get();

    dialog->AddChild(grid);

    auto error = CreateLabel("dbcError", 0, 0, 0, 36, "");
    error->SetTextColor(Color(176, 32, 32));
    error->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(error);
    form->error = error.get();

    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    auto buttons = CreateContainer("dbcButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->AddStretchSpacer(1);
    auto cancel = CreateButton("dbcCancel", 0, 0, 90, kRowHeight, "Cancel");
    cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto save = CreateButton("dbcSave", 0, 0, 90, kRowHeight, "Save");
    save->SetStyle(ButtonStyles::PrimaryStyle());
    save->SetOnClick([form]() { Save(*form); });
    buttons->AddChild(save);
    dialog->AddChild(buttons);

    driver->onSelectionChanged = [form](int, const DropdownItem&) { ShowRowsFor(*form); };
    ShowRowsFor(*form);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin
