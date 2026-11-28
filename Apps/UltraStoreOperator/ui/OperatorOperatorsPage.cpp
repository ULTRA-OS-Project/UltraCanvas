// Apps/UltraStoreOperator/ui/OperatorOperatorsPage.cpp
// The operator accounts after the design's SuperOperators artboard: the
// Settings page's sign-in (an email and a password at the marketplace
// service, which answers with a session that becomes the token in the
// settings and reaches the account service too), who is signed in with the
// role in the sidebar, and the Operators page - the accounts with their
// roles, a new account, a role changed, an account disabled or enabled
// (an admin's; anyone reads the list and changes their own name and
// password). The service refuses what the role does not allow; the window
// hides the admin's controls from the others.
// Version: 0.2.0 - the second factor (setup, on, off, recovery codes), the forgotten password, an admin's reset of another's second factor
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "OperatorWindow.h"

#include "OperatorTheme.h"

#include "UltraLocalisation/UltraLocalisation.h"

using namespace UltraCanvas;

namespace UltraStoreOperator::Ui {

namespace {

constexpr const char* kOperators = "operators";
constexpr const char* kSettings = "settings";

} // namespace

// ===========================================================================
// Who am I: the name and the role into the settings and the sidebar
// ===========================================================================
void OperatorWindow::RefreshWhoAmI() {
    if (!settings_.Complete()) return;
    Fetch<WhoAmI>(Tr("Asking who is signed in"), [client = Client()]() { return client.Me(); }, [this](const WhoAmI& me) {
        whoami_ = me;
        haveWhoAmI_ = true;
        if (!me.bootstrap) {
            settings_.name = me.name;
            settings_.role = me.role;
        } else {
            settings_.role = "admin";
        }
        if (const auto saved = settings_.Save(settingsPath_); !saved) SetStatus(saved.error.message, true);
        if (sName_) sName_->SetText(settings_.name);
        UpdateSidebarNote("");
        if (signInInfo_) signInInfo_->SetText(me.bootstrap ? Tr("The static operator token: the bootstrap admin. Make the first admin account below and sign in with it.")
                                                            : TrF("Signed in as {0} ({1}), {2}", me.name, me.id, RoleWord(me.role)));
        if (operatorsAdd_) operatorsAdd_->SetVisible(me.May("operators.manage"));
        ShowOperators();
        Relayout();
    });
}

// ===========================================================================
// The sign-in section of the Settings page
// ===========================================================================
void OperatorWindow::BuildSignInSection(const std::shared_ptr<Container>& body) {
    auto section = Theme::Section("uoSI", Tr("Sign in"));
    section->AddChild(Theme::Caption("uoSI.hint", Tr("An operator account at the marketplace service: the email and the password. The session it answers with is the token above, lasts the service's hours and reaches the account service too; the static token stays the bootstrap admin's.")));
    sEmail_ = Theme::Pin(CreateTextInput("uoSI.email", 0, 0, 320, Theme::kControlHeight));
    sEmail_->SetPlaceholder("you@example.org");
    section->AddChild(Theme::Field("uoSI.emailRow", Tr("Email"), sEmail_));
    sPassword_ = Theme::Pin(CreatePasswordInput("uoSI.password", 0, 0, 320, Theme::kControlHeight));
    section->AddChild(Theme::Field("uoSI.passwordRow", Tr("Password"), sPassword_));
    sCode_ = Theme::Pin(CreateTextInput("uoSI.code", 0, 0, 200, Theme::kControlHeight));
    sCode_->SetPlaceholder(Tr("when your account has a second factor"));
    section->AddChild(Theme::Field("uoSI.codeRow", Tr("Authenticator code"), sCode_));
    signInInfo_ = Theme::Caption("uoSI.info", "");
    section->AddChild(signInInfo_);
    auto buttons = Theme::Row("uoSI.buttons", Theme::kInnerGap, true);
    auto signIn = Theme::Button("uoSI.signin", Tr("Sign in"), 100, Theme::ButtonKind::Primary);
    signIn->onClick = [this]() { SignInNow(); };
    buttons->AddChild(signIn);
    auto signOut = Theme::Button("uoSI.signout", Tr("Sign out"), 100);
    signOut->onClick = [this]() { SignOutNow(); };
    buttons->AddChild(signOut);
    auto forgot = Theme::Button("uoSI.forgot", Tr("Forgot the password"), 160);
    forgot->onClick = [this]() { ForgotPasswordNow(); };
    buttons->AddChild(forgot);
    section->AddChild(buttons);
    // The reset, shown once a code was asked for.
    resetBox_ = Theme::Column("uoSI.reset", Theme::kInnerGap);
    resetBox_->AddChild(Theme::Caption("uoSI.reset.hint", Tr("The code from the mail, and the new password; the authenticator code above too when your account has a second factor.")));
    sResetCode_ = Theme::Pin(CreateTextInput("uoSI.reset.code", 0, 0, 140, Theme::kControlHeight));
    resetBox_->AddChild(Theme::Field("uoSI.reset.codeRow", Tr("Mailed code"), sResetCode_));
    sResetPassword_ = Theme::Pin(CreatePasswordInput("uoSI.reset.password", 0, 0, 320, Theme::kControlHeight));
    sResetPassword_->SetPlaceholder(Tr("12 characters or more"));
    resetBox_->AddChild(Theme::Field("uoSI.reset.passwordRow", Tr("New password"), sResetPassword_));
    auto reset = Theme::Button("uoSI.reset.go", Tr("Set the new password"), 170, Theme::ButtonKind::Primary);
    reset->onClick = [this]() { ResetPasswordNow(); };
    resetBox_->AddChild(reset);
    resetBox_->SetVisible(false);
    section->AddChild(resetBox_);
    body->AddChild(section);
}

void OperatorWindow::ForgotPasswordNow() {
    if (sUrl_) { settings_.url = sUrl_->GetText(); while (!settings_.url.empty() && settings_.url.back() == '/') settings_.url.pop_back(); }
    const std::string email = sEmail_->GetText();
    if (settings_.url.empty() || email.empty()) { SetStatus(Tr("The service's URL and your email are needed for a reset code"), true); return; }
    Fetch<int>(Tr("Asking for a reset code"), [client = Client(), email]() { return client.ForgotPassword(email); }, [this](int minutes) {
        SetStatus(TrF("If that is an operator's address, a code is in its mailbox for {0} minutes", minutes));
        resetBox_->SetVisible(true);
        Relayout();
    });
}

void OperatorWindow::ResetPasswordNow() {
    const std::string email = sEmail_->GetText(), code = sResetCode_->GetText(), password = sResetPassword_->GetText(), second = sCode_->GetText();
    if (email.empty() || code.empty() || password.size() < 12) { SetStatus(Tr("The email, the mailed code and a new password of 12 characters or more"), true); return; }
    Fetch<int>(Tr("Setting the new password"), [client = Client(), email, code, password, second]() -> Result<int> {
        const auto r = client.ResetPassword(email, code, password, second);
        if (!r) return Result<int>::Failure(r.error.code, r.error.detail == "two_factor_required" ? Tr("Your account has a second factor: the authenticator code goes in the field above, then the mailed code again (ask for a new one)") : r.error.message, r.error.detail);
        return Result<int>::Succeed(0);
    }, [this](int) {
        sResetCode_->SetText(""); sResetPassword_->SetText("");
        resetBox_->SetVisible(false);
        SetStatus(Tr("The password is changed: sign in with it"));
        Relayout();
    });
}

void OperatorWindow::SignInNow() {
    if (sUrl_) { settings_.url = sUrl_->GetText(); while (!settings_.url.empty() && settings_.url.back() == '/') settings_.url.pop_back(); }
    if (settings_.url.empty()) { SetStatus(Tr("Name the marketplace service's URL first"), true); return; }
    const std::string email = sEmail_->GetText(), password = sPassword_->GetText(), code = sCode_->GetText();
    if (email.empty() || password.empty()) { SetStatus(Tr("The sign-in needs the email and the password"), true); return; }
    Fetch<OperatorSignIn>(Tr("Signing in"), [client = Client(), email, password, code]() -> Result<OperatorSignIn> {
        auto in = client.SignIn(email, password, code);
        if (!in && in.error.detail == "two_factor_required") in.error.message = Tr("Your account has a second factor: the six-digit code from your authenticator app, or a recovery code, goes in the code field");
        return in;
    }, [this](const OperatorSignIn& in) {
        settings_.token = in.token;
        settings_.name = in.account.Name();
        settings_.role = in.account.role;
        sPassword_->SetText("");
        sCode_->SetText("");
        if (const auto saved = settings_.Save(settingsPath_); !saved) { SetStatus(saved.error.message, true); return; }
        LoadSettingsPage();
        SetStatus(TrF("Signed in as {0}, {1}; the session lasts {2} hours", settings_.name, RoleWord(settings_.role), in.hours));
        UpdateSidebarNote("");
    }, [this]() { RefreshWhoAmI(); });
}

void OperatorWindow::SignOutNow() {
    if (!settings_.SignedIn()) { SetStatus(Tr("No session to end: the token is the static one"), true); return; }
    settings_.token.clear();
    settings_.role.clear();
    haveWhoAmI_ = false;
    whoami_ = WhoAmI();
    if (const auto saved = settings_.Save(settingsPath_); !saved) { SetStatus(saved.error.message, true); return; }
    LoadSettingsPage();
    if (signInInfo_) signInInfo_->SetText("");
    UpdateSidebarNote("");
    SetStatus(Tr("Signed out; the session is forgotten here (it ends at the service by itself)"));
    ShowPage(kSettings);
}

// ===========================================================================
// The Operators page
// ===========================================================================
void OperatorWindow::BuildOperatorsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kOperators, Tr("Operators"), Tr("Who sits at this window, and what each may do. An admin makes the accounts and gives the roles; the service refuses what a role does not allow, and the audit log names the person."), header, body);
    auto refresh = Theme::Button("uoO.refresh", Tr("Refresh"), 90);
    refresh->onClick = [this]() { RefreshOperators(); };
    header->AddChild(refresh);

    auto roles = Theme::Section("uoO.roles", Tr("The roles"));
    roles->AddChild(Theme::Caption("uoO.roles.admin", Tr("admin - everything, the operator accounts included.")));
    roles->AddChild(Theme::Caption("uoO.roles.reviewer", Tr("reviewer - reads the merchants, reviews and releases the submissions, publishes the catalog; reads the reports, the users and the audit log.")));
    roles->AddChild(Theme::Caption("uoO.roles.support", Tr("support - handles the reports, sends notices, acts for a user (a notice, sign out everywhere, release the archive); reads the rest.")));
    roles->AddChild(Theme::Caption("uoO.roles.auditor", Tr("auditor - reads everything, changes nothing.")));
    body->AddChild(roles);

    operatorsList_ = Theme::Section("uoO.list", Tr("The accounts"));
    body->AddChild(operatorsList_);

    operatorsAdd_ = Theme::Section("uoO.add", Tr("A new account"));
    oEmail_ = Theme::Pin(CreateTextInput("uoO.email", 0, 0, 320, Theme::kControlHeight));
    oEmail_->SetPlaceholder("name@example.org");
    operatorsAdd_->AddChild(Theme::Field("uoO.emailRow", Tr("Email"), oEmail_));
    oName_ = Theme::Pin(CreateTextInput("uoO.name", 0, 0, 320, Theme::kControlHeight));
    oName_->SetPlaceholder(Tr("shown in the sidebar and the audit log"));
    operatorsAdd_->AddChild(Theme::Field("uoO.nameRow", Tr("Name"), oName_));
    oRole_ = Theme::Pin(CreateDropdown("uoO.role", 0, 0, 200, Theme::kControlHeight));
    for (const char* role : kRoles) oRole_->AddItem(RoleWord(role), role);
    oRole_->SetSelectedIndex(2, false);   // support
    operatorsAdd_->AddChild(Theme::Field("uoO.roleRow", Tr("Role"), oRole_));
    oPassword_ = Theme::Pin(CreatePasswordInput("uoO.password", 0, 0, 320, Theme::kControlHeight));
    oPassword_->SetPlaceholder(Tr("12 characters or more; the person changes it after the first sign-in"));
    operatorsAdd_->AddChild(Theme::Field("uoO.passwordRow", Tr("Password"), oPassword_));
    auto add = Theme::Button("uoO.addButton", Tr("Add the account"), 140, Theme::ButtonKind::Primary);
    add->onClick = [this]() {
        const int at = oRole_->GetSelectedIndex();
        const std::string role = at >= 0 ? oRole_->GetItem(at)->value : std::string("support");
        if (oEmail_->GetText().empty() || oPassword_->GetText().size() < 12) { SetStatus(Tr("An account has an email and a password of 12 characters or more"), true); return; }
        Fetch<OperatorAccount>(Tr("Adding the account"), [client = Client(), email = oEmail_->GetText(), name = oName_->GetText(), role, password = oPassword_->GetText()]() {
            return client.AddOperator(email, name, role, password);
        }, [this](const OperatorAccount& a) {
            oEmail_->SetText(""); oName_->SetText(""); oPassword_->SetText("");
            SetStatus(TrF("Added {0} as {1}, {2}", a.Name(), a.id, RoleWord(a.role)));
        }, [this]() { RefreshOperators(); });
    };
    operatorsAdd_->AddChild(add);
    operatorsAdd_->SetVisible(false);
    body->AddChild(operatorsAdd_);
}

void OperatorWindow::RefreshOperators() {
    if (!haveWhoAmI_) RefreshWhoAmI();
    Fetch<std::vector<OperatorAccount>>(Tr("Reading the operator accounts"), [client = Client()]() { return client.Operators(); },
                                        [this](const std::vector<OperatorAccount>& list) { operators_ = list; ShowOperators(); });
}

void OperatorWindow::ShowOperators() {
    if (!operatorsList_) return;
    operatorsList_->ClearChildren();
    const bool admin = haveWhoAmI_ && whoami_.May("operators.manage");
    if (operators_.empty()) {
        operatorsList_->AddChild(Theme::Caption("uoO.none", admin ? Tr("No accounts yet: the static token is the only operator. Make the first admin below.") : Tr("No accounts yet.")));
    }
    int n = 0;
    for (const OperatorAccount& a : operators_) {
        const std::string id = "uoO.r" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap, true);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", a.Name() + (a.id == whoami_.id ? " " + Tr("(you)") : std::string()), 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("{0} · {1} · made {2} by {3}{4}", a.id, a.email, a.createdAt.substr(0, 10), a.createdBy,
                                                       a.lastSignin.empty() ? Tr(" · never signed in") : TrF(" · last sign-in {0}", Age(a.lastSignin)) + " " + Tr("ago"))));
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".role", RoleWord(a.role), a.role == "admin" ? Theme::Tint::Blue : Theme::Tint::Grey));
        row->AddChild(Theme::Pill(id + ".state", a.active ? Tr("active") : Tr("disabled"), a.active ? Theme::Tint::Green : Theme::Tint::Red));
        if (a.twoFactor) row->AddChild(Theme::Pill(id + ".2fa", Tr("2FA"), Theme::Tint::Blue));
        if (admin && a.id != whoami_.id) {
            if (a.twoFactor) {
                auto reset2fa = Theme::Button(id + ".reset2fa", Tr("Reset 2FA"), 90);
                reset2fa->onClick = [this, oid = a.id, who = a.Name()]() {
                    Confirm(TrF("Clear the second factor of {0}? Only after the identity was checked another way; the next sign-in takes the password alone.", who), [this, oid]() {
                        Fetch<OperatorAccount>(Tr("Clearing the second factor"), [client = Client(), oid]() { return client.ResetOperatorTwoFactor(oid); },
                                               [this](const OperatorAccount& changed) { SetStatus(TrF("The second factor of {0} is cleared", changed.Name())); }, [this]() { RefreshOperators(); });
                    });
                };
                row->AddChild(reset2fa);
            }
            auto role = Theme::Pin(CreateDropdown(id + ".setRole", 0, 0, 130, Theme::kControlHeight));
            int selected = 0, k = 0;
            for (const char* r : kRoles) { role->AddItem(RoleWord(r), r); if (a.role == r) selected = k; ++k; }
            role->SetSelectedIndex(selected, false);
            role->onSelectionChanged = [this, oid = a.id](int, const DropdownItem& item) {
                Fetch<OperatorAccount>(Tr("Changing the role"), [client = Client(), oid, newRole = item.value]() { return client.UpdateOperator(oid, "", newRole, ""); },
                                       [this](const OperatorAccount& changed) { SetStatus(TrF("{0} is now {1}", changed.Name(), RoleWord(changed.role))); }, [this]() { RefreshOperators(); });
            };
            row->AddChild(role);
            auto toggle = Theme::Button(id + ".toggle", a.active ? Tr("Disable") : Tr("Enable"), 90, a.active ? Theme::ButtonKind::Danger : Theme::ButtonKind::Secondary);
            toggle->onClick = [this, oid = a.id, active = a.active]() {
                auto go = [this, oid, active]() {
                    Fetch<OperatorAccount>(active ? Tr("Disabling the account") : Tr("Enabling the account"), [client = Client(), oid, active]() { return client.UpdateOperator(oid, "", "", "", active ? 1 : 0); },
                                           [this](const OperatorAccount& changed) { SetStatus(changed.active ? TrF("{0} enabled", changed.Name()) : TrF("{0} disabled; the session is refused from now", changed.Name())); },
                                           [this]() { RefreshOperators(); });
                };
                if (active) Confirm(Tr("Disable the account? The person is refused from the next call on, until enabled again."), go);
                else go();
            };
            row->AddChild(toggle);
        }
        operatorsList_->AddChild(row);
    }
    // One's own name and password.
    if (haveWhoAmI_ && !whoami_.bootstrap) {
        auto own = Theme::Column("uoO.own", Theme::kInnerGap);
        own->AddChild(Theme::Text("uoO.own.t", Tr("Your own account"), 13.0f, FontWeight::Bold));
        auto fields = Theme::Row("uoO.own.f", Theme::kInnerGap, true);
        auto name = Theme::Pin(CreateTextInput("uoO.own.name", 0, 0, 220, Theme::kControlHeight));
        name->SetPlaceholder(Tr("a new name"));
        fields->AddChild(name);
        auto password = Theme::Pin(CreatePasswordInput("uoO.own.password", 0, 0, 220, Theme::kControlHeight));
        password->SetPlaceholder(Tr("a new password"));
        fields->AddChild(password);
        auto save = Theme::Button("uoO.own.save", Tr("Change"), 90);
        save->onClick = [this, nm = name.get(), pw = password.get()]() {
            if (nm->GetText().empty() && pw->GetText().empty()) { SetStatus(Tr("Nothing to change"), true); return; }
            if (!pw->GetText().empty() && pw->GetText().size() < 12) { SetStatus(Tr("A password is 12 characters or more"), true); return; }
            Fetch<OperatorAccount>(Tr("Changing your account"), [client = Client(), oid = whoami_.id, newName = nm->GetText(), newPassword = pw->GetText()]() {
                return client.UpdateOperator(oid, newName, "", newPassword);
            }, [this, pw](const OperatorAccount& changed) {
                pw->SetText("");
                settings_.name = changed.Name();
                if (const auto saved = settings_.Save(settingsPath_); !saved) SetStatus(saved.error.message, true);
                if (sName_) sName_->SetText(settings_.name);
                UpdateSidebarNote("");
                SetStatus(Tr("Your account is changed"));
            }, [this]() { RefreshOperators(); });
        };
        fields->AddChild(save);
        own->AddChild(fields);
        ShowTwoFactor(own);
        operatorsList_->AddChild(own);
    }
    Relayout();
}

// One's own second factor: off - the setup (the secret and the URL for the
// app) and the first code; on - a new set of recovery codes, or off, with a code.
void OperatorWindow::ShowTwoFactor(const std::shared_ptr<Container>& into) {
    const OperatorAccount* me = nullptr;
    for (const OperatorAccount& a : operators_) if (a.id == whoami_.id) me = &a;
    if (!me) return;
    auto box = Theme::Column("uoO.2fa", Theme::kInnerGap);
    box->AddChild(Theme::Text("uoO.2fa.t", me->twoFactor ? Tr("Two-factor sign-in is on") : Tr("Two-factor sign-in is off"), 13.0f, FontWeight::Bold));
    auto setupInfo = Theme::Caption("uoO.2fa.info", me->twoFactor ? TrN("{n} recovery code left; each signs you in once when the app is gone.", "{n} recovery codes left; each signs you in once when the app is gone.", me->recoveryCodesLeft)
                                                                  : Tr("An authenticator app (UltraAuthenticator or any other) gives a six-digit code at every sign-in. Set it up, then type the app's first code."));
    box->AddChild(setupInfo);
    auto row = Theme::Row("uoO.2fa.row", Theme::kInnerGap, true);
    auto code = Theme::Pin(CreateTextInput("uoO.2fa.code", 0, 0, 140, Theme::kControlHeight));
    code->SetPlaceholder(Tr("the app's code"));
    if (!me->twoFactor) {
        auto setup = Theme::Button("uoO.2fa.setup", Tr("Set up"), 90);
        setup->onClick = [this, info = setupInfo.get()]() {
            Fetch<struct TwoFactorSetup>(Tr("Setting the authenticator up"), [client = Client()]() { return client.TwoFactorSetup(); }, [this, info](const struct TwoFactorSetup& setup) {
                info->SetText(TrF("Add the account to your authenticator app: the secret {0}, or the URL {1}. Then type the app's first code and turn it on.", setup.secret, setup.otpauthUrl));
                Relayout();
            });
        };
        row->AddChild(setup);
        row->AddChild(code);
        auto enable = Theme::Button("uoO.2fa.enable", Tr("Turn on"), 90, Theme::ButtonKind::Primary);
        enable->onClick = [this, c = code.get()]() {
            if (c->GetText().empty()) { SetStatus(Tr("The app's first code turns it on"), true); return; }
            Fetch<std::vector<std::string>>(Tr("Turning two-factor sign-in on"), [client = Client(), code = c->GetText()]() { return client.TwoFactorEnable(code); }, [this](const std::vector<std::string>& codes) {
                std::string text = Tr("Two-factor sign-in is on. Your recovery codes - each signs you in once when the app is gone; keep them somewhere safe, they are not shown again:");
                for (const std::string& rc : codes) text += "\n" + rc;
                Alert(text, false);
            }, [this]() { RefreshOperators(); });
        };
        row->AddChild(enable);
    } else {
        row->AddChild(code);
        auto fresh = Theme::Button("uoO.2fa.codes", Tr("New recovery codes"), 150);
        fresh->onClick = [this, c = code.get()]() {
            Fetch<std::vector<std::string>>(Tr("Making new recovery codes"), [client = Client(), code = c->GetText()]() { return client.NewRecoveryCodes(code); }, [this](const std::vector<std::string>& codes) {
                std::string text = Tr("Your new recovery codes; the old ones are void. Keep them somewhere safe, they are not shown again:");
                for (const std::string& rc : codes) text += "\n" + rc;
                Alert(text, false);
            }, [this]() { RefreshOperators(); });
        };
        row->AddChild(fresh);
        auto disable = Theme::Button("uoO.2fa.disable", Tr("Turn off"), 90, Theme::ButtonKind::Danger);
        disable->onClick = [this, c = code.get()]() {
            Confirm(Tr("Turn two-factor sign-in off? The password alone signs you in afterwards."), [this, code = c->GetText()]() {
                Fetch<int>(Tr("Turning two-factor sign-in off"), [client = Client(), code]() -> Result<int> {
                    const auto r = client.TwoFactorDisable(code);
                    if (!r) return Result<int>::Failure(r.error.code, r.error.message, r.error.detail);
                    return Result<int>::Succeed(0);
                }, [this](int) { SetStatus(Tr("Two-factor sign-in is off")); }, [this]() { RefreshOperators(); });
            });
        };
        row->AddChild(disable);
    }
    box->AddChild(row);
    into->AddChild(box);
}

} // namespace UltraStoreOperator::Ui
