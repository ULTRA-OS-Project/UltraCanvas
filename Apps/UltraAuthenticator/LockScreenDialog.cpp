// Apps/UltraAuthenticator/LockScreenDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "LockScreenDialog.h"
#include "Theme.h"

#include "UltraCrypt/UltraCryptCore.h"

namespace UltraCanvas {
namespace Authenticator {

namespace {

void WipeString(std::string& s) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
}

} // namespace

void LockScreenDialog::CreateLockScreenDialog(const std::string& reason) {
    DialogConfig cfg;
    cfg.title         = "UltraAuthenticator — locked";
    cfg.width         = kDialogWidth;
    cfg.height        = kDialogHeight;
    cfg.message       = "";
    cfg.buttons       = DialogButtons::NoButtons;
    cfg.position      = DialogPosition::CenterParent;
    cfg.resizable     = false;
    cfg.closable      = false;
    cfg.closeOnEscape = false;   // Escape is not a password
    cfg.dialogType    = DialogType::Custom;   // see AddAccountDialog for why
    autoSizeHeight = false;
    CreateDialog(cfg);

    // The window manager's close button lands here. Refuse it unless the
    // dialog is closing itself; otherwise "close the lock screen" would be a
    // way past the lock.
    onWindowClosing = [this]() { return unlocked_ || quitting_; };

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    long y = margin;

    auto title = std::make_shared<UltraCanvasLabel>(
        "lock-title", margin, y, fieldWidth, 24, "Locked");
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    AddChild(title);
    y += 30;

    reasonLabel_ = std::make_shared<UltraCanvasLabel>(
        "lock-reason", margin, y, fieldWidth, 36,
        reason + " Enter your master password to show the codes again.");
    reasonLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    reasonLabel_->SetTextColor(Theme::kTextSecondary);
    reasonLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(reasonLabel_);
    y += 44;

    auto caption = std::make_shared<UltraCanvasLabel>(
        "lock-pw-lbl", margin, y, fieldWidth, 18, "Master password");
    caption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    caption->SetTextColor(Theme::kTextSecondary);
    AddChild(caption);
    y += 20;

    passwordInput_ = CreatePasswordInput("lock-pw", margin, y, fieldWidth, 28);
    passwordInput_->onEnterPressed = [this](const std::string&) {
        Attempt();
        return true;
    };
    AddChild(passwordInput_);
    y += 38;

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "lock-error", margin, y, fieldWidth, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    unlockButton_ = std::make_shared<UltraCanvasButton>(
        "lock-unlock", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    unlockButton_->SetText("Unlock");
    unlockButton_->onClick = [this]() { Attempt(); };
    AddChild(unlockButton_);

    auto quitBtn = std::make_shared<UltraCanvasButton>(
        "lock-quit", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    quitBtn->SetText("Quit");
    quitBtn->onClick = [this]() {
        quitting_ = true;
        if (passwordInput_) passwordInput_->SetText("");
        CloseDialog(DialogResult::Cancel);
        if (onQuit) onQuit();
    };
    AddChild(quitBtn);

    RefreshWaitState();
}

void LockScreenDialog::ShowModal(UltraCanvasWindowBase* parent) {
    UltraCanvasModalDialog::ShowModal(parent);
    if (passwordInput_) SetFocusedElement(passwordInput_.get());
}

void LockScreenDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void LockScreenDialog::RefreshWaitState() {
    const uint32_t wait = onQueryWait ? onQueryWait() : 0;
    if (unlockButton_) unlockButton_->SetDisabled(wait > 0);
    if (wait != lastWaitShown_) {
        // Only rewrite the label when the number changes, so a typed error
        // message is not clobbered every second while no delay is running.
        if (wait > 0) {
            SetError("Too many attempts. Try again in " + std::to_string(wait) +
                     (wait == 1 ? " second." : " seconds."));
        } else if (lastWaitShown_ > 0) {
            SetError("");
        }
        lastWaitShown_ = wait;
    }
}

void LockScreenDialog::Tick() {
    if (unlocked_ || quitting_) return;
    RefreshWaitState();
}

void LockScreenDialog::Attempt() {
    if (unlocked_ || quitting_ || !passwordInput_ || !onUnlock) return;

    std::string password = passwordInput_->GetText();
    passwordInput_->SetText("");

    if (password.empty()) {
        WipeString(password);
        SetError("Enter your master password.");
        return;
    }

    std::string error = onUnlock(password);
    WipeString(password);

    if (!error.empty()) {
        SetError(error);
        // A failure may have started a delay; grey the button right away
        // rather than on the next tick, so the state on screen matches. When
        // a delay is running its countdown replaces the error text.
        RefreshWaitState();
        return;
    }

    unlocked_ = true;
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
