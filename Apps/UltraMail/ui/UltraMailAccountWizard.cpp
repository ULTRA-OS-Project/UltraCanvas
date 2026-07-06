// Apps/UltraMail/ui/UltraMailAccountWizard.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountWizard.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

void AccountWizard::Show(UltraCanvasWindowBase* parent,
                         std::function<void(const AccountDraft&)> onSubmit) {
    DialogConfig config;
    config.title      = "Add email account";
    config.width      = 460;
    config.height     = 340;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::OKCancel;   // OK = Continue, Cancel

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);

    // Identity form. Layout coordinates are hints; the layout engine finalises
    // placement inside the dialog's content area.
    auto form = CreateContainer("wizardForm", 0, 0, 440, 260);

    form->AddChild(CreateLabel("wizIntro", 12, 8, 420, 40,
        "Enter your address and password — UltraMail finds the rest."));

    form->AddChild(CreateLabel("wizNameLabel", 12, 56, 140, 22, "Your name"));
    auto name = CreateTextInput("wizName", 160, 56, 260, 28);
    name->SetPlaceholder("Erika Example");
    form->AddChild(name);

    form->AddChild(CreateLabel("wizEmailLabel", 12, 96, 140, 22, "Email address"));
    auto email = CreateEmailInput("wizEmail", 160, 96, 260, 28);
    email->SetPlaceholder("erika@example.com");
    form->AddChild(email);

    form->AddChild(CreateLabel("wizPassLabel", 12, 136, 140, 22, "Password"));
    auto password = CreatePasswordInput("wizPass", 160, 136, 260, 28);
    form->AddChild(password);

    dialog->AddDialogElement(form);

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
