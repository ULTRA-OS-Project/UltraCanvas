// Apps/UltraAuthenticator/EditAccountDialog.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "EditAccountDialog.h"
#include "Theme.h"

#include "UltraCanvasButton.h"

#include <string>

namespace UltraCanvas {
namespace Authenticator {

namespace {

std::string Trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Parses a bounded unsigned field. Returns false on anything that is not a
// plain run of digits, so "30abc" is rejected rather than quietly read as 30.
bool ParseBounded(const std::string& text, uint64_t low, uint64_t high,
                  uint64_t& out) {
    const std::string t = Trim(text);
    if (t.empty() || t.size() > 20) return false;
    uint64_t value = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > high) return false;
    }
    if (value < low) return false;
    out = value;
    return true;
}

std::shared_ptr<UltraCanvasLabel> FieldLabel(const std::string& id, long x,
                                             long y, long w,
                                             const std::string& text) {
    auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, 18, text);
    label->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    label->SetTextColor(Theme::kTextSecondary);
    return label;
}

} // namespace

void EditAccountDialog::CreateEditAccountDialog(const Otp::Parameters& current) {
    current_ = current;

    DialogConfig cfg;
    cfg.title     = "Edit account";
    cfg.width     = kDialogWidth;
    cfg.height    = kDialogHeight;
    cfg.message   = "";
    cfg.buttons   = DialogButtons::NoButtons;
    cfg.position  = DialogPosition::CenterParent;
    cfg.resizable = false;
    // Custom for the same reasons AddAccountDialog documents: the standard
    // layout's icon column narrows the message container and pushes a form
    // this wide off the right edge, and AddDialogElement() would silently drop
    // every child because BuildDialogLayout() never runs to create the
    // container it appends to. Children go on the dialog window directly.
    cfg.dialogType = DialogType::Custom;
    autoSizeHeight = false;
    CreateDialog(cfg);

    const long margin     = Theme::kMargin;
    const long fieldWidth = kDialogWidth - 2 * margin;
    const long halfWidth  = (fieldWidth - 14) / 2;
    long y = margin;

    // --- Label ------------------------------------------------------------
    auto section1 = std::make_shared<UltraCanvasLabel>(
        "edit-sec1", margin, y, fieldWidth, 20, "Name");
    section1->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    section1->SetTextColor(Theme::kTextPrimary);
    AddChild(section1);
    y += 24;

    auto nameHelp = std::make_shared<UltraCanvasLabel>(
        "edit-name-help", margin, y, fieldWidth, 18,
        "Only affects how this account is shown here.");
    nameHelp->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    nameHelp->SetTextColor(Theme::kTextMuted);
    AddChild(nameHelp);
    y += 24;

    AddChild(FieldLabel("edit-issuer-lbl", margin, y, halfWidth,
                        "Service (issuer)"));
    AddChild(FieldLabel("edit-account-lbl", margin + halfWidth + 14, y,
                        halfWidth, "Account name"));
    y += 20;

    issuerInput_ = std::make_shared<UltraCanvasTextInput>(
        "edit-issuer", margin, y, halfWidth, 28);
    issuerInput_->SetText(current.issuer);
    AddChild(issuerInput_);

    accountInput_ = std::make_shared<UltraCanvasTextInput>(
        "edit-account", margin + halfWidth + 14, y, halfWidth, 28);
    accountInput_->SetText(current.accountName);
    AddChild(accountInput_);
    y += 44;

    // --- Parameters -------------------------------------------------------
    auto section2 = std::make_shared<UltraCanvasLabel>(
        "edit-sec2", margin, y, fieldWidth, 20, "Code settings");
    section2->SetFont(Theme::kUiFont, Theme::kSizeBody, FontWeight::Bold);
    section2->SetTextColor(Theme::kTextPrimary);
    AddChild(section2);
    y += 24;

    auto warn = std::make_shared<UltraCanvasLabel>(
        "edit-warn", margin, y, fieldWidth, 34,
        "These must match what the service expects. Changing them does not "
        "tell the service anything — a wrong value just produces codes it "
        "will reject.");
    warn->SetFont(Theme::kUiFont, Theme::kSizeSmall);
    warn->SetTextColor(Theme::kDanger);
    warn->SetWrap(TextWrap::WrapWord);
    AddChild(warn);
    y += 42;

    AddChild(FieldLabel("edit-digits-lbl", margin, y, halfWidth, "Digits"));
    AddChild(FieldLabel("edit-alg-lbl", margin + halfWidth + 14, y, halfWidth,
                        "Algorithm"));
    y += 20;

    digitsDrop_ = std::make_shared<UltraCanvasDropdown>(
        "edit-digits", margin, y, halfWidth, 28);
    digitsDrop_->AddItem("6");
    digitsDrop_->AddItem("7");
    digitsDrop_->AddItem("8");
    digitsDrop_->SetSelectedIndex(
        static_cast<int>(current.digits) - static_cast<int>(Otp::kMinDigits),
        false);
    AddChild(digitsDrop_);

    algorithmDrop_ = std::make_shared<UltraCanvasDropdown>(
        "edit-alg", margin + halfWidth + 14, y, halfWidth, 28);
    algorithmDrop_->AddItem("SHA-1");
    algorithmDrop_->AddItem("SHA-256");
    algorithmDrop_->AddItem("SHA-512");
    algorithmDrop_->SetSelectedIndex(static_cast<int>(current.algorithm), false);
    AddChild(algorithmDrop_);
    y += 44;

    // Period applies to TOTP; counter applies to HOTP. Only one of the two can
    // ever be meaningful, so only the relevant one is built.
    if (current.type == Otp::Type::Totp) {
        AddChild(FieldLabel("edit-period-lbl", margin, y, halfWidth,
                            "Period (seconds)"));
        y += 20;
        periodInput_ = std::make_shared<UltraCanvasTextInput>(
            "edit-period", margin, y, halfWidth, 28);
        periodInput_->SetText(std::to_string(current.periodSeconds));
        AddChild(periodInput_);
    } else {
        AddChild(FieldLabel("edit-counter-lbl", margin, y, halfWidth,
                            "Counter"));
        y += 20;
        counterInput_ = std::make_shared<UltraCanvasTextInput>(
            "edit-counter", margin, y, halfWidth, 28);
        counterInput_->SetText(std::to_string(current.counter));
        AddChild(counterInput_);

        auto counterHelp = std::make_shared<UltraCanvasLabel>(
            "edit-counter-help", margin + halfWidth + 14, y, halfWidth, 28,
            "Set this only to resynchronise with the service.");
        counterHelp->SetFont(Theme::kUiFont, Theme::kSizeSmall);
        counterHelp->SetTextColor(Theme::kTextMuted);
        counterHelp->SetWrap(TextWrap::WrapWord);
        AddChild(counterHelp);
    }
    y += 40;

    errorLabel_ = std::make_shared<UltraCanvasLabel>(
        "edit-error", margin, y, fieldWidth, 34, "");
    errorLabel_->SetFont(Theme::kUiFont, Theme::kSizeSecondary);
    errorLabel_->SetTextColor(Theme::kDanger);
    errorLabel_->SetWrap(TextWrap::WrapWord);
    AddChild(errorLabel_);

    auto saveBtn = std::make_shared<UltraCanvasButton>(
        "edit-ok", kDialogWidth - margin - 210, kDialogHeight - 54, 100, 32);
    saveBtn->SetText("Save");
    saveBtn->onClick = [this]() { Accept(); };
    AddChild(saveBtn);

    auto cancelBtn = std::make_shared<UltraCanvasButton>(
        "edit-cancel", kDialogWidth - margin - 100, kDialogHeight - 54, 100, 32);
    cancelBtn->SetText("Cancel");
    cancelBtn->onClick = [this]() { CloseDialog(DialogResult::Cancel); };
    AddChild(cancelBtn);
}

void EditAccountDialog::SetError(const std::string& text) {
    if (errorLabel_) errorLabel_->SetText(text);
}

void EditAccountDialog::Accept() {
    if (!issuerInput_ || !accountInput_) return;

    // Start from what the account already is, so any field this dialog does
    // not offer survives the edit untouched.
    Otp::Parameters edited = current_;
    edited.issuer      = Trim(issuerInput_->GetText());
    edited.accountName = Trim(accountInput_->GetText());

    if (edited.accountName.empty()) {
        SetError("An account name is required.");
        return;
    }

    if (digitsDrop_) {
        const int index = digitsDrop_->GetSelectedIndex();
        if (index >= 0) {
            edited.digits = Otp::kMinDigits + static_cast<uint32_t>(index);
        }
    }
    if (algorithmDrop_) {
        const int index = algorithmDrop_->GetSelectedIndex();
        if (index >= 0) {
            edited.algorithm = static_cast<Otp::Algorithm>(index);
        }
    }

    if (periodInput_) {
        uint64_t period = 0;
        if (!ParseBounded(periodInput_->GetText(), Otp::kMinPeriodSeconds,
                          Otp::kMaxPeriodSeconds, period)) {
            SetError("Period must be a whole number between " +
                     std::to_string(Otp::kMinPeriodSeconds) + " and " +
                     std::to_string(Otp::kMaxPeriodSeconds) + " seconds.");
            return;
        }
        edited.periodSeconds = static_cast<uint32_t>(period);
    }

    if (counterInput_) {
        uint64_t counter = 0;
        if (!ParseBounded(counterInput_->GetText(), 0,
                          static_cast<uint64_t>(-1) / 2, counter)) {
            SetError("Counter must be a whole number.");
            return;
        }
        edited.counter = counter;
    }

    std::string error;
    if (onAccept) error = onAccept(edited);
    if (!error.empty()) {
        SetError(error);
        return;
    }
    CloseDialog(DialogResult::OK);
}

} // namespace Authenticator
} // namespace UltraCanvas
