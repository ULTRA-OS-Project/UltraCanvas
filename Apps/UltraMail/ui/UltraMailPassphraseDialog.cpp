// Apps/UltraMail/ui/UltraMailPassphraseDialog.cpp
// Version: 0.7.0 - PassphrasePrompt; the Primary Password's words; a mistake asks again
// Version: 0.6.0 - a header with a lock tile; Set password / Unlock carry icons
// Version: 0.5.0 - themed inputs and buttons
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPassphraseDialog.h"

#include "UltraMailTheme.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"

#include <algorithm>
#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

void PassphraseDialog::Show(UltraCanvasWindowBase* parent,
                            bool firstRun,
                            const std::string& errorText,
                            std::function<void(const std::string&)> onSubmit) {
    PassphrasePrompt prompt;
    if (firstRun) {
        prompt.title       = "Choose a Primary Password";
        prompt.heading     = "Choose a Primary Password";
        prompt.text        = "It encrypts your mail passwords and the mail stored on this "
                             "computer, and it is not stored anywhere - if you forget it, "
                             "the mail kept here has to be downloaded again and your mail "
                             "passwords entered again.";
        prompt.fieldLabel  = "New password";
        prompt.placeholder = "Choose a Primary Password";
        prompt.okLabel     = "Set password";
        prompt.okIcon      = "check";
        prompt.confirm     = true;
    } else {
        prompt.heading = "Unlock your mail";
        prompt.text    = "Enter your Primary Password to open your mail and your mail "
                         "account passwords.";
    }
    prompt.errorText = errorText;
    Show(parent, std::move(prompt), std::move(onSubmit));
}

void PassphraseDialog::Show(UltraCanvasWindowBase* parent, PassphrasePrompt prompt,
                            std::function<void(const std::string&)> onSubmit) {
    // The header's text as tall as its lines (about 58 characters fit one),
    // and the dialog that much taller.
    const int lines = std::max(2, static_cast<int>((prompt.text.size() + 57) / 58));
    const float textHeight = 14.0f * static_cast<float>(lines) + 4.0f;

    DialogConfig config;
    config.title      = prompt.title;
    config.width      = 440;
    config.height     = (prompt.confirm ? 212 : 172) + static_cast<int>(textHeight)
                        + (prompt.errorText.empty() ? 0 : 24);
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

    auto header = Theme::MakeDialogHeader("passHeader", "lock", prompt.heading, prompt.text, textHeight);
    header->SetMargin(0, 0, 4, 0);
    content->AddChild(header);

    // A previous wrong attempt, shown in place rather than as a stacked alert.
    if (!prompt.errorText.empty()) {
        auto err = CreateLabel("passError", 0, 0, 400, 16, prompt.errorText);
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
    pass->SetPlaceholder(prompt.placeholder);
    addRow("passMain", prompt.fieldLabel, pass);

    std::shared_ptr<UltraCanvasTextInput> confirm;
    if (prompt.confirm) {
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
    Theme::FitToLabel(cancelBtn, 90);
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto okBtn = CreateButton("passOk", 0, 0, 140, Theme::kControlHeight, prompt.okLabel);
    Theme::FitToLabel(okBtn, 140);
    Theme::StylePrimary(okBtn);
    Theme::SetButtonIcon(okBtn, prompt.okIcon);
    okBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(okBtn);

    dialog->AddChild(buttonRow);

    // Enter in the last field confirms the dialog.
    auto& lastField = prompt.confirm ? confirm : pass;
    lastField->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [pass, confirm, prompt, onSubmit, parent](DialogResult result) {
            if (result != DialogResult::OK) {   // cancelled: the caller stays locked
                if (prompt.onCancel) prompt.onCancel();
                return;
            }
            const std::string entered = pass->GetText();
            std::string mistake;
            if (entered.empty())
                mistake = "No password was entered.";
            else if (prompt.confirm && confirm && entered != confirm->GetText())
                mistake = "The two passwords do not match - type them again.";
            if (!mistake.empty()) {
                PassphrasePrompt again = prompt;
                again.errorText = mistake;
                Show(parent, std::move(again), onSubmit);
                return;
            }
            if (onSubmit) onSubmit(entered);
        },
        parent);
}

} // namespace UltraMail
