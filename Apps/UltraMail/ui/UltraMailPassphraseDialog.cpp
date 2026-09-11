// Apps/UltraMail/ui/UltraMailPassphraseDialog.cpp
// Version: 0.5.0 - themed inputs and buttons
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPassphraseDialog.h"

#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"

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
    config.width      = 420;
    config.height     = firstRun ? 300 : 230;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for button callbacks: the dialog outlives its buttons and owns
    // them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto content = CreateContainer("passForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = CreateLabel("passIntro", 0, 0, 380, 60,
        firstRun
            ? "Choose a master password. It encrypts the passwords of your mail "
              "accounts, and it is not stored anywhere — if you forget it you "
              "will have to enter your mail passwords again."
            : "Enter your master password to unlock your mail account passwords.");
    intro->SetWrap(TextWrap::WrapWord);
    intro->SetFontSize(Theme::kSizeBody);
    intro->SetTextColor(Theme::kTextSecondary);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // A previous wrong attempt, shown in place rather than as a stacked alert.
    if (!errorText.empty()) {
        auto err = CreateLabel("passError", 0, 0, 380, 16, errorText);
        err->SetWrap(TextWrap::WrapWord);
        err->SetFontSize(Theme::kSizeBody);
        err->SetTextColor(Theme::kWaitingText);
        content->AddChild(err);
        err->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }

    // Build a [label + input] flex row and append it to the content column.
    auto addRow = [&content](const std::string& id, const std::string& labelText,
                             const std::shared_ptr<UltraCanvasTextInput>& input) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, Theme::kControlHeight);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kInnerGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto label = Theme::MakeLine(id + "Lbl", labelText, Theme::kControlHeight,
                                     Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(104.0f, Theme::kControlHeight));
        row->AddChild(label);
        Theme::StyleInput(input);
        row->AddChild(input);
        input->layoutItem.SetFlexGrow(1);
        content->AddChild(row);
        row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    };

    auto pass = CreatePasswordInput("passField", 0, 0, 0, Theme::kControlHeight);
    pass->SetPlaceholder(firstRun ? "Choose a master password" : "Your master password");
    addRow("passMain", firstRun ? "Master password" : "Password", pass);

    std::shared_ptr<UltraCanvasTextInput> confirm;
    if (firstRun) {
        confirm = CreatePasswordInput("passConfirm", 0, 0, 0, Theme::kControlHeight);
        confirm->SetPlaceholder("Type it again");
        addRow("passConfirm", "Repeat", confirm);
    }

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW =====
    auto buttonRow = CreateContainer("passButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("passCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto okBtn = CreateButton("passOk", 0, 0, 140, Theme::kControlHeight,
                              firstRun ? "Set password" : "Unlock");
    Theme::StylePrimary(okBtn);
    okBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(okBtn);

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
