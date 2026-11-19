// Apps/UltraStoreOperator/ui/OperatorUsersPage.cpp
// The Users page after the design's SuperUsers artboard, on the account
// service's operator API (Server/UltraStoreAccount; Operator.h, the user
// calls): the counts, the users by state with a search, one user's file
// with the devices, licences, notices, archives and history, and the
// actions - a notice, sign out every device, reset two-factor, release the
// archive, suspend or reinstate, delete on request - each with a reason
// that goes into the audit log. What the operator cannot see is not here:
// the password, the payment method, the year of birth, the listening
// history.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "OperatorWindow.h"

#include "OperatorTheme.h"

#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdlib>

using namespace UltraCanvas;

namespace UltraStoreOperator::Ui {

namespace {

constexpr const char* kUsers = "users";

int Number(const std::string& text, int fallback = 0) {
    if (text.empty()) return fallback;
    return std::atoi(text.c_str());   // locale-ok: digits only
}

std::string StateWord(const std::string& state) {
    if (state == "active") return Tr("active");
    if (state == "suspended") return Tr("suspended");
    if (state == "deleting") return Tr("deleting");
    if (state == "deleted") return Tr("deleted");
    return state;
}

std::string RequestWord(const std::string& request) {
    if (request == "archive") return Tr("Data archive");
    if (request == "deletion") return Tr("Deletion");
    return {};
}

std::string Day(const std::string& iso) { return iso.size() >= 10 ? iso.substr(0, 10) : iso; }

} // namespace

void OperatorWindow::BuildUsersPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kUsers, Tr("Users"), Tr("The people with an UltraStore account: who they are as far as the store needs to know, where they stand, and what they asked of us. The operator sees no password, no card, no listening history."), header, body);
    uSearch_ = Theme::Pin(CreateTextInput("uoU.search", 0, 0, 260, Theme::kControlHeight));
    uSearch_->SetPlaceholder(Tr("Search users: email, handle, name"));
    uSearch_->onTextChanged = [this](const std::string&) { RefreshUsers(); };
    header->AddChild(uSearch_);
    auto refresh = Theme::Button("uoU.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshUsers(); };
    header->AddChild(refresh);

    uStats_ = Theme::Row("uoU.stats", Theme::kGap, true);
    body->AddChild(uStats_);

    auto list = Theme::Section("uoU.list", Tr("All users"));
    uFilters_ = Theme::Row("uoU.filters", Theme::kInnerGap, true);
    list->AddChild(uFilters_);
    usersList_ = Theme::Column("uoU.rows", Theme::kInnerGap);
    list->AddChild(usersList_);
    body->AddChild(list);

    userFileBox_ = Theme::Section("uoU.file", Tr("User file"));
    userFileBox_->SetVisible(false);
    body->AddChild(userFileBox_);
}

void OperatorWindow::RefreshUsers() {
    if (!Ready(false)) return;
    if (!settings_.Accounts()) {
        usersList_->ClearChildren();
        usersList_->AddChild(Theme::Caption("uoU.noservice", Tr("No account service: name it and its operator token in Settings. Without one the store has no accounts, and this page nothing to show.")));
        Relayout();
        return;
    }
    const std::string query = uSearch_ ? uSearch_->GetText() : std::string();
    Fetch<UsersPage>(Tr("Reading the users"), [client = Client(), state = usersFilter_, query]() { return client.Users(state == "all" ? std::string() : state, query); },
                     [this](const UsersPage& page) { users_ = page.users; userStats_ = page.stats; ShowUsers(); });
}

void OperatorWindow::ShowUsers() {
    uStats_->ClearChildren();
    const UserStats& st = userStats_;
    uStats_->AddChild(Theme::Stat("uoU.st.accounts", Tr("Accounts"), std::to_string(st.accounts), TrF("+{0} in 30 days · {1} % with two-factor", st.newIn30Days, st.twoFactorPercent)));
    uStats_->AddChild(Theme::Stat("uoU.st.archives", Tr("Data archive requests"), std::to_string(st.archiveRequests), TrF("{0} due within a day · the service builds them, you release them", st.archiveRequestsDue)));
    uStats_->AddChild(Theme::Stat("uoU.st.deletions", Tr("Deletion requests"), std::to_string(st.deletionRequests), Tr("30-day grace, then the service deletes; invoices kept")));
    uStats_->AddChild(Theme::Stat("uoU.st.suspended", Tr("Suspended"), std::to_string(st.suspended), Tr("signed out everywhere, sign-in refused"), st.suspended > 0));

    uFilters_->ClearChildren();
    for (const auto& [id, label] : std::vector<std::pair<std::string, std::string>>{{"all", Tr("All")}, {"active", Tr("Active")}, {"suspended", Tr("Suspended")},
                                                                                     {"deleting", Tr("Deleting")}, {"requests", Tr("With a request")}}) {
        auto b = Theme::Button("uoU.f." + id, label, 120, usersFilter_ == id ? Theme::ButtonKind::Primary : Theme::ButtonKind::Secondary);
        b->onClick = [this, id]() { usersFilter_ = id; RefreshUsers(); };
        uFilters_->AddChild(b);
    }
    auto count = Theme::Caption("uoU.count", TrN("{n} shown", "{n} shown", static_cast<long long>(users_.size())));
    uFilters_->AddChild(count);

    usersList_->ClearChildren();
    int n = 0;
    for (const User& u : users_) {
        const std::string id = "uoU.r" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", u.Name(), 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("{0} · {1} · {2} · since {3} · two-factor {4} · {5} · {6}", u.id, u.email, u.country.empty() ? Tr("no country") : u.country, Day(u.createdAt),
                                                       u.twoFactor ? Tr("on") : Tr("off"), TrN("{n} device", "{n} devices", u.devices), TrN("{n} licence", "{n} licences", u.licences))));
        row->AddChild(text);
        if (!u.request.empty()) row->AddChild(Theme::Pill(id + ".q", RequestWord(u.request) + (u.state == "deleting" && !u.deleteAfter.empty() ? " · " + u.deleteAfter : std::string()), Theme::Tint::Amber));
        row->AddChild(Theme::Pill(id + ".p", StateWord(u.state), Theme::TintFor(u.state == "deleting" ? "paused" : u.state)));
        auto open = Theme::Button(id + ".open", Tr("Open file"), 90);
        open->onClick = [this, uid = u.id]() { OpenUser(uid); };
        row->AddChild(open);
        usersList_->AddChild(row);
        if (n < static_cast<int>(users_.size())) usersList_->AddChild(Theme::Rule(id + ".rule"));
    }
    if (n == 0) usersList_->AddChild(Theme::Caption("uoU.none", Tr("No user in this state.")));
    Relayout();
}

void OperatorWindow::OpenUser(const std::string& id) {
    Fetch<UserFile>(Tr("Opening the user's file"), [client = Client(), id]() { return client.GetUser(id); }, [this](const UserFile& f) {
        userFile_ = f;
        haveUserFile_ = true;
        ShowUserFile();
    });
}

void OperatorWindow::ShowUserFile() {
    userFileBox_->ClearChildren();
    userFileBox_->SetVisible(haveUserFile_);
    if (!haveUserFile_) { Relayout(); return; }
    const UserFile& f = userFile_;
    const User& u = f.user;
    auto head = Theme::Row("uoUF.head", Theme::kInnerGap, true);
    auto title = Theme::Text("uoUF.title", u.Name(), 18.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont);
    title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    head->AddChild(title);
    head->AddChild(Theme::Pill("uoUF.state", StateWord(u.state), Theme::TintFor(u.state == "deleting" ? "paused" : u.state)));
    auto close = Theme::Button("uoUF.close", Tr("Close"), 80);
    close->onClick = [this]() { haveUserFile_ = false; ShowUserFile(); };
    head->AddChild(close);
    userFileBox_->AddChild(head);
    userFileBox_->AddChild(Theme::Caption("uoUF.facts", TrF("{0} · {1} · {2} · since {3} · two-factor {4}{5}", u.id, u.email, u.country.empty() ? Tr("no country") : u.country, Day(u.createdAt),
                                                             u.twoFactor ? Tr("on") : Tr("off"), u.state == "suspended" ? " · " + TrF("suspended: {0}{1}", u.suspendedReason, u.suspendedUntil.empty() ? Tr(" until reinstated") : " " + TrF("until {0}", u.suspendedUntil)) : std::string())));
    if (u.state == "deleting") userFileBox_->AddChild(Theme::Caption("uoUF.deleting", TrF("Asked for the deletion {0}; the service deletes on {1} unless the person cancels. Delete now only on their request.", Day(u.deletionRequestedAt), u.deleteAfter)));

    // Devices
    userFileBox_->AddChild(Theme::Text("uoUF.devices.title", TrN("{n} device signed in", "{n} devices signed in", static_cast<long long>(f.devices.size())), 13.0f, FontWeight::Bold));
    int n = 0;
    for (const UserDevice& d : f.devices) {
        userFileBox_->AddChild(Theme::Caption("uoUF.d" + std::to_string(n++), d.name + " · " + d.os + " · " + d.client + " · " + TrF("last seen {0}", Day(d.lastSeen))));
    }
    // Licences
    userFileBox_->AddChild(Theme::Text("uoUF.lic.title", TrN("{n} licence", "{n} licences", static_cast<long long>(f.licences.size())), 13.0f, FontWeight::Bold));
    n = 0;
    for (const UserLicence& l : f.licences) {
        userFileBox_->AddChild(Theme::Caption("uoUF.l" + std::to_string(n++), l.app + " · " + l.id + " · " + TrF("bought {0}", l.issued) + " · " + TrF("seats {0} of {1}", l.seatsUsed, l.seats)
                                                                               + (l.validUntil.empty() ? "" : " · " + TrF("valid until {0}", l.validUntil))));
    }
    // Archives
    for (const UserArchive& a : f.archives) {
        auto row = Theme::Row("uoUF.a." + a.id, Theme::kInnerGap);
        auto text = Theme::Caption("uoUF.a." + a.id + ".t", TrF("Data archive {0}: {1}, requested {2}, due {3}", a.id, a.state, Day(a.requestedAt), a.due));
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(text);
        if (a.state == "built") {
            auto release = Theme::Button("uoUF.a." + a.id + ".release", Tr("Release"), 90, Theme::ButtonKind::Primary);
            release->onClick = [this, aid = a.id]() {
                Fetch<UserArchive>(Tr("Releasing the archive"), [client = Client(), aid]() { return client.ReleaseArchive(aid); },
                                   [this](const UserArchive&) { SetStatus(Tr("Released: the person is told by notice and mail")); }, [this]() { OpenUser(userFile_.user.id); });
            };
            row->AddChild(release);
        }
        userFileBox_->AddChild(row);
    }
    // Notices
    userFileBox_->AddChild(Theme::Text("uoUF.n.title", TrN("{n} notice", "{n} notices", static_cast<long long>(f.notices.size())), 13.0f, FontWeight::Bold));
    n = 0;
    for (const UserNotice& notice : f.notices) {
        if (n++ >= 8) break;
        userFileBox_->AddChild(Theme::Caption("uoUF.n" + std::to_string(n), Day(notice.createdAt) + " · " + notice.kind + " · " + notice.subject + (notice.readAt.empty() ? " · " + Tr("unread") : "")));
    }
    // History
    userFileBox_->AddChild(Theme::Text("uoUF.h.title", Tr("History"), 13.0f, FontWeight::Bold));
    n = 0;
    for (const AuditRow& r : f.history) {
        if (n++ >= 12) break;
        userFileBox_->AddChild(Theme::Caption("uoUF.h" + std::to_string(n), r.Line()));
    }
    for (const UserSignIn& si : f.signIns) {
        if (n++ >= 20) break;
        userFileBox_->AddChild(Theme::Caption("uoUF.si" + std::to_string(n), Day(si.at) + " · " + (si.ok ? Tr("signed in") : TrF("refused ({0})", si.reason)) + " · " + si.device + " · " + si.ip));
    }
    userFileBox_->AddChild(Theme::Caption("uoUF.cannot", Tr("Every action here is a row in the audit log with the reason and the user's id, and a notice to the person. What the operator cannot see: the password, the payment method, the listening and watch history, the private year of birth.")));

    // Actions
    auto actions = Theme::Column("uoUF.actions", Theme::kInnerGap);
    uReason_ = Theme::Pin(CreateTextInput("uoUF.reason", 0, 0, 560, Theme::kControlHeight));
    uReason_->SetPlaceholder(Tr("the reason, for the audit log and the notice"));
    actions->AddChild(Theme::Field("uoUF.reasonRow", Tr("Reason"), uReason_));
    uKind_ = Theme::Pin(CreateDropdown("uoUF.kind", 0, 0, 200, Theme::kControlHeight));
    for (const char* kind : {"notice", "security", "warning", "information_request"}) uKind_->AddItem(kind, kind);
    uKind_->SetSelectedIndex(0, false);
    actions->AddChild(Theme::Field("uoUF.kindRow", Tr("Notice kind"), uKind_));
    uSubject_ = Theme::Pin(CreateTextInput("uoUF.subject", 0, 0, 560, Theme::kControlHeight));
    actions->AddChild(Theme::Field("uoUF.subjectRow", Tr("Subject"), uSubject_));
    uText_ = Theme::Multiline("uoUF.text", 80.0f, Tr("the notice's text"));
    actions->AddChild(uText_);
    auto buttons = Theme::Row("uoUF.buttons", Theme::kInnerGap, true);
    const std::string uid = u.id;
    auto notify = Theme::Button("uoUF.notify", Tr("Send a notice"), 120, Theme::ButtonKind::Primary);
    notify->onClick = [this, uid]() {
        if (uSubject_->GetText().empty()) { SetStatus(Tr("A notice has a subject"), true); return; }
        const int at = uKind_->GetSelectedIndex();
        Fetch<UserNotice>(Tr("Sending the notice"), [client = Client(), uid, kind = at >= 0 ? uKind_->GetItem(at)->value : std::string("notice"), subject = uSubject_->GetText(), text = uText_->GetText()]() {
            return client.NotifyUser(uid, kind, subject, text);
        }, [this](const UserNotice& n) { uSubject_->SetText(""); uText_->SetText("", false); SetStatus(TrF("Notice {0} sent", n.id)); }, [this, uid]() { OpenUser(uid); });
    };
    buttons->AddChild(notify);
    auto signOut = Theme::Button("uoUF.signout", Tr("Sign out every device"), 160);
    signOut->onClick = [this, uid]() {
        Fetch<int>(Tr("Signing the devices out"), [client = Client(), uid, reason = uReason_->GetText()]() { return client.SignOutUser(uid, reason); },
                   [this](int count) { SetStatus(TrN("{n} device signed out", "{n} devices signed out", count)); }, [this, uid]() { OpenUser(uid); });
    };
    buttons->AddChild(signOut);
    auto reset = Theme::Button("uoUF.reset2fa", Tr("Reset two-factor (identity checked)"), 240);
    reset->onClick = [this, uid]() {
        Confirm(Tr("Reset the second factor? Only after the person's identity was checked another way. Every device is signed out and the person is told."), [this, uid]() {
            Fetch<bool>(Tr("Resetting two-factor"), [client = Client(), uid, reason = uReason_->GetText()]() -> Result<bool> {
                const auto r = client.ResetUserTwoFactor(uid, reason);
                if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                return Result<bool>::Succeed(true);
            }, [this](bool) { SetStatus(Tr("Two-factor reset; the person signs in with the password and sets it up again")); }, [this, uid]() { OpenUser(uid); });
        });
    };
    buttons->AddChild(reset);
    if (u.state == "suspended") {
        auto reinstate = Theme::Button("uoUF.reinstate", Tr("Reinstate"), 110, Theme::ButtonKind::Primary);
        reinstate->onClick = [this, uid]() {
            Fetch<User>(Tr("Reinstating"), [client = Client(), uid, reason = uReason_->GetText()]() { return client.ReinstateUser(uid, reason); },
                        [this](const User&) { SetStatus(Tr("Reinstated")); }, [this, uid]() { OpenUser(uid); RefreshUsers(); });
        };
        buttons->AddChild(reinstate);
    } else if (u.state != "deleted") {
        uDays_ = Theme::Pin(CreateTextInput("uoUF.days", 0, 0, 80, Theme::kControlHeight));
        uDays_->SetPlaceholder(Tr("days"));
        buttons->AddChild(uDays_);
        auto suspend = Theme::Button("uoUF.suspend", Tr("Suspend the account"), 160, Theme::ButtonKind::Danger);
        suspend->onClick = [this, uid]() {
            if (uReason_->GetText().empty()) { SetStatus(Tr("A suspension names its reason"), true); return; }
            Confirm(Tr("Suspend the account? The person is signed out everywhere and refused until reinstated or the days are over."), [this, uid]() {
                Fetch<User>(Tr("Suspending"), [client = Client(), uid, reason = uReason_->GetText(), days = Number(uDays_->GetText())]() { return client.SuspendUser(uid, reason, days); },
                            [this](const User&) { SetStatus(Tr("Suspended")); }, [this, uid]() { OpenUser(uid); RefreshUsers(); });
            });
        };
        buttons->AddChild(suspend);
    }
    if (u.state == "deleting") {
        auto del = Theme::Button("uoUF.delete", Tr("Delete now (on request)"), 180, Theme::ButtonKind::Danger);
        del->onClick = [this, uid]() {
            Confirm(Tr("Carry out the deletion now? The person asked for it; the account is removed, the invoices stay with the payment service."), [this, uid]() {
                Fetch<bool>(Tr("Deleting the account"), [client = Client(), uid, reason = uReason_->GetText()]() -> Result<bool> {
                    const auto r = client.DeleteUser(uid, reason);
                    if (!r) return Result<bool>::Failure(r.error.code, r.error.message, r.error.detail);
                    return Result<bool>::Succeed(true);
                }, [this](bool) { haveUserFile_ = false; ShowUserFile(); SetStatus(Tr("Deleted")); }, [this]() { RefreshUsers(); });
            });
        };
        buttons->AddChild(del);
    }
    actions->AddChild(buttons);
    userFileBox_->AddChild(actions);
    Relayout();
}

} // namespace UltraStoreOperator::Ui
