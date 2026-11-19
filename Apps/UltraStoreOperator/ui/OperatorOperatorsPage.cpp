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
    signInInfo_ = Theme::Caption("uoSI.info", "");
    section->AddChild(signInInfo_);
    auto buttons = Theme::Row("uoSI.buttons", Theme::kInnerGap, true);
    auto signIn = Theme::Button("uoSI.signin", Tr("Sign in"), 100, Theme::ButtonKind::Primary);
    signIn->onClick = [this]() { SignInNow(); };
    buttons->AddChild(signIn);
    auto signOut = Theme::Button("uoSI.signout", Tr("Sign out"), 100);
    signOut->onClick = [this]() { SignOutNow(); };
    buttons->AddChild(signOut);
    section->AddChild(buttons);
    body->AddChild(section);
}

void OperatorWindow::SignInNow() {
    if (sUrl_) { settings_.url = sUrl_->GetText(); while (!settings_.url.empty() && settings_.url.back() == '/') settings_.url.pop_back(); }
    if (settings_.url.empty()) { SetStatus(Tr("Name the marketplace service's URL first"), true); return; }
    const std::string email = sEmail_->GetText(), password = sPassword_->GetText();
    if (email.empty() || password.empty()) { SetStatus(Tr("The sign-in needs the email and the password"), true); return; }
    Fetch<OperatorSignIn>(Tr("Signing in"), [client = Client(), email, password]() { return client.SignIn(email, password); }, [this](const OperatorSignIn& in) {
        settings_.token = in.token;
        settings_.name = in.account.Name();
        settings_.role = in.account.role;
        sPassword_->SetText("");
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
        if (admin && a.id != whoami_.id) {
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
        operatorsList_->AddChild(own);
    }
    Relayout();
}

} // namespace UltraStoreOperator::Ui
