// UltraCloud/ui/UltraCloudAccountDialog.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCloudAccountDialog.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <memory>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace UltraCloud {

namespace {

constexpr float kLabelWidth = 130.0f;
constexpr float kRowHeight  = 30.0f;

// Providers in the order the dialog offers them: real ones first, the
// in-process demo last.
std::vector<std::shared_ptr<ICloudProvider>> OrderedProviders() {
    std::vector<std::shared_ptr<ICloudProvider>> out;
    for (const char* id : {"nextcloud", "webdav"})
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
                          std::function<void(const Account&)> onAdded) {
    DialogConfig config;
    config.title      = "Add cloud account";
    config.width      = 520;
    config.height     = 420;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for the widgets' callbacks: the dialog owns them, so a
    // shared_ptr capture would form a cycle. The worker thread below keeps a
    // weak_ptr instead and only touches the dialog on the UI thread.
    auto* dlg = dialog.get();
    std::weak_ptr<UltraCanvasModalDialog> weak = dialog;

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto form = CreateContainer("cloudAccForm", 0, 0, 0, 0);
    form->layout.SetFlexColumn()
                .SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // [label | control] rows.
    auto addRow = [&form](const std::string& id, const std::string& caption,
                          const std::shared_ptr<UltraCanvasUIElement>& control) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, kRowHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = CreateLabel(id + "Label", 0, 0, kLabelWidth, 26, caption);
        row->AddChild(label);
        row->AddChild(control);
        control->layoutItem.SetFlexGrow(1);
        form->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return row;
    };

    const auto providers = OrderedProviders();
    auto provider = CreateDropdown("cloudAccProvider", 0, 0, 300, 28);
    for (const auto& p : providers) provider->AddItem(p->DisplayName(), p->Id());
    if (!providers.empty()) provider->SetSelectedIndex(0, /*runNotifications=*/false);
    addRow("cloudAccProvider", "Provider", provider);

    auto name = CreateTextInput("cloudAccName", 0, 0, 300, 28);
    name->SetPlaceholder("My cloud (optional)");
    addRow("cloudAccName", "Name", name);

    auto server = CreateTextInput("cloudAccServer", 0, 0, 300, 28);
    server->SetPlaceholder("https://cloud.example.com");
    auto serverRow = addRow("cloudAccServer", "Server URL", server);

    auto user = CreateTextInput("cloudAccUser", 0, 0, 300, 28);
    user->SetPlaceholder("user name");
    addRow("cloudAccUser", "User", user);

    auto password = CreatePasswordInput("cloudAccPass", 0, 0, 300, 28);
    password->SetPlaceholder("password or app password");
    auto passwordRow = addRow("cloudAccPass", "Password", password);

    auto publicUrl = CreateTextInput("cloudAccPublic", 0, 0, 300, 28);
    publicUrl->SetPlaceholder("https://files.example.org/pub (links = this URL + path)");
    auto publicRow = addRow("cloudAccPublic", "Public URL", publicUrl);

    auto folder = CreateTextInput("cloudAccFolder", 0, 0, 300, 28);
    folder->SetText("/Shared from ULTRA OS");
    addRow("cloudAccFolder", "Upload folder", folder);

    auto makeDefault = UltraCanvasCheckbox::CreateCheckbox(
        "cloudAccDefault", 0, 0, 300, 26, "Use as the default cloud account", false);
    addRow("cloudAccDefaultRow", "", makeDefault);

    form->AddChild(CreateLabel("cloudAccHint", 0, 0, 0, 36,
        "Nextcloud: create an app password under Settings → Security and use it here."));

    auto status = CreateLabel("cloudAccStatus", 0, 0, 0, 22, "");
    status->SetTextColor(Color(180, 40, 40, 255));
    form->AddChild(status);

    dialog->AddChild(form);
    form->layoutItem.SetFlexGrow(1);

    // Show only the rows the chosen provider needs.
    auto applyProvider = [providers, serverRow, publicRow, passwordRow, provider]() {
        int idx = provider->GetSelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(providers.size())) return;
        const auto caps = providers[static_cast<std::size_t>(idx)]->Capabilities();
        const std::string id = providers[static_cast<std::size_t>(idx)]->Id();
        serverRow->SetVisible(caps.needsServerUrl);
        publicRow->SetVisible(id == "webdav");
        passwordRow->SetVisible(!caps.needsOAuth && id != "memory");
    };
    provider->onSelectionChanged = [applyProvider](int, const DropdownItem&) { applyProvider(); };
    applyProvider();

    // Buttons.
    auto buttons = CreateContainer("cloudAccButtons", 0, 0, 0, 36);
    buttons->layout.SetFlexRow()
                   .SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttons->AddStretchSpacer(1);
    auto addBtn = CreateButton("cloudAccAdd", 0, 0, 120, 28, "Add account");
    auto cancelBtn = CreateButton("cloudAccCancel", 0, 0, 80, 28, "Cancel");
    buttons->AddChild(addBtn);
    buttons->AddChild(cancelBtn);
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
        if (a.username.empty() && a.providerId != "memory") { status->SetText("Enter the user name."); return; }

        status->SetTextColor(Color(60, 60, 60, 255));
        status->SetText("Signing in…");
        addBtn->SetDisabled(true);

        // Verify + store off the UI thread; report back on it.
        std::thread([weak, dlg, added, addBtn, status, a, c, &service]() mutable {
            Result r = service.AddAccount(a, c, /*verify=*/true);
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
