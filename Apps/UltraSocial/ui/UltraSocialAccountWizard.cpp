// Apps/UltraSocial/ui/UltraSocialAccountWizard.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialAccountWizard.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <array>
#include <memory>

using namespace UltraCanvas;

namespace UltraSocial {

namespace {

// Per-network form wording. The three fields keep their positions; only
// labels, placeholders and the hint change with the dropdown.
struct NetworkForm {
    SocialNetwork network;
    const char* dropdownText;
    const char* hint;
    const char* serverLabel;
    const char* serverPlaceholder;
    const char* identifierLabel;
    const char* identifierPlaceholder;
    const char* secretLabel;
    const char* secretPlaceholder;
};

constexpr std::array<NetworkForm, 3> kForms{{
    { SocialNetwork::Mastodon, "Mastodon / Fediverse",
      "Enter your instance — UltraSocial registers itself there and signs "
      "you in through your browser; no keys needed. Advanced: paste an "
      "access token instead to skip the browser.",
      "Instance", "mastodon.social",
      "(not used)", "",
      "Access token", "optional — empty opens the browser" },
    { SocialNetwork::Bluesky, "Bluesky",
      "Create an app password under Settings → App Passwords on "
      "Bluesky, then sign in with it here — never your main password.",
      "Server (PDS)", "bsky.social (default)",
      "Handle / e-mail", "erika.bsky.social",
      "App password", "xxxx-xxxx-xxxx-xxxx" },
    { SocialNetwork::Telegram, "Telegram channel",
      "Create a bot with @BotFather, add it to your channel as an "
      "administrator, and paste its token here.",
      "Server", "(default)",
      "Channel", "@mychannel",
      "Bot token", "123456:ABC-DEF…" },
}};

} // namespace

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const WizardInput&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add social account";
    config.width      = 500;
    config.height     = 390;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and
    // owns them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto content = CreateContainer("swForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    struct Row {
        std::shared_ptr<UltraCanvasLabel> label;
        std::shared_ptr<UltraCanvasTextInput> input;
    };
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, 30);
        row->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto label = CreateLabel(id + "Label", 0, 0, 120, 26, labelText);
        row->AddChild(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return Row{label, input};
    };

    // Network picker row.
    auto networkRow = CreateContainer("swNetworkRow", 0, 0, 0, 30);
    networkRow->layout.SetFlexRow()
                      .SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto networkLabel = CreateLabel("swNetworkLabel", 0, 0, 120, 26, "Network");
    networkRow->AddChild(networkLabel);
    networkLabel->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    auto network = CreateDropdown("swNetwork", 0, 0, 260, 28);
    for (const auto& form : kForms) network->AddItem(form.dropdownText);
    networkRow->AddChild(network);
    network->layoutItem.SetFlexGrow(1);
    content->AddChild(networkRow);
    networkRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto hint = CreateLabel("swHint", 0, 0, 460, 78, kForms[0].hint);
    hint->SetWrap(TextWrap::WrapWord);
    content->AddChild(hint);
    hint->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto server = CreateTextInput("swServer", 0, 0, 260, 28);
    Row serverRow = addRow("swServer", kForms[0].serverLabel, server);

    auto identifier = CreateTextInput("swIdentifier", 0, 0, 260, 28);
    Row identifierRow = addRow("swIdentifier", kForms[0].identifierLabel, identifier);

    auto secret = CreatePasswordInput("swSecret", 0, 0, 260, 28);
    Row secretRow = addRow("swSecret", kForms[0].secretLabel, secret);

    auto applyForm = [hint, serverRow, identifierRow, secretRow](int index) {
        const auto& form = kForms[static_cast<std::size_t>(index)];
        hint->SetText(form.hint);
        serverRow.label->SetText(form.serverLabel);
        serverRow.input->SetPlaceholder(form.serverPlaceholder);
        identifierRow.label->SetText(form.identifierLabel);
        identifierRow.input->SetPlaceholder(form.identifierPlaceholder);
        secretRow.label->SetText(form.secretLabel);
        secretRow.input->SetPlaceholder(form.secretPlaceholder);
    };
    applyForm(0);
    network->SetSelectedIndex(0, /*runNotifications=*/false);
    network->onSelectionChanged = [applyForm](int index, const DropdownItem&) {
        if (index >= 0 && index < static_cast<int>(kForms.size())) applyForm(index);
    };

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Connect / Cancel =====
    auto buttonRow = CreateContainer("swButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto connectBtn = CreateButton("swConnect", 0, 0, 100, 28, "Connect");
    connectBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(connectBtn);

    auto cancelBtn = CreateButton("swCancel", 0, 0, 80, 28, "Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [network, server, identifier, secret, onSubmit](DialogResult result) {
            if (result != DialogResult::OK || !onSubmit) return;
            int index = network->GetSelectedIndex();
            if (index < 0 || index >= static_cast<int>(kForms.size())) index = 0;
            WizardInput input;
            input.network    = kForms[static_cast<std::size_t>(index)].network;
            input.server     = server->GetText();
            input.identifier = identifier->GetText();
            input.secret     = secret->GetText();
            onSubmit(input);
        },
        parent);
}

} // namespace UltraSocial
