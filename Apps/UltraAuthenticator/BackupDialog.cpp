// Apps/UltraAuthenticator/BackupDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "BackupDialog.h"
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

std::shared_ptr<UltraCanvasLabel> Caption(const std::string& id, long x, long y,
                                          long w, const std::string& text) {
    auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, 18, text);
    label->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    label->SetTextColor(Theme::kTextSecondary);
    return label;
}

} // namespace

void BackupDialog::CreateBackupDialog() {
    const bool exporting = (mode_ == Mode::Export);
    // Export carries three fields and a longer explanation than import.
    const long dialogHeight = exporting ? 470 : 350;

    DialogConfig cfg;
    cfg.title      = exporting ? "Back up accounts" : "Restore accounts";
    cfg.width      = kDialogWidth;
    cfg.height     = dialogHeight;
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

    // Wrapped labels need a height that covers their line count, or they paint
    // over whatever is above them.
    auto intro = std::make_shared<UltraCanvasLabel>(
        "bk-intro", margin, y, fieldWidth, 72,
        exporting
            ? "This writes every account to one encrypted file. Keep it "
              "somewhere safe: anyone who has both the file and its passphrase "
              "has all of your second factors."
            : "This adds the accounts from a backup file. Accounts you already "
              "have are kept as they are, never replaced.");
    intro->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    intro->SetTextColor(Theme::kTextSecondary);
    intro->SetWrap(TextWrap::WrapWord);
    AddChild(intro);
    // Both texts wrap to three lines at this width, so both need the same
    // clearance; advancing only 60 for import printed its last line across the
    // caption below.
    y += 80;

    if (exporting) {
        AddChild(Caption("bk-master-lbl", margin, y, fieldWidth,
                         "Master password"));
        y += 20;
        masterInput_ = CreatePasswordInput("bk-master", margin, y, fieldWidth, 28);
        AddChild(masterInput_);
        y += 38;
    }

    AddChild(Caption("bk-pass-lbl", margin, y, fieldWidth,
                     exporting ? "New backup passphrase"
                               : "Backup passphrase"));
    y += 20;
    passphraseInput_ = CreatePasswordInput("bk-pass", margin, y, fieldWidth, 28);
    AddChild(passphraseInput_);
    y += 38;

    if (exporting) {
        AddChild(Caption("bk-confirm-lbl", margin, y, fieldWidth,
                         "Confirm backup passphrase"));
        y += 20;
        confirmInput_ = CreatePasswordInput("bk-confirm", margin, y, fieldWidth,
                                            28);
        AddChild(confirmInput_);
        y += 38;

        // Height covers the wrapped line count. At 34 this painted across the
        // field above it and clipped its own last line.
        auto note = std::make_shared<UltraCanvasLabel>(
            "bk-note", margin, y, fieldWidth, 54,
            "Use a different passphrase from your master password — a backup "
            "travels, and this one is refused if they match.");
        note->SetFont(Theme::kUiFont, Theme::kSizeSmall);
        note->SetTextColor(Theme::kTextMuted);
        note->SetWrap(TextWrap::WrapWord);
        AddChild(note);
        y += 62;
    }

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "bk-error", margin, y, fieldWidth, 40, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto okBtn = std::make_shared<UltraCanvasButton>(
        "bk-ok", kDialogWidth - margin - 220, dialogHeight - 54, 110, 32);
    okBtn->SetText(exporting ? "Back up…" : "Restore…");
    okBtn->onClick = [this]() { Accept(); };
    AddChild(okBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "bk-cancel", kDialogWidth - margin - 100, dialogHeight - 54, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(cancelBtn);
}

void BackupDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void BackupDialog::Accept() {
    if (!passphraseInput_) return;

    std::string master     = masterInput_ ? masterInput_->GetText() : std::string();
    std::string passphrase = passphraseInput_->GetText();
    std::string confirm    = confirmInput_ ? confirmInput_->GetText() : std::string();

    // Everything checkable locally is checked first, so an obvious typo costs
    // nothing: the handler behind this runs Argon2id at least once.
    std::string error;
    if (passphrase.empty()) {
        error = "A backup passphrase is required.";
    } else if (mode_ == Mode::Export) {
        if (master.empty()) {
            error = "Your master password is required to read the accounts.";
        } else if (passphrase != confirm) {
            error = "The backup passphrases do not match.";
        }
    }

    if (error.empty() && onConfirm) error = onConfirm(master, passphrase);

    // Wipe unconditionally. Between them these two open the vault and every
    // backup ever written from it.
    WipeAndClear(master, masterInput_);
    WipeAndClear(passphrase, passphraseInput_);
    WipeAndClear(confirm, confirmInput_);

    if (!error.empty()) {
        SetError(error);
        return;
    }
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
