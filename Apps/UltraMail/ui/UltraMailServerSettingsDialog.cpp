// Apps/UltraMail/ui/UltraMailServerSettingsDialog.cpp
// Version: 0.2.0 - login check before saving
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailServerSettingsDialog.h"

#include "UltraMailTheme.h"
#include "UltraMailAlerts.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"

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
                                std::function<void(const DiscoveryResult&)> onSave,
                                Verifier verify) {
    DialogConfig config;
    config.title      = "Server settings for " + email;
    config.width      = 520;
    config.height     = 330;
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

    auto introLine = Theme::MakeLine("srvIntro", intro, 30, Theme::kSizeBody, Theme::kTextSecondary);
    introLine->SetWrap(TextWrap::WrapWord);
    content->AddChild(introLine);
    introLine->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // One server row: caption · host (grows) · port · security.
    struct ServerRow {
        std::shared_ptr<UltraCanvasTextInput> host, port;
        std::shared_ptr<UltraCanvasDropdown>  security;
    };
    auto addServerRow = [&content](const std::string& id, const std::string& caption,
                                   const MailServerSettings& s) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Lbl", caption, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);

        ServerRow r;
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

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return r;
    };

    ServerRow imap = addServerRow("srvImap", "Incoming (IMAP)", prefill.imap);
    ServerRow smtp = addServerRow("srvSmtp", "Outgoing (SMTP)", prefill.smtp);

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

    auto note = Theme::MakeLine("srvNote",
        "Ports are usually 993 (IMAP, SSL/TLS) or 143 (STARTTLS), and 465 (SMTP, "
        "SSL/TLS) or 587 (STARTTLS). Most providers list them under \"mail program "
        "settings\" or \"IMAP/SMTP\" in their help.", 30, Theme::kSizeBody,
        Theme::kTextSecondary);
    note->SetWrap(TextWrap::WrapWord);
    content->AddChild(note);
    note->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Validation feedback, in place.
    auto status = Theme::MakeLine("srvStatus", "", 16, Theme::kSizeBody, Theme::kWaitingText);
    status->SetWrap(TextWrap::WrapWord);
    content->AddChild(status);
    status->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW =====
    auto buttonRow = CreateContainer("srvButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("srvCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    // The validated result, filled by Save and handed out when the dialog
    // closes with OK.
    auto result = std::make_shared<DiscoveryResult>();
    // Read + validate the fields; false (with the reason in `status`) when
    // something is missing.
    auto collect = [imap, smtp, user, status, email](DiscoveryResult& r) {
        r = DiscoveryResult{};
        r.imap.host = Trim(imap.host->GetText());
        r.imap.port = ParsePort(imap.port->GetText());
        r.imap.security = SecurityAt(imap.security->GetSelectedIndex());
        r.smtp.host = Trim(smtp.host->GetText());
        r.smtp.port = ParsePort(smtp.port->GetText());
        r.smtp.security = SecurityAt(smtp.security->GetSelectedIndex());
        const std::string username = Trim(user->GetText());
        r.imap.username = username.empty() ? email : username;
        r.smtp.username = r.imap.username;

        if (r.imap.host.empty())      { status->SetText("Enter the incoming (IMAP) server."); return false; }
        if (r.imap.port == 0)         { status->SetText("The incoming port must be a number from 1 to 65535."); return false; }
        if (r.smtp.host.empty())      { status->SetText("Enter the outgoing (SMTP) server."); return false; }
        if (r.smtp.port == 0)         { status->SetText("The outgoing port must be a number from 1 to 65535."); return false; }
        r.found  = true;
        r.source = "manual";
        return true;
    };

    // "Save anyway" appears after a failed check, for a server that is down
    // right now or a check that could not run (no plug-in).
    auto anywayBtn = CreateButton("srvSaveAnyway", 0, 0, 130, Theme::kControlHeight, "Save anyway");
    Theme::StyleSecondary(anywayBtn);
    anywayBtn->SetVisible(false);
    anywayBtn->onClick = [dlg, collect, result]() {
        DiscoveryResult r;
        if (!collect(r)) return;
        *result = r;
        dlg->CloseDialog(DialogResult::OK);
    };
    buttonRow->AddChild(anywayBtn);

    auto saveBtn = CreateButton("srvSave", 0, 0, 110, Theme::kControlHeight, "Save");
    Theme::StylePrimary(saveBtn);
    // The check's answer may arrive after the page was cancelled: hold the
    // dialog weakly and drop the answer when it is gone.
    std::weak_ptr<UltraCanvasModalDialog> weak = dialog;
    // Raw pointers to the buttons: the dialog owns them (a shared_ptr in the
    // button's own callback would be a cycle), and every use below is guarded
    // by the dialog still being alive.
    UltraCanvasButton* save   = saveBtn.get();
    UltraCanvasButton* anyway = anywayBtn.get();
    saveBtn->onClick = [weak, collect, result, verify, status, save, anyway]() {
        DiscoveryResult r;
        if (!collect(r)) return;
        auto dlg = weak.lock();
        if (!dlg) return;
        if (!verify) {
            *result = r;
            dlg->CloseDialog(DialogResult::OK);
            return;
        }
        status->SetTextColor(Theme::kTextSecondary);
        status->SetText("Checking the sign-in at " + r.imap.host + "…");
        save->SetDisabled(true);
        anyway->SetVisible(false);
        verify(r, [weak, result, r, status, save, anyway](UltraNetResult outcome) {
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
            status->SetText("The sign-in at " + r.imap.host + " did not succeed: "
                            + FriendlyMessage(outcome)
                            + (detail.empty() ? "" : " (" + detail + ")"));
            anyway->SetVisible(true);
        });
    };
    buttonRow->AddChild(saveBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [result, onSave](DialogResult res) {
            if (res == DialogResult::OK && result->found && onSave) onSave(*result);
        },
        parent);
}

} // namespace UltraMail
