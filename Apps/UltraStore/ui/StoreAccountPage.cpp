// Apps/UltraStore/ui/StoreAccountPage.cpp
// The account in the store's window, after the design's AccountSignUp and
// AccountProfile artboards: the Sign in page with its steps (sign in,
// create an account, the mailed code, the second factor, a forgotten
// password, done), and the account page with its rail - profile, security
// and sign-in, devices, purchases and licences, notifications, privacy and
// data - every section asking the account service (Account.h) on a thread
// and drawing what it answers. The header's pill is "Sign in" until the
// store is signed in, then the person's initials and name.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreWindow.h"

#include "StoreTheme.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifndef ULTRASTORE_VERSION
#error "ULTRASTORE_VERSION must be passed by CMake (from Docs/UltraStore/CHANGELOG.md)"
#endif

using namespace UltraCanvas;

namespace UltraStore::Ui {

namespace {

constexpr const char* kDiscover = "discover";
constexpr const char* kSignIn   = "signin";
constexpr const char* kAccount  = "account";

constexpr const char* kSections[] = {"profile", "security", "devices", "licences", "notifications", "privacy"};

std::string SectionTitle(const std::string& section) {
    if (section == "profile") return Tr("Profile");
    if (section == "security") return Tr("Security and sign-in");
    if (section == "devices") return Tr("Devices");
    if (section == "licences") return Tr("Purchases and licences");
    if (section == "notifications") return Tr("Notifications");
    if (section == "privacy") return Tr("Privacy and data");
    return section;
}

std::shared_ptr<UltraCanvasContainer> FieldRow(const std::string& id, const std::string& label, const std::shared_ptr<UltraCanvasUIElement>& control) {
    auto row = Theme::Row(id, Theme::kGap);
    auto text = Theme::Text(id + ".label", label, 12.0f);
    text->SetElementSize(CSSLayout::Dimension::Px(180), CSSLayout::Dimension::Auto());
    row->AddChild(text);
    row->AddChild(Theme::Pin(control));
    return row;
}

std::shared_ptr<UltraCanvasContainer> Stack(const std::string& id, const std::string& label, const std::shared_ptr<UltraCanvasUIElement>& control) {
    auto col = Theme::Column(id, 4.0f);
    col->AddChild(Theme::Text(id + ".label", label, 12.0f, FontWeight::Bold));
    col->AddChild(Theme::Pin(control));
    return col;
}

std::shared_ptr<UltraCanvasTextInput> Input(const std::string& id, const std::string& placeholder, float width = 360.0f, bool password = false) {
    auto input = Theme::Pin(password ? CreatePasswordInput(id, 0, 0, width, Theme::kControlHeight) : CreateTextInput(id, 0, 0, width, Theme::kControlHeight));
    input->SetPlaceholder(placeholder);
    return input;
}

std::string Day(const std::string& iso) { return iso.size() >= 10 ? iso.substr(0, 10) : iso; }

int YearOf(const std::string& text) { return text.empty() ? 0 : std::atoi(text.c_str()); }   // locale-ok: digits only

const char* const kCountries[][2] = {{"DE", "Germany"}, {"AT", "Austria"}, {"CH", "Switzerland"}, {"NL", "Netherlands"}, {"FR", "France"},
                                     {"IT", "Italy"}, {"ES", "Spain"}, {"GB", "United Kingdom"}, {"US", "United States"}, {"", "Elsewhere"}};

std::string NotificationWord(const std::string& value) {
    if (value == "email") return Tr("By email");
    if (value == "email_and_devices") return Tr("Email and devices");
    if (value == "devices") return Tr("Devices only");
    if (value == "monthly_email") return Tr("One email a month at most");
    if (value == "off") return Tr("Off");
    return value;
}

} // namespace

// ===========================================================================
// The service, this machine, the pill
// ===========================================================================
std::string StoreWindow::AccountUrl() const {
    if (!settings_.accountUrl.empty()) return settings_.accountUrl;
    if (account_.SignedIn()) return account_.url;
    return catalog_ ? catalog_->accountUrl : std::string();
}

AccountClient StoreWindow::AccountClientNow() const {
    return AccountClient(AccountUrl());
}

DeviceInfo StoreWindow::ThisDevice() const {
    DeviceInfo d;
    d.name = DefaultDeviceName();
    d.os = platform_.Describe();
    d.client = std::string("UltraStore ") + ULTRASTORE_VERSION;
    return d;
}

void StoreWindow::UpdateAccountPill() {
    if (!accountButton_) return;
    if (!account_.SignedIn()) {
        accountButton_->SetText(Tr("Sign in"));
        accountButton_->SetTooltip(Tr("Sign in to your UltraStore account, or create one"));
        Theme::Style(accountButton_, Theme::ButtonKind::Secondary);
    } else {
        AccountProfile p;
        p.displayName = me_ ? me_->profile.displayName : account_.displayName;
        p.email = account_.email;
        const int unread = me_ ? me_->unreadNotices : 0;
        accountButton_->SetText(p.Initials() + "  " + p.Name() + (unread > 0 ? " · " + std::to_string(unread) : std::string()));
        accountButton_->SetTooltip(TrF("Your account: {0}", account_.email));
        Theme::Style(accountButton_, Theme::ButtonKind::Soft);
    }
    accountButton_->SetVisible(!AccountUrl().empty() || account_.SignedIn());
    header_->InvalidateLayout();
    header_->RequestRedraw();
}

void StoreWindow::OpenAccount() {
    detailsAppId_.clear();
    if (account_.SignedIn()) {
        ShowPage(kAccount);
        RefreshMe();
    } else {
        ShowSignIn("signin");
    }
}

void StoreWindow::ForgetAccount(const std::string& why) {
    if (engine_) AccountSession::Clear(engine_->Root());
    account_ = AccountSession();
    me_.reset();
    notices_.clear();
    UpdateAccountPill();
    if (currentPage_ == kAccount) ShowSignIn("signin");
    SetStatus(why.empty() ? Tr("Signed out") : why, !why.empty());
}

// ===========================================================================
// Sign in
// ===========================================================================
void StoreWindow::BuildSignInPage() {
    BuildScrollPage(kSignIn, signInBody_);
}

void StoreWindow::ShowSignIn(const std::string& mode) {
    signInMode_ = mode;
    RebuildSignIn();
    ShowPage(kSignIn);
}

void StoreWindow::RebuildSignIn() {
    if (!signInBody_) return;
    signInBody_->ClearChildren();
    siEmail_.reset(); siPassword_.reset(); siCode_.reset(); siYear_.reset(); siTotp_.reset(); siNewPassword_.reset();
    siCountry_.reset(); siConsent_.reset(); siMarketing_.reset();

    auto split = Theme::Row("usSI.split", Theme::kSectionGap);
    split->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // The brand panel: what the account is for.
    auto brand = Theme::Surface("usSI.brand", Theme::kGap, Theme::kAccentFaint);
    brand->SetElementSize(CSSLayout::Dimension::Px(380), CSSLayout::Dimension::Auto());
    brand->layoutItem.SetFlexShrink(0);
    brand->AddChild(Theme::Eyebrow("usSI.brand.eyebrow", Tr("UltraStore")));
    brand->AddChild(Theme::Display("usSI.brand.title", Tr("One account for every shop of UltraStore")));
    brand->AddChild(Theme::Text("usSI.brand.text", Tr("Apps, games, the marketplace, video and music: one sign-in, your licences and subscriptions on every machine, your library and your orders in one place.")));
    for (const auto& [title, text] : std::vector<std::pair<std::string, std::string>>{
             {Tr("No account needed to install"), Tr("Free apps install without signing in, as today. An account is for what follows you: purchases, subscriptions, the library.")},
             {Tr("Two-factor sign-in"), Tr("UltraAuthenticator or any TOTP app, and recovery codes kept in UltraPassword if you like.")},
             {Tr("Your data is yours"), Tr("Download it as a file any time, delete the account in one step, and the mixes and recommendations are computed on your machine.")}}) {
        auto point = Theme::Column("usSI.brand." + title, 2.0f);
        point->AddChild(Theme::Text("usSI.brand.t." + title, title, 13.0f, FontWeight::Bold));
        point->AddChild(Theme::Caption("usSI.brand.c." + title, text));
        brand->AddChild(point);
    }
    split->AddChild(brand);

    auto form = Theme::Surface("usSI.form", Theme::kGap);
    form->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    const std::string url = AccountUrl();
    if (url.empty()) {
        form->AddChild(Theme::Heading("usSI.none.title", Tr("No account service")));
        form->AddChild(Theme::Text("usSI.none.text", Tr("The catalog names no account service, so this store has no accounts. A service of your own goes into Settings.")));
    } else if (signInMode_ == "signup") {
        form->AddChild(Theme::Heading("usSI.title", Tr("Create your account")));
        form->AddChild(Theme::Caption("usSI.lead", Tr("An email, a password, and where you are for VAT and the age rating. Takes a minute.")));
        siEmail_ = Input("usSI.email", "anna@example.org");
        siEmail_->SetText(signInEmail_);
        form->AddChild(Stack("usSI.emailRow", Tr("Email"), siEmail_));
        form->AddChild(Theme::Caption("usSI.emailHint", Tr("We send a code to it in the next step. It is also your sign-in name.")));
        siPassword_ = Input("usSI.password", Tr("at least 12 characters"), 360.0f, true);
        form->AddChild(Stack("usSI.passwordRow", Tr("Password"), siPassword_));
        auto where = Theme::Row("usSI.where", Theme::kGap, true);
        siCountry_ = Theme::Pin(CreateDropdown("usSI.country", 0, 0, 220, Theme::kControlHeight));
        for (const auto& c : kCountries) siCountry_->AddItem(Tr(c[1]), c[0]);
        siCountry_->SetSelectedIndex(0, false);
        where->AddChild(Stack("usSI.countryRow", Tr("Country"), siCountry_));
        siYear_ = Input("usSI.year", "1990", 120.0f);
        where->AddChild(Stack("usSI.yearRow", Tr("Year of birth"), siYear_));
        form->AddChild(where);
        form->AddChild(Theme::Caption("usSI.whereHint", Tr("The country sets VAT and the currency; the year is for age-rated games and videos and is never shown.")));
        siConsent_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usSI.consent", 0, 0, 520, Theme::kControlHeight, Tr("I accept the terms of service and have read the privacy notice."), false));
        form->AddChild(siConsent_);
        siMarketing_ = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usSI.marketing", 0, 0, 520, Theme::kControlHeight, Tr("Send me the release notes of apps I own and the deals of the month. One mail a month at most."), false));
        form->AddChild(siMarketing_);
        auto buttons = Theme::Row("usSI.buttons", Theme::kInnerGap, true);
        auto go = Theme::Button("usSI.continue", Tr("Continue"), 130, Theme::ButtonKind::Primary);
        go->onClick = [this]() {
            if (!siConsent_->IsChecked()) { SetStatus(Tr("The terms of service have to be accepted first"), true); return; }
            const std::string email = siEmail_->GetText(), password = siPassword_->GetText();
            const int at = siCountry_->GetSelectedIndex();
            const std::string country = at >= 0 ? siCountry_->GetItem(at)->value : std::string();
            const int year = YearOf(siYear_->GetText());
            const bool marketing = siMarketing_->IsChecked();
            if (email.find('@') == std::string::npos) { SetStatus(Tr("That is not an email address"), true); return; }
            if (password.size() < 12) { SetStatus(Tr("A password is at least 12 characters"), true); return; }
            signInEmail_ = email;
            AccountTask<bool>(Tr("Creating the account"), [client = AccountClientNow(), email, password, country, year, lang = lang_, marketing]() {
                const auto r = client.SignUp(email, password, country, year, lang, marketing);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { signInHint_.clear(); ShowSignIn("verify"); SetStatus(TrF("A code was sent to {0}", signInEmail_)); });
        };
        buttons->AddChild(go);
        auto back = Theme::Button("usSI.haveOne", Tr("Already have one? Sign in"), 200, Theme::ButtonKind::Ghost);
        back->onClick = [this]() { ShowSignIn("signin"); };
        buttons->AddChild(back);
        form->AddChild(buttons);
    } else if (signInMode_ == "verify") {
        form->AddChild(Theme::Heading("usSI.title", Tr("Check your email")));
        form->AddChild(Theme::Text("usSI.lead", TrF("We sent a six-digit code to {0}. It is good for 10 minutes.", signInEmail_)));
        siCode_ = Input("usSI.code", "000000", 160.0f);
        form->AddChild(Stack("usSI.codeRow", Tr("Code"), siCode_));
        auto buttons = Theme::Row("usSI.buttons", Theme::kInnerGap, true);
        auto verify = Theme::Button("usSI.verify", Tr("Verify"), 110, Theme::ButtonKind::Primary);
        verify->onClick = [this]() {
            const std::string code = siCode_->GetText();
            AccountTask<SignedIn>(Tr("Verifying the code"), [client = AccountClientNow(), email = signInEmail_, code, device = ThisDevice()]() {
                return client.VerifySignUp(email, code, device);
            }, [this](const SignedIn& s) { FinishSignIn(s); });
        };
        buttons->AddChild(verify);
        auto again = Theme::Button("usSI.again", Tr("Send it again"), 130, Theme::ButtonKind::Ghost);
        again->onClick = [this]() {
            AccountTask<bool>(Tr("Sending the code again"), [client = AccountClientNow(), email = signInEmail_]() {
                const auto r = client.ResendCode(email);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { SetStatus(Tr("The code was sent again")); });
        };
        buttons->AddChild(again);
        auto change = Theme::Button("usSI.change", Tr("Change the address"), 150, Theme::ButtonKind::Ghost);
        change->onClick = [this]() { ShowSignIn("signup"); };
        buttons->AddChild(change);
        form->AddChild(buttons);
    } else if (signInMode_ == "twofactor") {
        form->AddChild(Theme::Heading("usSI.title", Tr("Your second factor")));
        form->AddChild(Theme::Text("usSI.lead", Tr("The six-digit code from UltraAuthenticator or your authenticator app, or one of your recovery codes.")));
        siTotp_ = Input("usSI.totp", "000000", 200.0f);
        form->AddChild(Stack("usSI.totpRow", Tr("Code"), siTotp_));
        auto go = Theme::Button("usSI.signin2", Tr("Sign in"), 110, Theme::ButtonKind::Primary);
        go->onClick = [this]() {
            const std::string code = siTotp_->GetText();
            const bool recovery = code.size() != 6 || !std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isdigit(c); });
            AccountTask<SignedIn>(Tr("Signing in"), [client = AccountClientNow(), email = signInEmail_, password = signInHint_, code, recovery, device = ThisDevice()]() {
                return client.SignIn(email, password, device, recovery ? std::string() : code, recovery ? code : std::string());
            }, [this](const SignedIn& s) { FinishSignIn(s); });
        };
        form->AddChild(go);
    } else if (signInMode_ == "reset") {
        form->AddChild(Theme::Heading("usSI.title", Tr("Reset your password")));
        form->AddChild(Theme::Text("usSI.lead", Tr("A code goes to your address; with it, and your second factor if you have one, you set a new password. Every machine is signed out.")));
        siEmail_ = Input("usSI.email", "anna@example.org");
        siEmail_->SetText(signInEmail_);
        form->AddChild(Stack("usSI.emailRow", Tr("Email"), siEmail_));
        auto send = Theme::Button("usSI.send", Tr("Send the code"), 130);
        send->onClick = [this]() {
            signInEmail_ = siEmail_->GetText();
            AccountTask<bool>(Tr("Sending the code"), [client = AccountClientNow(), email = signInEmail_]() {
                const auto r = client.ForgotPassword(email);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { SetStatus(Tr("If that address has an account, a code is on its way")); });
        };
        form->AddChild(send);
        siCode_ = Input("usSI.code", "000000", 160.0f);
        form->AddChild(Stack("usSI.codeRow", Tr("Code"), siCode_));
        siTotp_ = Input("usSI.totp", Tr("if two-factor is on"), 200.0f);
        form->AddChild(Stack("usSI.totpRow", Tr("Authenticator code"), siTotp_));
        siNewPassword_ = Input("usSI.newPassword", Tr("at least 12 characters"), 360.0f, true);
        form->AddChild(Stack("usSI.newPasswordRow", Tr("New password"), siNewPassword_));
        auto buttons = Theme::Row("usSI.buttons", Theme::kInnerGap, true);
        auto go = Theme::Button("usSI.reset", Tr("Set the password"), 150, Theme::ButtonKind::Primary);
        go->onClick = [this]() {
            signInEmail_ = siEmail_->GetText();
            AccountTask<bool>(Tr("Setting the password"), [client = AccountClientNow(), email = signInEmail_, code = siCode_->GetText(), totp = siTotp_->GetText(),
                                                           password = siNewPassword_->GetText()]() {
                const auto r = client.ResetPassword(email, code, password, totp);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { ShowSignIn("signin"); SetStatus(Tr("The password is set: sign in with it")); });
        };
        buttons->AddChild(go);
        auto back = Theme::Button("usSI.back", Tr("Back to sign in"), 130, Theme::ButtonKind::Ghost);
        back->onClick = [this]() { ShowSignIn("signin"); };
        buttons->AddChild(back);
        form->AddChild(buttons);
    } else if (signInMode_ == "done") {
        form->AddChild(Theme::Heading("usSI.title", TrF("Welcome, {0}", me_ ? me_->profile.Name() : account_.email)));
        form->AddChild(Theme::Text("usSI.lead", signInHint_.empty() ? Tr("Your account is ready. The store on this machine is signed in.") : signInHint_));
        auto buttons = Theme::Row("usSI.buttons", Theme::kInnerGap, true);
        auto tfa = Theme::Button("usSI.tfa", Tr("Add two-factor sign-in"), 180, Theme::ButtonKind::Primary);
        tfa->SetTooltip(Tr("Recommended before the first purchase: UltraAuthenticator or any TOTP app"));
        tfa->onClick = [this]() { ShowAccountSection("security"); ShowPage(kAccount); };
        buttons->AddChild(tfa);
        auto open = Theme::Button("usSI.open", Tr("Open your account"), 150);
        open->onClick = [this]() { ShowAccountSection("profile"); ShowPage(kAccount); };
        buttons->AddChild(open);
        auto store = Theme::Button("usSI.store", Tr("Go to the store"), 130, Theme::ButtonKind::Ghost);
        store->onClick = [this]() { ShowPage(kDiscover); };
        buttons->AddChild(store);
        form->AddChild(buttons);
    } else {
        form->AddChild(Theme::Heading("usSI.title", Tr("Sign in")));
        form->AddChild(Theme::Caption("usSI.lead", Tr("With your UltraStore email and password.")));
        siEmail_ = Input("usSI.email", "anna@example.org");
        siEmail_->SetText(signInEmail_);
        form->AddChild(Stack("usSI.emailRow", Tr("Email"), siEmail_));
        siPassword_ = Input("usSI.password", "", 360.0f, true);
        form->AddChild(Stack("usSI.passwordRow", Tr("Password"), siPassword_));
        auto buttons = Theme::Row("usSI.buttons", Theme::kInnerGap, true);
        auto go = Theme::Button("usSI.signin", Tr("Sign in"), 110, Theme::ButtonKind::Primary);
        go->onClick = [this]() {
            signInEmail_ = siEmail_->GetText();
            const std::string password = siPassword_->GetText();
            if (signInEmail_.empty() || password.empty()) { SetStatus(Tr("The email and the password sign you in"), true); return; }
            SetStatus(Tr("Signing in"));
            std::weak_ptr<int> token = alive_;
            std::thread([this, token, client = AccountClientNow(), email = signInEmail_, password, device = ThisDevice()]() {
                const auto r = client.SignIn(email, password, device);
                app_.PostToUIThread([this, token, r, password]() {
                    if (!token.lock()) return;
                    if (r) { FinishSignIn(r.Value()); return; }
                    if (r.error.detail == "two_factor_required") { signInHint_ = password; ShowSignIn("twofactor"); SetStatus(Tr("Your second factor is asked for")); return; }
                    if (r.error.detail == "email_unverified") { ShowSignIn("verify"); SetStatus(r.error.message, true); return; }
                    SetStatus(r.error.message, true);
                });
            }).detach();
        };
        buttons->AddChild(go);
        auto forgot = Theme::Button("usSI.forgot", Tr("Forgot it?"), 100, Theme::ButtonKind::Ghost);
        forgot->onClick = [this]() { signInEmail_ = siEmail_->GetText(); ShowSignIn("reset"); };
        buttons->AddChild(forgot);
        form->AddChild(buttons);
        form->AddChild(Theme::Rule("usSI.rule"));
        auto create = Theme::Row("usSI.create", Theme::kInnerGap, true);
        create->AddChild(Theme::Text("usSI.new", Tr("New here?"), 13.0f));
        auto signup = Theme::Button("usSI.signup", Tr("Create an account"), 150);
        signup->onClick = [this]() { signInEmail_ = siEmail_->GetText(); ShowSignIn("signup"); };
        create->AddChild(signup);
        form->AddChild(create);
        form->AddChild(Theme::Caption("usSI.privacy", TrF("Signing in sends your email and the password to {0} over HTTPS, nothing else. Passkeys are not here yet.", url)));
    }
    split->AddChild(form);
    signInBody_->AddChild(split);
    signInBody_->InvalidateLayout();
    signInBody_->RequestRedraw();
}

void StoreWindow::FinishSignIn(const SignedIn& s) {
    account_.url = AccountUrl();
    account_.token = s.token;
    account_.userId = s.profile.id;
    account_.email = s.profile.email;
    account_.displayName = s.profile.displayName;
    account_.deviceId = s.device.id;
    if (engine_) {
        if (const auto saved = account_.Save(engine_->Root()); !saved) SetStatus(saved.error.message, true);
    }
    signInHint_ = Tr("Your account is ready. The store on this machine is signed in.");
    me_.reset();
    UpdateAccountPill();
    ShowSignIn("done");
    SetStatus(TrF("Signed in as {0}", s.profile.email));
    RefreshMe();
    SyncAccountLicences();
}

void StoreWindow::SignOutAccount(bool everywhere) {
    if (!account_.SignedIn()) return;
    const std::string token = account_.token;
    AccountTask<bool>(everywhere ? Tr("Signing out everywhere") : Tr("Signing out"), [client = AccountClientNow(), token, everywhere]() {
        if (everywhere) { const auto r = client.SignOutOthers(token); if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail); }
        const auto r = client.SignOut(token);
        if (!r && r.error.detail != "signed_out") return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
        return Result<bool>::Succeed(true);
    }, [this](bool) { ForgetAccount(""); ShowPage(kDiscover); });
}

void StoreWindow::RefreshMe() {
    if (!account_.SignedIn()) return;
    const std::string token = account_.token;
    AccountTask<Me>(Tr("Reading your account"), [client = AccountClientNow(), token]() { return client.GetMe(token); }, [this](const Me& me) {
        me_ = me;
        account_.displayName = me.profile.displayName;
        account_.deviceId = me.device.id;
        UpdateAccountPill();
        SetStatus("");
        if (currentPage_ == kAccount) RebuildAccount();
        if (currentPage_ == kSignIn && signInMode_ == "done") RebuildSignIn();
    });
}

void StoreWindow::SyncAccountLicences() {
    if (!account_.SignedIn() || !storeError_.empty()) return;
    Job job;
    job.kind = JobKind::AccountSync;
    job.title = Tr("Licences in step with the account");
    job.showInQueue = false;
    Enqueue(std::move(job));
}

// ===========================================================================
// The account page
// ===========================================================================
void StoreWindow::BuildAccountPage() {
    std::shared_ptr<UltraCanvasContainer> body;
    BuildScrollPage(kAccount, body);
    auto split = Theme::Row("usA.split", Theme::kSectionGap);
    split->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    accountRail_ = Theme::Column("usA.rail", 2.0f);
    accountRail_->SetElementSize(CSSLayout::Dimension::Px(240), CSSLayout::Dimension::Auto());
    accountRail_->layoutItem.SetFlexShrink(0);
    split->AddChild(accountRail_);
    accountBody_ = Theme::Column("usA.body", Theme::kGap);
    accountBody_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    split->AddChild(accountBody_);
    body->AddChild(split);
}

void StoreWindow::ShowAccountSection(const std::string& section) {
    accountSection_ = section;
    if (section == "notifications" && account_.SignedIn()) {
        AccountTask<std::vector<AccountNotice>>(Tr("Reading the notices"), [client = AccountClientNow(), token = account_.token]() { return client.Notices(token); },
                                                [this](const std::vector<AccountNotice>& list) { notices_ = list; if (accountSection_ == "notifications") RebuildAccount(); });
    }
    RebuildAccount();
}

void StoreWindow::RebuildAccount() {
    if (!accountRail_ || !accountBody_) return;
    accountRail_->ClearChildren();
    accountBody_->ClearChildren();
    railButtons_.clear();
    apName_.reset(); apHandle_.reset(); apLanguage_.reset(); apCountry_.reset(); apYear_.reset(); apAbout_.reset();
    apEmail_.reset(); apEmailPassword_.reset(); apEmailCode_.reset(); apCurrentPassword_.reset(); apNewPassword_.reset(); apTotpCode_.reset(); apDeletePassword_.reset();
    if (!account_.SignedIn()) { accountBody_->AddChild(Theme::Text("usA.none", Tr("Not signed in."))); return; }
    const std::string token = account_.token;

    // The rail: the person, the sections, sign out.
    {
        auto head = Theme::Row("usA.head", Theme::kInnerGap);
        AccountProfile p = me_ ? me_->profile : AccountProfile();
        if (!me_) { p.email = account_.email; p.displayName = account_.displayName; }
        auto avatar = Theme::Button("usA.avatar", p.Initials(), 44, Theme::ButtonKind::Soft, 44);
        head->AddChild(avatar);
        auto who = Theme::Column("usA.who", 0);
        who->AddChild(Theme::Text("usA.name", p.Name(), 14.0f, FontWeight::Bold));
        who->AddChild(Theme::Caption("usA.handle", (p.handle.empty() ? p.email : "@" + p.handle) + (p.createdAt.empty() ? "" : " · " + TrF("since {0}", Day(p.createdAt)))));
        head->AddChild(who);
        accountRail_->AddChild(head);
        accountRail_->AddChild(Theme::Rule("usA.headRule"));
        for (const char* section : kSections) {
            auto b = Theme::Button(std::string("usA.rail.") + section, SectionTitle(section), 220, accountSection_ == section ? Theme::ButtonKind::Soft : Theme::ButtonKind::Ghost);
            b->onClick = [this, s = std::string(section)]() { ShowAccountSection(s); };
            railButtons_[section] = b;
            accountRail_->AddChild(b);
        }
        accountRail_->AddChild(Theme::Rule("usA.railRule"));
        auto out = Theme::Button("usA.signout", Tr("Sign out"), 220, Theme::ButtonKind::Ghost);
        out->onClick = [this]() { SignOutAccount(false); };
        accountRail_->AddChild(out);
    }

    if (!me_) { accountBody_->AddChild(Theme::Caption("usA.loading", Tr("Reading your account…"))); return; }
    const Me& me = *me_;
    const AccountProfile& p = me.profile;
    accountBody_->AddChild(Theme::Heading("usA.title", SectionTitle(accountSection_)));
    if (p.state == "deleting") {
        accountBody_->AddChild(Theme::Text("usA.deleting", TrF("This account is deleted on {0}. Cancel from Privacy and data to keep it.", p.deleteAfter), 13.0f, FontWeight::Bold, Theme::kDanger));
    }

    if (accountSection_ == "profile") {
        accountBody_->AddChild(Theme::Caption("usA.p.lead", Tr("What others see when you review an app, comment on a video or share a playlist. The email, the country and the year of birth are private.")));
        auto form = Theme::Surface("usA.p.form", Theme::kGap);
        apName_ = Input("usA.p.name", p.email, 360.0f); apName_->SetText(p.displayName);
        form->AddChild(FieldRow("usA.p.nameRow", Tr("Display name"), apName_));
        apHandle_ = Input("usA.p.handle", "annak", 240.0f); apHandle_->SetText(p.handle);
        form->AddChild(FieldRow("usA.p.handleRow", Tr("Handle"), apHandle_));
        form->AddChild(FieldRow("usA.p.emailRow", Tr("Email · private"), Theme::Text("usA.p.email", p.email + (p.emailVerified ? "  ·  " + Tr("Verified") : ""), 13.0f)));
        apLanguage_ = Input("usA.p.language", "de", 100.0f); apLanguage_->SetText(p.language);
        form->AddChild(FieldRow("usA.p.languageRow", Tr("Language of the store"), apLanguage_));
        apCountry_ = Input("usA.p.country", "DE", 100.0f); apCountry_->SetText(p.country);
        form->AddChild(FieldRow("usA.p.countryRow", Tr("Country · private, sets VAT and currency"), apCountry_));
        apYear_ = Input("usA.p.year", "1990", 120.0f); apYear_->SetText(p.yearOfBirth ? std::to_string(p.yearOfBirth) : "");
        form->AddChild(FieldRow("usA.p.yearRow", Tr("Year of birth · private, for age ratings"), apYear_));
        apAbout_ = Theme::Pin(std::make_shared<UltraCanvasTextArea>("usA.p.about", 0.0f, 72.0f));
        apAbout_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(72.0f));
        apAbout_->SetWordWrap(true); apAbout_->SetShowLineNumbers(false); apAbout_->SetHighlightSyntax(false); apAbout_->SetFontSize(12.0f);
        apAbout_->SetBackgroundColor(Theme::kPageBackground); apAbout_->SetBorders(1.0f, Theme::kBorder, 4.0f);
        apAbout_->SetText(p.about, false);
        form->AddChild(Theme::Text("usA.p.aboutLabel", Tr("About you · shown on reviews and public playlists"), 12.0f));
        form->AddChild(apAbout_);
        auto buttons = Theme::Row("usA.p.buttons", Theme::kInnerGap, true);
        auto save = Theme::Button("usA.p.save", Tr("Save changes"), 130, Theme::ButtonKind::Primary);
        save->onClick = [this, token]() {
            ProfileChange change;
            change.displayName = apName_->GetText(); change.handle = apHandle_->GetText(); change.language = apLanguage_->GetText();
            change.country = apCountry_->GetText(); change.about = apAbout_->GetText(); change.yearOfBirth = YearOf(apYear_->GetText());
            AccountTask<Me>(Tr("Saving the profile"), [client = AccountClientNow(), token, change]() { return client.UpdateProfile(token, change); },
                            [this](const Me& me) { me_ = me; UpdateAccountPill(); RebuildAccount(); SetStatus(Tr("Profile saved")); });
        };
        buttons->AddChild(save);
        auto discard = Theme::Button("usA.p.discard", Tr("Discard"), 100, Theme::ButtonKind::Ghost);
        discard->onClick = [this]() { RebuildAccount(); };
        buttons->AddChild(discard);
        form->AddChild(buttons);
        accountBody_->AddChild(form);
        // The email moves after a code to the new address.
        auto mail = Theme::Section("usA.p.mail", Tr("Change the email"), Tr("A code goes to the new address; the old one is told."));
        apEmail_ = Input("usA.p.newEmail", Tr("the new address"), 300.0f);
        mail->AddChild(FieldRow("usA.p.newEmailRow", Tr("New email"), apEmail_));
        apEmailPassword_ = Input("usA.p.emailPassword", "", 300.0f, true);
        mail->AddChild(FieldRow("usA.p.emailPasswordRow", Tr("Your password"), apEmailPassword_));
        apEmailCode_ = Input("usA.p.emailCode", "000000", 160.0f);
        mail->AddChild(FieldRow("usA.p.emailCodeRow", Tr("The code sent"), apEmailCode_));
        auto mailButtons = Theme::Row("usA.p.mailButtons", Theme::kInnerGap, true);
        auto send = Theme::Button("usA.p.sendCode", Tr("Send the code"), 130);
        send->onClick = [this, token]() {
            AccountTask<bool>(Tr("Sending the code"), [client = AccountClientNow(), token, email = apEmail_->GetText(), password = apEmailPassword_->GetText()]() {
                const auto r = client.ChangeEmail(token, email, password);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { SetStatus(Tr("The code was sent to the new address")); });
        };
        mailButtons->AddChild(send);
        auto confirm = Theme::Button("usA.p.confirmEmail", Tr("Move the account"), 140, Theme::ButtonKind::Primary);
        confirm->onClick = [this, token]() {
            AccountTask<Me>(Tr("Moving the account"), [client = AccountClientNow(), token, code = apEmailCode_->GetText()]() { return client.VerifyEmailChange(token, code); },
                            [this](const Me& me) { me_ = me; account_.email = me.profile.email; if (engine_) account_.Save(engine_->Root()); UpdateAccountPill(); RebuildAccount(); SetStatus(Tr("The account uses the new address")); });
        };
        mailButtons->AddChild(confirm);
        mail->AddChild(mailButtons);
        accountBody_->AddChild(mail);
    } else if (accountSection_ == "security") {
        accountBody_->AddChild(Theme::Caption("usA.s.lead", Tr("A purchase, a new device or a changed email always asks for the second factor.")));
        auto password = Theme::Section("usA.s.password", Tr("Password"), p.passwordChangedAt.empty() ? "" : TrF("Changed {0}", Day(p.passwordChangedAt)));
        apCurrentPassword_ = Input("usA.s.current", "", 300.0f, true);
        password->AddChild(FieldRow("usA.s.currentRow", Tr("Current password"), apCurrentPassword_));
        apNewPassword_ = Input("usA.s.new", Tr("at least 12 characters"), 300.0f, true);
        password->AddChild(FieldRow("usA.s.newRow", Tr("New password"), apNewPassword_));
        auto change = Theme::Button("usA.s.change", Tr("Change"), 100, Theme::ButtonKind::Primary);
        change->onClick = [this, token]() {
            AccountTask<int>(Tr("Changing the password"), [client = AccountClientNow(), token, current = apCurrentPassword_->GetText(), fresh = apNewPassword_->GetText()]() {
                return client.ChangePassword(token, current, fresh);
            }, [this](int signedOut) { RebuildAccount(); SetStatus(TrN("Password changed; {n} other device signed out", "Password changed; {n} other devices signed out", signedOut)); });
        };
        password->AddChild(change);
        accountBody_->AddChild(password);

        auto tfa = Theme::Section("usA.s.tfa", Tr("Two-factor sign-in"),
                                  p.twoFactor ? TrF("On · time-based codes in your authenticator app · {0} recovery codes unused", p.recoveryCodesLeft)
                                              : Tr("Off · UltraAuthenticator or any TOTP app; recommended before the first purchase"));
        if (!recoveryCodes_.empty()) {
            auto codes = Theme::Surface("usA.s.codes", Theme::kInnerGap, Theme::kAccentFaint);
            codes->AddChild(Theme::Text("usA.s.codes.title", Tr("Your recovery codes - shown once. Keep them in UltraPassword or on paper."), 13.0f, FontWeight::Bold));
            std::string joined;
            for (const std::string& c : recoveryCodes_) joined += (joined.empty() ? "" : "   ") + c;
            codes->AddChild(Theme::Text("usA.s.codes.list", joined, 14.0f, FontWeight::Bold, Theme::kTextPrimary, "monospace"));
            auto kept = Theme::Button("usA.s.codes.kept", Tr("I kept them"), 120);
            kept->onClick = [this]() { recoveryCodes_.clear(); RebuildAccount(); };
            codes->AddChild(kept);
            tfa->AddChild(codes);
        }
        if (!p.twoFactor && !twoFactorSetup_.secret.empty()) {
            tfa->AddChild(Theme::Text("usA.s.setup", Tr("Add this key to your authenticator app, then enter the code it shows:"), 13.0f));
            tfa->AddChild(Theme::Text("usA.s.secret", twoFactorSetup_.secret, 16.0f, FontWeight::Bold, Theme::kTextPrimary, "monospace"));
            auto openApp = Theme::Button("usA.s.openApp", Tr("Open in the authenticator app"), 220, Theme::ButtonKind::Ghost);
            openApp->onClick = [this]() { OpenURL(twoFactorSetup_.otpauthUrl); };
            tfa->AddChild(openApp);
        }
        apTotpCode_ = Input("usA.s.code", "000000", 160.0f);
        tfa->AddChild(FieldRow("usA.s.codeRow", Tr("Code from the app"), apTotpCode_));
        auto tfaButtons = Theme::Row("usA.s.tfaButtons", Theme::kInnerGap, true);
        if (!p.twoFactor) {
            if (twoFactorSetup_.secret.empty()) {
                auto start = Theme::Button("usA.s.start", Tr("Turn on"), 100, Theme::ButtonKind::Primary);
                start->onClick = [this, token]() {
                    AccountTask<TwoFactorSetup>(Tr("Preparing the second factor"), [client = AccountClientNow(), token]() { return client.TwoFactorSetupStart(token); },
                                                [this](const TwoFactorSetup& s) { twoFactorSetup_ = s; RebuildAccount(); });
                };
                tfaButtons->AddChild(start);
            } else {
                auto enable = Theme::Button("usA.s.enable", Tr("Confirm and turn on"), 160, Theme::ButtonKind::Primary);
                enable->onClick = [this, token]() {
                    AccountTask<std::vector<std::string>>(Tr("Turning two-factor on"), [client = AccountClientNow(), token, code = apTotpCode_->GetText()]() { return client.TwoFactorEnable(token, code); },
                                                          [this](const std::vector<std::string>& codes) { recoveryCodes_ = codes; twoFactorSetup_ = TwoFactorSetup(); RefreshMe(); SetStatus(Tr("Two-factor sign-in is on")); });
                };
                tfaButtons->AddChild(enable);
            }
        } else {
            auto newCodes = Theme::Button("usA.s.newCodes", Tr("New recovery codes"), 160);
            newCodes->onClick = [this, token]() {
                AccountTask<std::vector<std::string>>(Tr("Making new recovery codes"), [client = AccountClientNow(), token, code = apTotpCode_->GetText()]() { return client.NewRecoveryCodes(token, code); },
                                                      [this](const std::vector<std::string>& codes) { recoveryCodes_ = codes; RefreshMe(); });
            };
            tfaButtons->AddChild(newCodes);
            auto off = Theme::Button("usA.s.off", Tr("Turn off"), 100, Theme::ButtonKind::Danger);
            off->onClick = [this, token]() {
                AccountTask<bool>(Tr("Turning two-factor off"), [client = AccountClientNow(), token, code = apTotpCode_->GetText()]() {
                    const auto r = client.TwoFactorDisable(token, code);
                    if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                    return Result<bool>::Succeed(true);
                }, [this](bool) { RefreshMe(); SetStatus(Tr("Two-factor sign-in is off")); });
            };
            tfaButtons->AddChild(off);
        }
        tfa->AddChild(tfaButtons);
        accountBody_->AddChild(tfa);

        auto history = Theme::Section("usA.s.history", Tr("Sign-in history"), Tr("The last sign-ins, refused attempts included."));
        auto see = Theme::Button("usA.s.see", Tr("See all"), 100);
        see->onClick = [this, token]() {
            AccountTask<std::vector<AccountDevice>>(Tr("Reading the devices"), [client = AccountClientNow(), token]() { return client.Devices(token); },
                                                    [this](const std::vector<AccountDevice>&) { ShowAccountSection("devices"); });
        };
        history->AddChild(see);
        accountBody_->AddChild(history);
    } else if (accountSection_ == "devices") {
        accountBody_->AddChild(Theme::Caption("usA.d.lead", Tr("Machines signed in to this account. A licence counts the machines it is installed on; signing a machine out frees its seat.")));
        auto list = Theme::Surface("usA.d.list", Theme::kInnerGap);
        int n = 0;
        for (const AccountDevice& d : me.devices) {
            const std::string id = "usA.d.r" + std::to_string(n++);
            auto row = Theme::Row(id, Theme::kGap);
            auto text = Theme::Column(id + ".c", 0);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            text->AddChild(Theme::Text(id + ".t", d.name + (d.current ? "  ·  " + Tr("this device") : ""), 13.0f, FontWeight::Bold));
            std::string seats;
            for (const AccountLicence& l : me.licences) if (l.InstalledOn(d.id)) seats += (seats.empty() ? "" : ", ") + l.app;
            text->AddChild(Theme::Caption(id + ".s", d.os + " · " + d.client + " · " + TrF("last seen {0}", Day(d.lastSeen)) + (seats.empty() ? "" : " · " + seats)));
            row->AddChild(text);
            if (d.current) {
                auto rename = Theme::Button(id + ".rename", Tr("Rename"), 90);
                rename->onClick = [this, token, did = d.id]() {
                    UltraCanvasDialogManager::ShowInputDialog(Tr("The name the account shows for this machine"), Tr("Rename this device"), DefaultDeviceName(), InputType::Text,
                        [this, token, did](DialogResult result, const std::string& name) {
                            if (result != DialogResult::OK || name.empty()) return;
                            AccountTask<bool>(Tr("Renaming the device"), [client = AccountClientNow(), token, did, name]() {
                                const auto r = client.RenameDevice(token, did, name);
                                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                                return Result<bool>::Succeed(true);
                            }, [this](bool) { RefreshMe(); });
                        }, window_.get());
                };
                row->AddChild(rename);
            } else {
                auto out = Theme::Button(id + ".out", Tr("Sign out"), 100);
                out->onClick = [this, token, did = d.id]() {
                    AccountTask<bool>(Tr("Signing the device out"), [client = AccountClientNow(), token, did]() {
                        const auto r = client.SignOutDevice(token, did);
                        if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                        return Result<bool>::Succeed(true);
                    }, [this](bool) { RefreshMe(); });
                };
                row->AddChild(out);
            }
            list->AddChild(row);
            if (n < static_cast<int>(me.devices.size())) list->AddChild(Theme::Rule(id + ".rule"));
        }
        accountBody_->AddChild(list);
        auto all = Theme::Button("usA.d.all", Tr("Sign out every other device"), 220, Theme::ButtonKind::Danger);
        all->onClick = [this, token]() {
            AccountTask<int>(Tr("Signing the other devices out"), [client = AccountClientNow(), token]() { return client.SignOutOthers(token); },
                             [this](int count) { RefreshMe(); SetStatus(TrN("{n} device signed out", "{n} devices signed out", count)); });
        };
        accountBody_->AddChild(all);
    } else if (accountSection_ == "licences") {
        accountBody_->AddChild(Theme::Caption("usA.l.lead", Tr("Every purchase is a licence signed by the payment service; the machines it is on are counted against its seats. A licence bought on another machine is fetched here when the store signs in.")));
        auto list = Theme::Surface("usA.l.list", Theme::kInnerGap);
        int n = 0;
        for (const AccountLicence& l : me.licences) {
            const std::string id = "usA.l.r" + std::to_string(n++);
            auto row = Theme::Row(id, Theme::kGap);
            auto text = Theme::Column(id + ".c", 0);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            std::string name = l.app;
            if (catalog_) for (const Component& c : catalog_->apps) if (c.id == l.app) { name = c.name; break; }
            text->AddChild(Theme::Text(id + ".t", name, 13.0f, FontWeight::Bold));
            Licence terms;
            terms.updates = l.updates; terms.updatesUntil = l.updatesUntil; terms.validUntil = l.validUntil;
            text->AddChild(Theme::Caption(id + ".s", TrF("Bought {0} · {1} · seats {2} of {3}", Day(l.issued), terms.UpdateTerms(), l.seatsUsed, l.seats > 0 ? std::to_string(l.seats) : Tr("unlimited"))
                                                     + (l.validUntil.empty() ? "" : " · " + TrF("valid until {0}", l.validUntil))));
            row->AddChild(text);
            const bool here = l.InstalledOn(me.device.id);
            auto seat = Theme::Button(id + ".seat", here ? Tr("Free this machine's seat") : Tr("Take a seat here"), 170, here ? Theme::ButtonKind::Secondary : Theme::ButtonKind::Primary);
            seat->onClick = [this, token, lid = l.id, here]() {
                AccountTask<AccountLicence>(here ? Tr("Freeing the seat") : Tr("Taking a seat"), [client = AccountClientNow(), token, lid, here]() {
                    return here ? client.UninstallLicence(token, lid) : client.InstallLicence(token, lid);
                }, [this](const AccountLicence&) { RefreshMe(); });
            };
            row->AddChild(seat);
            list->AddChild(row);
            if (n < static_cast<int>(me.licences.size())) list->AddChild(Theme::Rule(id + ".rule"));
        }
        if (n == 0) list->AddChild(Theme::Caption("usA.l.none", Tr("No licence on the account yet. A purchase made while signed in lands here.")));
        accountBody_->AddChild(list);
        auto sync = Theme::Button("usA.l.sync", Tr("Bring this machine's licences in step"), 260);
        sync->onClick = [this]() { SyncAccountLicences(); };
        accountBody_->AddChild(sync);
    } else if (accountSection_ == "notifications") {
        accountBody_->AddChild(Theme::Caption("usA.n.lead", Tr("By email, or on the devices where the store runs. Security mails cannot be turned off.")));
        auto prefs = Theme::Surface("usA.n.prefs", Theme::kInnerGap);
        const std::pair<const char*, std::string> rows[] = {
            {"security", Tr("Security: new device, password, email")}, {"orders", Tr("Orders, invoices, deliveries")}, {"updates", Tr("Updates of apps and games you own")},
            {"follows", Tr("New videos and releases from channels and artists you follow")}, {"replies", Tr("Replies to your reviews and comments")}, {"deals", Tr("Deals of the month")}};
        for (const auto& [key, label] : rows) {
            const std::string id = std::string("usA.n.") + key;
            auto row = Theme::Row(id, Theme::kGap);
            auto text = Theme::Text(id + ".t", label, 13.0f);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(text);
            auto it = p.settings.notifications.find(key);
            const std::string value = it == p.settings.notifications.end() ? std::string("devices") : it->second;
            if (std::string(key) == "security") {
                row->AddChild(Theme::Caption(id + ".v", Tr("Always by email")));
            } else {
                auto drop = Theme::Pin(CreateDropdown(id + ".drop", 0, 0, 220, Theme::kControlHeight));
                int selected = 0, i = 0;
                for (const char* option : {"email", "email_and_devices", "devices", "monthly_email", "off"}) {
                    drop->AddItem(NotificationWord(option), option);
                    if (value == option) selected = i;
                    ++i;
                }
                drop->SetSelectedIndex(selected, false);
                drop->onSelectionChanged = [this, token, key = std::string(key)](int at, const DropdownItem& item) {
                    (void)at;
                    AccountSettings s = me_->profile.settings;
                    s.notifications[key] = item.value;
                    AccountTask<Me>(Tr("Saving the setting"), [client = AccountClientNow(), token, s]() { return client.UpdateSettings(token, s); },
                                    [this](const Me& me) { me_ = me; SetStatus(Tr("Saved")); });
                };
                row->AddChild(drop);
            }
            prefs->AddChild(row);
        }
        accountBody_->AddChild(prefs);
        auto notices = Theme::Section("usA.n.notices", Tr("Notices"), Tr("What the store and the operator sent you; the unread first."));
        int n = 0;
        for (const AccountNotice& notice : notices_) {
            const std::string id = "usA.n.r" + std::to_string(n++);
            auto row = Theme::RowSurface(id, Theme::kGap);
            auto text = Theme::Column(id + ".c", 2.0f);
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            text->AddChild(Theme::Text(id + ".t", notice.subject, 13.0f, notice.Read() ? FontWeight::Normal : FontWeight::Bold));
            text->AddChild(Theme::Caption(id + ".s", Day(notice.createdAt) + " · " + notice.kind + (notice.createdBy == "operator" ? " · " + Tr("from the operator") : "")));
            if (!notice.text.empty()) text->AddChild(Theme::Text(id + ".b", notice.text, 13.0f));
            row->AddChild(text);
            if (!notice.Read()) {
                auto read = Theme::Button(id + ".read", Tr("Mark read"), 100);
                read->onClick = [this, token, nid = notice.id]() {
                    AccountTask<bool>(Tr("Marking it read"), [client = AccountClientNow(), token, nid]() {
                        const auto r = client.MarkNoticeRead(token, nid);
                        if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                        return Result<bool>::Succeed(true);
                    }, [this](bool) { RefreshMe(); ShowAccountSection("notifications"); });
                };
                row->AddChild(read);
            }
            notices->AddChild(row);
        }
        if (n == 0) notices->AddChild(Theme::Caption("usA.n.none", Tr("No notices.")));
        accountBody_->AddChild(notices);
    } else if (accountSection_ == "privacy") {
        accountBody_->AddChild(Theme::Caption("usA.v.lead", Tr("What the account keeps, where, and how to take it with you or end it.")));
        auto keep = Theme::Surface("usA.v.keep", Theme::kInnerGap);
        auto history = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usA.v.history", 0, 0, 600, Theme::kControlHeight,
                                                                      Tr("Listening and watch history on the account (off: each machine keeps its own, mixes are computed there)"), p.settings.historyOnAccount));
        keep->AddChild(history);
        auto publicProfile = Theme::Pin(UltraCanvasCheckbox::CreateCheckbox("usA.v.public", 0, 0, 600, Theme::kControlHeight,
                                                                            Tr("Public profile: reviews, comments and shared playlists show the name and the handle"), p.settings.publicProfile));
        keep->AddChild(publicProfile);
        auto save = Theme::Button("usA.v.save", Tr("Save"), 90, Theme::ButtonKind::Primary);
        save->onClick = [this, token, history, publicProfile]() {
            AccountSettings s = me_->profile.settings;
            s.historyOnAccount = history->IsChecked();
            s.publicProfile = publicProfile->IsChecked();
            AccountTask<Me>(Tr("Saving the settings"), [client = AccountClientNow(), token, s]() { return client.UpdateSettings(token, s); },
                            [this](const Me& me) { me_ = me; SetStatus(Tr("Saved")); });
        };
        keep->AddChild(save);
        accountBody_->AddChild(keep);

        auto archive = Theme::Section("usA.v.archive", Tr("Download your data"),
                                      Tr("Profile, devices, licences, notices and history as one JSON file, built at once and released by the operator within a day; downloaded once."));
        if (me.archive) {
            const AccountArchive& a = *me.archive;
            archive->AddChild(Theme::Caption("usA.v.archive.state", a.state == "released" ? Tr("Released: download it now, it can be downloaded once.") : TrF("Requested {0}, due {1}", Day(a.requestedAt), a.due)));
            if (a.state == "released") {
                auto download = Theme::Button("usA.v.download", Tr("Download the archive"), 160, Theme::ButtonKind::Primary);
                download->onClick = [this, token, aid = a.id]() {
                    AccountTask<std::string>(Tr("Downloading the archive"), [client = AccountClientNow(), token, aid]() { return client.DownloadArchive(token, aid); },
                                             [this](const std::string& json) {
                                                 // The program's own file: written directly, beside the registry.
                                                 const std::string path = PathToUtf8(PathFromUtf8(engine_->Root()) / "account-archive.json");
                                                 std::ofstream out(PathFromUtf8(path), std::ios::binary | std::ios::trunc);
                                                 out.write(json.data(), static_cast<std::streamsize>(json.size()));
                                                 if (!out) { SetStatus(TrF("Cannot write {0}", path), true); return; }
                                                 SetStatus(TrF("The archive is in {0}", path));
                                                 RefreshMe();
                                             });
                };
                archive->AddChild(download);
            }
        } else {
            auto request = Theme::Button("usA.v.request", Tr("Request the archive"), 150);
            request->onClick = [this, token]() {
                AccountTask<AccountArchive>(Tr("Requesting the archive"), [client = AccountClientNow(), token]() { return client.RequestArchive(token); },
                                            [this](const AccountArchive&) { RefreshMe(); SetStatus(Tr("The archive is being prepared; a notice comes when it is released")); });
            };
            archive->AddChild(request);
        }
        accountBody_->AddChild(archive);

        auto del = Theme::Section("usA.v.delete", Tr("Delete the account"),
                                  TrF("Ends the subscriptions at their period's end, keeps the invoices for the years the law asks, and removes everything else after {0} days. Bought apps keep working on the machines they are on, without updates.", kDeletionGraceDays));
        if (p.state == "deleting") {
            auto cancel = Theme::Button("usA.v.cancel", Tr("Keep my account"), 150, Theme::ButtonKind::Primary);
            cancel->onClick = [this, token]() {
                AccountTask<Me>(Tr("Keeping the account"), [client = AccountClientNow(), token]() { return client.CancelDeletion(token); },
                                [this](const Me& me) { me_ = me; RebuildAccount(); SetStatus(Tr("The deletion is cancelled")); });
            };
            del->AddChild(cancel);
        } else {
            apDeletePassword_ = Input("usA.v.password", Tr("your password"), 300.0f, true);
            del->AddChild(FieldRow("usA.v.passwordRow", Tr("Your password"), apDeletePassword_));
            auto go = Theme::Button("usA.v.go", Tr("Delete my account"), 160, Theme::ButtonKind::Danger);
            go->onClick = [this, token]() {
                UltraCanvasAlert::Confirm(TrF("Delete the account? It is kept for {0} days in case you change your mind, then removed.", kDeletionGraceDays), "UltraStore", [this, token](bool yes) {
                    if (!yes) return;
                    AccountTask<Me>(Tr("Requesting the deletion"), [client = AccountClientNow(), token, password = apDeletePassword_->GetText()]() { return client.RequestDeletion(token, password); },
                                    [this](const Me& me) { me_ = me; RebuildAccount(); SetStatus(TrF("The account is deleted on {0}", me.profile.deleteAfter)); });
                }, window_.get());
            };
            del->AddChild(go);
        }
        accountBody_->AddChild(del);
    }
    accountBody_->InvalidateLayout();
    accountBody_->RequestRedraw();
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

} // namespace UltraStore::Ui
