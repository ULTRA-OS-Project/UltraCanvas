// Apps/UltraMail/ui/UltraMailWaitDialog.cpp
// Version: 0.3.0 - the title in a header with an icon tile, a busy bar under
//                  the text
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailWaitDialog.h"

#include "UltraMailTheme.h"

#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

using namespace UltraCanvas;

namespace UltraMail {

std::shared_ptr<UltraCanvasModalDialog> WaitDialog::Show(
        UltraCanvasWindowBase* parent, const std::string& title,
        const std::string& text, std::function<void()> onCancel,
        const std::string& icon) {
    DialogConfig config;
    config.title      = title;
    config.width      = 420;
    config.height     = 210;
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

    dialog->AddChild(Theme::MakeDialogHeader("waitHeader", icon, title, text, 64.0f));

    // Something is happening, even while nothing changes on screen. It turns
    // until the dialog closes, and goes with it.
    auto busy = CreateBusyIndicator("waitBusy", 0, 0, 0, 4, BusyIndicatorKind::Bar);
    {
        BusyIndicatorStyle style = busy->GetStyle();
        style.arcColor   = Theme::kAccent;
        style.trackColor = Theme::kAccentSoft;
        busy->SetStyle(style);
    }
    busy->SetMargin(0, 0, 0, 52);   // under the text, past the header's icon
    dialog->AddChild(busy);
    busy->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    busy->Start();

    auto spacer = CreateContainer("waitSpace", 0, 0, 0, 0);
    dialog->AddChild(spacer);
    spacer->layoutItem.SetFlexGrow(1);

    auto buttonRow = CreateContainer("waitButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancelBtn = CreateButton("waitCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::FitToLabel(cancelBtn, 90);
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
        std::move(onCancel), "globe");
}

} // namespace UltraMail
