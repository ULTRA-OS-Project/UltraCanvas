// Apps/UltraAccount/ui/AccountWindow.h
// The UltraAccount window: the one ULTRA OS account a person signs in with,
// for every application that needs one - UltraStore's purchases,
// UltraAuthenticator's recovery from a forgotten master password, and next
// cloud storage and the paid AI services.
//
// Before signing in it is four small pages - sign in, create an account,
// the code mailed to the address, a forgotten password - and after it the
// account itself: the email (verified at sign-up), the phone number
// (verified by a texted code; a paid plan only, because a text costs money
// to send), the subscription (the plan, until when, a licence to activate),
// and the recovery keys applications have left with the account, which the
// person can remove. Signing in here signs in every application on this
// machine (UltraAccountClient.h, Session).
//
// The service calls are made on the window's thread: each is one small
// HTTPS request, and the window shows what it is doing in its status line.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAACCOUNT_ACCOUNTWINDOW_H
#define ULTRAACCOUNT_ACCOUNTWINDOW_H

#include "../core/UltraAccountClient.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraAccount {

class AccountWindow {
public:
    AccountWindow(UltraCanvas::UltraCanvasApplication& app, std::string serviceUrl);

    bool Create();
    void Show();

private:
    enum class Page { SignIn, SignUp, Code, Forgot, Account };

    // ----- building -----
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> NewPage(const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> AddLabel(UltraCanvas::UltraCanvasContainer& page, const std::string& id,
                                                            long x, long& y, long w, long h, const std::string& text,
                                                            float size, bool bold = false, bool muted = false);
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> AddField(UltraCanvas::UltraCanvasContainer& page, const std::string& id,
                                                                long x, long& y, long w, const std::string& caption,
                                                                bool password = false, const std::string& placeholder = {});
    std::shared_ptr<UltraCanvas::UltraCanvasButton> AddButton(UltraCanvas::UltraCanvasContainer& page, const std::string& id,
                                                              long x, long y, long w, const std::string& text, bool primary);
    void BuildSignIn();
    void BuildSignUp();
    void BuildCode();
    void BuildForgot();
    void BuildAccount();

    // ----- acting -----
    void ShowPage(Page page);
    void SetStatus(const std::string& text, bool isError = false);
    void SignIn();
    void SignUp();
    void VerifyCode();
    void ResendCode();
    void SendResetCode();
    void ResetPassword();
    void Refresh();
    void SignOut();
    void SendPhoneCode();
    void VerifyPhone();
    void RemovePhone();
    void Activate();
    void RemoveRecoveryKey();
    void Adopt(const Session& session);
    DeviceInfo ThisDevice() const;

    UltraCanvas::UltraCanvasApplication& app_;
    Client client_;
    Session session_;
    std::string pendingEmail_;   // the address a sign-up code went to

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> signInPage_, signUpPage_, codePage_, forgotPage_, accountPage_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;

    // Sign in
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> signInEmail_, signInPassword_, signInTotp_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> signInTotpCaption_;
    // Sign up
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> signUpEmail_, signUpPassword_, signUpConfirm_, signUpCountry_;
    // The mailed code
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> codeIntro_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> codeInput_;
    // Forgotten password
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> forgotEmail_, forgotCode_, forgotPassword_, forgotTotp_;
    // The account
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> accountEmail_, accountPlan_, phoneState_, phoneHint_, recoveryList_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> phoneInput_, phonePassword_, phoneCode_, licenceInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> recoveryPicker_;
    std::vector<RecoveryKey> recoveryKeys_;
    Me me_;

    static constexpr long kWidth = 560;
    static constexpr long kHeight = 760;
    static constexpr long kMargin = 28;
};

} // namespace UltraAccount

#endif // ULTRAACCOUNT_ACCOUNTWINDOW_H
