// Apps/UltraAuthenticator/RecoveryDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "RecoveryDialog.h"
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

RecoveryDialog::~RecoveryDialog() {
    ClearCode();
}

void RecoveryDialog::CreateRecoveryDialog() {
    DialogConfig cfg;
    cfg.title      = Tr("Recovery from a forgotten password");
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

    auto title = std::make_shared<UltraCanvasLabel>("rec-title", margin, y, width, 24, Tr("Recovery"));
    title->SetFont(Theme::kUiFont, Theme::kSizeTitle, FontWeight::Bold);
    title->SetTextColor(Theme::kTextPrimary);
    AddChild(title);
    y += 30;

    stateLabel_ = std::make_shared<UltraCanvasLabel>("rec-state", margin, y, width, 36, "");
    stateLabel_->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    stateLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(stateLabel_);
    y += 40;

    Caption(*this, "rec-intro", margin, y, width, 80,
            Tr("A code to your UltraAccount email - or, with UltraAccount Plus, your phone - or the recovery code shown "
               "below opens the vault once, to set a new password. Neither opens anything without this vault file."));
    y += 86;

    useAccount_ = std::make_shared<UltraCanvasCheckbox>("rec-use-account", margin, y, width, 22,
                                                        Tr("Keep the recovery key with my UltraAccount"));
    AddChild(useAccount_);
    y += 28;
    accountLabel_ = Caption(*this, "rec-account", margin, y, width, 36, "");
    y += 40;

    const long half = (width - 12) / 2;
    emailCaption_ = Caption(*this, "rec-email-lbl", margin, y, half, 18, Tr("UltraAccount email"));
    passwordCaption_ = Caption(*this, "rec-pw-lbl", margin + half + 12, y, half, 18, Tr("UltraAccount password"));
    y += 20;
    accountEmail_ = CreateTextInput("rec-email", margin, y, half, 28);
    Theme::StyleInput(accountEmail_);
    AddChild(accountEmail_);
    accountPassword_ = CreatePasswordInput("rec-account-pw", margin + half + 12, y, half, 28);
    Theme::StyleInput(accountPassword_);
    AddChild(accountPassword_);
    y += 36;
    totpCaption_ = Caption(*this, "rec-totp-lbl", margin, y, width, 18, Tr("Its two-factor code, if two-factor sign-in is on"));
    y += 20;
    accountTotp_ = CreateTextInput("rec-totp", margin, y, 160, 28);
    Theme::StyleInput(accountTotp_);
    AddChild(accountTotp_);
    y += 40;

    Caption(*this, "rec-master-lbl", margin, y, width, 18, Tr("Master password of this vault"));
    y += 20;
    masterInput_ = CreatePasswordInput("rec-master", margin, y, width, 28);
    Theme::StyleInput(masterInput_);
    AddChild(masterInput_);
    y += 40;

    codeCaption_ = Caption(*this, "rec-code-lbl", margin, y, width, 56, "");
    y += 60;
    codeLabel_ = std::make_shared<UltraCanvasLabel>("rec-code", margin, y, width, 44, "");
    codeLabel_->SetFont(Theme::kCodeFont, Theme::kSizeBody, FontWeight::Bold);
    codeLabel_->SetTextColor(Theme::kCodeText);
    codeLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(codeLabel_);
    y += 46;

    errorLabel_ = std::make_shared<UltraCanvasLabel>("rec-error", margin, y, width, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    const long buttonY = kDialogHeight - 54;
    onButton_ = std::make_shared<UltraCanvasButton>("rec-on", margin, buttonY, 130, 32);
    onButton_->SetText(Tr("Turn on"));
    Theme::StylePrimary(onButton_);
    onButton_->onClick = [this]() { TurnOn(); };
    AddChild(onButton_);

    offButton_ = std::make_shared<UltraCanvasButton>("rec-off", margin + 142, buttonY, 130, 32);
    offButton_->SetText(Tr("Turn off"));
    Theme::StyleSecondary(offButton_);
    offButton_->onClick = [this]() { TurnOff(); };
    AddChild(offButton_);

    auto closeBtn = std::make_shared<UltraCanvasButton>("rec-close", kDialogWidth - margin - 100, buttonY, 100, 32);
    closeBtn->SetText(Tr("Close"));
    Theme::StyleSecondary(closeBtn);
    closeBtn->onClick = [this]() {
        ClearCode();
        CloseDialog(DialogResult::OK);
    };
    AddChild(closeBtn);

    Refresh();
}

void RecoveryDialog::Refresh() {
    const VaultRecoveryInfo info = store_.GetRecoveryInfo();
    if (!info.enabled) {
        stateLabel_->SetText(Tr("Recovery is off: without the master password, this vault cannot be opened."));
        stateLabel_->SetTextColor(Theme::kDanger);
    } else if (info.escrowId.empty()) {
        stateLabel_->SetText(Tr("Recovery is on, with the recovery code you wrote down."));
        stateLabel_->SetTextColor(Theme::kCountdownCalm);
    } else {
        stateLabel_->SetText(Tr("Recovery is on: a code to your UltraAccount, or the recovery code you wrote down."));
        stateLabel_->SetTextColor(Theme::kCountdownCalm);
    }
    onButton_->SetText(info.enabled ? Tr("Renew") : Tr("Turn on"));
    offButton_->SetDisabled(!info.enabled);

#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    const UltraAccount::Session session = UltraAccount::Session::Load();
    const bool signedIn = session.SignedIn();
    accountLabel_->SetText(signedIn ? TrF("Signed in to UltraAccount as {0}.", session.email)
                                    : Tr("Not signed in on this machine: sign in below, or with the UltraAccount app."));
    for (UltraCanvasUIElement* element : {static_cast<UltraCanvasUIElement*>(accountEmail_.get()), static_cast<UltraCanvasUIElement*>(accountPassword_.get()),
                                          static_cast<UltraCanvasUIElement*>(accountTotp_.get()), static_cast<UltraCanvasUIElement*>(emailCaption_.get()),
                                          static_cast<UltraCanvasUIElement*>(passwordCaption_.get()), static_cast<UltraCanvasUIElement*>(totpCaption_.get())}) {
        element->SetVisible(!signedIn);
    }
    if (signedIn) accountEmail_->SetText(session.email);
    useAccount_->SetChecked(true);
#else
    accountLabel_->SetText(Tr("This build has no UltraAccount: recovery works with the recovery code alone."));
    useAccount_->SetChecked(false);
    useAccount_->SetDisabled(true);
    for (UltraCanvasUIElement* element : {static_cast<UltraCanvasUIElement*>(accountEmail_.get()), static_cast<UltraCanvasUIElement*>(accountPassword_.get()),
                                          static_cast<UltraCanvasUIElement*>(accountTotp_.get()), static_cast<UltraCanvasUIElement*>(emailCaption_.get()),
                                          static_cast<UltraCanvasUIElement*>(passwordCaption_.get()), static_cast<UltraCanvasUIElement*>(totpCaption_.get())}) {
        element->SetVisible(false);
    }
#endif
}

void RecoveryDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void RecoveryDialog::ClearCode() {
    if (!codeLabel_) return;
    std::string shown = codeLabel_->GetText();
    Wipe(shown);
    codeLabel_->SetText("");
    if (codeCaption_) codeCaption_->SetText("");
}

void RecoveryDialog::TurnOn() {
    SetError("");
    ClearCode();
    std::string master = masterInput_->GetText();
    masterInput_->SetText("");
    if (master.empty()) {
        SetError(Tr("Enter the master password of this vault."));
        return;
    }
    UltraCryptSecureBuffer masterPw = UltraCryptSecureBuffer::AdoptString(master);

    UltraCryptSecureBuffer share;
    if (!UltraCrypt_RandomSecureBuffer(kRecoveryShareBytes, share)) {
        SetError(Tr("Could not draw a recovery key."));
        return;
    }

    std::string escrowId;
    std::string serviceUrl;
    const std::string previousEscrow = store_.GetRecoveryInfo().escrowId;
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    UltraAccount::Session session;
    if (useAccount_->IsChecked()) {
        // Check the master password before anything leaves the machine.
        StoreResult verified = store_.VerifyPassword(masterPw);
        if (!verified) {
            SetError(verified.message);
            return;
        }
        session = UltraAccount::Session::Load();
        UltraAccount::Client client(session.SignedIn() ? session.url : UltraAccount::kDefaultServiceUrl);
        if (!session.SignedIn()) {
            std::string password = accountPassword_->GetText();
            accountPassword_->SetText("");
            const auto signedIn = client.SignIn(accountEmail_->GetText(), password,
                                                UltraAccount::DeviceInfo{UltraAccount::DefaultDeviceName(), UltraAccount::CurrentOs(),
                                                                         "UltraAuthenticator " ULTRAAUTHENTICATOR_VERSION},
                                                accountTotp_->GetText());
            Wipe(password);
            if (!signedIn) {
                SetError(UltraAccount::Describe(signedIn.error));
                return;
            }
            session = signedIn.Value();
            session.Save();
            accountTotp_->SetText("");
        }
        std::string hex = ShareToHex(share);
        const auto stored = client.StoreRecoveryKey(session.token, "UltraAuthenticator", UltraAccount::DefaultDeviceName(), hex);
        Wipe(hex);
        if (!stored) {
            SetError(UltraAccount::Describe(stored.error));
            return;
        }
        escrowId = stored.Value().id;
        serviceUrl = session.url;
    }
#endif

    StoreResult enabled = store_.EnableRecovery(masterPw, share, escrowId, serviceUrl);
    if (!enabled) {
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
        // The key the account now holds opens nothing: take it back.
        if (!escrowId.empty()) UltraAccount::Client(serviceUrl).DeleteRecoveryKey(session.token, escrowId);
#endif
        SetError(enabled.message);
        return;
    }
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    // A renewal: the account's copy of the previous key is spent.
    if (!previousEscrow.empty() && previousEscrow != escrowId && session.SignedIn()) {
        UltraAccount::Client(session.url).DeleteRecoveryKey(session.token, previousEscrow);
    }
#else
    (void)previousEscrow;
#endif

    codeCaption_->SetText(Tr("Your recovery code. Write it down and keep it away from this computer: it is shown only now, "
                             "and with this vault file it opens your second factors."));
    codeLabel_->SetText(FormatRecoveryCode(share));
    Refresh();
    if (onChanged) onChanged(escrowId.empty() ? Tr("Recovery is on, with the recovery code.")
                                              : Tr("Recovery is on: the key is kept with your UltraAccount."));
}

void RecoveryDialog::TurnOff() {
    SetError("");
    ClearCode();
    std::string master = masterInput_->GetText();
    masterInput_->SetText("");
    if (master.empty()) {
        SetError(Tr("Enter the master password of this vault."));
        return;
    }
    UltraCryptSecureBuffer masterPw = UltraCryptSecureBuffer::AdoptString(master);
    const VaultRecoveryInfo before = store_.GetRecoveryInfo();
    StoreResult disabled = store_.DisableRecovery(masterPw);
    if (!disabled) {
        SetError(disabled.message);
        return;
    }
#ifdef ULTRAAUTHENTICATOR_HAVE_ACCOUNT
    const UltraAccount::Session session = UltraAccount::Session::Load();
    if (!before.escrowId.empty() && session.SignedIn()) {
        UltraAccount::Client(session.url).DeleteRecoveryKey(session.token, before.escrowId);
    }
#else
    (void)before;
#endif
    Refresh();
    if (onChanged) onChanged(Tr("Recovery is off."));
}

} // namespace Authenticator
} // namespace UltraCanvas
