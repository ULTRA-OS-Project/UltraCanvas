// Apps/UltraAuthenticator/AddAccountDialog.cpp
// Version: 0.2.0 - styled from Theme.h like the other dialogs
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AddAccountDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cctype>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

namespace {

// Percent-encodes the characters that would otherwise change the shape of the
// URI. The label and issuer are free text typed by the user, so a ':' or '?'
// in an account name must not be able to invent a new field.
std::string PercentEncode(const std::string& text) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        const bool unreserved = std::isalnum(c) || c == '-' || c == '.' ||
                                c == '_' || c == '~' || c == '@';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string Trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

} // namespace

void AddAccountDialog::CreateAddAccountDialog() {
    DialogConfig cfg;
    cfg.title     = "Add account";
    cfg.width     = kDialogWidth;
    cfg.height    = kDialogHeight;
    cfg.message   = "";
    cfg.buttons   = DialogButtons::NoButtons;   // own footer, as below
    cfg.position  = DialogPosition::CenterParent;
    cfg.resizable = false;
    // DialogType::Custom skips BuildDialogLayout() entirely, so this dialog
    // owns its whole client area and the coordinates below mean exactly what
    // they say. The standard layout was not usable here: its icon column
    // indents the message container and narrows it, which pushed this form's
    // right edge and its footer buttons outside the dialog.
    //
    // The corollary is that AddDialogElement() must NOT be used — it appends
    // to messageContainer, which BuildDialogLayout() never created, so every
    // element would be silently dropped and the dialog would come up blank.
    // Children go straight onto the dialog window instead. Escape still
    // cancels: with no button entries, OnEvent falls back to CloseDialog().
    cfg.dialogType = DialogType::Custom;
    // The form is laid out absolutely; the fit-height-to-message pass would
    // collapse this empty-message dialog and clip it.
    autoSizeHeight = false;
    CreateDialog(cfg);

    const long fieldWidth = kDialogWidth - 2 * kMargin;
    long y = kMargin;

    auto intro = std::make_shared<UltraCanvasLabel>(
        "add-intro", kMargin, y, fieldWidth, 36,
        "Enter the setup key exactly as the service showed it. "
        "Spaces and lower case are fine.");
    intro->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    intro->SetTextColor(Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    AddChild(intro);
    y += 44;

    auto issuerLabel = std::make_shared<UltraCanvasLabel>(
        "add-issuer-lbl", kMargin, y, fieldWidth, 18, "Service (issuer)");
    issuerLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    issuerLabel->SetTextColor(Theme::kTextSecondary);
    AddChild(issuerLabel);
    y += 20;
    issuerInput_ = std::make_shared<UltraCanvasTextInput>(
        "add-issuer", kMargin, y, fieldWidth, 28);
    issuerInput_->SetPlaceholder("Example");
    Theme::StyleInput(issuerInput_);
    AddChild(issuerInput_);
    y += 38;

    auto accountLabel = std::make_shared<UltraCanvasLabel>(
        "add-account-lbl", kMargin, y, fieldWidth, 18, "Account name");
    accountLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    accountLabel->SetTextColor(Theme::kTextSecondary);
    AddChild(accountLabel);
    y += 20;
    accountInput_ = std::make_shared<UltraCanvasTextInput>(
        "add-account", kMargin, y, fieldWidth, 28);
    accountInput_->SetPlaceholder("you@example.com");
    Theme::StyleInput(accountInput_);
    AddChild(accountInput_);
    y += 38;

    auto secretLabel = std::make_shared<UltraCanvasLabel>(
        "add-secret-lbl", kMargin, y, fieldWidth, 18, "Setup key");
    secretLabel->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    secretLabel->SetTextColor(Theme::kTextSecondary);
    AddChild(secretLabel);
    y += 20;
    // Password mode: the setup key is the second factor itself, so it is
    // masked exactly like a password would be.
    secretInput_ = CreatePasswordInput("add-secret", kMargin, y, fieldWidth, 28);
    Theme::StyleInput(secretInput_);
    AddChild(secretInput_);
    y += 34;

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "add-error", kMargin, y, fieldWidth, 20, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    AddChild(errorLabel_);

    auto addBtn = std::make_shared<UltraCanvasButton>(
        "add-ok", kDialogWidth - kMargin - 2 * Theme::kButtonDialog - Theme::kButtonGap,
        kDialogHeight - 52, Theme::kButtonDialog, Theme::kButtonHeight);
    addBtn->SetText("Add");
    Theme::StylePrimary(addBtn);
    addBtn->onClick = [this]() { Accept(); };
    AddChild(addBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "add-cancel", kDialogWidth - kMargin - Theme::kButtonDialog, kDialogHeight - 52,
        Theme::kButtonDialog, Theme::kButtonHeight);
    cancelBtn->SetText("Cancel");
    Theme::StyleSecondary(cancelBtn);
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(cancelBtn);
}

void AddAccountDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void AddAccountDialog::Accept() {
    if (!issuerInput_ || !accountInput_ || !secretInput_) return;

    const std::string issuer  = Trim(issuerInput_->GetText());
    const std::string account = Trim(accountInput_->GetText());
    std::string secret        = Trim(secretInput_->GetText());

    if (account.empty()) {
        SetError("An account name is required.");
        return;
    }
    if (secret.empty()) {
        SetError("A setup key is required.");
        return;
    }

    // Assemble the provisioning URI. Validation deliberately is not repeated
    // here: ParseOtpAuthUri behind AccountStore::AddFromUri is the single
    // gate, so manual entry cannot accept a key the QR path would reject.
    std::string label = PercentEncode(account);
    if (!issuer.empty()) label = PercentEncode(issuer) + ":" + label;

    std::string uri = "otpauth://totp/" + label + "?secret=" + secret;
    if (!issuer.empty()) uri += "&issuer=" + PercentEncode(issuer);

    std::string error;
    if (onAccept) error = onAccept(uri);

    // Wipe the assembled URI and the typed key: both carry the seed.
    UltraCrypt_SecureZero(&uri[0], uri.size());
    if (!secret.empty()) UltraCrypt_SecureZero(&secret[0], secret.size());
    secretInput_->SetText("");

    if (!error.empty()) {
        SetError(error);
        return;
    }
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
