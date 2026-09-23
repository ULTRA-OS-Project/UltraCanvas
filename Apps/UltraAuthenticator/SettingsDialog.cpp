// Apps/UltraAuthenticator/SettingsDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "SettingsDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"

#include <string>

namespace UltraCanvas {
namespace Authenticator {

namespace {

// The idle choices offered. A dropdown rather than a free number: "lock after
// 7 minutes" is not a decision anyone needs to make, and a typed value would
// need validation and a way to say "never".
struct IdleChoice {
    const char* label;
    uint32_t    seconds;
};
const IdleChoice kIdleChoices[] = {
    {"1 minute",   60},
    {"2 minutes",  120},
    {"5 minutes",  300},
    {"10 minutes", 600},
    {"30 minutes", 1800},
    {"Never",      0},
};

// The entry whose value matches, or the nearest one above it when a hand-
// edited file holds a value not on the list — a shorter timeout is the safe
// direction to round.
int IdleChoiceIndexFor(uint32_t seconds) {
    if (seconds == 0) return static_cast<int>(sizeof(kIdleChoices) / sizeof(kIdleChoices[0])) - 1;
    for (size_t i = 0; i + 1 < sizeof(kIdleChoices) / sizeof(kIdleChoices[0]); ++i) {
        if (seconds <= kIdleChoices[i].seconds) return static_cast<int>(i);
    }
    return static_cast<int>(sizeof(kIdleChoices) / sizeof(kIdleChoices[0])) - 2;  // 30 minutes
}

} // namespace

void SettingsDialog::CreateSettingsDialog(const Preferences& current) {
    original_ = current;

    DialogConfig cfg;
    cfg.title      = "Settings";
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

    auto heading = std::make_shared<UltraCanvasLabel>(
        "set-lock-hd", margin, y, fieldWidth, 20, "Locking");
    heading->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    heading->SetTextColor(Theme::kTextPrimary);
    AddChild(heading);
    y += 26;

    auto idleCaption = std::make_shared<UltraCanvasLabel>(
        "set-idle-lbl", margin, y + 6, 200, 18, "Lock after no input for");
    idleCaption->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    idleCaption->SetTextColor(Theme::kTextSecondary);
    AddChild(idleCaption);

    idleDropdown_ = std::make_shared<UltraCanvasDropdown>(
        "set-idle", margin + 210, y, fieldWidth - 210, 30);
    for (const IdleChoice& choice : kIdleChoices) {
        idleDropdown_->AddItem(choice.label, std::to_string(choice.seconds));
    }
    idleDropdown_->SetSelectedIndex(IdleChoiceIndexFor(current.idleLockSeconds),
                                    /*runNotifications=*/false);
    AddChild(idleDropdown_);
    y += 42;

    minimizeSwitch_ = std::make_shared<UltraCanvasSwitch>(
        "set-minimize", margin, y, fieldWidth, 26, "Lock when the window is minimised");
    minimizeSwitch_->SetChecked(current.lockOnMinimize);
    AddChild(minimizeSwitch_);
    y += 36;

    // Two lines at this width; the box must cover both, because a label
    // paints all of its text whatever height it is given.
    auto note = std::make_shared<UltraCanvasLabel>(
        "set-lock-note", margin, y, fieldWidth, 32,
        "Locking drops the decrypted accounts from memory; the master "
        "password is needed to show codes again.");
    note->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    note->SetTextColor(Theme::kTextMuted);
    note->SetWrap(TextWrap::WrapWord);
    AddChild(note);
    y += 44;

    auto heading2 = std::make_shared<UltraCanvasLabel>(
        "set-disp-hd", margin, y, fieldWidth, 20, "Display");
    heading2->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    heading2->SetTextColor(Theme::kTextPrimary);
    AddChild(heading2);
    y += 26;

    hideSwitch_ = std::make_shared<UltraCanvasSwitch>(
        "set-hide", margin, y, fieldWidth, 26, "Hide codes until a card is clicked");
    hideSwitch_->SetChecked(current.hideCodes);
    AddChild(hideSwitch_);
    y += 36;

    auto saveBtn = std::make_shared<UltraCanvasButton>(
        "set-save", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    saveBtn->SetText("Save");
    saveBtn->onClick = [this]() { Save(); };
    AddChild(saveBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "set-cancel", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(cancelBtn);
}

Preferences SettingsDialog::Collect() const {
    Preferences edited = original_;
    if (idleDropdown_) {
        const int index = idleDropdown_->GetSelectedIndex();
        const int count = static_cast<int>(sizeof(kIdleChoices) / sizeof(kIdleChoices[0]));
        if (index >= 0 && index < count) {
            edited.idleLockSeconds = kIdleChoices[index].seconds;
        }
    }
    if (minimizeSwitch_) edited.lockOnMinimize = minimizeSwitch_->IsChecked();
    if (hideSwitch_)     edited.hideCodes      = hideSwitch_->IsChecked();
    return edited;
}

void SettingsDialog::Save() {
    const Preferences edited = Collect();
    if (edited != original_ && onSave) onSave(edited);
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
