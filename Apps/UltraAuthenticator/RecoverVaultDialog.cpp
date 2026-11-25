// Apps/UltraAuthenticator/RecoverVaultDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "RecoverVaultDialog.h"
#include "RecoveryCode.h"
#include "Theme.h"

#include "UltraLocalisation/UltraLocalisation.h"
#include "UltraCrypt/UltraCryptCore.h"

#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
#include "UltraAccountClient.h"
#endif

namespace UltraCanvas {
namespace Authenticator {

namespace {

void Wipe(std::string& s) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
}

std::shared_ptr<UltraCanvasLabel> Caption(UltraCanvasModalDialog& dialog, const std::string& id, long x, long y, long w,
                                          long h, const std::string& text) {
    auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, h, text);
    label->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    label->SetTextColor(Theme::kTextSecondary);
    label->SetWrap(TextWrap::WrapWord);
    dialog.AddChild(label);
    return label;
}

} // namespace

void RecoverVaultDialog::CreateRecoverVaultDialog() {
    DialogConfig cfg;
    cfg.title      = Tr("Forgot the master password");
    cfg.width      = kDialogWidth;
    cfg.height     = kDialogHeight;
    cfg.message    = "";
    cfg.buttons    = DialogButtons::NoButtons;
    cfg.position   = DialogPosition::CenterParent;
    cfg.resizable  = false;
    cfg.dialogType = DialogType::Custom;
    autoSizeHeight = false;
    CreateDialog(cfg);

    const long margin = Theme::kMargin;
    const long width  = kDialogWidth - 2 * margin;
    long y = margin;
    info_ = store_.LockedRecoveryInfo();

    auto title = std::make_shared<UltraCanvasLabel>("rv-title", margin, y, width, 24, Tr("Recover the vault"));
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    AddChild(title);
    y += 32;

    auto closeBtn = std::make_shared<UltraCanvasButton>("rv-close", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    closeBtn->SetText(Tr("Cancel"));
    Theme::StyleSecondary(closeBtn);
    closeBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(closeBtn);

    if (!info_.enabled) {
        Caption(*this, "rv-off", margin, y, width, 90,
                Tr("Recovery was not turned on for this vault. Without its master password it cannot be opened - by you or by "
                   "anyone else. If you have a backup file, its own passphrase opens it in a new vault."));
        return;
    }

    // ----- a code from the account -----
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    if (!info_.escrowId.empty()) {
        Caption(*this, "rv-account-intro", margin, y, width, 36,
                Tr("Your recovery key is kept with your UltraAccount. Choose where the code goes:"));
        y += 40;
        const auto keyInfo = UltraAccount::Client(info_.serviceUrl).GetRecoveryKeyInfo(info_.escrowId);
        if (!keyInfo) {
            Caption(*this, "rv-account-error", margin, y, width, 36, UltraAccount::Describe(keyInfo.error));
            y += 40;
        } else {
            long x = margin;
            for (const UltraAccount::Channel& channel : keyInfo.Value().channels) {
                auto button = std::make_shared<UltraCanvasButton>("rv-send-" + channel.channel, x, y, (width - 12) / 2, 32);
                button->SetText(channel.channel == "sms" ? TrF("Text {0}", channel.to) : TrF("Mail {0}", channel.to));
                Theme::StyleSecondary(button);
                const std::string name = channel.channel;
                button->onClick = [this, name]() { SendCode(name); };
                AddChild(button);
                x += (width - 12) / 2 + 12;
            }
            y += 40;
        }
        Caption(*this, "rv-code-lbl", margin, y, width, 18, Tr("The code you received"));
        y += 20;
        codeInput_ = CreateTextInput("rv-code", margin, y, 180, 28);
        Theme::StyleInput(codeInput_);
        AddChild(codeInput_);
        y += 40;
        Caption(*this, "rv-or", margin, y, width, 18, Tr("Or the recovery code you wrote down:"));
        y += 20;
    } else
#endif
    {
        Caption(*this, "rv-code-intro", margin, y, width, 36, Tr("Type the recovery code you wrote down when you turned recovery on:"));
        y += 40;
    }
    recoveryCodeInput_ = CreateTextInput("rv-recovery-code", margin, y, width, 28);
    recoveryCodeInput_->SetPlaceholder("ABCD-EFGH-…");
    Theme::StyleInput(recoveryCodeInput_);
    AddChild(recoveryCodeInput_);
    y += 44;

    Caption(*this, "rv-new-lbl", margin, y, width, 18, Tr("New master password"));
    y += 20;
    newPassword_ = CreatePasswordInput("rv-new", margin, y, width, 28);
    Theme::StyleInput(newPassword_);
    AddChild(newPassword_);
    y += 36;
    Caption(*this, "rv-confirm-lbl", margin, y, width, 18, Tr("New master password again"));
    y += 20;
    confirmPassword_ = CreatePasswordInput("rv-confirm", margin, y, width, 28);
    Theme::StyleInput(confirmPassword_);
    AddChild(confirmPassword_);
    y += 40;

    errorLabel_ = std::make_shared<UltraCanvasLabel>("rv-error", margin, y, width, 50, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto recoverBtn = std::make_shared<UltraCanvasButton>("rv-go", kDialogWidth - margin - 230, kDialogHeight - 54, 120, 32);
    recoverBtn->SetText(Tr("Recover"));
    Theme::StylePrimary(recoverBtn);
    recoverBtn->onClick = [this]() { Recover(); };
    AddChild(recoverBtn);
}

void RecoverVaultDialog::SetError(const std::string& text, bool isError) {
    if (!errorLabel_) return;
    errorLabel_->SetText(text);
    errorLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextSecondary);
}

void RecoverVaultDialog::SendCode(const std::string& channel) {
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    const auto sent = UltraAccount::Client(info_.serviceUrl).SendRecoveryCode(info_.escrowId, channel);
    if (!sent) {
        SetError(UltraAccount::Describe(sent.error));
        return;
    }
    SetError(TrF("A code went to {0}. It is good for ten minutes.", sent.Value()), false);
#else
    (void)channel;
#endif
}

void RecoverVaultDialog::Recover() {
    std::string next = newPassword_->GetText();
    std::string confirm = confirmPassword_->GetText();
    newPassword_->SetText("");
    confirmPassword_->SetText("");
    if (next.empty()) {
        SetError(Tr("Choose a new master password."));
        return;
    }
    if (next != confirm) {
        Wipe(next);
        Wipe(confirm);
        SetError(Tr("The two new passwords differ."));
        return;
    }
    Wipe(confirm);
    UltraCryptSecureBuffer newPw = UltraCryptSecureBuffer::AdoptString(next);

    UltraCryptSecureBuffer share;
    std::string typedCode = recoveryCodeInput_ ? recoveryCodeInput_->GetText() : std::string();
    if (!typedCode.empty()) {
        const bool parsed = ParseRecoveryCode(typedCode, share);
        Wipe(typedCode);
        if (!parsed) {
            SetError(Tr("That is not a recovery code: 52 letters and digits in groups of four."));
            return;
        }
    } else {
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
        const std::string code = codeInput_ ? codeInput_->GetText() : std::string();
        if (code.empty()) {
            SetError(Tr("Enter the code you received, or your recovery code."));
            return;
        }
        auto released = UltraAccount::Client(info_.serviceUrl).ReleaseRecoveryKey(info_.escrowId, code);
        if (!released) {
            SetError(UltraAccount::Describe(released.error));
            return;
        }
        std::string hex = *released.value;
        const bool parsed = ShareFromHex(hex, share);
        Wipe(hex);
        Wipe(*released.value);
        if (!parsed) {
            SetError(Tr("The account service answered with something that is not a recovery key."));
            return;
        }
#else
        SetError(Tr("Enter your recovery code."));
        return;
#endif
    }

    StoreResult recovered = store_.Recover(share, newPw, now_ ? now_() : 0);
    if (!recovered) {
        SetError(recovered.code == StoreResultCode::AuthenticationFailed
                     ? Tr("That recovery key does not open this vault.")
                     : recovered.message);
        return;
    }
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    // The key has been out: the account's copy goes too, when this machine
    // can ask for that. Otherwise the UltraAccount app lists it to remove.
    const UltraAccount::Session session = UltraAccount::Session::Load();
    if (!info_.escrowId.empty() && session.SignedIn()) {
        UltraAccount::Client(session.url).DeleteRecoveryKey(session.token, info_.escrowId);
    }
#endif
    CloseDialog(DialogResult::OK);
    if (onRecovered) onRecovered();
}

} // namespace Authenticator
} // namespace UltraCanvas
