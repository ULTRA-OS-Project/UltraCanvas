// Apps/UltraMail/ui/UltraMailOAuthCodeDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailOAuthCodeDialog.h"

#include "UltraMailAlerts.h"
#include "UltraMailTheme.h"

#include "UltraCanvasModalDialog.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"

#include <cctype>
#include <memory>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
// Trim surrounding whitespace: a pasted code often carries a leading/trailing
// space or newline from the browser's copy.
std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
} // namespace

void OAuthCodeDialog::Show(UltraCanvasWindowBase* parent,
                           const std::string& providerName,
                           const std::string& email,
                           std::function<void(const std::string&)> onSubmit) {
    DialogConfig config;
    config.title      = "Sign in with " + providerName;
    config.width      = 440;
    config.height     = 260;
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

    auto content = CreateContainer("oauthCodeForm", 0, 0, 0, 0);
    content->layout.SetFlexColumn()
                   .SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto intro = CreateLabel("oauthCodeIntro", 0, 0, 400, 90,
        "Your browser has opened " + providerName + "'s sign-in page. Sign in as "
        + email + " and allow UltraMail to read and send your mail. " + providerName
        + " will then show an authorization code — copy it and paste it here.");
    intro->SetWrap(TextWrap::WrapWord);
    intro->SetFontSize(Theme::kSizeBody);
    intro->SetTextColor(Theme::kTextSecondary);
    content->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto row = CreateContainer("oauthCodeRow", 0, 0, 0, Theme::kControlHeight);
    row->layout.SetFlexRow()
               .SetFlexGap(Theme::kInnerGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto label = Theme::MakeLine("oauthCodeLbl", "Code", Theme::kControlHeight,
                                 Theme::kSizeBody, Theme::kTextSecondary);
    label->SetElementSize(Size2Df(60.0f, Theme::kControlHeight));
    row->AddChild(label);
    auto code = CreateTextInput("oauthCodeField", 0, 0, 0, Theme::kControlHeight);
    code->SetPlaceholder("Paste the code from " + providerName);
    Theme::StyleInput(code);
    row->AddChild(code);
    code->layoutItem.SetFlexGrow(1);
    content->AddChild(row);
    row->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    dialog->AddChild(content);
    content->layoutItem.SetFlexGrow(1);

    // ===== BUTTON ROW =====
    auto buttonRow = CreateContainer("oauthCodeButtons", 0, 0, 0, Theme::kToolbarHeight);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(Theme::kInnerGap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);

    auto cancelBtn = CreateButton("oauthCodeCancel", 0, 0, 90, Theme::kControlHeight, "Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::Cancel); };
    buttonRow->AddChild(cancelBtn);

    auto okBtn = CreateButton("oauthCodeOk", 0, 0, 140, Theme::kControlHeight, "Continue");
    Theme::StylePrimary(okBtn);
    okBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(okBtn);

    dialog->AddChild(buttonRow);

    // Enter in the code field confirms the dialog.
    code->onEnterPressed = [dlg](const std::string&) {
        dlg->CloseDialog(DialogResult::OK);
        return true;
    };

    UltraCanvasDialogManager::ShowDialog(
        dialog,
        [code, onSubmit, parent, providerName](DialogResult result) {
            if (result != DialogResult::OK) return;   // cancelled: no sign-in
            const std::string entered = Trim(code->GetText());
            if (entered.empty()) {
                AlertWarning(parent, "No code was entered.",
                             "Paste the code " + providerName + " showed after sign-in, "
                             "or add the account again to retry.");
                return;
            }
            if (onSubmit) onSubmit(entered);
        },
        parent);
}

} // namespace UltraMail
