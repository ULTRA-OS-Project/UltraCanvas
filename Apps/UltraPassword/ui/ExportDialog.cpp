// Apps/UltraPassword/ui/ExportDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ExportDialog.h"

#include "Theme.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasModalDialog.h"

#include <memory>

using namespace UltraCanvas;

namespace UltraPassword {

void ExportDialog::Show(UltraCanvasWindowBase* parent,
                        std::function<void(ExportChoice)> onChoice) {
    DialogConfig config;
    config.title      = "Export passwords";
    config.width      = 480;
    config.height     = 330;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(20);
    dialog->SetBackgroundColor(Theme::kCardBackground);

    // The choice is remembered here and read when the dialog closes.
    auto choice = std::make_shared<ExportChoice>(ExportChoice::VaultCopy);

    auto addOption = [&dialog, dlg, choice](const std::string& id, const std::string& label,
                                            const std::string& text, ExportChoice value,
                                            bool primary, bool danger) {
        auto row = CreateContainer(id + "Row", 0, 0, 0, 48);
        row->layout.SetFlexRow()
                   .SetFlexGap(Theme::kGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto button = Theme::MakeButton(id, label, 170, primary);
        if (danger) Theme::StyleDanger(button);
        button->onClick = [dlg, choice, value]() {
            *choice = value;
            dlg->CloseDialog(DialogResult::OK);
        };
        row->AddChild(button);
        auto line = Theme::MakeLine(id + "Text", text, 44, Theme::kSizeSmall,
                                    danger ? Theme::kDanger : Theme::kTextSecondary);
        line->SetWrap(TextWrap::WrapWord);
        line->layoutItem.SetFlexGrow(1);
        row->AddChild(line);
        dialog->AddChild(row);
    };

    auto intro = Theme::MakeLine("exIntro",
        "The vault file is the export: a copy opens in UltraPassword on any "
        "computer with its password.", 30, Theme::kSizeBody, Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(intro);

    addOption("exCopy", "Vault file…",
              "Encrypted copy (." + std::string("upwvault") + "), opens with your master password.",
              ExportChoice::VaultCopy, /*primary=*/true, false);
    addOption("exOwn", "Vault file, own password…",
              "Encrypted copy under a separate password - for a backup kept apart or "
              "to hand to someone.",
              ExportChoice::VaultWithPassword, false, false);
    addOption("exCsv", "Plain CSV…",
              "NOT encrypted: every password readable by anyone with the file. For "
              "importing into a browser or another password manager.",
              ExportChoice::PlainCsv, false, /*danger=*/true);

    auto buttonRow = CreateContainer("exButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto cancel = Theme::MakeButton("exCancel", "Cancel", 90);
    cancel->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancel);
    dialog->AddStretchSpacer(1);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [choice, onChoice](DialogResult result) {
            if (result == DialogResult::OK && onChoice) onChoice(*choice);
        },
        parent);
}

} // namespace UltraPassword
