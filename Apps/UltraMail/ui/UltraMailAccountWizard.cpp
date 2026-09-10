// Apps/UltraMail/ui/UltraMailAccountWizard.cpp
// Version: 0.4.1 - live hint per address: browser sign-in for Gmail / Outlook,
//                  an app password for Yahoo / iCloud (or a typed password
//                  at an OAuth2 provider)
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountWizard.h"

#include "UltraCanvasModalDialog.h"
#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"
#include "UltraMailDiscovery.h"
#include "UltraMailOAuth.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kLabelWidth = 110.0f;
} // namespace

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const AccountDraft&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add email account";
    config.width      = 460;
    config.height     = 330;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and owns
    // them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    // A Custom dialog builds its own window layout + children (see the framework's
    // Texter dialogs). Vertical stack: content grows, button row sits at the bottom.
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    // ===== CONTENT: intro + labelled input rows =====
    auto content = CreateContainer("wizardForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = Theme::MakeLine("wizIntro",
        "Enter your address and password — UltraMail finds the rest.", 36,
        Theme::kSizeBody, Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto label = Theme::MakeLine(id + "Label", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(kLabelWidth, Theme::kControlHeight));
        row->AddChild(label);

        Theme::StyleInput(input);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto name = CreateTextInput("wizName", 0, 0, 0, Theme::kControlHeight);
    name->SetPlaceholder("Erika Example");
    addRow("wizName", "Your name", name);

    auto email = CreateEmailInput("wizEmail", 0, 0, 0, Theme::kControlHeight);
    email->SetPlaceholder("erika@example.com");
    addRow("wizEmail", "Email address", email);

    auto password = CreatePasswordInput("wizPass", 0, 0, 0, Theme::kControlHeight);
    password->SetPlaceholder("Your password");
    addRow("wizPass", "Password", password);

    // Provider-specific advice that follows the address as it is typed: Gmail
    // and Outlook sign in through the browser (OAuth2) when the password is
    // left empty; they, Yahoo and iCloud need an app password otherwise.
    auto hint = Theme::MakeLine("wizHint", "", 40, Theme::kSizeBody, Theme::kTextSecondary);
    hint->SetWrap(TextWrap::WrapWord);
    content->AddChild(hint);
    hint->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    email->onTextChanged = [hint, password](const std::string& text) {
        const DiscoveryResult d = AutoDiscovery::FromPresets(text);
        const std::string provider = OAuthProviderFor(d);
        if (!provider.empty() && OAuthApps::Has(provider)) {
            const std::string name = OAuthProviderDisplayName(provider);
            hint->SetText(d.displayName + ": leave the password empty to sign in with "
                          + name + " in your browser, or enter an app password.");
            password->SetPlaceholder("Leave empty to sign in with " + name);
        } else if (ProviderNeedsAppPassword(d)) {
            hint->SetText(d.displayName + " rejects the normal password in mail programs: "
                          "enter an app password generated in your account's security "
                          "settings.");
            password->SetPlaceholder("App password");
        } else {
            hint->SetText("");
            password->SetPlaceholder("Your password");
        }
    };

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Continue / Cancel =====
    auto buttonRow = CreateContainer("wizButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("wizCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto continueBtn = CreateButton("wizContinue", 0, 0, 110, Theme::kControlHeight, "Continue");
    Theme::StylePrimary(continueBtn);
    continueBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(continueBtn);

    dialog->AddChild(buttonRow);

    // Enter in the password field confirms the dialog.
    password->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [name, email, password, onSubmit, parent](DialogResult result) {
            if (result != DialogResult::OK) return;
            AccountDraft draft;
            draft.displayName = name->GetText();
            draft.email       = email->GetText();
            draft.password    = password->GetText();

            // Say why nothing happened rather than discarding what was typed.
            if (draft.email.empty()) {
                AlertWarning(parent, "No e-mail address was entered, so no "
                                     "account was added.",
                             "Enter the address of the mailbox you want to add, "
                             "for example you@example.com.");
                return;
            }
            if (!LooksLikeEmailAddress(draft.email)) {
                AlertWarning(parent, "\"" + draft.email + "\" is not a valid "
                             "e-mail address, so no account was added.",
                             "An address looks like you@example.com.");
                return;
            }
            if (onSubmit) onSubmit(draft);
        },
        parent);
}

} // namespace UltraMail
