// Apps/EmailCleaner/ui/EmailCleanerAccountsDialog.cpp
// Version: 0.2.0 - the server name is trimmed of spaces before it is checked
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerAccountsDialog.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

const Color kQuietColor(96, 96, 96, 255);
const Color kWarningColor(176, 96, 0, 255);

constexpr float kRowH   = 30.0f;
constexpr float kFieldH = 26.0f;

// The security dropdown's order.
const UltraMail::MailSecurity kSecurities[] = {
    UltraMail::MailSecurity::SslTls,
    UltraMail::MailSecurity::StartTls,
    UltraMail::MailSecurity::Plain,
};

int SecurityIndex(UltraMail::MailSecurity security) {
    for (int i = 0; i < 3; ++i)
        if (kSecurities[i] == security) return i;
    return 0;
}

std::shared_ptr<UltraCanvasContainer> FormRow(const std::string& id) {
    auto row = CreateContainer(id, 0, 0, 0, kFieldH);
    row->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    return row;
}

std::shared_ptr<UltraCanvasLabel> FieldLabel(const std::string& id, const std::string& text) {
    return CreateLabel(id, 0, 0, 124, 20, text);
}

} // namespace

void AccountsDialog::Show(const std::vector<Row>& rows) {
    rows_ = rows;
    busy_ = false;
    discovered_ = UltraMail::DiscoveryResult{};

    DialogConfig config;
    config.title      = "Accounts";
    config.width      = 720;
    config.height     = 640;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    dialog_ = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog_.get();
    dialog_->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog_->SetPadding(14);

    auto heading = CreateLabel(
        "ecAcctHeading", 0, 0, 680, 60,
        "Accounts set up in UltraMail are shared automatically. An account added "
        "here belongs to EmailCleaner alone: it keeps its own password and "
        "downloads its own copy of the inbox and junk folder.");
    heading->SetWrap(TextWrap::WrapWord);
    dialog_->AddChild(heading);

    // ---- The list ----------------------------------------------------------
    list_ = CreateScrollableContainer("ecAcctList", 0, 0, 680, 170);
    dialog_->AddChild(list_);
    list_->layoutItem.SetFlexGrow(1);

    // ---- The add form ------------------------------------------------------
    dialog_->AddChild(CreateLabel("ecAcctFormTitle", 0, 0, 680, 22, "Add an account"));

    auto emailRow = FormRow("ecAcctEmailRow");
    emailRow->AddChild(FieldLabel("ecAcctEmailLabel", "Email address"));
    email_ = CreateEmailInput("ecAcctEmail", 0, 0, 380, kFieldH);
    email_->SetPlaceholder("you@example.com");
    // Leaving the address field fills the servers in, the way a mail client's
    // wizard does — a second click on "Find servers" does the same on demand.
    email_->onFocusLost = [this]() {
        if (email_ && !email_->GetText().empty() && host_ && host_->GetText().empty())
            FindServers();
    };
    emailRow->AddChild(email_);
    findButton_ = CreateButton("ecAcctFind", 0, 0, 130, kFieldH, "Find servers");
    findButton_->onClick = [this]() { FindServers(); };
    emailRow->AddChild(findButton_);
    dialog_->AddChild(emailRow);

    auto nameRow = FormRow("ecAcctNameRow");
    nameRow->AddChild(FieldLabel("ecAcctNameLabel", "Name"));
    name_ = CreateTextInput("ecAcctName", 0, 0, 380, static_cast<int>(kFieldH));
    name_->SetPlaceholder("optional — shown in the account list");
    nameRow->AddChild(name_);
    dialog_->AddChild(nameRow);

    auto passRow = FormRow("ecAcctPassRow");
    passRow->AddChild(FieldLabel("ecAcctPassLabel", "Password"));
    password_ = CreateRevealablePasswordInput("ecAcctPass", 0, 0, 380,
                                              static_cast<int>(kFieldH));
    password_->SetPlaceholder("for Gmail, Outlook, Yahoo: an app password");
    passRow->AddChild(password_);
    dialog_->AddChild(passRow);

    auto serverRow = FormRow("ecAcctServerRow");
    serverRow->AddChild(FieldLabel("ecAcctServerLabel", "IMAP server"));
    host_ = CreateTextInput("ecAcctHost", 0, 0, 250, static_cast<int>(kFieldH));
    host_->SetPlaceholder("imap.example.com");
    serverRow->AddChild(host_);
    port_ = CreateNumberInput("ecAcctPort", 0, 0, 70, kFieldH);
    port_->SetPlaceholder("993");
    serverRow->AddChild(port_);
    security_ = CreateDropdown("ecAcctSecurity", 0, 0, 120, kFieldH);
    security_->AddItem("SSL/TLS", "ssl");
    security_->AddItem("STARTTLS", "starttls");
    security_->AddItem("None", "none");
    security_->SetSelectedIndex(0, false);
    serverRow->AddChild(security_);
    dialog_->AddChild(serverRow);

    auto userRow = FormRow("ecAcctUserRow");
    userRow->AddChild(FieldLabel("ecAcctUserLabel", "Username"));
    username_ = CreateTextInput("ecAcctUser", 0, 0, 380, static_cast<int>(kFieldH));
    username_->SetPlaceholder("usually the email address");
    userRow->AddChild(username_);
    dialog_->AddChild(userRow);

    status_ = CreateLabel("ecAcctStatus", 0, 0, 680, 56, "");
    status_->SetWrap(TextWrap::WrapWord);
    status_->SetTextColor(kQuietColor);
    dialog_->AddChild(status_);

    // ---- Buttons -----------------------------------------------------------
    auto buttonRow = CreateContainer("ecAcctButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    addButton_ = std::make_shared<UltraCanvasButton>("ecAcctAdd", 0, 0, 170, 28);
    addButton_->SetText("Sign in and add");
    addButton_->onClick = [this]() { Submit(); };
    buttonRow->AddChild(addButton_);

    auto closeBtn = std::make_shared<UltraCanvasButton>("ecAcctClose", 0, 0, 90, 28);
    closeBtn->SetText("Close");
    closeBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(closeBtn);
    dialog_->AddChild(buttonRow);

    RebuildList();
    UltraCanvasDialogManager::ShowDialog(dialog_, nullptr, nullptr);
}

void AccountsDialog::SetRows(const std::vector<Row>& rows) {
    rows_ = rows;
    RebuildList();
}

void AccountsDialog::RebuildList() {
    if (!list_) return;
    list_->ClearChildren();

    if (rows_.empty()) {
        auto empty = CreateLabel("ecAcctEmpty", 0, 0, 640, 40,
            "No accounts yet. Add one below, or set one up in UltraMail — "
            "it appears here on the next start.");
        empty->SetWrap(TextWrap::WrapWord);
        list_->AddChild(empty);
        return;
    }

    float y = 0.0f;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& entry = rows_[i];
        const std::string id = "ecAcctRow" + std::to_string(i);
        auto row = CreateContainer(id, 0, y, 650, kRowH);

        const std::string title = entry.account.email.empty()
            ? entry.account.accountId : entry.account.email;
        row->AddChild(CreateLabel(id + ".email", 0, 5, 280, 20, title));
        auto detail = CreateLabel(id + ".src", 286, 5, 250, 20, entry.detail);
        detail->SetTextColor(kQuietColor);
        row->AddChild(detail);

        if (entry.account.source == AccountSource::Own) {
            auto removeBtn = CreateButton(id + ".rm", 546, 3, 96, 24, "Remove");
            const StoredAccount account = entry.account;
            removeBtn->onClick = [this, account]() {
                if (onRemove) onRemove(account);
            };
            row->AddChild(removeBtn);
        }

        list_->AddChild(row);
        y += kRowH + 2.0f;
    }
}

void AccountsDialog::FindServers() {
    const std::string email = email_ ? email_->GetText() : std::string();
    if (!UltraMail::LooksLikeEmailAddress(email)) {
        SetStatus("Enter the email address first.", true);
        return;
    }
    // The provider table answers at once; only an unknown domain waits on the
    // network, and the app decides how.
    const UltraMail::DiscoveryResult preset = UltraMail::AutoDiscovery::FromPresets(email);
    if (preset.found || !onDiscover) {
        Prefill(preset.found ? preset : UltraMail::AutoDiscovery::GuessForDomain(email));
        return;
    }
    SetStatus("Looking up the servers for " + UltraMail::EmailDomain(email) + "…", false);
    if (findButton_) findButton_->SetDisabled(true);
    onDiscover(email, [this](const UltraMail::DiscoveryResult& result) {
        if (findButton_) findButton_->SetDisabled(false);
        Prefill(result);
    });
}

void AccountsDialog::Prefill(const UltraMail::DiscoveryResult& result) {
    discovered_ = result;
    if (host_)     host_->SetText(result.imap.host);
    if (port_)     port_->SetText(result.imap.port > 0 ? std::to_string(result.imap.port) : "");
    if (security_) security_->SetSelectedIndex(SecurityIndex(result.imap.security), false);
    if (username_) username_->SetText(result.imap.username);

    if (!result.imap.Valid()) {
        SetStatus("No server settings were found — enter the IMAP server by hand.", true);
    } else if (!result.found) {
        SetStatus("Nothing was found for this domain, so this is a guess — check it "
                  "against your provider's settings.", true);
    } else {
        std::string text = "Servers found";
        if (!result.displayName.empty()) text += " (" + result.displayName + ")";
        text += ". ";
        if (result.imap.oauth || result.imap.auth == UltraNetMailAuth::OAuth2)
            text += "This provider needs an app password here, created in its "
                    "account security settings.";
        SetStatus(text, false);
    }
}

bool AccountsDialog::ReadForm(NewAccountRequest& out, std::string& error) const {
    out.email       = email_ ? email_->GetText() : std::string();
    out.displayName = name_ ? name_->GetText() : std::string();
    out.password    = password_ ? password_->GetText() : std::string();

    // Start from what discovery said (it carries the outgoing server and the
    // provider's name), then take what the form holds for the incoming one —
    // the form wins, because the user may have corrected it.
    out.settings = discovered_;
    if (out.settings.imap.host.empty() && !out.settings.found)
        out.settings = UltraMail::AutoDiscovery::GuessForDomain(out.email);
    std::string host = host_ ? host_->GetText() : std::string();
    const auto first = host.find_first_not_of(" \t");
    host = first == std::string::npos ? std::string()
                                      : host.substr(first, host.find_last_not_of(" \t") - first + 1);
    out.settings.imap.host = host;

    const std::string portText = port_ ? port_->GetText() : std::string();
    int port = 0;
    if (!portText.empty()) {
        char* end = nullptr;
        const long parsed = std::strtol(portText.c_str(), &end, 10);
        if (end == portText.c_str() || *end != '\0' || parsed <= 0 || parsed > 65535) {
            error = "The port must be a number between 1 and 65535.";
            return false;
        }
        port = static_cast<int>(parsed);
    }
    const int securityIndex = security_ ? security_->GetSelectedIndex() : 0;
    out.settings.imap.security =
        kSecurities[(securityIndex >= 0 && securityIndex < 3) ? securityIndex : 0];
    // An empty port means the security mode's usual one.
    out.settings.imap.port = port > 0 ? port
        : (out.settings.imap.security == UltraMail::MailSecurity::SslTls ? 993 : 143);
    out.settings.imap.username = username_ ? username_->GetText() : std::string();
    return true;
}

void AccountsDialog::Submit() {
    if (busy_) return;
    NewAccountRequest request;
    std::string error;
    if (!ReadForm(request, error)) {
        SetStatus(error, true);
        return;
    }
    if (!onAdd) return;

    busy_ = true;
    if (addButton_) addButton_->SetDisabled(true);
    SetStatus("Signing in to " + request.settings.imap.host + "…", false);

    onAdd(request, [this](const std::string& failure) {
        busy_ = false;
        if (addButton_) addButton_->SetDisabled(false);
        if (!failure.empty()) {
            SetStatus(failure, true);
            return;
        }
        // Added: clear the form for the next one and keep the dialog open, so
        // the new row is visible where the user is looking.
        for (auto* field : { email_.get(), name_.get(), password_.get(), host_.get(),
                             port_.get(), username_.get() })
            if (field) field->SetText("");
        discovered_ = UltraMail::DiscoveryResult{};
        SetStatus("Added. Its mail is being downloaded — the map fills in when it "
                  "is done.", false);
    });
}

void AccountsDialog::SetStatus(const std::string& text, bool warning) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(warning ? kWarningColor : kQuietColor);
}

} // namespace EmailCleaner
