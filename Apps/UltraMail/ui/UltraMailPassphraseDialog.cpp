// Apps/UltraMail/ui/UltraMailPassphraseDialog.cpp
// Version: 0.4.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPassphraseDialog.h"

#include "UltraMailAlerts.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

void PassphraseDialog::Show(UltraCanvasWindowBase* parent,
                            bool firstRun,
                            const std::string& errorText,
                            std::function<void(const std::string&)> onSubmit) {
    DialogConfig config;
    config.title      = firstRun ? "Choose a master password" : "Master password";
    config.width      = 460;
    config.height     = firstRun ? 380 : 280;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and owns
    // them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(16);

    auto content = CreateContainer("passForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = CreateLabel("passIntro", 0, 0, 420, 84,
        firstRun
            ? "Choose a master password. It encrypts the passwords of your mail "
              "accounts, and it is not stored anywhere — if you forget it you "
              "will have to enter your mail passwords again."
            : "Enter your master password to unlock your mail account passwords.");
    intro->SetWrap(TextWrap::WrapWord);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // A previous wrong attempt, shown in place rather than as a stacked alert.
    if (!errorText.empty()) {
        auto err = CreateLabel("passError", 0, 0, 420, 20, errorText);
        err->SetWrap(TextWrap::WrapWord);
        content->AddChild(err);
        err->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, 30);
        row->layout.SetFlexRow()
                   .SetFlexGap(10)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = CreateLabel(id + "Lbl", 0, 0, 140, 24, labelText);
        row->AddChild(label);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto pass = CreatePasswordInput("passField", 0, 0, 260, 28);
    pass->SetPlaceholder(firstRun ? "Choose a master password" : "Your master password");
    addRow("passMain", firstRun ? "Master password" : "Password", pass);

    std::shared_ptr<UltraCanvasTextInput> confirm;
    if (firstRun) {
        confirm = CreatePasswordInput("passConfirm", 0, 0, 260, 28);
        confirm->SetPlaceholder("Type it again");
        addRow("passConfirm", "Repeat", confirm);
    }

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW =====
    auto buttonRow = CreateContainer("passButtons", 0, 0, 0, 36);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto okBtn = std::make_shared<UltraCanvasButton>("passOk", 0, 0, 140, 28);
    okBtn->SetText(firstRun ? "Set password" : "Unlock");
    okBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(okBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>("passCancel", 0, 0, 80, 28);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    dialog->AddChild(buttonRow);

    // Enter in the last field confirms the dialog.
    auto& lastField = firstRun ? confirm : pass;
    lastField->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [pass, confirm, firstRun, onSubmit, parent](DialogResult result) {
            if (result != DialogResult::OK) return;   // cancelled: stay locked
            const std::string entered = pass->GetText();
            if (entered.empty()) {
                AlertWarning(parent, "No master password was entered.",
                             "Your mail passwords stay locked until you enter it.");
                return;
            }
            if (firstRun && confirm && entered != confirm->GetText()) {
                AlertWarning(parent, "The two passwords do not match.",
                             "Nothing was saved — try again.");
                return;
            }
            if (onSubmit) onSubmit(entered);
        },
        parent);
}

} // namespace UltraMail
