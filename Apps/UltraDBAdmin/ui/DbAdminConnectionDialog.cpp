// Apps/UltraDBAdmin/ui/DbAdminConnectionDialog.cpp
// Version: 0.4.0 - SSH tunnels: the bastion, signing in there, the host key trusted
// Version: 0.3.0 - MySQL / MariaDB servers, each with its default port
// Version: 0.2.0 - auto-commit
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminConnectionDialog.h"

#include "DbAdminTheme.h"

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
constexpr int kDialogHeight = 724;
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
    UltraCanvasCheckbox* autoCommit = nullptr;
    // The SSH tunnel.
    UltraCanvasCheckbox* tunnel = nullptr;
    UltraCanvasTextInput* sshHost = nullptr;
    UltraCanvasTextInput* sshPort = nullptr;
    UltraCanvasTextInput* sshUser = nullptr;
    UltraCanvasDropdown* sshAuth = nullptr;
    UltraCanvasTextInput* sshPassword = nullptr;
    UltraCanvasTextInput* sshKeyFile = nullptr;
    UltraCanvasTextInput* sshPassphrase = nullptr;
    UltraCanvasCheckbox* forgetHostKey = nullptr;
    UltraCanvasLabel* error = nullptr;
    // The rows only one type has: caption and control, hidden together.
    std::vector<UltraCanvasUIElement*> sqliteRows;
    std::vector<UltraCanvasUIElement*> serverRows;
    // Shown with a server and the tunnel ticked; and of those, what one way
    // of signing in at the bastion has.
    std::vector<UltraCanvasUIElement*> tunnelRows;
    std::vector<UltraCanvasUIElement*> sshPasswordRows;
    std::vector<UltraCanvasUIElement*> sshKeyRows;
    ConnectionSpec original;
    SaveConnectionFn onSave;
};

// The Type dropdown's items, in order.
const char* const kDrivers[] = {"sqlite", "postgresql", "mysql"};

std::string DriverChoice(const Form& form) {
    const int at = form.driver->GetSelectedIndex();
    return at >= 0 && at < 3 ? kDrivers[at] : kDrivers[0];
}

bool IsSqliteChoice(const Form& form) { return DriverChoice(form) == "sqlite"; }

int DefaultPort(const Form& form) { return DriverChoice(form) == "mysql" ? 3306 : 5432; }

// The sign-in dropdown's items, in order: ConnectionSpec::sshAuth.
const char* const kSshAuth[] = {"agent", "password", "key"};

std::string SshAuthChoice(const Form& form) {
    const int at = form.sshAuth->GetSelectedIndex();
    return at >= 0 && at < 3 ? kSshAuth[at] : kSshAuth[0];
}

void ShowRowsFor(Form& form) {
    const bool sqlite = IsSqliteChoice(form);
    for (auto* element : form.sqliteRows) element->SetVisible(sqlite);
    for (auto* element : form.serverRows) element->SetVisible(!sqlite);
    const bool tunnel = !sqlite && form.tunnel->IsChecked();
    for (auto* element : form.tunnelRows) element->SetVisible(tunnel);
    const std::string auth = SshAuthChoice(form);
    for (auto* element : form.sshPasswordRows) element->SetVisible(tunnel && auth == "password");
    for (auto* element : form.sshKeyRows) element->SetVisible(tunnel && auth == "key");
    // A server's own default port, and its kind of local socket.
    const bool mysql = DriverChoice(form) == "mysql";
    form.port->SetPlaceholder(std::to_string(DefaultPort(form)));
    form.host->SetPlaceholder(mysql ? "localhost, a name, an address or a socket file (/run/mysqld/mysqld.sock)"
                                    : "localhost, a name, an address or a socket directory");
    form.error->SetText("");
}

// The tunnel's fields into `spec` and `secrets`; false with the form saying
// what is missing.
bool SaveTunnel(Form& form, ConnectionSpec& spec, ConnectionStore::SecretChanges& secrets) {
    const ConnectionSpec& before = form.original;
    spec.sshHost = Trim(form.sshHost->GetText());
    spec.sshUser = Trim(form.sshUser->GetText());
    if (spec.sshHost.empty()) {
        form.error->SetText("Name the SSH server (the bastion) the database is reached through.");
        return false;
    }
    const int port = ParsePort(form.sshPort->GetText());
    if (port < 0) {
        form.error->SetText("The SSH port is a number from 1 to 65535, or empty for 22.");
        return false;
    }
    spec.sshPort = port;
    spec.sshAuth = SshAuthChoice(form);
    // Another bastion is another machine: the key trusted for the old one
    // says nothing about it.
    if (spec.sshHost != before.sshHost || form.forgetHostKey->IsChecked()) spec.sshHostKey.clear();
    const bool sameAuth = before.UsesTunnel() && before.sshAuth == spec.sshAuth;
    if (spec.sshAuth == "agent") {
        secrets.clearSshSecret = true;
        secrets.clearSshPassphrase = true;
    } else if (spec.sshAuth == "password") {
        secrets.sshSecret = form.sshPassword->GetText();
        secrets.clearSshPassphrase = true;
        if (secrets.sshSecret.empty() && !(sameAuth && before.hasSshSecret)) {
            form.error->SetText("Type the password for the SSH server.");
            return false;
        }
    } else {
        const std::string path = Trim(form.sshKeyFile->GetText());
        if (path.empty()) {
            if (!(sameAuth && before.hasSshSecret)) {
                form.error->SetText("Choose the private key file that signs in at the SSH server.");
                return false;
            }
        } else {
            const FileBytesResult file = UltraCanvasFileLoader::LoadFile(path);
            if (!file.success) {
                form.error->SetText("The key file cannot be read: " + file.error);
                return false;
            }
            const std::string text(file.bytes.begin(), file.bytes.end());
            if (text.rfind("-----BEGIN ", 0) != 0 || text.find("PRIVATE KEY-----") == std::string::npos) {
                form.error->SetText("That is not a private key file (it begins \"-----BEGIN ... PRIVATE KEY-----\"; "
                                    "the .pub file beside it is the public half).");
                return false;
            }
            secrets.sshSecret = text;
            // A new key brings its own passphrase, or none.
            secrets.clearSshPassphrase = true;
        }
        const std::string passphrase = form.sshPassphrase->GetText();
        if (!passphrase.empty()) {
            secrets.sshPassphrase = passphrase;
            secrets.clearSshPassphrase = false;
        }
    }
    return true;
}

void ChooseKeyFile(Form& form) {
    FileDialogOptions options;
    options.SetTitle("SSH private key")
           .AddFilter("All files", "*");
    UltraCanvasTextInput* file = form.sshKeyFile;
    UltraCanvasFileLoader::OpenFileDialog(options, [file](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) file->SetText(path);
    });
}

void Save(Form& form) {
    ConnectionSpec spec = form.original;
    spec.name = Trim(form.name->GetText());
    spec.driver = DriverChoice(form);
    spec.readOnly = form.readOnly->IsChecked();
    spec.production = form.production->IsChecked();
    spec.autoCommit = form.autoCommit->IsChecked();
    ConnectionStore::SecretChanges secrets;
    std::string& password = secrets.password;
    bool& clearPassword = secrets.clearPassword;
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
            form.error->SetText("The port is a number from 1 to 65535, or empty for the default (" +
                                std::to_string(DefaultPort(form)) + ").");
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
    if (spec.IsSqlite() || !form.tunnel->IsChecked()) {
        spec.sshHost.clear();   // the store drops its SSH secrets with it
    } else if (!SaveTunnel(form, spec, secrets)) {
        return;
    }
    if (spec.name.empty()) spec.name = Describe(spec);
    const std::string error = form.onSave ? form.onSave(spec, secrets) : std::string();
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
    dialog->SetFollowTheme(true);
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
    driver->AddItem("MySQL / MariaDB server", "mysql");
    driver->SetSelectedIndex(spec.IsSqlite() ? 0 : spec.IsMysql() ? 2 : 1, /*runNotifications=*/false);
    AddFormRow(grid, "dbcDriver", "Type:", driver);
    form->driver = driver.get();

    // SQLite: the file, typed or chosen.
    auto fileRow = CreateFormCellRow("dbcFileRow", 8.0f, static_cast<int>(kRowHeight));
    auto file = CreateTextInput("dbcFile", 0, 0, 0, kRowHeight);
    file->SetText(spec.IsSqlite() ? spec.database : std::string());
    file->SetPlaceholder("/path/to/database.db");
    file->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    fileRow->AddChild(file);
    auto browse = CreateButton("dbcBrowse", 0, 0, 0, kRowHeight, "Choose");
    StyleButton(*browse, ButtonRole::Standard);
    browse->SetPadding(4.0f, 11.0f);
    browse->layoutItem.SetFlexShrink(0);
    fileRow->AddChild(browse);
    AddTypedRow(grid, "dbcFile", "File:", fileRow, form->sqliteRows);
    form->file = file.get();
    browse->SetOnClick([form]() { ChooseFile(*form); });

    // A server: where, who, how.
    const bool server = !spec.IsSqlite();
    // Host and port on one line, as the SSH server's below.
    auto hostRow = CreateFormCellRow("dbcHostRow", 8.0f, static_cast<int>(kRowHeight));
    auto host = CreateTextInput("dbcHost", 0, 0, 0, kRowHeight);
    host->SetText(server ? spec.host : std::string());
    host->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    hostRow->AddChild(host);
    auto port = CreateTextInput("dbcPort", 0, 0, 64, kRowHeight);
    port->SetText(server && spec.port > 0 ? std::to_string(spec.port) : std::string());
    port->SetTooltip("The port; empty for the engine's own");
    port->layoutItem.SetFlexShrink(0);
    hostRow->AddChild(port);
    AddTypedRow(grid, "dbcHost", "Host, port:", hostRow, form->serverRows);
    form->host = host.get();
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

    // The SSH tunnel: host and port above are then the database's as the
    // bastion reaches it.
    auto tunnel = UltraCanvasCheckbox::CreateCheckbox("dbcTunnel", 0, 0, 0, 24,
                                                      "Through an SSH tunnel - the database sits behind a bastion",
                                                      server && spec.UsesTunnel());
    tunnel->SetTooltip("Host and port above are then the database's as the SSH server reaches it "
                       "(empty host: the SSH server itself).");
    AddFormWideRow(grid, tunnel);
    form->serverRows.push_back(tunnel.get());
    form->tunnel = tunnel.get();

    auto sshRow = CreateFormCellRow("dbcSshRow", 8.0f, static_cast<int>(kRowHeight));
    auto sshHost = CreateTextInput("dbcSshHost", 0, 0, 0, kRowHeight);
    sshHost->SetText(spec.sshHost);
    sshHost->SetPlaceholder("bastion.example.com");
    sshHost->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    sshRow->AddChild(sshHost);
    auto sshPort = CreateTextInput("dbcSshPort", 0, 0, 64, kRowHeight);
    sshPort->SetText(spec.sshPort > 0 ? std::to_string(spec.sshPort) : std::string());
    sshPort->SetPlaceholder("22");
    sshPort->layoutItem.SetFlexShrink(0);
    sshRow->AddChild(sshPort);
    AddTypedRow(grid, "dbcSshHost", "SSH server, port:", sshRow, form->tunnelRows);
    form->sshHost = sshHost.get();
    form->sshPort = sshPort.get();

    auto sshUser = CreateTextInput("dbcSshUser", 0, 0, 0, kRowHeight);
    sshUser->SetText(spec.sshUser);
    sshUser->SetPlaceholder("who signs in there");
    AddTypedRow(grid, "dbcSshUser", "SSH user:", sshUser, form->tunnelRows);
    form->sshUser = sshUser.get();

    auto sshAuth = CreateDropdown("dbcSshAuth", 0, 0, 0, kRowHeight);
    sshAuth->AddItem("The SSH agent, then my default keys", "agent");
    sshAuth->AddItem("A password", "password");
    sshAuth->AddItem("A private key file", "key");
    sshAuth->SetSelectedIndex(spec.sshAuth == "password" ? 1 : spec.sshAuth == "key" ? 2 : 0,
                              /*runNotifications=*/false);
    AddTypedRow(grid, "dbcSshAuth", "Sign in with:", sshAuth, form->tunnelRows);
    form->sshAuth = sshAuth.get();

    const bool storedSecret = spec.UsesTunnel() && spec.hasSshSecret;
    auto sshPassword = CreatePasswordInput("dbcSshPassword", 0, 0, 0, kRowHeight);
    sshPassword->SetPlaceholder(storedSecret && spec.sshAuth == "password" ? "stored in the vault - type to replace it"
                                                                          : "kept in the app's encrypted vault");
    AddTypedRow(grid, "dbcSshPassword", "SSH password:", sshPassword, form->sshPasswordRows);
    form->sshPassword = sshPassword.get();

    auto keyRow = CreateFormCellRow("dbcSshKeyRow", 8.0f, static_cast<int>(kRowHeight));
    auto keyFile = CreateTextInput("dbcSshKey", 0, 0, 0, kRowHeight);
    keyFile->SetPlaceholder(storedSecret && spec.sshAuth == "key" ? "stored in the vault - choose a file to replace it"
                                                                  : "~/.ssh/id_ed25519 - read into the vault");
    keyFile->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    keyRow->AddChild(keyFile);
    auto chooseKey = CreateButton("dbcSshKeyChoose", 0, 0, 0, kRowHeight, "Choose");
    StyleButton(*chooseKey, ButtonRole::Standard);
    chooseKey->SetPadding(4.0f, 11.0f);
    chooseKey->layoutItem.SetFlexShrink(0);
    keyRow->AddChild(chooseKey);
    AddTypedRow(grid, "dbcSshKey", "Private key:", keyRow, form->sshKeyRows);
    form->sshKeyFile = keyFile.get();
    chooseKey->SetOnClick([form]() { ChooseKeyFile(*form); });

    auto passphrase = CreatePasswordInput("dbcSshPassphrase", 0, 0, 0, kRowHeight);
    passphrase->SetPlaceholder(spec.UsesTunnel() && spec.hasSshPassphrase ? "stored in the vault - type to replace it"
                                                                         : "the key's, if it has one");
    AddTypedRow(grid, "dbcSshPassphrase", "Passphrase:", passphrase, form->sshKeyRows);
    form->sshPassphrase = passphrase.get();

    // The key trusted, and beside it a way to forget it.
    auto hostKeyRow = CreateFormCellRow("dbcSshHostKeyRow", 8.0f, static_cast<int>(kRowHeight));
    auto hostKey = CreateLabel("dbcSshHostKey", 0, 0, 0, kRowHeight,
                               spec.sshHostKey.empty() ? "asked to be trusted on the first connection"
                                                       : spec.sshHostKey);
    if (spec.sshHostKey.empty()) hostKey->SetTextColor(StatusColor());
    // The whole fingerprint in the tooltip: the label is too narrow for it.
    hostKey->SetTooltip((spec.sshHostKey.empty() ? std::string() : spec.sshHostKey + "\n") +
                        "The SSH server's host key, as ssh-keygen -l prints it. A server that presents another "
                        "is refused.");
    hostKey->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    hostKeyRow->AddChild(hostKey);
    auto forgetHostKey = UltraCanvasCheckbox::CreateCheckbox("dbcSshForgetKey", 0, 0, 80, 24, "Forget", false);
    forgetHostKey->SetTooltip("Forget the trusted host key - the server was reinstalled; the next connection asks again");
    forgetHostKey->layoutItem.SetFlexShrink(0);
    forgetHostKey->SetVisible(!spec.sshHostKey.empty());
    hostKeyRow->AddChild(forgetHostKey);
    AddTypedRow(grid, "dbcSshHostKey", "Host key:", hostKeyRow, form->tunnelRows);
    form->forgetHostKey = forgetHostKey.get();

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

    auto autoCommit = UltraCanvasCheckbox::CreateCheckbox(
        "dbcAutoCommit", 0, 0, 0, 24, "Auto-commit - a row change is written at once",
        spec.autoCommit);
    autoCommit->SetTooltip("As phpMyAdmin does. A deletion, and any change on a production connection, still asks first.");
    AddFormWideRow(grid, autoCommit);
    form->autoCommit = autoCommit.get();

    dialog->AddChild(grid);

    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    // What is missing, beside the buttons, where it costs the form no line.
    auto buttons = CreateContainer("dbcButtons", 0, 0, 0, 40);
    buttons->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto error = CreateLabel("dbcError", 0, 0, 0, 40, "");
    error->SetTextColor(ErrorColor());
    error->SetWrap(TextWrap::WrapWord);
    error->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    buttons->AddChild(error);
    form->error = error.get();
    auto cancel = MakeDialogButton("dbcCancel", "Cancel", 90);
    cancel->SetOnClick([form]() { form->dialog->CloseDialog(DialogResult::Cancel); });
    buttons->AddChild(cancel);
    auto save = MakeDialogButton("dbcSave", "Save", 90, ButtonRole::Primary);
    save->SetOnClick([form]() { Save(*form); });
    buttons->AddChild(save);
    dialog->AddChild(buttons);

    driver->onSelectionChanged = [form](int, const DropdownItem&) { ShowRowsFor(*form); };
    sshAuth->onSelectionChanged = [form](int, const DropdownItem&) { ShowRowsFor(*form); };
    tunnel->onChecked = [form]() { ShowRowsFor(*form); };
    tunnel->onUnchecked = [form]() { ShowRowsFor(*form); };
    ShowRowsFor(*form);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, parent);
}

} // namespace UltraDBAdmin
