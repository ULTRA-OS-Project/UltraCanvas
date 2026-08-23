// Apps/UltraAuthenticator/EditAccountDialog.h
// Edits an existing account: its label, and the parameters used to compute
// its codes.
//
// Why this exists at all: before it, an account's issuer and name were
// permanent. The app deliberately never hands a seed back to the UI, so
// "remove and re-add" was not a workaround — the user had nothing to re-enter.
// A typo made at enrolment could only be fixed by re-enrolling with the
// service, which for a second factor often means account recovery.
//
// The two halves of this form carry very different risk, and the dialog says
// so rather than presenting them as equals:
//
//  - **Label** (issuer, account name) is cosmetic. It changes what this app
//    displays and how the entry is keyed. Nothing can break.
//  - **Parameters** (digits, period, algorithm, counter) must match what the
//    service expects. Nothing here re-negotiates with anyone; changing them
//    just changes what this app computes, so a wrong value silently produces
//    codes that are rejected. They are grouped under a warning, and left at
//    the values the account arrived with unless deliberately touched.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef EDITACCOUNTDIALOG_H
#define EDITACCOUNTDIALOG_H

#include "otp/UltraOtp.h"

#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {

class EditAccountDialog : public UltraCanvasModalDialog {
public:
    EditAccountDialog() = default;
    ~EditAccountDialog() override = default;

    // `current` seeds every field, so an untouched dialog submits exactly what
    // it was given.
    void CreateEditAccountDialog(const Otp::Parameters& current);

    // Returns an error to display, or an empty string on success (which
    // closes). Same contract as AddAccountDialog: a rejected edit stays on
    // screen for correction rather than being thrown away.
    std::function<std::string(const Otp::Parameters& edited)> onAccept;

private:
    void Accept();
    void SetError(const std::string& text);

    Otp::Parameters current_;

    std::shared_ptr<UltraCanvasTextInput> issuerInput_;
    std::shared_ptr<UltraCanvasTextInput> accountInput_;
    std::shared_ptr<UltraCanvasDropdown>  digitsDrop_;
    std::shared_ptr<UltraCanvasDropdown>  algorithmDrop_;
    std::shared_ptr<UltraCanvasTextInput> periodInput_;
    std::shared_ptr<UltraCanvasTextInput> counterInput_;   // HOTP only
    std::shared_ptr<UltraCanvasLabel>     errorLabel_;

    static constexpr long kDialogWidth  = 480;
    static constexpr long kDialogHeight = 500;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // EDITACCOUNTDIALOG_H
