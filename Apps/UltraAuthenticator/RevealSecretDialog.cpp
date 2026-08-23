// Apps/UltraAuthenticator/RevealSecretDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "RevealSecretDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"
#include "UltraCrypt/UltraCryptCore.h"

namespace UltraCanvas {
namespace Authenticator {

namespace {

// Pulls the Base32 secret out of an otpauth:// URI for separate display. Most
// services ask for this run of characters, not the whole URI, when enrolling
// by hand.
std::string ExtractSecret(const std::string& uri) {
    const std::string needle = "secret=";
    size_t at = uri.find(needle);
    if (at == std::string::npos) return std::string();
    at += needle.size();
    size_t end = uri.find('&', at);
    if (end == std::string::npos) end = uri.size();
    return uri.substr(at, end - at);
}

void WipeString(std::string& s) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
}

} // namespace

RevealSecretDialog::~RevealSecretDialog() {
    WipeShownSecret();
}

void RevealSecretDialog::CreateRevealSecretDialog(const std::string& displayName) {
    DialogConfig cfg;
    cfg.title      = "Show setup key";
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

    auto title = std::make_shared<UltraCanvasLabel>(
        "rev-title", margin, y, fieldWidth, 22, displayName);
    title->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    AddChild(title);
    y += 28;

    promptLabel_ = std::make_shared<UltraCanvasLabel>(
        "rev-prompt", margin, y, fieldWidth, 50,
        "The setup key is the second factor itself: anyone who copies it can "
        "generate this account's codes forever. Enter your master password to "
        "show it.");
    promptLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    promptLabel_->SetTextColor(Theme::kTextSecondary);
    promptLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(promptLabel_);
    y += 58;

    auto caption = std::make_shared<UltraCanvasLabel>(
        "rev-pw-lbl", margin, y, fieldWidth, 18, "Master password");
    caption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    caption->SetTextColor(Theme::kTextSecondary);
    AddChild(caption);
    y += 20;

    passwordInput_ = CreatePasswordInput("rev-pw", margin, y, fieldWidth, 28);
    AddChild(passwordInput_);
    y += 38;

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "rev-error", margin, y, fieldWidth, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto showBtn = std::make_shared<UltraCanvasButton>(
        "rev-ok", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    showBtn->SetText("Show");
    showBtn->onClick = [this]() { Attempt(); };
    AddChild(showBtn);

    auto closeBtn = std::make_shared<UltraCanvasButton>(
        "rev-close", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    closeBtn->SetText("Close");
    closeBtn->onClick = [this]() {
        WipeShownSecret();
        CloseDialog(DialogResult::Cancel);
    };
    AddChild(closeBtn);
}

void RevealSecretDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void RevealSecretDialog::Attempt() {
    if (revealed_ || !passwordInput_ || !onReveal) return;

    std::string password = passwordInput_->GetText();
    std::string uri;
    std::string error = onReveal(password, uri);

    WipeString(password);
    passwordInput_->SetText("");

    if (!error.empty()) {
        WipeString(uri);
        SetError(error);
        return;
    }

    SetError("");
    ShowSecret(uri);
    WipeString(uri);
}

void RevealSecretDialog::ShowSecret(const std::string& uri) {
    revealed_ = true;

    // The password step is finished; replace its instructions rather than
    // leaving a stale prompt above the secret.
    if (promptLabel_) {
        promptLabel_->SetText(
            "Enter this on the other device, or scan it there if you can "
            "generate a QR code from the URI below.");
    }
    if (passwordInput_) passwordInput_->SetText("");

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    long y = margin + 28 + 58;

    const std::string secret = ExtractSecret(uri);

    secretCaption_ = std::make_shared<UltraCanvasLabel>(
        "rev-secret-lbl", margin, y, fieldWidth, 18, "Setup key");
    secretCaption_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    secretCaption_->SetTextColor(Theme::kTextSecondary);
    AddChild(secretCaption_);
    y += 20;

    // A text input rather than a label, because the point of showing this is
    // to copy it, and a label cannot be selected.
    secretField_ = std::make_shared<UltraCanvasTextInput>(
        "rev-secret", margin, y, fieldWidth, 30);
    secretField_->SetText(secret);
    AddChild(secretField_);
    y += 42;

    uriCaption_ = std::make_shared<UltraCanvasLabel>(
        "rev-uri-lbl", margin, y, fieldWidth, 18, "Full otpauth:// URI");
    uriCaption_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    uriCaption_->SetTextColor(Theme::kTextSecondary);
    AddChild(uriCaption_);
    y += 20;

    uriField_ = std::make_shared<UltraCanvasTextInput>(
        "rev-uri", margin, y, fieldWidth, 30);
    uriField_->SetText(uri);
    AddChild(uriField_);
    y += 40;

    handlingWarning_ = std::make_shared<UltraCanvasLabel>(
        "rev-warn", margin, y, fieldWidth, 34,
        "Do not photograph this or paste it into a chat. Close this window "
        "once the other device is enrolled.");
    handlingWarning_->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    handlingWarning_->SetTextColor(Theme::kDanger);
    handlingWarning_->SetWrap(TextWrap::WrapWord);
    AddChild(handlingWarning_);
}

void RevealSecretDialog::WipeShownSecret() {
    // Overwrite before clearing: SetText("") alone would drop the reference
    // and leave the seed in whatever buffer the widget had grown.
    if (secretField_) {
        std::string blanked(secretField_->GetText().size(), 'x');
        secretField_->SetText(blanked);
        secretField_->SetText("");
    }
    if (uriField_) {
        std::string blanked(uriField_->GetText().size(), 'x');
        uriField_->SetText(blanked);
        uriField_->SetText("");
    }
}

} // namespace Authenticator
} // namespace UltraCanvas
