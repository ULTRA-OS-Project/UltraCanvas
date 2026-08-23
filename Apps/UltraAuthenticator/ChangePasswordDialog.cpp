// Apps/UltraAuthenticator/ChangePasswordDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ChangePasswordDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"
#include "UltraCrypt/UltraCryptCore.h"

namespace UltraCanvas {
namespace Authenticator {

namespace {

void WipeAndClear(std::string& s,
                  const std::shared_ptr<UltraCanvasTextInput>& input) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
    if (input) input->SetText("");
}

} // namespace

void ChangePasswordDialog::CreateChangePasswordDialog() {
    DialogConfig cfg;
    cfg.title      = "Change master password";
    cfg.width      = kDialogWidth;
    cfg.height     = kDialogHeight;
    cfg.message    = "";
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = false;
    cfg.dialogType = DialogType::Custom;   // see AddAccountDialog for why
    autoSizeHeight = false;
    CreateDialog(cfg);

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    long y = margin;

    auto intro = std::make_shared<UltraCanvasLabel>(
        "cpw-intro", margin, y, fieldWidth, 34,
        "This re-encrypts the vault with a key derived from the new password. "
        "Copies of the old file stay readable with the old password.");
    intro->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    intro->SetTextColor(Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    AddChild(intro);
    y += 46;

    struct Field {
        const char* id;
        const char* caption;
        std::shared_ptr<UltraCanvasTextInput>* target;
    };
    const Field fields[] = {
        {"cpw-current", "Current password", &currentInput_},
        {"cpw-next",    "New password",     &nextInput_},
        {"cpw-confirm", "Confirm new password", &confirmInput_},
    };

    for (const Field& field : fields) {
        auto caption = std::make_shared<UltraCanvasLabel>(
            std::string(field.id) + "-lbl", margin, y, fieldWidth, 18,
            field.caption);
        caption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
        caption->SetTextColor(Theme::kTextSecondary);
        AddChild(caption);
        y += 20;

        *field.target = CreatePasswordInput(field.id, margin, y, fieldWidth, 28);
        AddChild(*field.target);
        y += 38;
    }

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "cpw-error", margin, y, fieldWidth, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto okBtn = std::make_shared<UltraCanvasButton>(
        "cpw-ok", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    okBtn->SetText("Change");
    okBtn->onClick = [this]() { Accept(); };
    AddChild(okBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "cpw-cancel", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(cancelBtn);
}

void ChangePasswordDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void ChangePasswordDialog::Accept() {
    if (!currentInput_ || !nextInput_ || !confirmInput_) return;

    std::string current = currentInput_->GetText();
    std::string next    = nextInput_->GetText();
    std::string confirm = confirmInput_->GetText();

    // Check what can be checked locally first, so an obvious typo does not
    // cost the ~100 ms Argon2id verification behind onAccept.
    std::string localError;
    if (next.empty()) {
        localError = "The new password cannot be empty.";
    } else if (next != confirm) {
        localError = "The new passwords do not match.";
    } else if (next == current) {
        localError = "The new password is the same as the current one.";
    }

    std::string error = localError;
    if (error.empty() && onAccept) error = onAccept(current, next);

    // Wipe unconditionally: these are the keys to every second factor in the
    // vault, and a failed attempt is no reason to leave them in a widget.
    WipeAndClear(current, currentInput_);
    WipeAndClear(next, nextInput_);
    WipeAndClear(confirm, confirmInput_);

    if (!error.empty()) {
        SetError(error);
        return;
    }
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
