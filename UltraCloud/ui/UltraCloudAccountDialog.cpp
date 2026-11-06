// UltraCloud/ui/UltraCloudAccountDialog.cpp
// Version: 0.8.2 - the FTP provider asks for the protocol, the server and the
//                  port each on its own; the private key is chosen with a
//                  file dialog; a refusal is shown whole, on two lines; the
//                  callbacks reach the widgets through plain pointers; the
//                  texts through Tr
// Version: 0.8.1 - a failed sign-in says why in a translated sentence by its
//                  result code (DescribeResult); the service's technical
//                  message is the status line's tooltip
// Version: 0.8.0 - every text a person reads goes through Tr / TrF (the
//                  standard set: UltraFiler and UltraPress both show this
//                  dialog); the OAuth hint is one sentence with the provider
//                  as {0}
// Version: 0.7.1 - a private key row for SFTP
// Version: 0.7.0 - the shared_ptr form: the sign-in thread holds the service
//                  until it ends
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCloudAccountDialog.h"
#include "UltraCloudUiStyle.h"
#include "UltraCloudResultText.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"   // the private key's file dialog
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"     // GetEnvUtf8, PathFromUtf8
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"   // OpenURL
#include "UltraLocalisation/UltraLocalisation.h"   // Tr, TrF

#include <UltraCloud/UltraCloudFtp.h>   // FtpServer, FtpServerUrl, FtpDefaultPort
#include <UltraCloud/UltraCloudOAuth.h>

#include <atomic>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace UltraCloud {

namespace {

// A caption and the control it describes, kept together so a row the chosen
// provider does not need leaves the grid whole. Hiding only the control would
// leave its caption behind and push every row below it into the wrong column.
// Plain pointers: the grid owns both, for as long as the dialog is there.
struct FormRow {
    UltraCanvasLabel* caption = nullptr;
    UltraCanvasUIElement* control = nullptr;

    void SetVisible(bool visible) const {
        if (caption) caption->SetVisible(visible);
        if (control) control->SetVisible(visible);
    }
};

// Providers in the order the dialog offers them: real ones first, the
// in-process demo last.
std::vector<std::shared_ptr<ICloudProvider>> OrderedProviders() {
    std::vector<std::shared_ptr<ICloudProvider>> out;
    for (const char* id : {"nextcloud", "dropbox", "onedrive", "googledrive", "webdav"})
        if (auto p = GetProvider(id)) out.push_back(p);
    for (const auto& p : ListProviders()) {
        bool seen = false;
        for (const auto& o : out) if (o->Id() == p->Id()) seen = true;
        if (!seen && p->Id() != "memory") out.push_back(p);
    }
    if (auto demo = GetProvider("memory")) out.push_back(demo);
    return out;
}

// The protocols the FTP provider speaks, in the order the dialog offers
// them: the one a web host hands out today first, the unencrypted one last.
// The scheme is what FtpTransportFor reads back from the stored URL; the
// menu's text and the hint under the form are looked up by it, so that each
// is a Tr site of its own.
constexpr const char* kFtpSchemes[] = {"sftp", "ftpes", "ftps", "ftp"};

std::string ProtocolLabel(std::string_view scheme) {
    if (scheme == "ftpes") return Tr("FTP with TLS - explicit, FTPES (port 21)");
    if (scheme == "ftps")  return Tr("FTP over TLS - implicit, FTPS (port 990)");
    if (scheme == "ftp")   return Tr("FTP - plain, unencrypted (port 21)");
    return Tr("SFTP - over SSH (port 22)");
}

std::string ProtocolHint(std::string_view scheme) {
    if (scheme == "ftpes")
        return Tr("Connects on the plain FTP port and asks for TLS before signing in (AUTH TLS): the usual encrypted FTP.");
    if (scheme == "ftps")
        return Tr("TLS from the first byte, on a port of its own - conventionally 990.");
    if (scheme == "ftp")
        return Tr("Plain FTP sends the password and the files in the clear; prefer SFTP or FTP with TLS where the server offers them.");
    return Tr("SFTP signs in with the password, or with the private key chosen above - the password is then its passphrase.");
}

// The scheme the protocol menu stands on; SFTP, the first, when it stands on
// nothing.
std::string SchemeSelected(const UltraCanvasDropdown* protocol) {
    const DropdownItem* item = protocol ? protocol->GetSelectedItem() : nullptr;
    if (item)
        for (const char* scheme : kFtpSchemes)
            if (item->value == scheme) return scheme;
    return kFtpSchemes[0];
}

std::string Trim(const std::string& s) {
    std::string::size_type b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The port typed into the form: 0 for an empty field (the protocol's own),
// -1 for anything that is not a number from 1 to 65535.
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

std::string HomeFolder() {
    std::string home = GetEnvUtf8("HOME");
    if (home.empty()) home = GetEnvUtf8("USERPROFILE");
    return home;
}

// "~/.ssh/id_ed25519" as the transport needs it: a shell expands the "~", a
// path handed to a library is opened as it stands.
std::string ExpandHome(const std::string& path) {
    if (path.size() < 2 || path[0] != '~' || (path[1] != '/' && path[1] != '\\')) return path;
    const std::string home = HomeFolder();
    return home.empty() ? path : home + path.substr(1);
}

// The private key, chosen rather than typed: keys live in ~/.ssh, a hidden
// folder, so the chooser starts there and shows hidden files. `input`
// belongs to the dialog, which the chooser is modal over, so it is still
// there when the answer comes.
void ChooseKeyFile(UltraCanvasTextInput* input) {
    FileDialogOptions options;
    options.SetTitle(Tr("SSH private key"))
           .AddFilter(Tr("All files"), "*")
           .SetShowHidden(true)
           .SetRegisterAsRecent(false);
    const std::string home = HomeFolder();
    if (!home.empty()) {
        std::error_code ec;
        const std::filesystem::path ssh = PathFromUtf8(home) / ".ssh";
        if (std::filesystem::is_directory(ssh, ec)) options.SetInitialDirectory(PathToUtf8(ssh));
    }
    UltraCanvasFileLoader::OpenFileDialog(options, [input](DialogResult result, const std::string& path) {
        if (result == DialogResult::OK && !path.empty()) input->SetText(path);
    });
}

// What the callbacks share. The widgets belong to the dialog, which outlives
// every callback stored on one of them, so this holds plain pointers: a
// callback a widget holds then owns neither that widget nor the dialog above
// it. A shared_ptr captured there is a cycle - the provider menu's own
// callback once held one to the menu, which kept the menu alive for good.
struct Form {
    std::vector<std::shared_ptr<ICloudProvider>> providers;
    UltraCanvasDropdown*  provider    = nullptr;
    UltraCanvasDropdown*  protocol    = nullptr;
    UltraCanvasTextInput* name        = nullptr;
    UltraCanvasTextInput* server      = nullptr;
    UltraCanvasTextInput* host        = nullptr;
    UltraCanvasTextInput* port        = nullptr;
    UltraCanvasTextInput* user        = nullptr;
    UltraCanvasTextInput* password    = nullptr;
    UltraCanvasTextInput* keyFile     = nullptr;
    UltraCanvasTextInput* publicUrl   = nullptr;
    UltraCanvasTextInput* folder      = nullptr;
    UltraCanvasCheckbox*  makeDefault = nullptr;
    UltraCanvasLabel*     hint        = nullptr;
    UltraCanvasLabel*     status      = nullptr;
    UltraCanvasButton*    addBtn      = nullptr;
    FormRow serverRow, protocolRow, hostRow, userRow, passwordRow, keyRow, publicRow;

    std::shared_ptr<ICloudProvider> ChosenProvider() const {
        const int idx = provider ? provider->GetSelectedIndex() : -1;
        if (idx < 0 || idx >= static_cast<int>(providers.size())) return nullptr;
        return providers[static_cast<std::size_t>(idx)];
    }
};

// Show only the rows the chosen provider needs. OAuth providers sign in
// through the browser: no user / password rows, and the button says so. The
// FTP provider's rows follow its protocol too: the key row is SFTP's.
void ApplyProvider(const Form& form) {
    const std::shared_ptr<ICloudProvider> p = form.ChosenProvider();
    if (!p) return;
    const auto caps = p->Capabilities();
    const std::string id = p->Id();
    const bool ftp = id == "ftp";
    const std::string scheme = SchemeSelected(form.protocol);
    form.serverRow.SetVisible(caps.needsServerUrl && !ftp);
    form.protocolRow.SetVisible(ftp);
    form.hostRow.SetVisible(ftp);
    form.publicRow.SetVisible(id == "webdav");
    form.userRow.SetVisible(!caps.needsOAuth);
    form.passwordRow.SetVisible(!caps.needsOAuth && id != "memory");
    form.keyRow.SetVisible(ftp && scheme == "sftp");
    if (ftp) form.port->SetPlaceholder(std::to_string(FtpDefaultPort(scheme)));
    if (caps.needsOAuth) {
        form.addBtn->SetText(Tr("Sign in in browser"));
        form.hint->SetText(CanSignIn(id)
            ? TrF("Your browser opens the sign-in page of {0}; come back here when it says you are done.",
                  p->DisplayName())
            : TrF("The sign-in service is off and no OAuth client id is configured for {0} - set "
                  "ULTRACLOUD_OAUTH_SERVICE_URL or ULTRACLOUD_{1}_CLIENT_ID (see "
                  "Docs/Modules/UltraCloud/README.md).",
                  p->DisplayName(), id));
    } else {
        form.addBtn->SetText(Tr("Add account"));
        form.hint->SetText(id == "nextcloud"
            ? Tr("Nextcloud: create an app password under Settings → Security and use it here.")
            : id == "webdav" ? Tr("Links need a public URL that serves the same folder as the DAV root.")
            : ftp ? ProtocolHint(scheme)
            : std::string());
    }
}

} // namespace

void ShowAddAccountDialog(UltraCanvasWindowBase* parent, CloudService& service,
                          std::function<void(const Account&)> onAdded,
                          std::function<bool(const std::string& providerId)> providerFilter,
                          const std::string& title, AddAccountWatch watch) {
    // Not owned: an aliasing pointer with no owner, so the caller's promise
    // to keep the service alive is what keeps it alive, as it always was.
    std::shared_ptr<CloudService> unowned(std::shared_ptr<CloudService>(), &service);
    ShowAddAccountDialog(parent, std::move(unowned), std::move(onAdded),
                         std::move(providerFilter), title, std::move(watch));
}

void ShowAddAccountDialog(UltraCanvasWindowBase* parent, std::shared_ptr<CloudService> service,
                          std::function<void(const Account&)> onAdded,
                          std::function<bool(const std::string& providerId)> providerFilter,
                          const std::string& title, AddAccountWatch watch) {
    if (!service) return;
    DialogConfig config;
    config.title      = title.empty() ? Tr("Add cloud account") : title;
    config.width      = 520;
    // Tall enough for the provider that needs every row: an SFTP server's
    // eight captioned rows (8x32: provider, name, protocol, server and port,
    // user, password, key, folder) plus the checkbox (26), the hint (36) and
    // the two-line status (36), eleven 8 px row gaps, the 20 px padding on
    // each side, and the button row (36) with its own gaps. The rows are not
    // allowed to shrink into each other, so the dialog has to be tall enough
    // to hold them rather than the layout absorbing the difference.
    config.height     = 550;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for the widgets' callbacks: the dialog owns them, so a
    // shared_ptr capture would form a cycle. The worker thread below keeps a
    // weak_ptr instead and only touches the dialog on the UI thread.
    auto* dlg = dialog.get();
    std::weak_ptr<UltraCanvasModalDialog> weak = dialog;

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(UiStyle::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(UiStyle::kPadding);
    dialog->SetBackgroundColor(UiStyle::kSurface);

    // One two-column grid rather than a flex container per row: the caption
    // column is exactly as wide as the widest caption in it (so a longer
    // translation of "Upload folder" makes the column wider instead of being
    // cut off), every control starts where that column ends, and the grid
    // never scrolls - a row per caption used to raise a scrollbar pair inside
    // each row as soon as the dialog was a few pixels too short for its rows.
    auto grid = CreateFormGrid("cloudAccForm", 8.0f, UiStyle::kGap);

    // [caption | control] rows. The control carries no explicit width: the
    // `1fr` column gives it the whole remaining width, whatever the dialog is.
    auto addRow = [&grid](const std::string& id, const std::string& caption,
                          const std::shared_ptr<UltraCanvasUIElement>& control) {
        auto label = UiStyle::MakeCaption(id + "Label", caption);
        if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(control))
            UiStyle::StyleInput(input);
        AddFormRow(grid, label, control);
        return FormRow{label.get(), control.get()};
    };

    auto form = std::make_shared<Form>();

    // The caller's filter narrows the list; an empty result would leave a
    // dialog with nothing to choose, so in that case the unfiltered list is
    // offered rather than an unusable form.
    form->providers = OrderedProviders();
    if (providerFilter) {
        std::vector<std::shared_ptr<ICloudProvider>> kept;
        for (const auto& p : form->providers)
            if (providerFilter(p->Id())) kept.push_back(p);
        if (!kept.empty()) form->providers = std::move(kept);
    }
    auto provider = CreateDropdown("cloudAccProvider", 0, 0, 0, UiStyle::kControlHeight);
    for (const auto& p : form->providers) provider->AddItem(p->DisplayName(), p->Id());
    if (!form->providers.empty()) provider->SetSelectedIndex(0, /*runNotifications=*/false);
    addRow("cloudAccProvider", Tr("Provider"), provider);
    form->provider = provider.get();

    auto name = CreateTextInput("cloudAccName", 0, 0, 0, UiStyle::kControlHeight);
    name->SetPlaceholder(Tr("My cloud (optional)"));
    addRow("cloudAccName", Tr("Name"), name);
    form->name = name.get();

    auto server = CreateTextInput("cloudAccServer", 0, 0, 0, UiStyle::kControlHeight);
    server->SetPlaceholder("https://cloud.example.com");
    form->serverRow = addRow("cloudAccServer", Tr("Server URL"), server);
    form->server = server.get();

    // The FTP provider asks for the protocol, the server and the port each on
    // its own instead of one URL. A URL pasted from the browser's address bar
    // starts with https://, which is no FTP at all, and the one field took it
    // and tried plain FTP on port 21 without a word; three fields cannot be
    // filled in that way, and the port, which the one field hid behind a
    // "host:port" nobody was told about, has a place of its own.
    auto protocol = CreateDropdown("cloudAccProtocol", 0, 0, 0, UiStyle::kControlHeight);
    for (const char* scheme : kFtpSchemes) protocol->AddItem(ProtocolLabel(scheme), scheme);
    protocol->SetSelectedIndex(0, /*runNotifications=*/false);
    form->protocolRow = addRow("cloudAccProtocol", Tr("Protocol"), protocol);
    form->protocol = protocol.get();

    // The server and its port on one line, as a database connection dialog
    // has them: the port is short, and a row of its own would say more
    // about it than it deserves.
    auto hostLine = CreateFormCellRow("cloudAccHostLine", 8.0f, static_cast<int>(UiStyle::kControlHeight));
    auto host = CreateTextInput("cloudAccHost", 0, 0, 0, UiStyle::kControlHeight);
    host->SetPlaceholder(Tr("files.example.org or 203.0.113.5"));
    UiStyle::StyleInput(host);
    host->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    hostLine->AddChild(host);
    hostLine->AddChild(UiStyle::MakeCaption("cloudAccPortLabel", Tr("Port")));
    auto port = CreateTextInput("cloudAccPort", 0, 0, 72, UiStyle::kControlHeight);
    port->SetPlaceholder(std::to_string(FtpDefaultPort(kFtpSchemes[0])));
    port->SetTooltip(Tr("Empty: the protocol's own port"));
    UiStyle::StyleInput(port);
    port->layoutItem.SetFlexShrink(0);
    hostLine->AddChild(port);
    form->hostRow = addRow("cloudAccHost", Tr("Server"), hostLine);
    form->host = host.get();
    form->port = port.get();

    auto user = CreateTextInput("cloudAccUser", 0, 0, 0, UiStyle::kControlHeight);
    user->SetPlaceholder(Tr("user name"));
    form->userRow = addRow("cloudAccUser", Tr("User"), user);
    form->user = user.get();

    auto password = CreatePasswordInput("cloudAccPass", 0, 0, 0, UiStyle::kControlHeight);
    password->SetPlaceholder(Tr("password or app password"));
    form->passwordRow = addRow("cloudAccPass", Tr("Password"), password);
    form->password = password.get();

    // SFTP with a key: the private key file's path, chosen with the file
    // dialog or typed; the password above is then its passphrase (and the
    // server's password, failing the key).
    auto keyLine = CreateFormCellRow("cloudAccKeyLine", 8.0f, static_cast<int>(UiStyle::kControlHeight));
    auto keyFile = CreateTextInput("cloudAccKey", 0, 0, 0, UiStyle::kControlHeight);
    keyFile->SetPlaceholder(Tr("none - the password signs in (optional)"));
    UiStyle::StyleInput(keyFile);
    keyFile->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    keyLine->AddChild(keyFile);
    auto chooseKey = CreateButton("cloudAccKeyChoose", 0, 0, 90, UiStyle::kControlHeight, Tr("Choose"));
    UiStyle::StyleSecondary(chooseKey);
    chooseKey->layoutItem.SetFlexShrink(0);
    keyLine->AddChild(chooseKey);
    form->keyRow = addRow("cloudAccKey", Tr("Private key"), keyLine);
    form->keyFile = keyFile.get();
    chooseKey->onClick = [keyInput = keyFile.get()]() { ChooseKeyFile(keyInput); };

    auto publicUrl = CreateTextInput("cloudAccPublic", 0, 0, 0, UiStyle::kControlHeight);
    publicUrl->SetPlaceholder(TrF("{0} (links = this URL + path)", "https://files.example.org/pub"));
    form->publicRow = addRow("cloudAccPublic", Tr("Public URL"), publicUrl);
    form->publicUrl = publicUrl.get();

    auto folder = CreateTextInput("cloudAccFolder", 0, 0, 0, UiStyle::kControlHeight);
    // A folder name on the server, not a text of the dialog: the same in
    // every language, so a second device finds the folder the first made.
    folder->SetText("/Shared from ULTRA OS");
    addRow("cloudAccFolder", Tr("Upload folder"), folder);
    form->folder = folder.get();

    // Its own caption, so it spans both columns - as do the hint and the
    // status line below it.
    auto makeDefault = UltraCanvasCheckbox::CreateCheckbox(
        "cloudAccDefault", 0, 0, 300, 26, Tr("Use as the default cloud account"), false);
    AddFormWideRow(grid, makeDefault);
    form->makeDefault = makeDefault.get();

    // Its text follows the provider (ApplyProvider).
    auto hint = CreateLabel("cloudAccHint", 0, 0, 0, 36, "");
    hint->SetWrap(TextWrap::WrapWord);
    hint->SetFontSize(UiStyle::kFontSize - 1.0f);
    hint->SetTextColor(UiStyle::kTextSecondary);
    AddFormWideRow(grid, hint);
    form->hint = hint.get();

    // Two lines, wrapped: a server's refusal names the host, the port and
    // what it said, which is more than one line holds, and a line that ends
    // at "sign in: Failed to connect to" explains nothing.
    auto status = CreateLabel("cloudAccStatus", 0, 0, 0, 36, "");
    status->SetWrap(TextWrap::WrapWord);
    status->SetFontSize(UiStyle::kFontSize);
    status->SetTextColor(UiStyle::kDanger);
    AddFormWideRow(grid, status);
    form->status = status.get();

    dialog->AddChild(grid);
    // The form keeps its rows at their own height (CreateFormGrid sets
    // flex-shrink: 0) and the spacer takes the slack, so the buttons sit at
    // the bottom whether the provider needs three rows or eight.
    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    // Buttons (created here so ApplyProvider can relabel the Add button).
    auto addBtn = CreateButton("cloudAccAdd", 0, 0, 160, UiStyle::kControlHeight, Tr("Add account"));
    UiStyle::StylePrimary(addBtn);
    form->addBtn = addBtn.get();

    provider->onSelectionChanged = [form](int, const DropdownItem&) { ApplyProvider(*form); };
    protocol->onSelectionChanged = [form](int, const DropdownItem&) { ApplyProvider(*form); };
    ApplyProvider(*form);

    // Button row.
    auto buttons = CreateContainer("cloudAccButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("cloudAccCancel", 0, 0, 90, UiStyle::kControlHeight, Tr("Cancel"));
    UiStyle::StyleSecondary(cancelBtn);
    buttons->AddChild(cancelBtn);
    buttons->AddChild(addBtn);
    dialog->AddChild(buttons);

    // The result travels from the worker to the close callback through here.
    auto added = std::make_shared<Account>();
    // Set when the dialog goes without an account - Cancel, or the window's
    // close box. A sign-in still running is called off then: it neither holds
    // its thread waiting on the browser nor stores an account the user has
    // given up on.
    auto gaveUp = std::make_shared<std::atomic<bool>>(false);

    addBtn->onClick = [form, weak, dlg, service, watch, added, gaveUp]() {
        const std::shared_ptr<ICloudProvider> chosenProvider = form->ChosenProvider();
        if (!chosenProvider) return;
        UltraCanvasLabel* status = form->status;
        // What is wrong with the form is said in the refusal colour; the grey
        // of a "Signing in…" left by the attempt before would read as a note.
        status->SetTextColor(UiStyle::kDanger);
        const auto caps = chosenProvider->Capabilities();
        const bool ftp = chosenProvider->Id() == "ftp";
        const std::string scheme = SchemeSelected(form->protocol);
        Account a;
        a.providerId    = chosenProvider->Id();
        a.displayName   = form->name->GetText();
        a.serverUrl     = form->server->GetText();
        a.username      = form->user->GetText();
        a.publicBaseUrl = form->publicUrl->GetText();
        a.remoteFolder  = form->folder->GetText();
        a.isDefault     = form->makeDefault->IsChecked();
        if (ftp) {
            // The protocol, the server and the port as one URL, the form the
            // account stores and the command line takes.
            const std::string typed = Trim(form->host->GetText());
            if (typed.empty()) { status->SetText(Tr("Enter the server's name or address.")); return; }
            if (typed.find("://") != std::string::npos) {
                status->SetText(Tr("Enter the server's name or address alone - files.example.org, "
                                   "not a URL. The protocol is chosen above."));
                return;
            }
            // "host:2222" or "host/folder" typed into the field count as what
            // they say; a port in its own field wins over one typed here.
            FtpServer where = FtpServerOf(typed);
            where.scheme = scheme;
            const int portNumber = ParsePort(form->port->GetText());
            if (portNumber < 0) {
                status->SetText(TrF("The port is a number from 1 to 65535, or empty for the protocol's own ({0}).",
                                    FtpDefaultPort(scheme)));
                return;
            }
            if (portNumber > 0) where.port = portNumber;
            a.serverUrl = FtpServerUrl(where);
        }
        Credentials c;
        c.username = a.username;
        c.password = form->password->GetText();
        c.privateKeyFile = ftp && scheme == "sftp"
                               ? ExpandHome(Trim(form->keyFile->GetText())) : std::string();

        if (caps.needsServerUrl && a.serverUrl.empty()) { status->SetText(Tr("Enter the server URL.")); return; }
        if (!caps.needsOAuth && a.username.empty() && a.providerId != "memory") {
            status->SetText(Tr("Enter the user name.")); return;
        }
        if (caps.needsOAuth && !CanSignIn(a.providerId)) {
            status->SetText(Tr("The sign-in service is off and no OAuth client id is configured for this provider.")); return;
        }

        status->SetTextColor(Color(60, 60, 60, 255));
        status->SetText(caps.needsOAuth ? Tr("Waiting for the browser sign-in…")
                                        : TrF("Signing in at {0}…", a.serverUrl));
        form->addBtn->SetDisabled(true);

        // Verify + store off the UI thread; report back on it. OAuth providers
        // open the consent page in the system browser and wait for the
        // loopback redirect.
        const bool oauth = caps.needsOAuth;
        // The thread holds the service until the sign-in has ended: a sign-in
        // can wait minutes on the browser, and the dialog - or the window that
        // opened it - can be gone long before. It reaches the widgets through
        // `form`, whose pointers are good for as long as the dialog is, which
        // `weak` says on the UI thread before any of them is touched.
        std::thread([weak, dlg, form, added, a, c, oauth, service, watch, gaveUp]() mutable {
            std::function<bool()> previousCancel = SetThreadSignInCancel(
                    [gaveUp, hostCancelled = watch.cancelled]() {
                        return gaveUp->load() || (hostCancelled && hostCancelled());
                    });
            if (watch.onBegin) watch.onBegin(a);
            Result r = oauth
                ? service->SignInAccount(a, [](const std::string& url) { UltraCanvas::OpenURL(url); })
                : service->AddAccount(a, c, /*verify=*/true);
            if (watch.onEnd) watch.onEnd(a, r);
            SetThreadSignInCancel(std::move(previousCancel));
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            auto finish = [weak, dlg, form, added, a, r]() {
                const std::shared_ptr<UltraCanvasModalDialog> alive = weak.lock();
                if (!alive) return;   // dialog already closed
                if (r.code == ResultCode::Cancelled) return;   // closed meanwhile
                if (r) { *added = a; dlg->CloseDialog(DialogResult::OK); return; }
                form->status->SetTextColor(UiStyle::kDanger);
                // DescribeResult: a sentence in the reader's language for
                // the result code; the service's own words in the tooltip.
                form->status->SetText(DescribeResult(r));
                form->status->SetTooltip(ResultDetail(r));
                form->addBtn->SetDisabled(false);
            };
            if (app) app->PostToUIThread(finish); else finish();
        }).detach();
    };
    cancelBtn->onClick = [dlg, gaveUp]() {
        gaveUp->store(true);
        dlg->CloseDialog(DialogResult::Cancel);
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [added, onAdded, gaveUp](DialogResult result) {
            if (result == DialogResult::OK && onAdded && !added->accountId.empty()) {
                onAdded(*added);
                return;
            }
            gaveUp->store(true);   // the close box: as Cancel
        },
        parent);
}

} // namespace UltraCloud
