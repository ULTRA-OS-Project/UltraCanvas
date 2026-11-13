// Apps/UltraStoreOperator/ui/OperatorPages.cpp
// The Merchants page with a merchant's file, the App review page with the
// queue, one submission's checks and the decision, the publish; the
// Reports page with one report's findings and its closing; the Terms and
// notices page with the versions, the enforcement policy and a notice to
// send. The shell and the other pages are OperatorWindow.cpp.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "OperatorWindow.h"

#include "OperatorTheme.h"

#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

using namespace UltraCanvas;

namespace UltraStoreOperator::Ui {

namespace {

constexpr const char* kMerchants = "merchants";
constexpr const char* kReview = "review";
constexpr const char* kReports = "reports";
constexpr const char* kTerms = "terms";

bool Waiting(const Submission& s) { return s.state == "checked" || s.state == "failed_checks" || s.state == "in_review"; }

int Number(const std::string& text, int fallback = 0) {
    if (text.empty()) return fallback;
    return std::atoi(text.c_str());   // locale-ok: digits only
}

std::string Joined(const std::vector<std::string>& words, const char* glue = ", ") {
    std::string out;
    for (const std::string& w : words) out += (out.empty() ? "" : glue) + w;
    return out;
}

std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) {
        while (!w.empty() && w.back() == ',') w.pop_back();
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

// The state's word as the pills say it.
std::string StateWord(const std::string& state) {
    if (state == "checked") return Tr("ready");
    if (state == "failed_checks") return Tr("check failed");
    if (state == "in_review") return Tr("in review");
    if (state == "changes_requested") return Tr("changes requested");
    if (state == "released") return Tr("released");
    if (state == "rejected") return Tr("rejected");
    if (state == "withdrawn") return Tr("withdrawn");
    if (state == "active") return Tr("active");
    if (state == "paused") return Tr("paused");
    if (state == "suspended") return Tr("suspended");
    if (state == "open") return Tr("open");
    if (state == "assigned") return Tr("assigned");
    if (state == "resolved") return Tr("resolved");
    if (state == "dismissed") return Tr("dismissed");
    return state;
}

std::string KindWord(const std::string& kind) {
    if (kind == "announcement") return Tr("announcement");
    if (kind == "reminder") return Tr("reminder");
    if (kind == "information_request") return Tr("information request");
    if (kind == "warning") return Tr("warning");
    if (kind == "suspension") return Tr("suspension");
    return kind;
}

} // namespace

// ===========================================================================
// Merchants
// ===========================================================================
void OperatorWindow::BuildMerchantsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kMerchants, Tr("Merchants"), Tr("The publishers who run a shop in UltraStore: who they are, what they submitted, and where they stand with the terms."), header, body);
    auto refresh = Theme::Button("uoM.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshMerchants(); };
    header->AddChild(refresh);

    auto list = Theme::Section("uoM.list", Tr("All merchants"));
    merchantsList_ = Theme::Column("uoM.rows", Theme::kInnerGap);
    list->AddChild(merchantsList_);
    body->AddChild(list);

    merchantFileBox_ = Theme::Section("uoM.file", Tr("Merchant file"));
    merchantFileBox_->SetVisible(false);
    body->AddChild(merchantFileBox_);

    auto add = Theme::Section("uoM.add", Tr("Register a merchant"));
    add->AddChild(Theme::Caption("uoM.add.hint", Tr("The merchant's key is the public half of the key that signs its submissions (`ultrastoreadmin key export`), 64 hex characters. The id is theirs to put into market.merchant_id.")));
    mName_ = Theme::Pin(CreateTextInput("uoM.name", 0, 0, 320, Theme::kControlHeight));
    add->AddChild(Theme::Field("uoM.nameRow", Tr("Name"), mName_));
    mId_ = Theme::Pin(CreateTextInput("uoM.id", 0, 0, 200, Theme::kControlHeight));
    mId_->SetPlaceholder(Tr("made by the service when empty"));
    add->AddChild(Theme::Field("uoM.idRow", Tr("Id"), mId_));
    mContact_ = Theme::Pin(CreateTextInput("uoM.contact", 0, 0, 320, Theme::kControlHeight));
    mContact_->SetPlaceholder(Tr("an address, or the merchant's web site"));
    add->AddChild(Theme::Field("uoM.contactRow", Tr("Contact"), mContact_));
    mKey_ = Theme::Pin(CreateTextInput("uoM.key", 0, 0, 560, Theme::kControlHeight));
    add->AddChild(Theme::Field("uoM.keyRow", Tr("Key"), mKey_));
    auto registerButton = Theme::Button("uoM.register", Tr("Register"), 110, Theme::ButtonKind::Primary);
    registerButton->onClick = [this]() {
        if (mName_->GetText().empty() || mKey_->GetText().size() != 64) { SetStatus(Tr("A name and the 64-character key register a merchant"), true); return; }
        Fetch<Merchant>(Tr("Registering the merchant"),
            [client = Client(), id = mId_->GetText(), name = mName_->GetText(), contact = mContact_->GetText(), key = mKey_->GetText()]() {
                return client.RegisterMerchant(id, name, contact, key);
            }, [this](const Merchant& m) {
                mId_->SetText(""); mName_->SetText(""); mContact_->SetText(""); mKey_->SetText("");
                SetStatus(TrF("Registered {0} as {1}", m.name, m.id));
            }, [this]() { RefreshMerchants(); });
    };
    add->AddChild(registerButton);
    body->AddChild(add);
}

void OperatorWindow::RefreshMerchants() {
    if (!Ready(false)) return;
    Fetch<std::vector<Merchant>>(Tr("Reading the merchants"), [client = Client()]() { return client.Merchants(); }, [this](const std::vector<Merchant>& list) {
        merchants_ = list;
        ShowMerchants();
    });
}

void OperatorWindow::ShowMerchants() {
    merchantsList_->ClearChildren();
    int n = 0;
    for (const Merchant& m : merchants_) {
        const std::string id = "uoM.r" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", m.name, 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("{0} · {1} · terms {2} of {3} · {4}", m.id, m.contact.empty() ? Tr("no contact") : m.contact, m.termsAccepted, m.termsInForce,
                                                       TrN("{n} strike", "{n} strikes", m.strikes))));
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".p", StateWord(m.standing), Theme::TintFor(m.standing)));
        auto open = Theme::Button(id + ".open", Tr("Open file"), 90);
        open->onClick = [this, mid = m.id]() { OpenMerchant(mid); };
        row->AddChild(open);
        merchantsList_->AddChild(row);
        if (n < static_cast<int>(merchants_.size())) merchantsList_->AddChild(Theme::Rule(id + ".rule"));
    }
    if (n == 0) merchantsList_->AddChild(Theme::Caption("uoM.none", Tr("No merchant registered yet.")));
    Relayout();
}

void OperatorWindow::OpenMerchant(const std::string& id) {
    Fetch<MerchantFile>(Tr("Opening the merchant's file"), [client = Client(), id]() { return client.GetMerchant(id); }, [this](const MerchantFile& f) {
        merchantFile_ = f;
        haveMerchantFile_ = true;
        ShowMerchantFile();
    });
}

void OperatorWindow::ShowMerchantFile() {
    merchantFileBox_->ClearChildren();
    merchantFileBox_->SetVisible(haveMerchantFile_);
    if (!haveMerchantFile_) { Relayout(); return; }
    const MerchantFile& f = merchantFile_;
    const Merchant& m = f.merchant;
    auto head = Theme::Row("uoMF.head", Theme::kInnerGap, true);
    auto title = Theme::Text("uoMF.title", m.name, 18.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont);
    title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    head->AddChild(title);
    head->AddChild(Theme::Pill("uoMF.standing", StateWord(m.standing), Theme::TintFor(m.standing)));
    auto close = Theme::Button("uoMF.close", Tr("Close"), 80);
    close->onClick = [this]() { haveMerchantFile_ = false; ShowMerchantFile(); };
    head->AddChild(close);
    merchantFileBox_->AddChild(head);
    merchantFileBox_->AddChild(Theme::Caption("uoMF.facts", TrF("{0} · merchant since {1} · contact {2} · {3} · terms {4} accepted of {5} in force", m.id, m.createdAt.substr(0, 10),
                                                                 m.contact.empty() ? Tr("none") : m.contact, TrN("{n} strike", "{n} strikes", m.strikes), m.termsAccepted, m.termsInForce)));
    merchantFileBox_->AddChild(Theme::Caption("uoMF.key", TrF("Key {0}", m.key)));
    if (!f.notes.empty()) merchantFileBox_->AddChild(Theme::Caption("uoMF.notes", f.notes));

    // Submissions
    merchantFileBox_->AddChild(Theme::Text("uoMF.subs.title", TrN("{n} submission", "{n} submissions", static_cast<long long>(f.submissions.size())), 13.0f, FontWeight::Bold));
    int n = 0;
    for (const Submission& s : f.submissions) {
        const std::string id = "uoMF.s" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Caption(id + ".t", s.id + "  " + s.Name() + " " + s.version + "  " + s.createdAt.substr(0, 10));
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".p", StateWord(s.state), Theme::TintFor(s.state)));
        auto open = Theme::Button(id + ".open", Tr("Review"), 80);
        open->onClick = [this, sid = s.id]() { ShowPage(kReview); OpenSubmission(sid); };
        row->AddChild(open);
        merchantFileBox_->AddChild(row);
    }

    // History: the acceptances and the suspensions
    merchantFileBox_->AddChild(Theme::Text("uoMF.history.title", Tr("History"), 13.0f, FontWeight::Bold));
    n = 0;
    for (const Acceptance& a : f.acceptances)
        merchantFileBox_->AddChild(Theme::Caption("uoMF.a" + std::to_string(n++), TrF("{0}  terms {1} accepted by {2}", a.acceptedAt, a.version, a.acceptedBy)));
    for (const Suspension& s : f.suspensions)
        merchantFileBox_->AddChild(Theme::Caption("uoMF.p" + std::to_string(n++),
            TrF("{0}  suspended by {1}{2}{3}{4}", s.start, s.createdBy, s.clause.empty() ? "" : " · " + TrF("clause {0}", s.clause),
                s.end.empty() ? " · " + Tr("until reinstated") : " · " + TrF("until {0}", s.end), s.Lifted() ? " · " + TrF("lifted {0}", s.liftedAt) : "") +
            (s.reason.empty() ? "" : "\n" + s.reason)));
    if (n == 0) merchantFileBox_->AddChild(Theme::Caption("uoMF.h.none", Tr("No acceptance and no suspension yet.")));

    // Actions: a notice or a warning, a suspension, a reinstatement
    merchantFileBox_->AddChild(Theme::Text("uoMF.act.title", Tr("Act"), 13.0f, FontWeight::Bold));
    merchantFileBox_->AddChild(Theme::Caption("uoMF.act.hint", Tr("The merchant is told why, what stops, and how to come back. Everything here goes into the audit log; a warning counts as a strike and names its clause.")));
    mKind_ = Theme::Pin(CreateDropdown("uoMF.kind", 0, 0, 220, Theme::kControlHeight));
    for (const char* kind : kNoticeKinds) mKind_->AddItem(KindWord(kind), kind);
    mKind_->SetSelectedIndex(0);
    merchantFileBox_->AddChild(Theme::Field("uoMF.kindRow", Tr("Notice kind"), mKind_));
    mSubject_ = Theme::Pin(CreateTextInput("uoMF.subject", 0, 0, 480, Theme::kControlHeight));
    merchantFileBox_->AddChild(Theme::Field("uoMF.subjectRow", Tr("Subject"), mSubject_));
    mClause_ = Theme::Pin(CreateTextInput("uoMF.clause", 0, 0, 160, Theme::kControlHeight));
    mClause_->SetPlaceholder("7.2");
    merchantFileBox_->AddChild(Theme::Field("uoMF.clauseRow", Tr("Clause"), mClause_));
    mText_ = Theme::Multiline("uoMF.text", 80, Tr("the evidence, and how to come back"));
    merchantFileBox_->AddChild(mText_);
    auto noticeRow = Theme::Row("uoMF.noticeRow", Theme::kInnerGap, true);
    auto send = Theme::Button("uoMF.send", Tr("Send the notice"), 130, Theme::ButtonKind::Primary);
    send->onClick = [this, mid = m.id]() {
        const auto* item = mKind_->GetSelectedItem();
        if (!item || mSubject_->GetText().empty()) { SetStatus(Tr("A notice has a kind and a subject"), true); return; }
        if (item->value == "warning" && mClause_->GetText().empty()) { SetStatus(Tr("A warning names the clause it is about"), true); return; }
        const std::string kind = item->value;
        auto go = [this, mid, kind]() {
            Fetch<NoticeReceipt>(Tr("Sending the notice"),
                [client = Client(), kind, subject = mSubject_->GetText(), text = mText_->GetText(), clause = mClause_->GetText(), mid]() {
                    return client.SendNotice(kind, subject, text, clause, {mid});
                }, [this](const NoticeReceipt& r) {
                    SetStatus(r.suspended.empty() ? TrF("Sent {0}", r.notice.id) : TrF("Sent {0}; the policy suspended the merchant on it", r.notice.id));
                }, [this, mid]() { OpenMerchant(mid); });
        };
        if (kind == "warning") Confirm(TrF("Warn {0} under clause {1}? It counts as a strike; the policy may suspend the shop on it.", mid, mClause_->GetText()), go);
        else go();
    };
    noticeRow->AddChild(send);
    merchantFileBox_->AddChild(noticeRow);

    auto suspendRow = Theme::Row("uoMF.suspendRow", Theme::kInnerGap, true);
    mReason_ = Theme::Pin(CreateTextInput("uoMF.reason", 0, 0, 360, Theme::kControlHeight));
    mReason_->SetPlaceholder(Tr("the reason, for the file and the merchant"));
    suspendRow->AddChild(mReason_);
    mDays_ = Theme::Pin(CreateTextInput("uoMF.days", 0, 0, 80, Theme::kControlHeight));
    mDays_->SetPlaceholder(Tr("days"));
    suspendRow->AddChild(mDays_);
    if (f.Suspended()) {
        auto reinstate = Theme::Button("uoMF.reinstate", Tr("Reinstate"), 110, Theme::ButtonKind::Primary);
        reinstate->onClick = [this, mid = m.id]() {
            Fetch<Merchant>(Tr("Reinstating"), [client = Client(), mid, reason = mReason_->GetText()]() { return client.ReinstateMerchant(mid, reason); },
                            [this](const Merchant& r) { SetStatus(TrF("{0} is {1}", r.name, StateWord(r.standing))); }, [this, mid]() { OpenMerchant(mid); RefreshMerchants(); });
        };
        suspendRow->AddChild(reinstate);
    } else {
        auto suspend = Theme::Button("uoMF.suspend", Tr("Suspend the shop"), 140, Theme::ButtonKind::Danger);
        suspend->onClick = [this, mid = m.id, name = m.name]() {
            const int days = Number(mDays_->GetText());
            Confirm(days > 0 ? TrF("Suspend {0} for {1}? Its apps leave the next catalog.", name, TrN("{n} day", "{n} days", days))
                             : TrF("Suspend {0} until reinstated? Its apps leave the next catalog.", name), [this, mid, days]() {
                Fetch<Merchant>(Tr("Suspending"), [client = Client(), mid, clause = mClause_->GetText(), reason = mReason_->GetText(), days]() {
                    return client.SuspendMerchant(mid, clause, reason, days);
                }, [this](const Merchant& r) { SetStatus(TrF("{0} is {1}", r.name, StateWord(r.standing))); }, [this, mid]() { OpenMerchant(mid); RefreshMerchants(); });
            });
        };
        suspendRow->AddChild(suspend);
    }
    merchantFileBox_->AddChild(suspendRow);
    Relayout();
}

// ===========================================================================
// App review
// ===========================================================================
void OperatorWindow::BuildReviewPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kReview, Tr("App review"), Tr("Every new app and every release a merchant submits, checked by the machine and then by a person, released with the operator's next catalog publish."), header, body);
    auto refresh = Theme::Button("uoR.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshReview(); };
    header->AddChild(refresh);

    auto publish = Theme::Section("uoR.publish", Tr("The catalog"));
    auto publishRow = Theme::Row("uoR.publishRow", Theme::kInnerGap, true);
    releasedLabel_ = Theme::Caption("uoR.released", "");
    releasedLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    publishRow->AddChild(releasedLabel_);
    auto publishButton = Theme::Button("uoR.publishNow", Tr("Publish the catalog"), 150, Theme::ButtonKind::Primary);
    publishButton->onClick = [this]() { PublishNow(); };
    publishRow->AddChild(publishButton);
    publish->AddChild(publishRow);
    body->AddChild(publish);

    reviewFilters_ = Theme::Row("uoR.filters", Theme::kInnerGap, true);
    body->AddChild(reviewFilters_);
    auto list = Theme::Section("uoR.list", Tr("Submissions"));
    reviewList_ = Theme::Column("uoR.rows", Theme::kInnerGap);
    list->AddChild(reviewList_);
    body->AddChild(list);

    submissionBox_ = Theme::Section("uoR.one", Tr("Submission"));
    submissionBox_->SetVisible(false);
    body->AddChild(submissionBox_);
}

void OperatorWindow::RefreshReview() {
    if (!Ready(false)) return;
    const OperatorClient client = Client();
    RunJob(Tr("Reading the review queue"), [this, client]() {
        JobOutcome out;
        const auto subs = client.Submissions();
        if (!subs) { out.ok = false; out.message = subs.error.message; return out; }
        const auto released = client.GetReleased();
        if (!released) { out.ok = false; out.message = released.error.message; return out; }
        out.apply = [this, subs = subs.Value(), released = released.Value()]() {
            submissions_ = subs;
            released_ = released;
            int waiting = 0;
            for (const Submission& s : submissions_) if (Waiting(s)) ++waiting;
            sidebar_->SetBadge(kReview, waiting);
            ShowReview();
        };
        return out;
    });
}

void OperatorWindow::ShowReview() {
    releasedLabel_->SetText(TrF("Last publish: sequence {0}. {1} go into the next one, from merchants in good standing and apps not suspended.", released_.published,
                                TrN("{n} released app", "{n} released apps", static_cast<long long>(released_.entries.size()))));
    reviewFilters_->ClearChildren();
    int waiting = 0, changes = 0, released = 0;
    for (const Submission& s : submissions_) {
        if (Waiting(s)) ++waiting;
        else if (s.state == "changes_requested") ++changes;
        else if (s.state == "released") ++released;
    }
    auto chip = [this](const char* key, const std::string& label) {
        auto b = Theme::Button(std::string("uoR.f.") + key, label, 150, reviewFilter_ == key ? Theme::ButtonKind::Primary : Theme::ButtonKind::Secondary);
        b->onClick = [this, key]() { reviewFilter_ = key; ShowReview(); };
        reviewFilters_->AddChild(b);
    };
    chip("waiting", TrF("Waiting {0}", waiting));
    chip("changes", TrF("Changes requested {0}", changes));
    chip("released", TrF("Released {0}", released));
    chip("all", TrF("All {0}", submissions_.size()));

    reviewList_->ClearChildren();
    int n = 0;
    for (const Submission& s : submissions_) {
        const bool show = reviewFilter_ == "all" || (reviewFilter_ == "waiting" && Waiting(s)) || (reviewFilter_ == "changes" && s.state == "changes_requested") ||
                          (reviewFilter_ == "released" && s.state == "released");
        if (!show) continue;
        const std::string id = "uoR.r" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", s.Name() + " " + s.version + (s.answers.empty() ? "" : " · " + TrF("answers {0}", s.answers)), 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("{0} · {1} · submitted {2} ago", s.id, s.merchant, Age(s.createdAt))));
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".p", StateWord(s.state), Theme::TintFor(s.state)));
        auto open = Theme::Button(id + ".open", Tr("Open"), 70);
        open->onClick = [this, sid = s.id]() { OpenSubmission(sid); };
        row->AddChild(open);
        reviewList_->AddChild(row);
    }
    if (n == 0) reviewList_->AddChild(Theme::Caption("uoR.none", Tr("Nothing here.")));
    Relayout();
}

void OperatorWindow::OpenSubmission(const std::string& id) {
    Fetch<Submission>(Tr("Opening the submission"), [client = Client(), id]() { return client.GetSubmission(id); }, [this](const Submission& s) {
        submission_ = s;
        haveSubmission_ = true;
        ShowSubmission();
    });
}

void OperatorWindow::ShowSubmission() {
    submissionBox_->ClearChildren();
    submissionBox_->SetVisible(haveSubmission_);
    if (!haveSubmission_) { Relayout(); return; }
    const Submission& s = submission_;
    auto head = Theme::Row("uoRS.head", Theme::kInnerGap, true);
    auto title = Theme::Text("uoRS.title", s.Name() + " " + s.version, 18.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont);
    title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    head->AddChild(title);
    head->AddChild(Theme::Pill("uoRS.state", StateWord(s.state), Theme::TintFor(s.state)));
    auto close = Theme::Button("uoRS.close", Tr("Close"), 80);
    close->onClick = [this]() { haveSubmission_ = false; ShowSubmission(); };
    head->AddChild(close);
    submissionBox_->AddChild(head);
    submissionBox_->AddChild(Theme::Caption("uoRS.facts", TrF("{0} · {1} · {2} · submitted {3}{4}", s.id, s.app, s.merchant, s.createdAt,
                                                             s.answers.empty() ? "" : " · " + TrF("answers {0}", s.answers))));

    // The automatic checks
    int passed = 0;
    for (const auto& [name, outcome] : s.checks) if (outcome == "ok") ++passed;
    submissionBox_->AddChild(Theme::Text("uoRS.checks.title", TrF("Automatic checks · {0} of {1}", passed, s.checks.size()), 13.0f, FontWeight::Bold));
    int n = 0;
    for (const auto& [name, outcome] : s.checks) {
        const std::string id = "uoRS.c" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        row->AddChild(Theme::Pill(id + ".p", outcome == "ok" ? Tr("ok") : outcome == "failed" ? Tr("failed") : outcome == "not run" ? Tr("not run") : outcome,
                                  outcome == "ok" ? Theme::Tint::Green : outcome == "failed" ? Theme::Tint::Red : Theme::Tint::Amber));
        auto label = Theme::Caption(id + ".t", name);
        label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(label);
        submissionBox_->AddChild(row);
    }

    // As the store will show it
    const JSONValue& c = s.component;
    const std::string summary = c.Get("summary").Get("en").GetString(c.Get("summary").GetString());
    const std::string price = c.Get("pricing").Get("price").GetString() + " " + c.Get("pricing").Get("currency").GetString();
    auto card = Theme::Column("uoRS.card", 2);
    card->SetBackgroundColor(Theme::kSurfaceSoft);
    card->SetBorders(1.0f, Theme::kBorder, 12.0f);
    card->SetPadding(12);
    card->AddChild(Theme::Caption("uoRS.card.lead", Tr("As the store will show it")));
    card->AddChild(Theme::Text("uoRS.card.name", s.Name(), 15.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont));
    card->AddChild(Theme::Caption("uoRS.card.pub", c.Get("publisher").GetString() + (c.Get("categories").GetSize() > 0 ? " · " + c.Get("categories").At(0).GetString() : "")));
    if (!summary.empty()) card->AddChild(Theme::Text("uoRS.card.summary", summary, 13.0f));
    card->AddChild(Theme::Caption("uoRS.card.price", c.Get("pricing").Get("model").GetString("free") == "free" ? Tr("Free") : price +
                                  (c.Get("pricing").Get("trial_days").GetInteger(0) > 0 ? " · " + TrN("{n}-day trial", "{n}-day trial", c.Get("pricing").Get("trial_days").GetInteger(0)) : "")));
    submissionBox_->AddChild(card);

    // The packages
    if (s.packages.GetSize() > 0) {
        submissionBox_->AddChild(Theme::Text("uoRS.pk.title", TrN("{n} package", "{n} packages", static_cast<long long>(s.packages.GetSize())), 13.0f, FontWeight::Bold));
        n = 0;
        for (const JSONValue& p : s.packages.GetElements())
            submissionBox_->AddChild(Theme::Caption("uoRS.pk" + std::to_string(n++), p.Get("file").GetString(p.Get("url").GetString()) + "  " + p.Get("sha256").GetString() + "  " +
                                                                                        std::to_string(p.Get("size").GetInteger(0)) + " B"));
    }

    // The decisions so far
    if (!s.decisions.empty()) {
        submissionBox_->AddChild(Theme::Text("uoRS.d.title", Tr("Decisions"), 13.0f, FontWeight::Bold));
        n = 0;
        for (const Decision& d : s.decisions)
            submissionBox_->AddChild(Theme::Caption("uoRS.d" + std::to_string(n++), d.at + "  " + d.action + " · " + d.actor +
                                                                                       (d.sequence > 0 ? " · " + TrF("sequence {0}", d.sequence) : "") + (d.message.empty() ? "" : "\n" + d.message)));
    }

    // The decision
    const bool decidable = Waiting(s);
    submissionBox_->AddChild(Theme::Text("uoRS.decide.title", Tr("Decision"), 13.0f, FontWeight::Bold));
    submissionBox_->AddChild(Theme::Caption("uoRS.decide.hint", decidable ? Tr("The message goes to the merchant with every decision. A release goes out with the next publish.")
                                                                         : Tr("This submission is decided; a resubmission answers it.")));
    decisionMessage_ = Theme::Multiline("uoRS.message", 72, Tr("to the merchant"));
    submissionBox_->AddChild(decisionMessage_);
    auto row = Theme::Row("uoRS.buttons", Theme::kInnerGap, true);
    decisionSequence_ = Theme::Pin(CreateTextInput("uoRS.sequence", 0, 0, 110, Theme::kControlHeight));
    decisionSequence_->SetPlaceholder(Tr("sequence"));
    decisionSequence_->SetTooltip(Tr("The catalog sequence a release gets; the next one when empty"));
    if (decidable) {
        if (s.state != "in_review") {
            auto start = Theme::Button("uoRS.start", Tr("Start the review"), 130);
            start->onClick = [this]() { DecideNow("start_review"); };
            row->AddChild(start);
        }
        auto changes = Theme::Button("uoRS.changes", Tr("Request changes"), 130);
        changes->onClick = [this]() { DecideNow("request_changes"); };
        row->AddChild(changes);
        row->AddChild(decisionSequence_);
        auto release = Theme::Button("uoRS.release", Tr("Approve and release"), 160, Theme::ButtonKind::Primary);
        release->onClick = [this]() { DecideNow("release"); };
        row->AddChild(release);
        auto reject = Theme::Button("uoRS.reject", Tr("Reject"), 90, Theme::ButtonKind::Danger);
        reject->onClick = [this]() { DecideNow("reject"); };
        row->AddChild(reject);
    }
    submissionBox_->AddChild(row);
    Relayout();
}

void OperatorWindow::DecideNow(const std::string& action) {
    if (!haveSubmission_) return;
    const std::string id = submission_.id;
    const std::string message = decisionMessage_->GetText();
    const int sequence = Number(decisionSequence_->GetText());
    auto go = [this, id, action, message, sequence]() {
        Fetch<Submission>(Tr("Recording the decision"), [client = Client(), id, action, message, sequence]() { return client.Decide(id, action, message, sequence); },
                          [this](const Submission& s) {
                              submission_ = s;
                              SetStatus(TrF("{0}: {1}", s.id, StateWord(s.state)) + (s.releasedSequence > 0 ? " · " + TrF("sequence {0}", s.releasedSequence) : ""));
                              ShowSubmission();
                          }, [this]() { RefreshReview(); });
    };
    if (action == "release") {
        if (submission_.ChecksFailed() && message.empty()) { SetStatus(Tr("A check failed: say in the message why it is released all the same"), true); return; }
        Confirm(TrF("Release {0} {1}? It goes out with the next publish.", submission_.Name(), submission_.version), go);
    } else if (action == "reject") {
        if (message.empty()) { SetStatus(Tr("A rejection tells the merchant why: write the message"), true); return; }
        Confirm(TrF("Reject {0} {1}? The merchant must submit anew.", submission_.Name(), submission_.version), go);
    } else if (action == "request_changes" && message.empty()) {
        SetStatus(Tr("Say what must change: write the message"), true);
    } else {
        go();
    }
}

void OperatorWindow::PublishNow() {
    Confirm(TrF("Publish the catalog now? {0} from the released submissions, signed with the operator's key, into the publish folder the stores read.",
                TrN("{n} app", "{n} apps", static_cast<long long>(released_.entries.size()))), [this]() {
        Fetch<PublishReport>(Tr("Publishing the catalog"), [client = Client()]() { return client.Publish(); }, [this](const PublishReport& p) {
            std::string text = TrF("Published sequence {0}: {1}, {2}, {3}.", p.sequence, TrN("{n} app", "{n} apps", p.apps), TrN("{n} component", "{n} components", p.components),
                                   TrN("{n} publisher", "{n} publishers", p.publishers));
            if (!p.leftOut.empty()) text += "\n" + TrF("Left out: {0}", Joined(p.leftOut));
            Alert(text, false);
        }, [this]() { RefreshReview(); });
    });
}

// ===========================================================================
// Reports
// ===========================================================================
void OperatorWindow::BuildReportsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kReports, Tr("Reports"), Tr("What users and merchants report about apps and merchants, and what the operator did about it. The reporter stays private to the operator."), header, body);
    auto refresh = Theme::Button("uoP.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshReports(); };
    header->AddChild(refresh);

    reportsFilters_ = Theme::Row("uoP.filters", Theme::kInnerGap, true);
    body->AddChild(reportsFilters_);
    auto list = Theme::Section("uoP.list", Tr("Reports"));
    reportsList_ = Theme::Column("uoP.rows", Theme::kInnerGap);
    list->AddChild(reportsList_);
    body->AddChild(list);

    reportBox_ = Theme::Section("uoP.one", Tr("Report"));
    reportBox_->SetVisible(false);
    body->AddChild(reportBox_);

    auto open = Theme::Section("uoP.open", Tr("Open a report from mail"));
    rTargetKind_ = Theme::Pin(CreateDropdown("uoP.targetKind", 0, 0, 160, Theme::kControlHeight));
    rTargetKind_->AddItem(Tr("An app"), "app");
    rTargetKind_->AddItem(Tr("A release"), "release");
    rTargetKind_->AddItem(Tr("A merchant"), "merchant");
    rTargetKind_->SetSelectedIndex(0);
    open->AddChild(Theme::Field("uoP.targetKindRow", Tr("About"), rTargetKind_));
    rTarget_ = Theme::Pin(CreateTextInput("uoP.target", 0, 0, 320, Theme::kControlHeight));
    rTarget_->SetPlaceholder(Tr("the app id, app@version or merchant id"));
    open->AddChild(Theme::Field("uoP.targetRow", Tr("Target"), rTarget_));
    rKind_ = Theme::Pin(CreateTextInput("uoP.kind", 0, 0, 200, Theme::kControlHeight));
    rKind_->SetPlaceholder("malware, not-working, content, copyright, privacy, other");
    open->AddChild(Theme::Field("uoP.kindRow", Tr("Kind"), rKind_));
    rReporter_ = Theme::Pin(CreateTextInput("uoP.reporter", 0, 0, 320, Theme::kControlHeight));
    rReporter_->SetPlaceholder(Tr("who wrote, for the file; never shown to the merchant"));
    open->AddChild(Theme::Field("uoP.reporterRow", Tr("Reporter"), rReporter_));
    rText_ = Theme::Multiline("uoP.text", 80, Tr("what the mail says"));
    open->AddChild(rText_);
    auto file = Theme::Button("uoP.file", Tr("Open the report"), 130, Theme::ButtonKind::Primary);
    file->onClick = [this]() {
        const auto* item = rTargetKind_->GetSelectedItem();
        if (!item || rTarget_->GetText().empty() || rKind_->GetText().empty() || rText_->GetText().empty()) { SetStatus(Tr("A report names its target, its kind and the text"), true); return; }
        Fetch<Report>(Tr("Opening the report"),
            [client = Client(), kind = item->value, target = rTarget_->GetText(), what = rKind_->GetText(), text = rText_->GetText(), reporter = rReporter_->GetText()]() {
                return client.OpenReport(kind, target, what, text, reporter);
            }, [this](const Report& r) {
                rTarget_->SetText(""); rKind_->SetText(""); rText_->SetText(""); rReporter_->SetText("");
                report_ = r;
                haveReport_ = true;
                ShowReport();
            }, [this]() { RefreshReports(); });
    };
    open->AddChild(file);
    body->AddChild(open);
}

void OperatorWindow::RefreshReports() {
    if (!Ready(false)) return;
    Fetch<std::vector<Report>>(Tr("Reading the reports"), [client = Client()]() { return client.Reports(); }, [this](const std::vector<Report>& list) {
        reports_ = list;
        int open = 0;
        for (const Report& r : reports_) if (r.Open()) ++open;
        sidebar_->SetBadge(kReports, open);
        ShowReports();
    });
}

void OperatorWindow::ShowReports() {
    reportsFilters_->ClearChildren();
    int open = 0, mine = 0, closed = 0;
    for (const Report& r : reports_) {
        if (r.Open()) ++open; else ++closed;
        if (r.Open() && !settings_.name.empty() && r.assignee == settings_.name) ++mine;
    }
    auto chip = [this](const char* key, const std::string& label) {
        auto b = Theme::Button(std::string("uoP.f.") + key, label, 130, reportsFilter_ == key ? Theme::ButtonKind::Primary : Theme::ButtonKind::Secondary);
        b->onClick = [this, key]() { reportsFilter_ = key; ShowReports(); };
        reportsFilters_->AddChild(b);
    };
    chip("open", TrF("Open {0}", open));
    chip("mine", TrF("Mine {0}", mine));
    chip("closed", TrF("Closed {0}", closed));
    chip("all", TrF("All {0}", reports_.size()));

    reportsList_->ClearChildren();
    int n = 0;
    for (const Report& r : reports_) {
        const bool show = reportsFilter_ == "all" || (reportsFilter_ == "open" && r.Open()) || (reportsFilter_ == "closed" && !r.Open()) ||
                          (reportsFilter_ == "mine" && r.Open() && !settings_.name.empty() && r.assignee == settings_.name);
        if (!show) continue;
        const std::string id = "uoP.r" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", r.id + " · " + r.kind + " · " + r.target, 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("by {0} · {1} ago{2}", r.source, Age(r.createdAt), r.assignee.empty() ? "" : " · " + r.assignee)));
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".p", r.kind == "malware" && r.Open() ? Tr("urgent") : StateWord(r.state), r.kind == "malware" && r.Open() ? Theme::Tint::Red : Theme::TintFor(r.state)));
        auto openButton = Theme::Button(id + ".open", Tr("Open"), 70);
        openButton->onClick = [this, rid = r.id]() { OpenReport(rid); };
        row->AddChild(openButton);
        reportsList_->AddChild(row);
    }
    if (n == 0) reportsList_->AddChild(Theme::Caption("uoP.none", Tr("Nothing here.")));
    Relayout();
}

void OperatorWindow::OpenReport(const std::string& id) {
    Fetch<Report>(Tr("Opening the report"), [client = Client(), id]() { return client.GetReport(id); }, [this](const Report& r) {
        report_ = r;
        haveReport_ = true;
        ShowReport();
    });
}

void OperatorWindow::ShowReport() {
    reportBox_->ClearChildren();
    reportBox_->SetVisible(haveReport_);
    if (!haveReport_) { Relayout(); return; }
    const Report& r = report_;
    auto head = Theme::Row("uoPR.head", Theme::kInnerGap, true);
    auto title = Theme::Text("uoPR.title", r.id + " · " + r.kind + " · " + r.target, 18.0f, FontWeight::Bold, Theme::kTextPrimary, Theme::kDisplayFont);
    title->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    head->AddChild(title);
    head->AddChild(Theme::Pill("uoPR.state", StateWord(r.state), Theme::TintFor(r.state)));
    auto close = Theme::Button("uoPR.close", Tr("Close"), 80);
    close->onClick = [this]() { haveReport_ = false; ShowReport(); };
    head->AddChild(close);
    reportBox_->AddChild(head);
    reportBox_->AddChild(Theme::Caption("uoPR.facts", TrF("Filed {0} from {1} · about {2} {3}{4}", r.createdAt, r.source == "store" ? Tr("the store's app page") : r.source == "merchant" ? Tr("a merchant") : Tr("mail"),
                                                            r.targetKind, r.target, r.assignee.empty() ? "" : " · " + TrF("assigned to {0}", r.assignee))));
    reportBox_->AddChild(Theme::Caption("uoPR.reporter", TrF("Reporter: {0}{1} - the operator's to see, never the merchant's", r.reporter.empty() ? Tr("anonymous") : r.reporter,
                                                               r.reporterMerchant.empty() ? "" : " (" + r.reporterMerchant + ")")));
    reportBox_->AddChild(Theme::Text("uoPR.text", r.text, 13.0f));
    int n = 0;
    for (const std::string& a : r.attachments) reportBox_->AddChild(Theme::Caption("uoPR.a" + std::to_string(n++), a));
    if (!r.Open()) {
        reportBox_->AddChild(Theme::Text("uoPR.found.title", Tr("What the operator found"), 13.0f, FontWeight::Bold));
        reportBox_->AddChild(Theme::Caption("uoPR.found", r.findings.empty() ? Tr("nothing noted") : r.findings));
        reportBox_->AddChild(Theme::Caption("uoPR.outcome", TrF("Outcome for the reporter: {0} ({1})", r.outcome.empty() ? Tr("none") : r.outcome, r.resolvedAt)));
        Relayout();
        return;
    }

    auto assignRow = Theme::Row("uoPR.assignRow", Theme::kInnerGap, true);
    rAssignee_ = Theme::Pin(CreateTextInput("uoPR.assignee", 0, 0, 220, Theme::kControlHeight));
    rAssignee_->SetText(r.assignee.empty() ? settings_.name : r.assignee);
    assignRow->AddChild(Theme::Field("uoPR.assigneeField", Tr("Assigned to"), rAssignee_));
    auto assign = Theme::Button("uoPR.assign", Tr("Assign"), 90);
    assign->onClick = [this, rid = r.id]() {
        Fetch<Report>(Tr("Assigning"), [client = Client(), rid, who = rAssignee_->GetText()]() { return client.AssignReport(rid, who); },
                      [this](const Report& updated) { report_ = updated; ShowReport(); }, [this]() { RefreshReports(); });
    };
    assignRow->AddChild(assign);
    reportBox_->AddChild(assignRow);

    reportBox_->AddChild(Theme::Text("uoPR.close.title", Tr("Close the report"), 13.0f, FontWeight::Bold));
    reportBox_->AddChild(Theme::Caption("uoPR.close.hint", Tr("The findings stay in the file. The outcome is the one line the reporter and the merchant learn - never the reporter's name.")));
    rFindings_ = Theme::Multiline("uoPR.findings", 72, Tr("what the operator found"));
    rFindings_->SetText(r.findings);
    reportBox_->AddChild(rFindings_);
    rOutcome_ = Theme::Pin(CreateTextInput("uoPR.outcome", 0, 0, 560, Theme::kControlHeight));
    rOutcome_->SetPlaceholder(Tr("the outcome, one line"));
    reportBox_->AddChild(Theme::Field("uoPR.outcomeRow", Tr("Outcome"), rOutcome_));
    auto buttons = Theme::Row("uoPR.buttons", Theme::kInnerGap, true);
    auto resolve = Theme::Button("uoPR.resolve", Tr("Resolve as upheld"), 150, Theme::ButtonKind::Primary);
    resolve->onClick = [this, rid = r.id]() {
        if (rOutcome_->GetText().empty()) { SetStatus(Tr("The reporter learns the outcome: write the line"), true); return; }
        Fetch<Report>(Tr("Closing the report"), [client = Client(), rid, findings = rFindings_->GetText(), outcome = rOutcome_->GetText()]() {
            return client.CloseReport(rid, true, findings, outcome);
        }, [this](const Report& updated) { report_ = updated; ShowReport(); }, [this]() { RefreshReports(); });
    };
    buttons->AddChild(resolve);
    auto dismiss = Theme::Button("uoPR.dismiss", Tr("Dismiss"), 100, Theme::ButtonKind::Danger);
    dismiss->onClick = [this, rid = r.id]() {
        if (rOutcome_->GetText().empty()) { SetStatus(Tr("The reporter learns the outcome: write the line"), true); return; }
        Confirm(TrF("Dismiss {0}? The reporter is told the outcome line.", rid), [this, rid]() {
            Fetch<Report>(Tr("Closing the report"), [client = Client(), rid, findings = rFindings_->GetText(), outcome = rOutcome_->GetText()]() {
                return client.CloseReport(rid, false, findings, outcome);
            }, [this](const Report& updated) { report_ = updated; ShowReport(); }, [this]() { RefreshReports(); });
        });
    };
    buttons->AddChild(dismiss);
    reportBox_->AddChild(buttons);
    Relayout();
}

// ===========================================================================
// Terms and notices
// ===========================================================================
void OperatorWindow::BuildTermsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kTerms, Tr("Terms and notices"), Tr("The merchant terms, who has accepted which version, the enforcement policy, and every notice the operator sent."), header, body);
    auto refresh = Theme::Button("uoT.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshTerms(); };
    header->AddChild(refresh);

    auto versions = Theme::Section("uoT.versions", Tr("Versions of the merchant terms"));
    termsList_ = Theme::Column("uoT.rows", Theme::kInnerGap);
    versions->AddChild(termsList_);
    body->AddChild(versions);

    auto draft = Theme::Section("uoT.draft", Tr("A new version"));
    draft->AddChild(Theme::Caption("uoT.draft.hint", Tr("From the day it comes into force, a merchant who has not accepted it is paused: its apps leave the catalog until it does. The merchant backend's Terms page shows the text and takes the acceptance.")));
    tInForce_ = Theme::Pin(CreateTextInput("uoT.inForce", 0, 0, 140, Theme::kControlHeight));
    tInForce_->SetPlaceholder("2026-12-31");
    draft->AddChild(Theme::Field("uoT.inForceRow", Tr("In force from"), tInForce_));
    tChanges_ = Theme::Pin(CreateTextInput("uoT.changes", 0, 0, 560, Theme::kControlHeight));
    tChanges_->SetPlaceholder(Tr("what changed, in a line"));
    draft->AddChild(Theme::Field("uoT.changesRow", Tr("What changed"), tChanges_));
    tVersion_ = Theme::Pin(CreateTextInput("uoT.version", 0, 0, 80, Theme::kControlHeight));
    tVersion_->SetPlaceholder(Tr("next"));
    draft->AddChild(Theme::Field("uoT.versionRow", Tr("Version"), tVersion_));
    tText_ = Theme::Multiline("uoT.text", 160, Tr("the terms"));
    draft->AddChild(tText_);
    auto publishTerms = Theme::Button("uoT.add", Tr("Publish this version"), 160, Theme::ButtonKind::Primary);
    publishTerms->onClick = [this]() {
        if (tText_->GetText().empty() || tInForce_->GetText().size() != 10) { SetStatus(Tr("The terms' text and the day they come into force (YYYY-MM-DD)"), true); return; }
        Confirm(TrF("Publish these terms, in force from {0}? Every merchant must accept them by then.", tInForce_->GetText()), [this]() {
            Fetch<TermsVersion>(Tr("Publishing the terms"),
                [client = Client(), text = tText_->GetText(), changes = tChanges_->GetText(), from = tInForce_->GetText(), version = Number(tVersion_->GetText())]() {
                    return client.AddTerms(text, changes, from, version);
                }, [this](const TermsVersion& t) {
                    tText_->SetText(""); tChanges_->SetText(""); tVersion_->SetText("");
                    SetStatus(TrF("Terms version {0}, in force from {1}", t.version, t.inForceFrom));
                }, [this]() { RefreshTerms(); });
        });
    };
    draft->AddChild(publishTerms);
    body->AddChild(draft);

    auto policy = Theme::Section("uoT.policy", Tr("Enforcement policy"));
    policy->AddChild(Theme::Caption("uoT.policy.hint", Tr("The rules the warnings follow: a warning counts as a strike this long; this many unexpired strikes suspend the shop for this long; the clauses that suspend at once, no warning first.")));
    pExpiry_ = Theme::Pin(CreateTextInput("uoT.p.expiry", 0, 0, 80, Theme::kControlHeight));
    policy->AddChild(Theme::Field("uoT.p.expiryRow", Tr("A strike expires after, days"), pExpiry_));
    pStrikes_ = Theme::Pin(CreateTextInput("uoT.p.strikes", 0, 0, 80, Theme::kControlHeight));
    policy->AddChild(Theme::Field("uoT.p.strikesRow", Tr("Strikes before a suspension"), pStrikes_));
    pDays_ = Theme::Pin(CreateTextInput("uoT.p.days", 0, 0, 80, Theme::kControlHeight));
    policy->AddChild(Theme::Field("uoT.p.daysRow", Tr("That suspension lasts, days"), pDays_));
    pDeadline_ = Theme::Pin(CreateTextInput("uoT.p.deadline", 0, 0, 80, Theme::kControlHeight));
    policy->AddChild(Theme::Field("uoT.p.deadlineRow", Tr("A merchant answers within, days"), pDeadline_));
    pClauses_ = Theme::Pin(CreateTextInput("uoT.p.clauses", 0, 0, 400, Theme::kControlHeight));
    pClauses_->SetPlaceholder(Tr("9.1 12 10, separated by spaces"));
    policy->AddChild(Theme::Field("uoT.p.clausesRow", Tr("Clauses that suspend at once"), pClauses_));
    auto savePolicy = Theme::Button("uoT.p.save", Tr("Save the policy"), 130, Theme::ButtonKind::Primary);
    savePolicy->onClick = [this]() {
        Policy p;
        p.strikeExpiryDays = Number(pExpiry_->GetText(), p.strikeExpiryDays);
        p.strikesBeforeSuspension = Number(pStrikes_->GetText(), p.strikesBeforeSuspension);
        p.suspensionDays = Number(pDays_->GetText(), p.suspensionDays);
        p.answerDeadlineDays = Number(pDeadline_->GetText(), p.answerDeadlineDays);
        p.clausesThatSuspend = Words(pClauses_->GetText());
        Fetch<Policy>(Tr("Saving the policy"), [client = Client(), p]() { return client.SetPolicy(p); }, [this](const Policy& saved) {
            policy_ = saved;
            SetStatus(Tr("Policy saved"));
        });
    };
    policy->AddChild(savePolicy);
    body->AddChild(policy);

    auto send = Theme::Section("uoT.send", Tr("Send a notice"));
    send->AddChild(Theme::Caption("uoT.send.hint", Tr("A banner in the merchant backend until acknowledged. A warning names its clause and counts as a strike for each merchant it goes to; the policy may suspend on it.")));
    nKind_ = Theme::Pin(CreateDropdown("uoT.n.kind", 0, 0, 220, Theme::kControlHeight));
    for (const char* kind : kNoticeKinds) nKind_->AddItem(KindWord(kind), kind);
    nKind_->SetSelectedIndex(0);
    send->AddChild(Theme::Field("uoT.n.kindRow", Tr("Kind"), nKind_));
    nTo_ = Theme::Pin(CreateTextInput("uoT.n.to", 0, 0, 400, Theme::kControlHeight));
    nTo_->SetPlaceholder(Tr("merchant ids separated by spaces, or * for everyone"));
    send->AddChild(Theme::Field("uoT.n.toRow", Tr("To"), nTo_));
    nSubject_ = Theme::Pin(CreateTextInput("uoT.n.subject", 0, 0, 560, Theme::kControlHeight));
    send->AddChild(Theme::Field("uoT.n.subjectRow", Tr("Subject"), nSubject_));
    nClause_ = Theme::Pin(CreateTextInput("uoT.n.clause", 0, 0, 120, Theme::kControlHeight));
    send->AddChild(Theme::Field("uoT.n.clauseRow", Tr("Clause"), nClause_));
    nText_ = Theme::Multiline("uoT.n.text", 100, Tr("the message"));
    send->AddChild(nText_);
    auto sendButton = Theme::Button("uoT.n.send", Tr("Send"), 100, Theme::ButtonKind::Primary);
    sendButton->onClick = [this]() {
        const auto* item = nKind_->GetSelectedItem();
        const std::vector<std::string> to = Words(nTo_->GetText());
        if (!item || to.empty() || nSubject_->GetText().empty()) { SetStatus(Tr("A notice has a kind, recipients and a subject"), true); return; }
        if (item->value == "warning" && nClause_->GetText().empty()) { SetStatus(Tr("A warning names the clause it is about"), true); return; }
        const std::string kind = item->value;
        auto go = [this, kind, to]() {
            Fetch<NoticeReceipt>(Tr("Sending the notice"),
                [client = Client(), kind, to, subject = nSubject_->GetText(), text = nText_->GetText(), clause = nClause_->GetText()]() {
                    return client.SendNotice(kind, subject, text, clause, to);
                }, [this](const NoticeReceipt& r) {
                    nSubject_->SetText(""); nText_->SetText(""); nClause_->SetText("");
                    SetStatus(r.suspended.empty() ? TrF("Sent {0}", r.notice.id) : TrF("Sent {0}; the policy suspended {1} on it", r.notice.id, TrN("{n} merchant", "{n} merchants", static_cast<long long>(r.suspended.size()))));
                }, [this]() { RefreshTerms(); });
        };
        const std::string who = to.size() == 1 && to[0] == "*" ? Tr("every merchant") : TrN("{n} merchant", "{n} merchants", static_cast<long long>(to.size()));
        if (kind == "warning") Confirm(TrF("Warn {0} under clause {1}? It counts as a strike for each; the policy may suspend a shop on it.", who, nClause_->GetText()), go);
        else Confirm(TrF("Send the {0} to {1}?", KindWord(kind), who), go);
    };
    send->AddChild(sendButton);
    body->AddChild(send);

    auto sent = Theme::Section("uoT.notices", Tr("Notices sent"));
    noticesList_ = Theme::Column("uoT.n.rows", Theme::kInnerGap);
    sent->AddChild(noticesList_);
    body->AddChild(sent);
}

void OperatorWindow::RefreshTerms() {
    if (!Ready(false)) return;
    const OperatorClient client = Client();
    RunJob(Tr("Reading the terms"), [this, client]() {
        JobOutcome out;
        const auto terms = client.Terms();
        if (!terms) { out.ok = false; out.message = terms.error.message; return out; }
        const auto policy = client.GetPolicy();
        if (!policy) { out.ok = false; out.message = policy.error.message; return out; }
        const auto notices = client.Notices();
        if (!notices) { out.ok = false; out.message = notices.error.message; return out; }
        std::vector<Merchant> merchants;
        if (const auto m = client.Merchants()) merchants = m.Value();
        out.apply = [this, terms = terms.Value(), policy = policy.Value(), notices = notices.Value(), merchants]() {
            terms_ = terms;
            policy_ = policy;
            notices_ = notices;
            merchants_ = merchants;
            ShowTerms();
        };
        return out;
    });
}

void OperatorWindow::ShowTerms() {
    termsList_->ClearChildren();
    int n = 0;
    for (auto it = terms_.versions.rbegin(); it != terms_.versions.rend(); ++it) {
        const TermsVersion& t = *it;
        int accepted = 0;
        for (const Merchant& m : merchants_) if (m.termsAccepted >= t.version) ++accepted;
        const std::string id = "uoT.v" + std::to_string(n++);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", TrF("Version {0}", t.version) + (t.changes.empty() ? "" : " · " + t.changes), 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("in force from {0} · accepted by {1} of {2}", t.inForceFrom, accepted, merchants_.size())));
        row->AddChild(text);
        if (t.version == terms_.inForce) row->AddChild(Theme::Pill(id + ".p", Tr("current"), Theme::Tint::Green));
        else if (t.version > terms_.inForce) row->AddChild(Theme::Pill(id + ".p", Tr("coming"), Theme::Tint::Blue));
        else row->AddChild(Theme::Pill(id + ".p", Tr("superseded"), Theme::Tint::Grey));
        auto read = Theme::Button(id + ".read", Tr("Read"), 70);
        read->onClick = [this, text = t.text, version = t.version]() { Alert(TrF("Terms version {0}", version) + "\n\n" + text, false); };
        row->AddChild(read);
        termsList_->AddChild(row);
    }
    if (n == 0) termsList_->AddChild(Theme::Caption("uoT.v.none", Tr("No terms published yet. Until a version is in force, every merchant stands active.")));
    std::vector<std::string> due;
    for (const Merchant& m : merchants_) if (m.TermsDue()) due.push_back(m.name);
    if (!due.empty()) termsList_->AddChild(Theme::Caption("uoT.v.due", TrF("Not on version {0}: {1}", terms_.inForce, Joined(due))));

    pExpiry_->SetText(std::to_string(policy_.strikeExpiryDays));
    pStrikes_->SetText(std::to_string(policy_.strikesBeforeSuspension));
    pDays_->SetText(std::to_string(policy_.suspensionDays));
    pDeadline_->SetText(std::to_string(policy_.answerDeadlineDays));
    pClauses_->SetText(Joined(policy_.clausesThatSuspend, " "));

    noticesList_->ClearChildren();
    n = 0;
    for (auto it = notices_.rbegin(); it != notices_.rend(); ++it) {
        const Notice& notice = *it;
        const std::string id = "uoT.n" + std::to_string(n++);
        std::vector<std::string> to;
        for (const std::string& r : notice.recipients) to.push_back(r == "*" ? Tr("every merchant") : r);
        auto row = Theme::Row(id, Theme::kInnerGap);
        auto text = Theme::Column(id + ".c", 0);
        text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        text->AddChild(Theme::Text(id + ".t", notice.subject, 13.0f, FontWeight::Bold));
        text->AddChild(Theme::Caption(id + ".s", TrF("{0} · to {1}{2}{3}", notice.createdAt, Joined(to), notice.clause.empty() ? "" : " · " + TrF("clause {0}", notice.clause),
                                                       notice.deadline.empty() ? "" : " · " + TrF("answer by {0}", notice.deadline))));
        row->AddChild(text);
        row->AddChild(Theme::Pill(id + ".p", KindWord(notice.kind), notice.kind == "warning" || notice.kind == "suspension" ? Theme::Tint::Red : Theme::Tint::Blue));
        noticesList_->AddChild(row);
    }
    if (n == 0) noticesList_->AddChild(Theme::Caption("uoT.n.none", Tr("No notice sent yet.")));
    Relayout();
}

} // namespace UltraStoreOperator::Ui
