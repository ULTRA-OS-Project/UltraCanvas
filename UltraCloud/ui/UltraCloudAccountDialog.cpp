// UltraCloud/ui/UltraCloudAccountDialog.cpp
// Version: 0.4.0 - the form is a UltraCanvasFormLayout grid, not a row of
//                  flex containers with hard-coded caption widths
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCloudAccountDialog.h"
#include "UltraCloudUiStyle.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"   // OpenURL

#include <UltraCloud/UltraCloudOAuth.h>

#include <memory>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace UltraCloud {

namespace {

// A caption and the control it describes, kept together so a row the chosen
// provider does not need leaves the grid whole. Hiding only the control would
// leave its caption behind and push every row below it into the wrong column.
struct FormRow {
    std::shared_ptr<UltraCanvasLabel> caption;
    std::shared_ptr<UltraCanvasUIElement> control;

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

} // namespace

void ShowAddAccountDialog(UltraCanvasWindowBase* parent, CloudService& service,
                          std::function<void(const Account&)> onAdded,
                          std::function<bool(const std::string& providerId)> providerFilter,
                          const std::string& title) {
    DialogConfig config;
    config.title      = title.empty() ? "Add cloud account" : title;
    config.width      = 520;
    // Tall enough for the provider that needs every row: WebDAV's seven
    // captioned rows (7x32) plus the checkbox (26), the hint (36) and the
    // status line (22), nine 8 px row gaps, the 20 px padding on each side,
    // and the button row (36) with its own gaps. The rows are not allowed to
    // shrink into each other any more, so the dialog has to be tall enough to
    // hold them rather than the layout absorbing the difference.
    config.height     = 490;
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
    auto form = CreateFormGrid("cloudAccForm", 8.0f, UiStyle::kGap);

    // [caption | control] rows. The control carries no explicit width: the
    // `1fr` column gives it the whole remaining width, whatever the dialog is.
    auto addRow = [&form](const std::string& id, const std::string& caption,
                          const std::shared_ptr<UltraCanvasUIElement>& control) {
        auto label = UiStyle::MakeCaption(id + "Label", caption);
        if (auto input = std::dynamic_pointer_cast<UltraCanvasTextInput>(control))
            UiStyle::StyleInput(input);
        AddFormRow(form, label, control);
        return FormRow{label, control};
    };

    // The caller's filter narrows the list; an empty result would leave a
    // dialog with nothing to choose, so in that case the unfiltered list is
    // offered rather than an unusable form.
    std::vector<std::shared_ptr<ICloudProvider>> providers = OrderedProviders();
    if (providerFilter) {
        std::vector<std::shared_ptr<ICloudProvider>> kept;
        for (const auto& p : providers)
            if (providerFilter(p->Id())) kept.push_back(p);
        if (!kept.empty()) providers = std::move(kept);
    }
    auto provider = CreateDropdown("cloudAccProvider", 0, 0, 0, UiStyle::kControlHeight);
    for (const auto& p : providers) provider->AddItem(p->DisplayName(), p->Id());
    if (!providers.empty()) provider->SetSelectedIndex(0, /*runNotifications=*/false);
    addRow("cloudAccProvider", "Provider", provider);

    auto name = CreateTextInput("cloudAccName", 0, 0, 0, UiStyle::kControlHeight);
    name->SetPlaceholder("My cloud (optional)");
    addRow("cloudAccName", "Name", name);

    auto server = CreateTextInput("cloudAccServer", 0, 0, 0, UiStyle::kControlHeight);
    server->SetPlaceholder("https://cloud.example.com");
    auto serverRow = addRow("cloudAccServer", "Server URL", server);

    auto user = CreateTextInput("cloudAccUser", 0, 0, 0, UiStyle::kControlHeight);
    user->SetPlaceholder("user name");
    auto userRow = addRow("cloudAccUser", "User", user);

    auto password = CreatePasswordInput("cloudAccPass", 0, 0, 0, UiStyle::kControlHeight);
    password->SetPlaceholder("password or app password");
    auto passwordRow = addRow("cloudAccPass", "Password", password);

    auto publicUrl = CreateTextInput("cloudAccPublic", 0, 0, 0, UiStyle::kControlHeight);
    publicUrl->SetPlaceholder("https://files.example.org/pub (links = this URL + path)");
    auto publicRow = addRow("cloudAccPublic", "Public URL", publicUrl);

    auto folder = CreateTextInput("cloudAccFolder", 0, 0, 0, UiStyle::kControlHeight);
    folder->SetText("/Shared from ULTRA OS");
    addRow("cloudAccFolder", "Upload folder", folder);

    // Its own caption, so it spans both columns - as do the hint and the
    // status line below it.
    auto makeDefault = UltraCanvasCheckbox::CreateCheckbox(
        "cloudAccDefault", 0, 0, 300, 26, "Use as the default cloud account", false);
    AddFormWideRow(form, makeDefault);

    auto hint = CreateLabel("cloudAccHint", 0, 0, 0, 36,
        "Nextcloud: create an app password under Settings → Security and use it here.");
    hint->SetWrap(TextWrap::WrapWord);
    hint->SetFontSize(UiStyle::kFontSize - 1.0f);
    hint->SetTextColor(UiStyle::kTextSecondary);
    AddFormWideRow(form, hint);

    auto status = CreateLabel("cloudAccStatus", 0, 0, 0, 22, "");
    status->SetFontSize(UiStyle::kFontSize);
    status->SetTextColor(UiStyle::kDanger);
    AddFormWideRow(form, status);

    dialog->AddChild(form);
    // The form keeps its rows at their own height (CreateFormGrid sets
    // flex-shrink: 0) and the spacer takes the slack, so the buttons sit at
    // the bottom whether the provider needs three rows or seven.
    dialog->AddChild(std::make_shared<UltraCanvasSpacer>(0, 0, 1.0f));

    // Buttons (created here so applyProvider can relabel the Add button).
    auto addBtn = CreateButton("cloudAccAdd", 0, 0, 160, UiStyle::kControlHeight, "Add account");
    UiStyle::StylePrimary(addBtn);

    // Show only the rows the chosen provider needs. OAuth providers sign in
    // through the browser: no user / password rows, and the button says so.
    auto applyProvider = [providers, serverRow, publicRow, passwordRow, userRow, provider,
                          hint, addBtn]() {
        int idx = provider->GetSelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(providers.size())) return;
        const auto& p = providers[static_cast<std::size_t>(idx)];
        const auto caps = p->Capabilities();
        const std::string id = p->Id();
        serverRow.SetVisible(caps.needsServerUrl);
        publicRow.SetVisible(id == "webdav");
        userRow.SetVisible(!caps.needsOAuth);
        passwordRow.SetVisible(!caps.needsOAuth && id != "memory");
        if (caps.needsOAuth) {
            addBtn->SetText("Sign in in browser");
            hint->SetText(HasOAuthApp(id)
                ? "Your browser opens " + p->DisplayName() + "'s sign-in page; come back here when it says you are done."
                : "No OAuth client id is configured for " + p->DisplayName()
                  + " — set ULTRACLOUD_" + id + "_CLIENT_ID (see Docs/Modules/UltraCloud/README.md).");
        } else {
            addBtn->SetText("Add account");
            hint->SetText(id == "nextcloud"
                ? "Nextcloud: create an app password under Settings → Security and use it here."
                : id == "webdav" ? "Links need a public URL that serves the same folder as the DAV root."
                : "");
        }
    };
    provider->onSelectionChanged = [applyProvider](int, const DropdownItem&) { applyProvider(); };
    applyProvider();

    // Button row.
    auto buttons = CreateContainer("cloudAccButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("cloudAccCancel", 0, 0, 90, UiStyle::kControlHeight, "Cancel");
    UiStyle::StyleSecondary(cancelBtn);
    buttons->AddChild(cancelBtn);
    buttons->AddChild(addBtn);
    dialog->AddChild(buttons);

    // The result travels from the worker to the close callback through here.
    auto added = std::make_shared<Account>();

    addBtn->onClick = [=, &service]() {
        int idx = provider->GetSelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(providers.size())) return;
        Account a;
        a.providerId    = providers[static_cast<std::size_t>(idx)]->Id();
        a.displayName   = name->GetText();
        a.serverUrl     = server->GetText();
        a.username      = user->GetText();
        a.publicBaseUrl = publicUrl->GetText();
        a.remoteFolder  = folder->GetText();
        a.isDefault     = makeDefault->IsChecked();
        Credentials c;
        c.username = a.username;
        c.password = password->GetText();

        const auto caps = providers[static_cast<std::size_t>(idx)]->Capabilities();
        if (caps.needsServerUrl && a.serverUrl.empty()) { status->SetText("Enter the server URL."); return; }
        if (!caps.needsOAuth && a.username.empty() && a.providerId != "memory") {
            status->SetText("Enter the user name."); return;
        }
        if (caps.needsOAuth && !HasOAuthApp(a.providerId)) {
            status->SetText("No OAuth client id configured for this provider."); return;
        }

        status->SetTextColor(Color(60, 60, 60, 255));
        status->SetText(caps.needsOAuth ? "Waiting for the browser sign-in…" : "Signing in…");
        addBtn->SetDisabled(true);

        // Verify + store off the UI thread; report back on it. OAuth providers
        // open the consent page in the system browser and wait for the
        // loopback redirect.
        const bool oauth = caps.needsOAuth;
        std::thread([weak, dlg, added, addBtn, status, a, c, oauth, &service]() mutable {
            Result r = oauth
                ? service.SignInAccount(a, [](const std::string& url) { UltraCanvas::OpenURL(url); })
                : service.AddAccount(a, c, /*verify=*/true);
            auto* app = UltraCanvasApplicationBase::GetCurrent();
            auto finish = [weak, dlg, added, addBtn, status, a, r]() {
                if (weak.expired()) return;   // dialog already closed
                if (r) { *added = a; dlg->CloseDialog(DialogResult::OK); return; }
                status->SetTextColor(Color(180, 40, 40, 255));
                status->SetText(r.message);
                addBtn->SetDisabled(false);
            };
            if (app) app->PostToUIThread(finish); else finish();
        }).detach();
    };
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [added, onAdded](DialogResult result) {
            if (result == DialogResult::OK && onAdded && !added->accountId.empty()) onAdded(*added);
        },
        parent);
}

} // namespace UltraCloud
