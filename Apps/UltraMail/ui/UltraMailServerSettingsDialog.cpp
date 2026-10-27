// Apps/UltraMail/ui/UltraMailServerSettingsDialog.cpp
// Version: 0.8.4 - an editable password row on the setup and server-re-entry
//                  pages (AccountFields::askPassword), prefilled with the typed
//                  password, so a failed sign-in can be fixed in place
// Version: 0.8.3 - a header (icon tile, title, the intro); Save, Details, Delete account, Edit signature and the
//                  browser sign-in carry icons
// Version: 0.8.2 - switching a password account between IMAP and JMAP asks
//                  for the password again: a warning on the switch, and Save /
//                  "Save anyway" wait for it
// Version: 0.8.1 - the outgoing server is optional for a JMAP account (it
//                  sends through JMAP); the port's security nudge leaves a
//                  disabled security dropdown alone
// Version: 0.8.0 - the Protocol row: the incoming server is IMAP or JMAP; the
//                  incoming rows, the port, the security and the sign-in
//                  choices follow the choice
// Version: 0.7.0 - a server name that cannot be one (an @, a scheme, a port
//                  after a colon, ...) is reported on Save, before the sign-in
// Version: 0.6.0 - the Signature row: the account's signature in a few words and
//                  "Edit signature…" (UltraMailSignatureDialog)
// Version: 0.5.0 - the app-wide view options (preview pane, sender icons) moved to
//                  the Settings window (UltraMailSettingsDialog)
// Version: 0.4.0 - an authentication method per server (Automatic, normal or
//                  encrypted password, OAuth2, Kerberos, NTLM, none for SMTP)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailServerSettingsDialog.h"

#include "UltraMailTheme.h"
#include "UltraMailAlerts.h"
#include "UltraMailSignature.h"
#include "UltraMailSignatureDialog.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"

#include <cctype>
#include <cstdlib>
#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kLabelWidth  = 100.0f;
constexpr float kPortWidth   = 54.0f;
constexpr float kSecWidth    = 104.0f;

int SecurityIndex(MailSecurity s) {
    switch (s) {
        case MailSecurity::SslTls:   return 0;
        case MailSecurity::StartTls: return 1;
        case MailSecurity::Plain:     return 2;
    }
    return 0;
}

MailSecurity SecurityAt(int index) {
    return index == 1 ? MailSecurity::StartTls
         : index == 2 ? MailSecurity::Plain
                      : MailSecurity::SslTls;
}

// The authentication methods in dropdown order. "No authentication" is only
// offered for the outgoing server: an SMTP relay may trust the network, but an
// IMAP mailbox always needs a sign-in.
struct AuthChoice { UltraNetMailAuth auth; const char* label; };
constexpr AuthChoice kAuthChoices[] = {
    { UltraNetMailAuth::Any,               "Automatic" },
    { UltraNetMailAuth::Password,          "Normal password" },
    { UltraNetMailAuth::EncryptedPassword, "Encrypted password" },
    { UltraNetMailAuth::OAuth2,            "OAuth2" },
    { UltraNetMailAuth::Kerberos,          "Kerberos / GSSAPI" },
    { UltraNetMailAuth::NTLM,              "NTLM" },
    { UltraNetMailAuth::NoAuth,              "No authentication" },
};

// Whether a sign-in method is offered for a server of `protocol`. JMAP signs
// in over HTTPS with a password (Basic) or an OAuth2 token (Bearer): the
// SASL mechanisms of IMAP and SMTP do not exist there. "No authentication"
// is for the outgoing server only (`offerNone`): an SMTP relay may trust the
// network, but a mailbox always needs a sign-in.
bool OffersAuth(UltraNetMailAuth auth, MailProtocol protocol, bool offerNone) {
    if (auth == UltraNetMailAuth::NoAuth) return offerNone;
    if (protocol != MailProtocol::Jmap) return true;
    return auth == UltraNetMailAuth::Any || auth == UltraNetMailAuth::Password
        || auth == UltraNetMailAuth::OAuth2;
}

// (Re)fill a sign-in dropdown with the methods `protocol` offers, selecting
// `current` - or Automatic when `current` is not offered.
void FillAuthChoices(UltraCanvasDropdown& dd, MailProtocol protocol, bool offerNone,
                     UltraNetMailAuth current) {
    dd.ClearItems();
    int selected = 0;
    for (const AuthChoice& c : kAuthChoices) {
        if (!OffersAuth(c.auth, protocol, offerNone)) continue;
        if (c.auth == current) selected = dd.GetItemCount();
        dd.AddItem(c.label, ToString(c.auth));
    }
    dd.SetSelectedIndex(selected, /*runNotifications=*/false);
}

// The method a sign-in dropdown shows; Automatic when nothing is selected.
UltraNetMailAuth SelectedAuth(const UltraCanvasDropdown& dd) {
    const DropdownItem* item = dd.GetSelectedItem();
    return item ? MailAuthFromString(item->value) : UltraNetMailAuth::Any;
}

// The protocols in dropdown order.
struct ProtocolChoice { MailProtocol protocol; const char* label; };
constexpr ProtocolChoice kProtocolChoices[] = {
    { MailProtocol::Imap, "IMAP" },
    { MailProtocol::Jmap, "JMAP" },
};

int ProtocolIndex(MailProtocol protocol) {
    return protocol == MailProtocol::Jmap ? 1 : 0;
}

MailProtocol ProtocolAt(int index) {
    return index == 1 ? MailProtocol::Jmap : MailProtocol::Imap;
}

std::string ProtocolName(MailProtocol protocol) {
    return protocol == MailProtocol::Jmap ? "JMAP" : "IMAP";
}

// The line under the fields: the usual ports, and where to find them.
std::string NoteFor(MailProtocol protocol) {
    if (protocol == MailProtocol::Jmap)
        return "A JMAP server is reached over HTTPS, usually on port 443, and "
               "answers at /.well-known/jmap (Fastmail: api.fastmail.com; Stalwart, "
               "Cyrus and Apache James: the mail host). Mail is read and sent "
               "through it: the outgoing (SMTP) server is optional - leave it "
               "empty to send through JMAP.";
    return "Ports are usually 993 (IMAP, SSL/TLS) or 143 (STARTTLS), and 465 (SMTP, "
           "SSL/TLS) or 587 (STARTTLS). Most providers list them under \"mail program "
           "settings\" or \"IMAP/SMTP\" in their help.";
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// 1..65535, else 0.
int ParsePort(const std::string& text) {
    const std::string t = Trim(text);
    if (t.empty() || t.size() > 5) return 0;
    for (char c : t) if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    const long v = std::strtol(t.c_str(), nullptr, 10);
    return (v >= 1 && v <= 65535) ? static_cast<int>(v) : 0;
}
} // namespace

void ServerSettingsDialog::Show(UltraCanvasWindowBase* parent, const std::string& email,
                                const std::string& intro, const DiscoveryResult& prefill,
                                std::function<void(const Result&)> onSave,
                                Verifier verify, const AccountFields& account) {
    DialogConfig config;
    config.title      = account.edit ? ("Settings for " + email)
                                      : ("Server settings for " + email);
    config.width      = 520;
    // The account settings page carries a display-name row and up to two
    // credential rows (a password field and/or a "Sign in with …" button) on
    // top of the server rows, plus room for a multi-line sign-in error.
    // A Protocol row sits above the server rows and two authentication rows
    // under them on both. The Signature row adds one more line.
    // The header (an icon tile, a title over the intro) is 56 high.
    config.height     = account.edit ? (account.onSaveSignature ? 656 : 624) : 464;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();   // see the wizard: no shared_ptr in button callbacks

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto content = CreateContainer("srvForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto header = Theme::MakeDialogHeader("srvHeader", account.edit ? "envelope" : "server",
                                          account.edit ? "Account settings" : "Server settings",
                                          intro, 30.0f);
    header->SetMargin(0, 0, 4, 0);
    content->AddChild(header);

    // Display-name row (account settings page only), above the servers.
    std::shared_ptr<UltraCanvasTextInput> nameInput;
    if (account.edit) {
        auto row = CreateContainer("srvNameRow", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine("srvNameLbl", "Display name", Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);
        nameInput = CreateTextInput("srvName", 0, 0, 0, Theme::kControlHeight);
        nameInput->SetPlaceholder(EmailLocalPart(email));
        nameInput->SetText(account.displayName);
        Theme::StyleInput(nameInput);
        row->AddChild(nameInput);
        nameInput->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    // The protocol of the incoming server. IMAP for every account from before
    // the choice existed; JMAP (JSON over HTTPS) for Fastmail, Stalwart, Cyrus
    // and the like. The incoming rows below follow it: their caption, the
    // usual port, the security (HTTPS, fixed) and the sign-in methods.
    auto protocolRow = CreateContainer("srvProtocolRow", 0, 0, 0, Theme::kControlHeight);
    protocolRow->layout.SetFlexRow()
                       .SetFlexGap(Theme::kInnerGap)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto protocolLabel = Theme::MakeLine("srvProtocolLbl", "Protocol", Theme::kControlHeight,
                                         Theme::kSizeBody, Theme::kTextSecondary);
    protocolLabel->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
    protocolRow->AddChild(protocolLabel);
    auto protocol = CreateDropdown("srvProtocol", 0, 0, 0, Theme::kControlHeight);
    for (const ProtocolChoice& c : kProtocolChoices)
        protocol->AddItem(c.label, ToString(c.protocol));
    Theme::StyleDropdown(protocol);
    protocol->SetSelectedIndex(ProtocolIndex(prefill.imap.protocol), /*runNotifications=*/false);
    protocolRow->AddChild(protocol);
    protocol->layoutItem.SetFlexGrow(1);
    content->AddChild(protocolRow);
    protocolRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // One server row: caption · host (grows) · port · security.
    struct ServerRow {
        std::shared_ptr<UltraCanvasLabel>     label;
        std::shared_ptr<UltraCanvasTextInput> host, port;
        std::shared_ptr<UltraCanvasDropdown>  security;
    };
    auto addServerRow = [&content](const std::string& id, const std::string& caption,
                                   const MailServerSettings& s) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        ServerRow r;
        r.label = Theme::MakeLine(id + "Lbl", caption, Theme::kControlHeight,
                                  Theme::kSizeBody, Theme::kTextSecondary);
        r.label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(r.label);

        r.host = CreateTextInput(id + "Host", 0, 0, 0, Theme::kControlHeight);
        r.host->SetPlaceholder("mail.example.com");
        r.host->SetText(s.host);
        Theme::StyleInput(r.host);
        row->AddChild(r.host);
        r.host->layoutItem.SetFlexGrow(1);

        r.port = CreateTextInput(id + "Port", 0, 0, kPortWidth, Theme::kControlHeight);
        r.port->SetPlaceholder("Port");
        r.port->SetText(s.port > 0 ? std::to_string(s.port) : "");
        Theme::StyleInput(r.port);
        row->AddChild(r.port);

        r.security = CreateDropdown(id + "Sec", 0, 0, kSecWidth, Theme::kControlHeight);
        r.security->AddItem("SSL/TLS", "ssl");
        r.security->AddItem("STARTTLS", "starttls");
        r.security->AddItem("None", "none");
        Theme::StyleDropdown(r.security);
        r.security->SetSelectedIndex(SecurityIndex(s.security), /*runNotifications=*/false);
        row->AddChild(r.security);

        // Keep the security choice consistent with a well-known port, so the
        // user cannot save 587 + SSL/TLS (implicit TLS on a STARTTLS port) -
        // that sends a TLS ClientHello before the greeting and gets the IP
        // fail2ban-banned (see EffectiveSecurity). Only nudge between the two
        // TLS modes; a deliberate "None" is left alone, and so is a dropdown
        // the protocol has fixed and disabled (JMAP is HTTPS on any port).
        r.port->onTextChanged = [sec = r.security](const std::string& text) {
            if (sec->IsDisabled()) return;
            const int port = std::atoi(text.c_str());
            if (port <= 0) return;
            const MailSecurity cur = SecurityAt(sec->GetSelectedIndex());
            if (cur == MailSecurity::Plain) return;
            const MailSecurity want = EffectiveSecurity(port, cur);
            if (want != cur)
                sec->SetSelectedIndex(SecurityIndex(want), /*runNotifications=*/false);
        };

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return r;
    };

    const MailProtocol startProtocol = prefill.imap.protocol;
    ServerRow imap = addServerRow("srvImap", "Incoming (" + ProtocolName(startProtocol) + ")",
                                  prefill.imap);
    ServerRow smtp = addServerRow("srvSmtp", "Outgoing (SMTP)", prefill.smtp);
    // What an empty outgoing server means: for JMAP, sending through it.
    auto smtpPlaceholder = [smtpHost = smtp.host.get()](MailProtocol p) {
        smtpHost->SetPlaceholder(p == MailProtocol::Jmap
                                     ? "Optional: empty sends through JMAP"
                                     : "mail.example.com");
    };
    smtpPlaceholder(startProtocol);
    // A JMAP server is HTTPS: the security is not a choice there.
    if (startProtocol == MailProtocol::Jmap) {
        imap.security->SetSelectedIndex(SecurityIndex(MailSecurity::SslTls),
                                        /*runNotifications=*/false);
        imap.security->SetDisabled(true);
    }

    // Authentication method per server, as Thunderbird offers it: prefilled
    // from the provider table (OAuth2 for Gmail, Outlook, Yahoo) or the
    // provider's autoconfig, else Automatic — whatever the server offers.
    struct AuthRow {
        std::shared_ptr<UltraCanvasLabel>    label;
        std::shared_ptr<UltraCanvasDropdown> method;
    };
    auto addAuthRow = [&content](const std::string& id, const std::string& caption,
                                 MailProtocol forProtocol, UltraNetMailAuth current,
                                 bool offerNone) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        AuthRow r;
        r.label = Theme::MakeLine(id + "Lbl", caption, Theme::kControlHeight,
                                  Theme::kSizeBody, Theme::kTextSecondary);
        r.label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(r.label);
        r.method = CreateDropdown(id, 0, 0, 0, Theme::kControlHeight);
        Theme::StyleDropdown(r.method);
        FillAuthChoices(*r.method, forProtocol, offerNone, current);
        row->AddChild(r.method);
        r.method->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return r;
    };
    AuthRow imapAuth = addAuthRow("srvImapAuth", "Sign-in (" + ProtocolName(startProtocol) + ")",
                                  startProtocol, prefill.imap.auth, false);
    AuthRow smtpAuth = addAuthRow("srvSmtpAuth", "Sign-in (SMTP)", MailProtocol::Imap,
                                  prefill.smtp.auth, true);

    // Username row.
    auto userRow = CreateContainer("srvUserRow", 0, 0, 0, Theme::kControlHeight);
    userRow->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto userLabel = Theme::MakeLine("srvUserLbl", "Username", Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
    userLabel->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
    userRow->AddChild(userLabel);
    auto user = CreateTextInput("srvUser", 0, 0, 0, Theme::kControlHeight);
    user->SetPlaceholder(email);
    user->SetText(prefill.imap.username.empty() ? email : prefill.imap.username);
    Theme::StyleInput(user);
    userRow->AddChild(user);
    user->layoutItem.SetFlexGrow(1);
    content->AddChild(userRow);
    userRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Credential rows (account settings page only). A provider may offer both:
    // Gmail takes an app password OR "Sign in with Google", so a password row
    // and an OAuth-button row can appear together. The OAuth button's action is
    // wired below, once collect/result exist.
    auto credRow = [&content](const std::string& id, const std::string& caption) {
        auto row = CreateContainer(id, 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Lbl", caption, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return row;
    };

    std::shared_ptr<UltraCanvasTextInput> passInput;
    std::shared_ptr<UltraCanvasButton>    oauthBtn;
    // The password row also appears on the non-edit pages (setup / server
    // re-entry) when askPassword is set, so a failed sign-in can be corrected
    // here instead of leaving the user stuck. It is prefilled with
    // initialPassword (the password typed in the add-account wizard); an account
    // that already has one stored says "leave blank to keep", a brand-new one
    // says "Password".
    if (account.acceptsPassword && (account.edit || account.askPassword)) {
        auto row = credRow("srvPassRow", "Password");
        passInput = CreatePasswordInput("srvPass", 0, 0, 0, Theme::kControlHeight);
        passInput->SetPlaceholder(account.edit || account.keepExisting
                                      ? "Leave blank to keep the current password"
                                      : "Password");
        if (!account.initialPassword.empty()) passInput->SetText(account.initialPassword);
        Theme::StyleInput(passInput);
        row->AddChild(passInput);
        passInput->layoutItem.SetFlexGrow(1);
    }
    if (account.edit && account.canOAuth) {
        auto row = credRow("srvOAuthRow", "Sign-in");
        const std::string prov = account.providerName.empty() ? "the provider"
                                                              : account.providerName;
        oauthBtn = CreateButton("srvReauth", 0, 0, 0, Theme::kControlHeight,
                                "Sign in with " + prov);
        Theme::StyleSecondary(oauthBtn);
        Theme::SetButtonIcon(oauthBtn, "globe");
        row->AddChild(oauthBtn);
        oauthBtn->layoutItem.SetFlexGrow(1);
    }

    // Signature row (account settings page only): what the account signs with,
    // and the button to the editor. The editor saves by itself, so the row
    // keeps the latest signature for the next time it is opened.
    if (account.edit && account.onSaveSignature) {
        auto row = credRow("srvSigRow", "Signature");
        auto current = std::make_shared<Signature>(account.signature);
        auto summary = Theme::MakeLine("srvSigSummary", DescribeSignature(*current),
                                       Theme::kControlHeight, Theme::kSizeBody,
                                       Theme::kTextPrimary);
        row->AddChild(summary);
        summary->layoutItem.SetFlexGrow(1);
        auto editBtn = CreateButton("srvSigEdit", 0, 0, 120, Theme::kControlHeight,
                                    "Edit signature\xE2\x80\xA6");
        Theme::FitToLabel(editBtn, 120);
        Theme::StyleSecondary(editBtn);
        Theme::SetButtonIcon(editBtn, "signature");
        std::function<void(const Signature&)> onSaveSignature = account.onSaveSignature;
        UltraCanvasLabel* summaryLabel = summary.get();   // a sibling in this dialog
        editBtn->onClick = [dlg, email, current, summaryLabel, onSaveSignature]() {
            // The editor is modal over this page, so the page (and the
            // label) outlive it.
            SignatureDialog::Show(dlg, email, *current,
                [current, summaryLabel, onSaveSignature](const Signature& edited) {
                    *current = edited;
                    summaryLabel->SetText(DescribeSignature(edited));
                    if (onSaveSignature) onSaveSignature(edited);
                });
        };
        row->AddChild(editBtn);
    }

    auto note = Theme::MakeLine("srvNote", NoteFor(startProtocol), 0, Theme::kSizeBody,
                                Theme::kTextSecondary);
    note->SetWrap(TextWrap::WrapWord);
    content->AddChild(note);
    note->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Validation feedback, in place. It grows into the column's spare vertical
    // space so a long sign-in error wraps across as many lines as it needs
    // instead of being clipped to one line. (Added below the note; made here
    // for the protocol switch's password warning.)
    auto status = Theme::MakeText("srvStatus", "", Theme::kSizeBody, Theme::kWaitingText);
    status->SetWrap(TextWrap::WrapWord);
    status->SetAlignment(TextAlignment::Left);

    // A password account switched to the other protocol needs its password
    // typed again: the stored one was the old server's, and a JMAP server
    // often takes its own app password or API token - kept, the switched
    // account cannot sign in. An account signed in with OAuth2 keeps its
    // tokens. The page says so on the switch and Save asks for it.
    const MailProtocol savedProtocol = prefill.imap.protocol;
    // The "enter the password again after a protocol switch" prompt is for the
    // account-settings page only: on setup / server re-entry the password field
    // is prefilled or kept, and the stored-password-is-stale warning does not
    // apply.
    const bool passwordAccount = passInput && account.edit && !account.signedInWithOAuth;
    auto passwordWarning = std::make_shared<std::string>();
    auto needsPassword = [passwordAccount, savedProtocol](MailProtocol chosen) {
        return passwordAccount && chosen != savedProtocol;
    };
    auto showPasswordNeeded = [passInput, status, passwordWarning, savedProtocol](
            MailProtocol chosen, bool needed) {
        if (needed) {
            passInput->SetPlaceholder("Required: the password for the " + ProtocolName(chosen)
                                      + " server");
            *passwordWarning = "The protocol changes from " + ProtocolName(savedProtocol)
                + " to " + ProtocolName(chosen) + ": enter the password again. The "
                "stored password is not used for the new protocol (a JMAP server often "
                "wants its own app password or API token), so without it the account "
                "cannot connect.";
            status->SetTextColor(Theme::kWaitingText);
            status->SetText(*passwordWarning);
        } else if (passInput) {
            passInput->SetPlaceholder("Leave blank to keep the current password");
            if (!passwordWarning->empty() && status->GetText() == *passwordWarning)
                status->SetText("");
            passwordWarning->clear();
        }
    };

    // Switching the protocol: the incoming rows say which one, a port that
    // was the other protocol's usual one becomes this one's (a typed
    // unusual port stays), the security is fixed to HTTPS for JMAP, and the
    // sign-in methods are the ones the protocol has. Siblings, not the
    // dropdown itself, are captured - no cycle.
    protocol->onSelectionChanged = [imap, imapAuth, note, smtpPlaceholder, needsPassword,
                                    showPasswordNeeded](int index, const DropdownItem&) {
        const MailProtocol chosen = ProtocolAt(index);
        smtpPlaceholder(chosen);
        const MailProtocol other  = chosen == MailProtocol::Jmap ? MailProtocol::Imap
                                                                 : MailProtocol::Jmap;
        imap.label->SetText("Incoming (" + ProtocolName(chosen) + ")");
        imapAuth.label->SetText("Sign-in (" + ProtocolName(chosen) + ")");
        const int port = ParsePort(imap.port->GetText());
        const bool usual = port == 0
            || port == DefaultPort(other, MailSecurity::SslTls)
            || port == DefaultPort(other, MailSecurity::StartTls);
        if (chosen == MailProtocol::Jmap) {
            imap.security->SetSelectedIndex(SecurityIndex(MailSecurity::SslTls),
                                            /*runNotifications=*/false);
            imap.security->SetDisabled(true);
        } else {
            imap.security->SetDisabled(false);
        }
        if (usual)
            imap.port->SetText(std::to_string(
                DefaultPort(chosen, SecurityAt(imap.security->GetSelectedIndex()))));
        FillAuthChoices(*imapAuth.method, chosen, false, SelectedAuth(*imapAuth.method));
        note->SetText(NoteFor(chosen));
        showPasswordNeeded(chosen, needsPassword(chosen));
    };

    content->AddChild(status);
    status->layoutItem.SetFlexGrow(1);
    status->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW =====
    auto buttonRow = CreateContainer("srvButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    // Destructive action on the far left, apart from Cancel/Save: a red "Delete
    // account" button (settings page only). It closes this page and hands off to
    // the callback, which runs its own confirm-and-remove. Copy the callback out
    // of `account` — that reference does not outlive Show, but the modal does.
    if (account.edit && account.onDelete) {
        std::function<void()> onDelete = account.onDelete;
        auto deleteBtn = CreateButton("srvDelete", 0, 0, 130, Theme::kControlHeight,
                                      "Delete account");
        Theme::FitToLabel(deleteBtn, 130);
        Theme::StyleDanger(deleteBtn);
        Theme::SetButtonIcon(deleteBtn, "trash");
        deleteBtn->onClick = [dlg, onDelete]() {
            dlg->CloseDialog(DialogResult::Cancel);
            if (onDelete) onDelete();
        };
        buttonRow->AddChild(deleteBtn);
    }

    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("srvCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    // The validated result, filled by Save and handed out when the dialog
    // closes with OK.
    auto result = std::make_shared<Result>();
    // Read + validate the fields; false (with the reason in `status`) when
    // something is missing. Fills the servers plus, on the settings page, the
    // edited display name and any typed new password.
    auto collect = [protocol, imap, smtp, imapAuth, smtpAuth, user, status, email, nameInput,
                    passInput, prefill](Result& out) {
        DiscoveryResult& r = out.settings;
        r = DiscoveryResult{};
        r.imap.protocol = ProtocolAt(protocol->GetSelectedIndex());
        r.imap.host = Trim(imap.host->GetText());
        r.imap.port = ParsePort(imap.port->GetText());
        // JMAP is HTTPS: whatever the (disabled) dropdown shows.
        r.imap.security = r.imap.protocol == MailProtocol::Jmap
            ? MailSecurity::SslTls : SecurityAt(imap.security->GetSelectedIndex());
        r.smtp.host = Trim(smtp.host->GetText());
        r.smtp.port = ParsePort(smtp.port->GetText());
        r.smtp.security = SecurityAt(smtp.security->GetSelectedIndex());
        r.imap.auth = SelectedAuth(*imapAuth.method);
        r.smtp.auth = SelectedAuth(*smtpAuth.method);
        const std::string username = Trim(user->GetText());
        r.imap.username = username.empty() ? email : username;
        r.smtp.username = r.imap.username;
        // Carry the provider identity the page does not edit — the OAuth
        // capability flags and the provider name — so saving edited servers
        // does not silently turn a Gmail/Outlook account into a generic one.
        r.imap.oauth  = prefill.imap.oauth;
        r.smtp.oauth  = prefill.smtp.oauth;
        r.displayName = prefill.displayName;
        out.displayName = nameInput ? Trim(nameInput->GetText()) : std::string();
        out.newPassword = passInput ? passInput->GetText() : std::string();  // not trimmed

        // A name that cannot be a server is reported here, before a sign-in
        // spends its ten-second timeout on it.
        auto badHost = [status](const char* field, const std::string& host) {
            const std::string problem = ServerNameProblem(host);
            if (problem.empty()) return false;
            status->SetTextColor(Theme::kWaitingText);
            status->SetText(std::string(field) + " \"" + host + "\": " + problem);
            return true;
        };
        const std::string incoming = "Incoming (" + ProtocolName(r.imap.protocol) + ") server";
        if (r.imap.host.empty())      { status->SetText("Enter the incoming (" + ProtocolName(r.imap.protocol) + ") server."); return false; }
        if (badHost(incoming.c_str(), r.imap.host)) return false;
        if (r.imap.port == 0)         { status->SetText("The incoming port must be a number from 1 to 65535."); return false; }
        // A JMAP account sends through its JMAP server unless an outgoing
        // server is given; everything else needs one.
        if (r.smtp.host.empty() && r.imap.protocol == MailProtocol::Jmap) {
            r.smtp = MailServerSettings{};
        } else {
            if (r.smtp.host.empty())      { status->SetText("Enter the outgoing (SMTP) server."); return false; }
            if (badHost("Outgoing (SMTP) server", r.smtp.host)) return false;
            if (r.smtp.port == 0)         { status->SetText("The outgoing port must be a number from 1 to 65535."); return false; }
        }
        r.found  = true;
        r.source = "manual";
        return true;
    };

    // The OAuth "Sign in again" button saves the (validated) name + servers and
    // flags a re-auth; the app then runs the browser sign-in. No login check
    // here — the check is the browser round-trip.
    if (oauthBtn) {
        oauthBtn->onClick = [dlg, collect, result]() {
            Result r;
            if (!collect(r)) return;
            r.reauth = true;
            *result = r;
            dlg->CloseDialog(DialogResult::OK);
        };
    }

    // "Details" appears after a failed check that reported its connection
    // chain (UltraNetResult::diagnostics): which component, server, TLS mode,
    // sign-in, library versions and trusted roots were involved. The status
    // line has room for the reason only; this is what a bug report needs.
    auto diagnostics = std::make_shared<std::string>();
    auto detailsBtn = CreateButton("srvDetails", 0, 0, 90, Theme::kControlHeight, "Details");
    Theme::FitToLabel(detailsBtn, 90);
    Theme::StyleSecondary(detailsBtn);
    Theme::SetButtonIcon(detailsBtn, "info");
    detailsBtn->SetVisible(false);
    detailsBtn->onClick = [diagnostics, status]() {
        AlertWarning(nullptr, "Connection details",
                     WithDiagnostics(status->GetText(), *diagnostics));
    };
    buttonRow->AddChild(detailsBtn);

    // "Save anyway" appears after a failed check, for a server that is down
    // right now or a check that could not run (no plug-in).
    auto anywayBtn = CreateButton("srvSaveAnyway", 0, 0, 130, Theme::kControlHeight, "Save anyway");
    Theme::FitToLabel(anywayBtn, 130);
    Theme::StyleSecondary(anywayBtn);
    anywayBtn->SetVisible(false);
    // Save and "Save anyway" both need the password a protocol switch asks
    // for: saved without it, the account could not connect. (The OAuth
    // button signs in through the browser instead.)
    auto passwordMissing = [needsPassword, showPasswordNeeded, passInput](const Result& r) {
        if (!needsPassword(r.settings.imap.protocol) || !r.newPassword.empty()) return false;
        showPasswordNeeded(r.settings.imap.protocol, true);
        passInput->SetFocus(true);
        return true;
    };
    anywayBtn->onClick = [dlg, collect, result, passwordMissing]() {
        Result r;
        if (!collect(r)) return;
        if (passwordMissing(r)) return;
        *result = r;
        dlg->CloseDialog(DialogResult::OK);
    };
    buttonRow->AddChild(anywayBtn);

    auto saveBtn = CreateButton("srvSave", 0, 0, 110, Theme::kControlHeight, "Save");
    Theme::FitToLabel(saveBtn, 110);
    Theme::StylePrimary(saveBtn);
    Theme::SetButtonIcon(saveBtn, "check");
    // The check's answer may arrive after the page was cancelled: hold the
    // dialog weakly and drop the answer when it is gone.
    std::weak_ptr<UltraCanvasModalDialog> weak = dialog;
    // Raw pointers to the buttons: the dialog owns them (a shared_ptr in the
    // button's own callback would be a cycle), and every use below is guarded
    // by the dialog still being alive.
    UltraCanvasButton* save   = saveBtn.get();
    UltraCanvasButton* anyway = anywayBtn.get();
    UltraCanvasButton* details = detailsBtn.get();
    saveBtn->onClick = [weak, collect, result, verify, status, save, anyway, details, diagnostics,
                        passwordMissing]() {
        Result r;
        if (!collect(r)) return;
        if (passwordMissing(r)) return;
        auto dlg = weak.lock();
        if (!dlg) return;
        if (!verify) {
            *result = r;
            dlg->CloseDialog(DialogResult::OK);
            return;
        }
        status->SetTextColor(Theme::kTextSecondary);
        status->SetText("Checking the sign-in at " + r.settings.imap.host + "…");
        save->SetDisabled(true);
        anyway->SetVisible(false);
        details->SetVisible(false);
        verify(r, [weak, result, r, status, save, anyway, details, diagnostics](UltraNetResult outcome) {
            auto dlg = weak.lock();
            if (!dlg) return;   // cancelled meanwhile
            if (outcome) {
                *result = r;
                dlg->CloseDialog(DialogResult::OK);
                return;
            }
            save->SetDisabled(false);
            status->SetTextColor(Theme::kWaitingText);
            const std::string detail = DetailLine(outcome);
            status->SetText("The sign-in at " + r.settings.imap.host + " did not succeed: "
                            + FriendlyMessage(outcome)
                            + (detail.empty() ? "" : " (" + detail + ")"));
            anyway->SetVisible(true);
            *diagnostics = outcome.diagnostics;
            details->SetVisible(!diagnostics->empty());
        });
    };
    buttonRow->AddChild(saveBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [result, onSave](DialogResult res) {
            if (res == DialogResult::OK && result->settings.found && onSave) onSave(*result);
        },
        parent);
}

} // namespace UltraMail
