// Apps/UltraMail/ui/UltraMailWaitDialog.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailWaitDialog.h"

#include "UltraMailTheme.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

using namespace UltraCanvas;

namespace UltraMail {

std::shared_ptr<UltraCanvasModalDialog> WaitDialog::Show(
        UltraCanvasWindowBase* parent, const std::string& title,
        const std::string& text, std::function<void()> onCancel) {
    DialogConfig config;
    config.title      = title;
    config.width      = 460;
    config.height     = 250;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;  // Custom dialog builds its own.

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer for the button callback: the dialog outlives its buttons and
    // owns them, so capturing the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    auto label = CreateLabel("waitText", 0, 0, 420, 110, text);
    label->SetWrap(TextWrap::WrapWord);
    label->SetFontSize(Theme::kSizeBody);
    label->SetTextColor(Theme::kTextSecondary);
    dialog->AddChild(label);
    label->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto buttonRow = CreateContainer("waitButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("waitCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [onCancel](DialogResult result) {
            // Closed by the app (Close → OK) means the step finished; any
            // other way out is the user giving up.
            if (result != DialogResult::OK && onCancel) onCancel();
        },
        parent);
    return dialog;
}

void WaitDialog::Close(const std::weak_ptr<UltraCanvasModalDialog>& dialog) {
    if (auto d = dialog.lock()) d->CloseDialog(DialogResult::OK);
}

std::shared_ptr<UltraCanvasModalDialog> OAuthWaitDialog::Show(
        UltraCanvasWindowBase* parent, const std::string& providerName,
        const std::string& email, std::function<void()> onCancel) {
    return WaitDialog::Show(parent, "Sign in with " + providerName,
        "Your browser has opened " + providerName + "'s sign-in page. Sign in as "
        + email + " and allow UltraMail to read and send your mail, then come back "
        "here — this window closes by itself once the sign-in is done.",
        std::move(onCancel));
}

} // namespace UltraMail
