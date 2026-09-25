// Apps/UltraAuthenticator/NewVaultDialog.cpp
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "NewVaultDialog.h"
#include "BrandHeader.h"
#include "PasswordAdvice.h"
#include "Theme.h"

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

void NewVaultDialog::CreateNewVaultDialog() {
    DialogConfig cfg;
    cfg.title         = "Set up UltraAuthenticator";
    cfg.width         = kDialogWidth;
    cfg.height        = kDialogHeight;
    cfg.message       = "";
    cfg.buttons       = DialogButtons::NoButtons;
    cfg.position      = DialogPosition::Center;   // there is no parent window yet
    cfg.resizable     = false;
    cfg.closable      = false;
    cfg.closeOnEscape = false;
    cfg.dialogType    = DialogType::Custom;   // see AddAccountDialog for why
    autoSizeHeight = false;
    CreateDialog(cfg);

    // The window manager's close button is not a third way out.
    onWindowClosing = [this]() { return created_ || quitting_; };

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    long y = AddBrandHeader(*this, "nv", kDialogWidth, margin);

    auto title = std::make_shared<UltraCanvasLabel>(
        "nv-title", margin, y, fieldWidth, 24, "Choose a master password");
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    AddChild(title);
    y += 30;

    // Small type: this is the fine print under the title, not a second title.
    // Three to four lines at this width, and the box must cover all of them: a
    // label centres its text in the box it is given, so a line the box has no
    // room for spills half above (into the title) and half below (into the
    // caption). Sized for four; the first attempt at three lost a line each way.
    auto intro = std::make_shared<UltraCanvasLabel>(
        "nv-intro", margin, y, fieldWidth, 60,
        "It protects every account in this app and cannot be recovered: "
        "forget it and the accounts are gone. It is typed twice, since a "
        "typo here would become the password.");
    intro->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    intro->SetTextColor(Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    AddChild(intro);
    y += 68;

    auto caption = std::make_shared<UltraCanvasLabel>(
        "nv-pw-lbl", margin, y, fieldWidth, 18, "Master password");
    caption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    caption->SetTextColor(Theme::kTextSecondary);
    AddChild(caption);
    y += 20;

    passwordInput_ = CreatePasswordInput("nv-pw", margin, y, fieldWidth, 28);
    AddChild(passwordInput_);
    y += 34;

    // The meter follows the first field as it is typed. It is advice, not a
    // gate: a short password is the user's decision, and it is theirs to make
    // with the bar in front of them rather than a rule in their way.
    strengthMeter_ = CreateBarStrengthMeter("nv-strength", margin, y, fieldWidth, 18);
    strengthMeter_->LinkToInput(passwordInput_.get());
    AddChild(strengthMeter_);
    y += 24;

    // Why the bar is short, rule by rule. Also advice only: Create never
    // checks these (PasswordAdvice.h).
    y = AddPasswordAdvice(*this, "nv", margin, y, fieldWidth, passwordInput_.get());

    auto confirmCaption = std::make_shared<UltraCanvasLabel>(
        "nv-confirm-lbl", margin, y, fieldWidth, 18, "Confirm master password");
    confirmCaption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    confirmCaption->SetTextColor(Theme::kTextSecondary);
    AddChild(confirmCaption);
    y += 20;

    confirmInput_ = CreatePasswordInput("nv-confirm", margin, y, fieldWidth, 28);
    confirmInput_->onEnterPressed = [this](const std::string&) {
        Accept();
        return true;
    };
    AddChild(confirmInput_);
    y += 38;

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "nv-error", margin, y, fieldWidth, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto createBtn = std::make_shared<UltraCanvasButton>(
        "nv-create", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    createBtn->SetText("Create");
    createBtn->onClick = [this]() { Accept(); };
    AddChild(createBtn);

    auto quitBtn = std::make_shared<UltraCanvasButton>(
        "nv-quit", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    quitBtn->SetText("Quit");
    quitBtn->onClick = [this]() {
        quitting_ = true;
        if (passwordInput_) passwordInput_->SetText("");
        if (confirmInput_)  confirmInput_->SetText("");
        CloseDialog(DialogResult::Cancel);
        if (onQuit) onQuit();
    };
    AddChild(quitBtn);
}

void NewVaultDialog::ShowModal(UltraCanvasWindowBase* parent) {
    UltraCanvasModalDialog::ShowModal(parent);
    if (passwordInput_) SetFocusedElement(passwordInput_.get());
}

void NewVaultDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void NewVaultDialog::Accept() {
    if (created_ || quitting_ || !passwordInput_ || !confirmInput_) return;

    std::string password = passwordInput_->GetText();
    std::string confirm  = confirmInput_->GetText();

    // Local checks first, so a mismatch costs no Argon2id work.
    std::string error;
    if (password.empty()) {
        error = "A master password is required.";
    } else if (password != confirm) {
        error = "The two passwords do not match.";
    }

    if (error.empty() && onAccept) error = onAccept(password);

    // A mismatch leaves the first field for correction of the second; any
    // other outcome, including success, wipes both.
    const bool mismatch = (error == "The two passwords do not match.");
    WipeAndClear(confirm, confirmInput_);
    if (!mismatch) WipeAndClear(password, passwordInput_);
    else if (!password.empty()) UltraCrypt_SecureZero(&password[0], password.size());

    if (!error.empty()) {
        SetError(error);
        if (mismatch && confirmInput_) SetFocusedElement(confirmInput_.get());
        return;
    }

    created_ = true;
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
