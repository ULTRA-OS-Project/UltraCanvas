// Apps/UltraAccount/ui/AccountWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AccountWindow.h"

#include "UltraLocalisation/UltraLocalisation.h"

#ifndef ULTRAACCOUNT_VERSION
#error "ULTRAACCOUNT_VERSION is set by Apps/UltraAccount/CMakeLists.txt from Docs/UltraAccount/CHANGELOG.md"
#endif

namespace UltraAccount {

using namespace UltraCanvas;

namespace {

// Wipes a password the person typed once it has been sent: the copy here,
// and the field's text.
void Forget(std::string& text, const std::shared_ptr<UltraCanvasTextInput>& input) {
    for (char& c : text) c = 0;
    text.clear();
    if (input) input->SetText("");
}

std::string Trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

AccountWindow::AccountWindow(UltraCanvasApplication& app, std::string serviceUrl)
    : app_(app), client_(std::move(serviceUrl)) {}

DeviceInfo AccountWindow::ThisDevice() const {
    return DeviceInfo{DefaultDeviceName(), CurrentOs(), "UltraAccount " ULTRAACCOUNT_VERSION};
}

// ===========================================================================
// Building
// ===========================================================================
std::shared_ptr<UltraCanvasContainer> AccountWindow::NewPage(const std::string& id) {
    auto page = std::make_shared<UltraCanvasContainer>(id, 0, 0, kWidth, kHeight - 40);
    window_->AddChild(page);
    page->SetVisible(false);
    return page;
}

std::shared_ptr<UltraCanvasLabel> AccountWindow::AddLabel(UltraCanvasContainer& page, const std::string& id, long x, long& y, long w,
                                                          long h, const std::string& text, float size, bool bold, bool muted) {
    auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, h, text);
    label->SetFont("Sans", size, bold ? FontWeight::Bold : FontWeight::Normal);
    label->SetWrap(TextWrap::WrapWord);
    if (muted) label->SetTextColor(Color(104, 112, 125));
    page.AddChild(label);
    y += h + 6;
    return label;
}

std::shared_ptr<UltraCanvasTextInput> AccountWindow::AddField(UltraCanvasContainer& page, const std::string& id, long x, long& y, long w,
                                                              const std::string& caption, bool password, const std::string& placeholder) {
    AddLabel(page, id + "-caption", x, y, w, 18, caption, 12.0f, false, true);
    y -= 4;
    auto input = password ? CreatePasswordInput(id, x, y, w, 30) : CreateTextInput(id, x, y, w, 30);
    if (!placeholder.empty()) input->SetPlaceholder(placeholder);
    page.AddChild(input);
    y += 30 + 12;
    return input;
}

std::shared_ptr<UltraCanvasButton> AccountWindow::AddButton(UltraCanvasContainer& page, const std::string& id, long x, long y, long w,
                                                            const std::string& text, bool primary) {
    auto button = std::make_shared<UltraCanvasButton>(id, x, y, w, 32);
    button->SetText(text);
    if (primary) {
        ButtonStyle style = button->GetStyle();
        style.fontWeight = FontWeight::Bold;
        button->SetStyle(style);
    }
    page.AddChild(button);
    return button;
}

bool AccountWindow::Create() {
    WindowConfig cfg;
    cfg.title = "UltraAccount " ULTRAACCOUNT_VERSION;
    cfg.width = kWidth;
    cfg.height = kHeight;
    cfg.x = 160;
    cfg.y = 100;
    cfg.resizable = false;
    cfg.type = WindowType::Standard;
    window_ = CreateWindow(cfg);
    if (!window_) return false;
    window_->onWindowClosing = [this]() {
        app_.RequestExit();
        return true;
    };

    status_ = std::make_shared<UltraCanvasLabel>("status", kMargin, kHeight - 36, kWidth - 2 * kMargin, 28, "");
    status_->SetFont("Sans", 12.0f);
    status_->SetWrap(TextWrap::WrapWord);
    window_->AddChild(status_);

    BuildSignIn();
    BuildSignUp();
    BuildCode();
    BuildForgot();
    BuildAccount();

    session_ = Session::Load();
    if (session_.SignedIn()) {
        client_ = Client(session_.url);
        Refresh();
    } else {
        ShowPage(Page::SignIn);
    }
    return true;
}

void AccountWindow::Show() {
    if (window_) window_->Show();
}

void AccountWindow::BuildSignIn() {
    signInPage_ = NewPage("signin-page");
    auto& page = *signInPage_;
    const long w = kWidth - 2 * kMargin;
    long y = kMargin;
    AddLabel(page, "signin-title", kMargin, y, w, 30, Tr("Sign in to UltraAccount"), 19.0f, true);
    AddLabel(page, "signin-intro", kMargin, y, w, 54,
             Tr("One account for every ULTRA OS application: the store, recovering UltraAuthenticator, cloud storage and the AI "
                "services."),
             12.0f, false, true);
    y += 6;
    signInEmail_ = AddField(page, "signin-email", kMargin, y, w, Tr("Email"), false, "name@example.org");
    signInPassword_ = AddField(page, "signin-password", kMargin, y, w, Tr("Password"), true);
    {
        long captionY = y;
        signInTotpCaption_ = AddLabel(page, "signin-totp-caption", kMargin, captionY, w, 18,
                                      Tr("The six-digit code from your authenticator app"), 12.0f, false, true);
        signInTotp_ = CreateTextInput("signin-totp", kMargin, y + 18, w, 30);
        page.AddChild(signInTotp_);
        signInTotpCaption_->SetVisible(false);
        signInTotp_->SetVisible(false);
        y += 18 + 30 + 12;
    }
    AddButton(page, "signin-go", kMargin, y, 140, Tr("Sign in"), true)->onClick = [this]() { SignIn(); };
    AddButton(page, "signin-forgot", kMargin + 152, y, 180, Tr("Forgot password"), false)->onClick = [this]() {
        if (forgotEmail_ && signInEmail_) forgotEmail_->SetText(signInEmail_->GetText());
        ShowPage(Page::Forgot);
    };
    y += 56;
    AddLabel(page, "signin-new", kMargin, y, w, 20, Tr("No account yet?"), 13.0f, true);
    AddButton(page, "signin-create", kMargin, y, 180, Tr("Create an account"), false)->onClick = [this]() { ShowPage(Page::SignUp); };
}

void AccountWindow::BuildSignUp() {
    signUpPage_ = NewPage("signup-page");
    auto& page = *signUpPage_;
    const long w = kWidth - 2 * kMargin;
    long y = kMargin;
    AddLabel(page, "signup-title", kMargin, y, w, 30, Tr("Create an UltraAccount"), 19.0f, true);
    AddLabel(page, "signup-intro", kMargin, y, w, 40,
             Tr("A code goes to your address to verify it. The password is at least 12 characters."), 12.0f, false, true);
    y += 6;
    signUpEmail_ = AddField(page, "signup-email", kMargin, y, w, Tr("Email"), false, "name@example.org");
    signUpPassword_ = AddField(page, "signup-password", kMargin, y, w, Tr("Password"), true);
    signUpConfirm_ = AddField(page, "signup-confirm", kMargin, y, w, Tr("Password again"), true);
    signUpCountry_ = AddField(page, "signup-country", kMargin, y, 120, Tr("Country (two letters)"), false, "DE");
    AddButton(page, "signup-go", kMargin, y, 160, Tr("Create account"), true)->onClick = [this]() { SignUp(); };
    AddButton(page, "signup-back", kMargin + 172, y, 120, Tr("Back"), false)->onClick = [this]() { ShowPage(Page::SignIn); };
}

void AccountWindow::BuildCode() {
    codePage_ = NewPage("code-page");
    auto& page = *codePage_;
    const long w = kWidth - 2 * kMargin;
    long y = kMargin;
    AddLabel(page, "code-title", kMargin, y, w, 30, Tr("Verify your address"), 19.0f, true);
    codeIntro_ = AddLabel(page, "code-intro", kMargin, y, w, 40, "", 12.0f, false, true);
    y += 6;
    codeInput_ = AddField(page, "code-input", kMargin, y, 200, Tr("The six-digit code"), false, "123456");
    AddButton(page, "code-go", kMargin, y, 140, Tr("Verify"), true)->onClick = [this]() { VerifyCode(); };
    AddButton(page, "code-resend", kMargin + 152, y, 160, Tr("Send the code again"), false)->onClick = [this]() { ResendCode(); };
    AddButton(page, "code-back", kMargin + 324, y, 120, Tr("Back"), false)->onClick = [this]() { ShowPage(Page::SignIn); };
}

void AccountWindow::BuildForgot() {
    forgotPage_ = NewPage("forgot-page");
    auto& page = *forgotPage_;
    const long w = kWidth - 2 * kMargin;
    long y = kMargin;
    AddLabel(page, "forgot-title", kMargin, y, w, 30, Tr("Forgotten password"), 19.0f, true);
    AddLabel(page, "forgot-intro", kMargin, y, w, 54,
             Tr("A code goes to the account's address. With it, set a new password; every machine signed in is signed out."),
             12.0f, false, true);
    y += 6;
    forgotEmail_ = AddField(page, "forgot-email", kMargin, y, w, Tr("Email"), false, "name@example.org");
    AddButton(page, "forgot-send", kMargin, y, 160, Tr("Send the code"), false)->onClick = [this]() { SendResetCode(); };
    y += 48;
    forgotCode_ = AddField(page, "forgot-code", kMargin, y, 200, Tr("The code from the mail"), false, "123456");
    forgotPassword_ = AddField(page, "forgot-password", kMargin, y, w, Tr("New password"), true);
    forgotTotp_ = AddField(page, "forgot-totp", kMargin, y, 200, Tr("Authenticator code, if two-factor sign-in is on"));
    AddButton(page, "forgot-go", kMargin, y, 180, Tr("Set the password"), true)->onClick = [this]() { ResetPassword(); };
    AddButton(page, "forgot-back", kMargin + 192, y, 120, Tr("Back"), false)->onClick = [this]() { ShowPage(Page::SignIn); };
}

void AccountWindow::BuildAccount() {
    accountPage_ = NewPage("account-page");
    auto& page = *accountPage_;
    const long w = kWidth - 2 * kMargin;
    long y = kMargin;
    AddLabel(page, "account-title", kMargin, y, w - 140, 30, Tr("Your UltraAccount"), 19.0f, true);
    AddButton(page, "account-signout", kWidth - kMargin - 120, kMargin, 120, Tr("Sign out"), false)->onClick = [this]() { SignOut(); };
    accountEmail_ = AddLabel(page, "account-email", kMargin, y, w, 20, "", 13.0f);
    accountPlan_ = AddLabel(page, "account-plan", kMargin, y, w, 36, "", 12.0f, false, true);

    // The subscription
    y += 4;
    AddLabel(page, "plan-heading", kMargin, y, w, 22, Tr("Subscription"), 15.0f, true);
    licenceInput_ = AddField(page, "plan-licence", kMargin, y, w - 150, Tr("A licence for UltraAccount Plus"), false, "ULIC1.…");
    AddButton(page, "plan-activate", kWidth - kMargin - 140, y - 42, 140, Tr("Activate"), true)->onClick = [this]() { Activate(); };

    // The phone
    AddLabel(page, "phone-heading", kMargin, y, w, 22, Tr("Phone number"), 15.0f, true);
    phoneState_ = AddLabel(page, "phone-state", kMargin, y, w, 20, "", 13.0f);
    phoneHint_ = AddLabel(page, "phone-hint", kMargin, y, w, 36, "", 12.0f, false, true);
    const long half = (w - 12) / 2;
    long rowY = y;
    phoneInput_ = AddField(page, "phone-number", kMargin, rowY, half, Tr("Number with the country code"), false, "+49 151 23456789");
    rowY = y;
    phonePassword_ = AddField(page, "phone-password", kMargin + half + 12, rowY, half, Tr("Your account password"), true);
    y = rowY;
    AddButton(page, "phone-send", kMargin, y, 160, Tr("Send a code"), false)->onClick = [this]() { SendPhoneCode(); };
    AddButton(page, "phone-remove", kMargin + 172, y, 160, Tr("Remove number"), false)->onClick = [this]() { RemovePhone(); };
    y += 44;
    rowY = y;
    phoneCode_ = AddField(page, "phone-code", kMargin, rowY, 160, Tr("The texted code"), false, "123456");
    AddButton(page, "phone-verify", kMargin + 172, y + 18, 120, Tr("Verify"), true)->onClick = [this]() { VerifyPhone(); };
    y = rowY;

    // The recovery keys
    AddLabel(page, "recovery-heading", kMargin, y, w, 22, Tr("Recovery keys"), 15.0f, true);
    recoveryList_ = AddLabel(page, "recovery-list", kMargin, y, w, 36, "", 12.0f, false, true);
    recoveryPicker_ = std::make_shared<UltraCanvasDropdown>("recovery-picker", kMargin, y, w - 150, 30);
    page.AddChild(recoveryPicker_);
    AddButton(page, "recovery-remove", kWidth - kMargin - 140, y, 140, Tr("Remove"), false)->onClick = [this]() { RemoveRecoveryKey(); };
}

// ===========================================================================
// Acting
// ===========================================================================
void AccountWindow::ShowPage(Page page) {
    signInPage_->SetVisible(page == Page::SignIn);
    signUpPage_->SetVisible(page == Page::SignUp);
    codePage_->SetVisible(page == Page::Code);
    forgotPage_->SetVisible(page == Page::Forgot);
    accountPage_->SetVisible(page == Page::Account);
    if (window_) window_->RequestRedraw();
}

void AccountWindow::SetStatus(const std::string& text, bool isError) {
    if (!status_) return;
    status_->SetText(text);
    status_->SetTextColor(isError ? Color(197, 48, 48) : Color(104, 112, 125));
}

void AccountWindow::Adopt(const Session& session) {
    session_ = session;
    if (!session_.Save()) SetStatus(Tr("Signed in, but the session could not be kept on this machine"), true);
    Refresh();
}

void AccountWindow::SignIn() {
    const std::string email = Trimmed(signInEmail_->GetText());
    std::string password = signInPassword_->GetText();
    if (email.empty() || password.empty()) {
        SetStatus(Tr("Enter the email and the password"), true);
        return;
    }
    SetStatus(Tr("Signing in…"));
    const auto signedIn = client_.SignIn(email, password, ThisDevice(), Trimmed(signInTotp_->GetText()));
    if (!signedIn) {
        if (signedIn.error.code == "two_factor_required") {
            signInTotpCaption_->SetVisible(true);
            signInTotp_->SetVisible(true);
            SetStatus(Tr("Two-factor sign-in is on: enter the code from your authenticator app"), true);
            return;   // the password stays in its field for the second try
        }
        if (signedIn.error.code == "email_unverified") {
            pendingEmail_ = email;
            codeIntro_->SetText(TrF("The code was mailed to {0}.", email));
            ShowPage(Page::Code);
        }
        Forget(password, signInPassword_);
        SetStatus(Describe(signedIn.error), true);
        return;
    }
    Forget(password, signInPassword_);
    signInTotp_->SetText("");
    Adopt(signedIn.Value());
}

void AccountWindow::SignUp() {
    const std::string email = Trimmed(signUpEmail_->GetText());
    std::string password = signUpPassword_->GetText();
    std::string confirm = signUpConfirm_->GetText();
    if (email.empty() || password.empty()) {
        SetStatus(Tr("Enter the email and a password"), true);
        return;
    }
    if (password != confirm) {
        Forget(confirm, signUpConfirm_);
        SetStatus(Tr("The two passwords differ"), true);
        return;
    }
    SetStatus(Tr("Creating the account…"));
    const auto done = client_.SignUp(email, password, Trimmed(signUpCountry_->GetText()), UltraLocalisation::CurrentLanguage());
    Forget(password, signUpPassword_);
    Forget(confirm, signUpConfirm_);
    if (!done) {
        SetStatus(Describe(done.error), true);
        return;
    }
    pendingEmail_ = email;
    codeIntro_->SetText(TrF("A six-digit code went to {0}. It is good for ten minutes.", email));
    SetStatus("");
    ShowPage(Page::Code);
}

void AccountWindow::VerifyCode() {
    const std::string code = Trimmed(codeInput_->GetText());
    if (code.empty()) {
        SetStatus(Tr("Enter the code from the mail"), true);
        return;
    }
    const auto verified = client_.VerifySignUp(pendingEmail_, code, ThisDevice());
    if (!verified) {
        SetStatus(Describe(verified.error), true);
        return;
    }
    codeInput_->SetText("");
    Adopt(verified.Value());
}

void AccountWindow::ResendCode() {
    const auto sent = client_.ResendCode(pendingEmail_);
    SetStatus(sent ? Tr("A new code is on its way") : Describe(sent.error), !sent);
}

void AccountWindow::SendResetCode() {
    const std::string email = Trimmed(forgotEmail_->GetText());
    if (email.empty()) {
        SetStatus(Tr("Enter the account's email"), true);
        return;
    }
    const auto sent = client_.ForgotPassword(email);
    SetStatus(sent ? Tr("If the address has an account, a code is on its way") : Describe(sent.error), !sent);
}

void AccountWindow::ResetPassword() {
    const std::string email = Trimmed(forgotEmail_->GetText());
    std::string password = forgotPassword_->GetText();
    const auto reset = client_.ResetPassword(email, Trimmed(forgotCode_->GetText()), password, Trimmed(forgotTotp_->GetText()));
    Forget(password, forgotPassword_);
    if (!reset) {
        SetStatus(Describe(reset.error), true);
        return;
    }
    forgotCode_->SetText("");
    forgotTotp_->SetText("");
    signInEmail_->SetText(email);
    ShowPage(Page::SignIn);
    SetStatus(Tr("The password is changed: sign in with it"));
}

void AccountWindow::Refresh() {
    const auto me = client_.GetMe(session_.token);
    if (!me) {
        if (me.error.code == "signed_out" || me.error.code == "unauthorized") {
            Session::Clear();
            session_ = Session{};
            ShowPage(Page::SignIn);
        }
        SetStatus(Describe(me.error), true);
        if (!session_.SignedIn()) return;
        ShowPage(Page::Account);
        return;
    }
    me_ = me.Value();
    const Profile& p = me_.profile;
    accountEmail_->SetText(p.email + "  -  " + (p.emailVerified ? Tr("verified") : Tr("not verified")));
    if (me_.plan.paid) {
        accountPlan_->SetText(me_.plan.validUntil.empty() ? TrF("Plan: {0}, until cancelled", me_.plan.plan)
                                                          : TrF("Plan: {0}, until {1}", me_.plan.plan, me_.plan.validUntil.substr(0, 10)));
    } else {
        accountPlan_->SetText(Tr("Plan: free. UltraAccount Plus adds codes by text message, and later cloud storage and the AI services."));
    }

    if (p.phone.empty()) {
        phoneState_->SetText(Tr("No phone number"));
    } else {
        phoneState_->SetText(p.phone + "  -  " + (p.phoneVerified ? Tr("verified") : Tr("not verified")));
    }
    phoneHint_->SetText(me_.plan.sms ? Tr("Recovery codes can be texted to a verified number.")
                                     : Tr("A phone number needs UltraAccount Plus: a text message costs money to send."));

    recoveryPicker_->ClearItems();
    recoveryKeys_.clear();
    const auto keys = client_.RecoveryKeys(session_.token);
    std::string listing;
    if (keys) {
        recoveryKeys_ = keys.Value();
        for (const RecoveryKey& k : recoveryKeys_) {
            const std::string name = k.app + (k.label.empty() ? std::string() : " - " + k.label);
            recoveryPicker_->AddItem(name + " (" + k.createdAt.substr(0, 10) + ")", k.id);
        }
        listing = recoveryKeys_.empty()
                      ? Tr("No application has left a recovery key with this account.")
                      : TrN("One application can be recovered with a code to your email or phone.",
                            "{n} applications can be recovered with a code to your email or phone.",
                            static_cast<int>(recoveryKeys_.size()));
    } else {
        listing = Describe(keys.error);
    }
    recoveryList_->SetText(listing);
    if (!recoveryKeys_.empty()) recoveryPicker_->SetSelectedIndex(0, false);

    SetStatus(TrF("Signed in on {0}", session_.url));
    ShowPage(Page::Account);
}

void AccountWindow::SignOut() {
    client_.SignOut(session_.token);
    Session::Clear();
    session_ = Session{};
    ShowPage(Page::SignIn);
    SetStatus(Tr("Signed out on this machine"));
}

void AccountWindow::SendPhoneCode() {
    std::string password = phonePassword_->GetText();
    const auto sent = client_.AddPhone(session_.token, Trimmed(phoneInput_->GetText()), password);
    Forget(password, phonePassword_);
    SetStatus(sent ? TrF("A code was texted to {0}", sent.Value()) : Describe(sent.error), !sent);
}

void AccountWindow::VerifyPhone() {
    const auto verified = client_.VerifyPhone(session_.token, Trimmed(phoneCode_->GetText()));
    if (!verified) {
        SetStatus(Describe(verified.error), true);
        return;
    }
    phoneCode_->SetText("");
    phoneInput_->SetText("");
    Refresh();
    SetStatus(Tr("The phone number is verified"));
}

void AccountWindow::RemovePhone() {
    std::string password = phonePassword_->GetText();
    if (password.empty()) {
        SetStatus(Tr("Enter your account password to remove the number"), true);
        return;
    }
    const auto removed = client_.RemovePhone(session_.token, password);
    Forget(password, phonePassword_);
    if (!removed) {
        SetStatus(Describe(removed.error), true);
        return;
    }
    Refresh();
    SetStatus(Tr("The phone number is removed"));
}

void AccountWindow::Activate() {
    const std::string licence = Trimmed(licenceInput_->GetText());
    if (licence.empty()) {
        SetStatus(Tr("Paste the licence first"), true);
        return;
    }
    const auto plan = client_.Subscribe(session_.token, licence);
    if (!plan) {
        SetStatus(Describe(plan.error), true);
        return;
    }
    licenceInput_->SetText("");
    Refresh();
    SetStatus(Tr("UltraAccount Plus is active"));
}

void AccountWindow::RemoveRecoveryKey() {
    const int index = recoveryPicker_->GetSelectedIndex();
    if (index < 0 || index >= static_cast<int>(recoveryKeys_.size())) {
        SetStatus(Tr("Choose a recovery key first"), true);
        return;
    }
    const auto removed = client_.DeleteRecoveryKey(session_.token, recoveryKeys_[static_cast<size_t>(index)].id);
    if (!removed) {
        SetStatus(Describe(removed.error), true);
        return;
    }
    Refresh();
    SetStatus(Tr("The recovery key is removed: that application can no longer be recovered by a code"));
}

} // namespace UltraAccount
