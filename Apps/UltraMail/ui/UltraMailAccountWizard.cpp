// Apps/UltraMail/ui/UltraMailAccountWizard.cpp
// Version: 0.2.0
// Last Modified: 2026-07-08
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountWizard.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const AccountDraft&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add email account";
    config.width      = 460;
    config.height     = 300;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and owns
    // them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    // A Custom dialog builds its own window layout + children (see the framework's
    // Texter dialogs). Vertical stack: content grows, button row sits at the bottom.
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    // ===== CONTENT: intro + labelled input rows =====
    auto content = CreateContainer("wizardForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = CreateLabel("wizIntro", 0, 0, 420, 36,
        "Enter your address and password — UltraMail finds the rest.");
    intro->SetWrap(TextWrap::WrapWord);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, 30);
        row->layout.SetFlexRow()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto label = CreateLabel(id + "Label", 0, 0, 110, 26, labelText);
        row->AddChild(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);

        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto name = CreateTextInput("wizName", 0, 0, 260, 28);
    name->SetPlaceholder("Erika Example");
    addRow("wizName", "Your name", name);

    auto email = CreateEmailInput("wizEmail", 0, 0, 260, 28);
    email->SetPlaceholder("erika@example.com");
    addRow("wizEmail", "Email address", email);

    auto password = CreatePasswordInput("wizPass", 0, 0, 260, 28);
    password->SetPlaceholder("Your password");
    addRow("wizPass", "Password", password);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW: Continue / Cancel =====
    auto buttonRow = CreateContainer("wizButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto continueBtn = std::make_shared<UltraCanvasButton>("wizContinue", 0, 0, 100, 28);
    continueBtn->SetText("Continue");
    continueBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(continueBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>("wizCancel", 0, 0, 80, 28);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    // Enter in the password field confirms the dialog.
    password->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [name, email, password, onSubmit](DialogResult result) {
            if (result != DialogResult::OK) return;
            AccountDraft draft;
            draft.displayName = name->GetText();
            draft.email       = email->GetText();
            draft.password    = password->GetText();
            if (onSubmit && !draft.email.empty()) onSubmit(draft);
        },
        parent);
}

} // namespace UltraMail
