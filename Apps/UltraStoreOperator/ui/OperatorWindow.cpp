// Apps/UltraStoreOperator/ui/OperatorWindow.cpp
// The shell (the dark sidebar, the pages' frame, the status line), the
// jobs on the worker, and the Dashboard, Audit log and Settings pages.
// The merchants, the review, the reports and the terms are in
// OperatorPages.cpp.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "OperatorWindow.h"

#include "OperatorTheme.h"

#include "UltraCanvasAlert.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>

#ifndef ULTRASTOREOPERATOR_VERSION
#error "ULTRASTOREOPERATOR_VERSION must be passed by CMake (from Docs/UltraStoreOperator/CHANGELOG.md)"
#endif

using namespace UltraCanvas;

namespace UltraStoreOperator::Ui {

namespace {

constexpr const char* kDashboard = "dashboard";
constexpr const char* kMerchants = "merchants";
constexpr const char* kReview = "review";
constexpr const char* kReports = "reports";
constexpr const char* kTerms = "terms";
constexpr const char* kAudit = "audit";
constexpr const char* kSettings = "settings";

std::shared_ptr<UltraCanvasContainer> MakeScrollList(const std::string& id) {
    auto list = CreateScrollableContainer(id, 0, 0, 0, 0);
    ContainerStyle style = list->GetContainerStyle();
    style.forceShowVerticalScrollbar = false;
    style.autoShowScrollbars = true;
    style.autoShowHorizontalScrollbar = false;
    list->SetContainerStyle(style);
    list->layout.SetFlexColumn().SetFlexGap(Theme::kGap).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    list->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    list->SetPadding(2, 16, 2, 2);
    return list;
}

bool Waiting(const Submission& s) { return s.state == "checked" || s.state == "failed_checks" || s.state == "in_review"; }

} // namespace

OperatorWindow::OperatorWindow(UltraCanvasApplication& app, std::string settingsPathUtf8, std::string startPage)
    : app_(app), settingsPath_(std::move(settingsPathUtf8)), startPage_(std::move(startPage)) {}

OperatorWindow::~OperatorWindow() {
    alive_.reset();
    if (worker_.joinable()) worker_.join();
}

bool OperatorWindow::Create() {
    if (const auto loaded = OperatorSettings::Load(settingsPath_)) settings_ = loaded.Value().WithEnvironment();
    else settingsError_ = loaded.error.message;

    WindowConfig config;
    config.title = TrF("UltraStore Operations {0}", ULTRASTOREOPERATOR_VERSION);
    config.width = 1280;
    config.height = 820;
    config.minWidth = kMinWidth;
    config.minHeight = kMinHeight;
    config.backgroundColor = Theme::kSurfaceSoft;
    window_ = CreateWindow(config);
    if (!window_) return false;

    BuildShell();
    LayoutForSize(static_cast<float>(config.width), static_cast<float>(config.height));
    window_->onWindowResize = [this](int w, int h) { LayoutForSize(static_cast<float>(w), static_cast<float>(h)); };
    window_->onWindowClosing = [this]() { return OnClosing(); };

    LoadSettingsPage();
    if (!settingsError_.empty()) SetStatus(settingsError_, true);
    if (settings_.Complete()) ShowPage(pages_.count(startPage_) ? startPage_ : kDashboard);
    else { ShowPage(kSettings); SetStatus(Tr("Name the marketplace service and the operator token to begin."), false); }
    return true;
}

void OperatorWindow::Show() {
    if (window_) window_->Show();
}

// ===========================================================================
// The shell
// ===========================================================================
void OperatorWindow::BuildShell() {
    root_ = Theme::Column("uoRoot", 0);
    root_->SetBackgroundColor(Theme::kSurfaceSoft);
    auto body = Theme::Row("uoBody", 0);
    body->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    // The dark column: the operator tier's mark.
    auto side = Theme::Column("uoSide", 0);
    side->SetBackgroundColor(Theme::kSidebar);
    side->SetElementSize(CSSLayout::Dimension::Px(240), CSSLayout::Dimension::Auto());
    auto brand = Theme::Column("uoBrand", 2);
    brand->SetPadding(20, 20, 8, 20);
    brand->AddChild(Theme::Text("uoBrand.name", Tr("UltraStore Operations"), 15.0f, FontWeight::Bold, Colors::White, Theme::kDisplayFont));
    brand->AddChild(Theme::Text("uoBrand.sub", Tr("The operator's tier"), 12.0f, FontWeight::Normal, Theme::kSidebarMuted));
    side->AddChild(brand);
    sidebar_ = Theme::Pin(CreateSidebarNavigation("uoSidebar", 0, 0, 240, 0));
    sidebar_->SetStyle(Theme::SidebarStyle());
    sidebar_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    sidebar_->AddItem(kDashboard, Tr("Dashboard"), Theme::Icon("home.svg"));
    sidebar_->AddItem(kMerchants, Tr("Merchants"), Theme::Icon("user-plus.svg"));
    sidebar_->AddItem(kReview, Tr("App review"), Theme::Icon("list-check.svg"));
    sidebar_->AddItem(kReports, Tr("Reports"), Theme::Icon("envelope.svg"));
    sidebar_->AddItem(kTerms, Tr("Terms and notices"), Theme::Icon("clipboard-list.svg"));
    sidebar_->AddItem(kAudit, Tr("Audit log"), Theme::Icon("clock-five.svg"));
    sidebar_->AddItem(kSettings, Tr("Settings"), Theme::Icon("settings.svg"));
    sidebar_->onSelectionChanged = [this](const std::string& id) { if (currentPage_ != id) ShowPage(id); };
    side->AddChild(sidebar_);
    auto note = Theme::Column("uoSideNote", 4);
    note->SetPadding(12);
    note->SetBackgroundColor(Theme::kSidebarHover);
    note->SetBorders(0.0f, Theme::kSidebarHover, 12.0f);
    sidebarNote_ = Theme::Text("uoSideNote.text", "", 12.0f, FontWeight::Normal, Theme::kSidebarMuted);
    note->AddChild(sidebarNote_);
    sidebarNoteBox_ = note;
    auto noteWrap = Theme::Column("uoSideNoteWrap", 0);
    noteWrap->SetPadding(12);
    noteWrap->AddChild(note);
    side->AddChild(noteWrap);
    UpdateSidebarNote("");

    pageHost_ = Theme::Column("uoPages", 0);
    pageHost_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    BuildDashboardPage();
    BuildMerchantsPage();
    BuildReviewPage();
    BuildReportsPage();
    BuildTermsPage();
    BuildAuditPage();
    BuildSettingsPage();

    body->AddChild(side);
    body->AddChild(pageHost_);

    auto statusRow = Theme::Row("uoStatusRow", 0);
    statusRow->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(Theme::kStatusHeight));
    statusRow->SetPadding(0, Theme::kPagePadding);
    statusRow->SetBackgroundColor(Theme::kSurface);
    statusRow->SetBorders(0.0f, Theme::kBorder, 0.0f);
    statusLabel_ = Theme::Caption("uoStatus", "");
    statusLabel_->SetWrap(TextWrap::WrapNone);
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    statusRow->AddChild(statusLabel_);

    root_->AddChild(body);
    root_->AddChild(statusRow);
    window_->AddChild(root_);
}

std::shared_ptr<OperatorWindow::Container> OperatorWindow::BuildPage(const std::string& id, const std::string& title, const std::string& lead,
                                                                     std::shared_ptr<Container>& header, std::shared_ptr<Container>& body) {
    auto page = Theme::Column(id, Theme::kGap);
    page->SetPadding(Theme::kPagePadding);
    page->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header = Theme::Row(id + ".header", Theme::kGap, true);
    auto titles = Theme::Column(id + ".titles", 4);
    titles->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    titles->AddChild(Theme::Heading(id + ".title", title));
    if (!lead.empty()) titles->AddChild(Theme::Text(id + ".lead", lead, 13.0f, FontWeight::Normal, Theme::kTextSecondary));
    header->AddChild(titles);
    body = MakeScrollList(id + ".body");
    page->AddChild(header);
    page->AddChild(body);
    pages_[id] = page;
    pageHost_->AddChild(page);
    page->SetVisible(false);
    return page;
}

void OperatorWindow::ShowPage(const std::string& id) {
    currentPage_ = id;
    for (auto& [key, page] : pages_) page->SetVisible(key == id);
    if (sidebar_->GetSelectedId() != id) sidebar_->Select(id, false);
    if (settings_.Complete()) {
        if (id == kDashboard) RefreshDashboard();
        else if (id == kMerchants) RefreshMerchants();
        else if (id == kReview) RefreshReview();
        else if (id == kReports) RefreshReports();
        else if (id == kTerms) RefreshTerms();
        else if (id == kAudit) RefreshAudit();
    }
    Relayout();
}

// The dark box under the sidebar: the service, who is signed in, and the
// dashboard's numbers once it fetched them.
void OperatorWindow::UpdateSidebarNote(const std::string& numbers) {
    if (!sidebarNote_) return;
    std::string text = numbers;
    if (!settings_.url.empty()) text += (text.empty() ? "" : "\n") + settings_.url;
    if (!settings_.name.empty()) text += (text.empty() ? "" : "\n") + TrF("Signed in as {0}, operator", settings_.name);
    sidebarNote_->SetText(text);
    if (sidebarNoteBox_) sidebarNoteBox_->SetVisible(!text.empty());
}

void OperatorWindow::Relayout() {
    if (!pageHost_) return;
    pageHost_->InvalidateLayout();
    pageHost_->RequestRedraw();
}

void OperatorWindow::LayoutForSize(float width, float height) {
    if (!root_) return;
    root_->SetElementAbsolutePosition(Point2Df(0.0f, 0.0f));
    root_->SetElementSize(Size2Df(std::max(static_cast<float>(kMinWidth), width), std::max(static_cast<float>(kMinHeight), height)));
    root_->InvalidateLayout();
    if (window_) window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

void OperatorWindow::SetStatus(const std::string& text, bool isError) {
    if (!statusLabel_) return;
    statusLabel_->SetText(text);
    statusLabel_->SetTextColor(isError ? Theme::kDanger : Theme::kTextSecondary);
    statusLabel_->RequestRedraw();
}

void OperatorWindow::Alert(const std::string& message, bool isError) {
    if (isError) UltraCanvasAlert::Error(message, Tr("UltraStore Operations"), nullptr, window_.get());
    else UltraCanvasAlert::Info(message, Tr("UltraStore Operations"), nullptr, window_.get());
}

void OperatorWindow::Confirm(const std::string& message, std::function<void()> then) {
    UltraCanvasAlert::Confirm(message, Tr("UltraStore Operations"), [then](bool yes) { if (yes && then) then(); }, window_.get());
}

bool OperatorWindow::OnClosing() {
    if (busy_) {
        SetStatus(Tr("The service is still answering: close the window once it has."), true);
        return false;
    }
    app_.RequestExit();
    return true;
}

OperatorClient OperatorWindow::Client() const {
    return OperatorClient(settings_);
}

bool OperatorWindow::Ready(bool say) {
    if (!settings_.Complete()) {
        if (say) SetStatus(Tr("Settings: the marketplace service's URL and the operator token are needed first."), true);
        return false;
    }
    if (busy_) {
        if (say) SetStatus(Tr("The service is still answering the last request."), true);
        return false;
    }
    return true;
}

// ===========================================================================
// Jobs
// ===========================================================================
void OperatorWindow::RunJob(const std::string& title, std::function<JobOutcome()> work, std::function<void(const JobOutcome&)> done) {
    if (!Ready()) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    SetStatus(title + "…");
    std::weak_ptr<int> token = alive_;
    worker_ = std::thread([this, token, work = std::move(work), done = std::move(done)]() {
        JobOutcome out = work();
        app_.PostToUIThread([this, token, out = std::move(out), done]() {
            if (!token.lock()) return;
            busy_ = false;
            if (out.apply) out.apply();
            SetStatus(out.ok ? (out.message.empty() ? Tr("Up to date") : out.message) : out.message, !out.ok);
            if (!out.ok) Alert(out.message);
            if (done) done(out);
            Relayout();
        });
    });
}

// ===========================================================================
// Dashboard
// ===========================================================================
void OperatorWindow::BuildDashboardPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kDashboard, Tr("Dashboard"), Tr("What needs an operator today."), header, body);
    auto refresh = Theme::Button("uoD.refresh", Tr("Refresh"), 100, Theme::ButtonKind::Secondary);
    refresh->onClick = [this]() { RefreshDashboard(); };
    header->AddChild(refresh);
    auto review = Theme::Button("uoD.review", Tr("Start reviewing"), 140, Theme::ButtonKind::Primary);
    review->onClick = [this]() { ShowPage(kReview); };
    header->AddChild(review);

    stats_ = Theme::Row("uoD.stats", Theme::kGap, true);
    stats_->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->AddChild(stats_);

    auto columns = Theme::Row("uoD.columns", Theme::kGap, true);
    columns->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    auto queue = Theme::Section("uoD.queue", Tr("Review queue"));
    queue->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    queue->SetElementSize(CSSLayout::Dimension::Px(380), CSSLayout::Dimension::Auto());
    dashQueue_ = Theme::Column("uoD.queue.list", Theme::kInnerGap);
    queue->AddChild(dashQueue_);
    columns->AddChild(queue);
    auto reports = Theme::Section("uoD.reports", Tr("Reports, open first"));
    reports->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    reports->SetElementSize(CSSLayout::Dimension::Px(380), CSSLayout::Dimension::Auto());
    dashReports_ = Theme::Column("uoD.reports.list", Theme::kInnerGap);
    reports->AddChild(dashReports_);
    columns->AddChild(reports);
    body->AddChild(columns);

    auto lower = Theme::Row("uoD.lower", Theme::kGap, true);
    lower->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    auto compliance = Theme::Section("uoD.compliance", Tr("Compliance"));
    compliance->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    compliance->SetElementSize(CSSLayout::Dimension::Px(380), CSSLayout::Dimension::Auto());
    dashCompliance_ = Theme::Column("uoD.compliance.list", Theme::kInnerGap);
    compliance->AddChild(dashCompliance_);
    lower->AddChild(compliance);
    auto actions = Theme::Section("uoD.actions", Tr("Operator actions, the latest"));
    actions->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    actions->SetElementSize(CSSLayout::Dimension::Px(380), CSSLayout::Dimension::Auto());
    dashActions_ = Theme::Column("uoD.actions.list", Theme::kInnerGap);
    actions->AddChild(dashActions_);
    lower->AddChild(actions);
    body->AddChild(lower);
}

void OperatorWindow::RefreshDashboard() {
    if (!Ready(false)) return;
    const OperatorClient client = Client();
    RunJob(Tr("Asking the marketplace service"), [this, client]() {
        JobOutcome out;
        std::vector<Merchant> merchants;
        std::vector<Submission> submissions;
        std::vector<Report> reports;
        TermsList terms;
        Released released;
        std::vector<AuditRow> actions;
        std::string error;
        if (const auto m = client.Merchants()) merchants = m.Value(); else error = m.error.message;
        if (const auto s = client.Submissions()) submissions = s.Value(); else error = s.error.message;
        if (const auto r = client.Reports()) reports = r.Value(); else error = r.error.message;
        if (const auto t = client.Terms()) terms = t.Value(); else error = t.error.message;
        if (const auto r = client.GetReleased()) released = r.Value(); else error = r.error.message;
        if (const auto check = client.VerifyAudit()) {
            const int64_t since = std::max<int64_t>(0, check.Value().rows - 8);
            if (const auto page = client.Audit(since, 8)) actions = page.Value().rows;
        }
        if (!error.empty()) { out.ok = false; out.message = error; return out; }
        out.apply = [this, merchants, submissions, reports, terms, released, actions]() {
            merchants_ = merchants;
            submissions_ = submissions;
            reports_ = reports;
            terms_ = terms;
            released_ = released;
            audit_ = actions;
            std::reverse(audit_.begin(), audit_.end());

            // The numbers
            int suspended = 0, paused = 0, warned = 0, notOnTerms = 0;
            for (const Merchant& m : merchants_) {
                if (m.standing == "suspended") ++suspended;
                if (m.standing == "paused") ++paused;
                if (m.strikes > 0) ++warned;
                if (m.TermsDue()) ++notOnTerms;
            }
            int waiting = 0, failed = 0;
            std::string oldest;
            for (const Submission& s : submissions_) {
                if (!Waiting(s)) continue;
                ++waiting;
                if (s.ChecksFailed()) ++failed;
                if (oldest.empty() || s.createdAt < oldest) oldest = s.createdAt;
            }
            int open = 0, malware = 0;
            for (const Report& r : reports_) {
                if (!r.Open()) continue;
                ++open;
                if (r.kind == "malware") ++malware;
            }
            sidebar_->SetBadge(kReview, waiting);
            sidebar_->SetBadge(kReports, open);
            UpdateSidebarNote(TrF("Catalog sequence {0} · {1} · {2}", released_.published,
                                  TrN("{n} merchant", "{n} merchants", static_cast<long long>(merchants_.size())),
                                  TrN("{n} app live", "{n} apps live", static_cast<long long>(released_.entries.size()))));

            stats_->ClearChildren();
            stats_->AddChild(Theme::Stat("uoD.s1", Tr("Merchants"), std::to_string(merchants_.size()),
                                         TrF("{0} · {1}", TrN("{n} suspended", "{n} suspended", suspended), TrN("{n} paused by the terms", "{n} paused by the terms", paused))));
            stats_->AddChild(Theme::Stat("uoD.s2", Tr("Awaiting review"), std::to_string(waiting),
                                         oldest.empty() ? Tr("nothing waiting") : TrF("oldest waiting {0} · {1}", Age(oldest), TrN("{n} check failed", "{n} checks failed", failed))));
            stats_->AddChild(Theme::Stat("uoD.s3", Tr("Open reports"), std::to_string(open),
                                         malware > 0 ? TrN("{n} urgent: possible malware", "{n} urgent: possible malware", malware) : Tr("none urgent")));
            stats_->AddChild(Theme::Stat("uoD.s4", terms_.inForce > 0 ? TrF("Not on terms {0}", terms_.inForce) : Tr("Terms"), std::to_string(notOnTerms),
                                         terms_.inForce > 0 ? Tr("a merchant not on the terms in force is paused") : Tr("no terms in force yet")));
            stats_->AddChild(Theme::Stat("uoD.s5", Tr("Catalog sequence"), std::to_string(released_.published),
                                         TrN("{n} app in the next publish", "{n} apps in the next publish", static_cast<long long>(released_.entries.size())), true));

            // The queue
            dashQueue_->ClearChildren();
            int n = 0;
            for (const Submission& s : submissions_) {
                if (!Waiting(s)) continue;
                const std::string id = "uoD.q" + std::to_string(n++);
                auto row = Theme::Row(id, Theme::kInnerGap);
                auto text = Theme::Column(id + ".c", 0);
                text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                text->AddChild(Theme::Text(id + ".t", s.Name() + " " + s.version, 13.0f, FontWeight::Bold));
                text->AddChild(Theme::Caption(id + ".s", TrF("{0} · waiting {1}", s.merchant, Age(s.createdAt))));
                row->AddChild(text);
                row->AddChild(Theme::Pill(id + ".p", s.ChecksFailed() ? Tr("check failed") : s.state == "in_review" ? Tr("in review") : Tr("ready"),
                                          s.ChecksFailed() ? Theme::Tint::Red : Theme::Tint::Blue));
                auto open = Theme::Button(id + ".open", Tr("Open"), 70);
                open->onClick = [this, sid = s.id]() { ShowPage(kReview); OpenSubmission(sid); };
                row->AddChild(open);
                dashQueue_->AddChild(row);
            }
            if (n == 0) dashQueue_->AddChild(Theme::Caption("uoD.q.none", Tr("Nothing waits for a review.")));

            // The reports
            dashReports_->ClearChildren();
            n = 0;
            for (const Report& r : reports_) {
                if (!r.Open()) continue;
                const std::string id = "uoD.r" + std::to_string(n++);
                auto row = Theme::Row(id, Theme::kInnerGap);
                auto text = Theme::Column(id + ".c", 0);
                text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                text->AddChild(Theme::Text(id + ".t", r.id + " · " + r.kind + " · " + r.target, 13.0f, FontWeight::Bold));
                text->AddChild(Theme::Caption(id + ".s", TrF("by {0} · {1} ago{2}", r.source, Age(r.createdAt), r.assignee.empty() ? "" : " · " + r.assignee)));
                row->AddChild(text);
                row->AddChild(Theme::Pill(id + ".p", r.kind == "malware" ? Tr("urgent") : r.state, r.kind == "malware" ? Theme::Tint::Red : Theme::TintFor(r.state)));
                auto open = Theme::Button(id + ".open", Tr("Open"), 70);
                open->onClick = [this, rid = r.id]() { ShowPage(kReports); OpenReport(rid); };
                row->AddChild(open);
                dashReports_->AddChild(row);
            }
            if (n == 0) dashReports_->AddChild(Theme::Caption("uoD.r.none", Tr("No open report.")));

            // Compliance
            dashCompliance_->ClearChildren();
            n = 0;
            auto line = [&](const std::string& text, Theme::Tint tint, const std::string& word) {
                const std::string id = "uoD.c" + std::to_string(n++);
                auto row = Theme::Row(id, Theme::kInnerGap);
                row->AddChild(Theme::Pill(id + ".p", word, tint));
                auto label = Theme::Caption(id + ".t", text);
                label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                row->AddChild(label);
                dashCompliance_->AddChild(row);
            };
            if (notOnTerms > 0) line(TrF("{0} not on terms {1}: they are paused until they accept", TrN("{n} merchant", "{n} merchants", notOnTerms), terms_.inForce), Theme::Tint::Amber, Tr("terms"));
            if (warned > 0) line(TrN("{n} merchant carries a strike", "{n} merchants carry strikes", warned), Theme::Tint::Amber, Tr("strikes"));
            if (suspended > 0) line(TrN("{n} merchant is suspended", "{n} merchants are suspended", suspended), Theme::Tint::Red, Tr("suspended"));
            if (n == 0) line(Tr("Every merchant is on the terms, with no strike and no suspension."), Theme::Tint::Green, Tr("all clear"));

            // The latest actions
            dashActions_->ClearChildren();
            n = 0;
            for (const AuditRow& r : audit_) dashActions_->AddChild(Theme::Caption("uoD.a" + std::to_string(n++), r.Line()));
            if (n == 0) dashActions_->AddChild(Theme::Caption("uoD.a.none", Tr("Nothing in the audit log yet.")));
        };
        return out;
    });
}

// ===========================================================================
// Audit log
// ===========================================================================
void OperatorWindow::BuildAuditPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kAudit, Tr("Audit log"), Tr("Every operator action, one row, hash-chained: a gap or an edit shows."), header, body);
    auto refresh = Theme::Button("uoA.refresh", Tr("Refresh"), 100);
    refresh->onClick = [this]() { RefreshAudit(); };
    header->AddChild(refresh);
    auto verify = Theme::Button("uoA.verify", Tr("Verify the chain"), 140, Theme::ButtonKind::Primary);
    verify->onClick = [this]() {
        Fetch<AuditCheck>(Tr("Walking the chain"), [client = Client()]() { return client.VerifyAudit(); }, [this](const AuditCheck& c) {
            auditCheck_->SetText(c.ok ? TrF("The chain verifies: {0}, last hash {1}", TrN("{n} row", "{n} rows", static_cast<long long>(c.rows)), c.lastHash)
                                      : TrF("The chain is BROKEN at row {0} of {1}: a row was edited or removed.", c.brokenAt, c.rows));
            auditCheck_->SetTextColor(c.ok ? Theme::kSuccess : Theme::kDanger);
        });
    };
    header->AddChild(verify);
    auditCheck_ = Theme::Caption("uoA.check", "");
    body->AddChild(auditCheck_);
    auto list = Theme::Section("uoA.list", Tr("The latest rows"));
    auditList_ = Theme::Column("uoA.rows", 4);
    list->AddChild(auditList_);
    body->AddChild(list);
}

void OperatorWindow::RefreshAudit() {
    if (!Ready(false)) return;
    const OperatorClient client = Client();
    Fetch<std::vector<AuditRow>>(Tr("Reading the audit log"), [client]() -> Result<std::vector<AuditRow>> {
        using R = Result<std::vector<AuditRow>>;
        const auto check = client.VerifyAudit();
        if (!check) return R::Failure(check.error.code, check.error.message, check.error.detail);
        const auto page = client.Audit(std::max<int64_t>(0, check.Value().rows - 200), 200);
        if (!page) return R::Failure(page.error.code, page.error.message, page.error.detail);
        std::vector<AuditRow> rows = page.Value().rows;
        std::reverse(rows.begin(), rows.end());
        return R::Succeed(rows);
    }, [this](const std::vector<AuditRow>& rows) {
        audit_ = rows;
        auditList_->ClearChildren();
        int n = 0;
        for (const AuditRow& r : audit_) {
            const std::string id = "uoA.r" + std::to_string(n++);
            auto row = Theme::Row(id, Theme::kInnerGap);
            auto number = Theme::Caption(id + ".n", std::to_string(r.id));
            number->SetElementSize(CSSLayout::Dimension::Px(56), CSSLayout::Dimension::Auto());
            row->AddChild(number);
            auto text = Theme::Caption(id + ".t", r.Line());
            text->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
            row->AddChild(text);
            auditList_->AddChild(row);
        }
        if (n == 0) auditList_->AddChild(Theme::Caption("uoA.none", Tr("Nothing in the audit log yet.")));
    });
}

// ===========================================================================
// Settings
// ===========================================================================
void OperatorWindow::BuildSettingsPage() {
    std::shared_ptr<Container> header, body;
    BuildPage(kSettings, Tr("Settings"), Tr("The marketplace service this window drives, and the token it drives it with."), header, body);
    auto surface = Theme::Section("uoS.service", Tr("The marketplace service"));
    sUrl_ = Theme::Pin(CreateTextInput("uoS.url", 0, 0, 480, Theme::kControlHeight));
    sUrl_->SetPlaceholder("https://market.example");
    surface->AddChild(Theme::Field("uoS.urlRow", Tr("Service URL"), sUrl_));
    sToken_ = Theme::Pin(CreatePasswordInput("uoS.token", 0, 0, 480, Theme::kControlHeight));
    sToken_->SetPlaceholder(Tr("the service's ULTRASTORE_MARKET_OPERATOR_TOKEN"));
    surface->AddChild(Theme::Field("uoS.tokenRow", Tr("Operator token"), sToken_));
    sName_ = Theme::Pin(CreateTextInput("uoS.name", 0, 0, 300, Theme::kControlHeight));
    sName_->SetPlaceholder(Tr("shown in the window, and as the assignee of a report"));
    surface->AddChild(Theme::Field("uoS.nameRow", Tr("Your name"), sName_));
    settingsInfo_ = Theme::Caption("uoS.info", "");
    surface->AddChild(settingsInfo_);
    auto buttons = Theme::Row("uoS.buttons", Theme::kInnerGap, true);
    auto save = Theme::Button("uoS.save", Tr("Save"), 100, Theme::ButtonKind::Primary);
    save->onClick = [this]() { SaveSettingsPage(); };
    buttons->AddChild(save);
    auto check = Theme::Button("uoS.check", Tr("Check the service"), 140);
    check->onClick = [this]() {
        SaveSettingsPage();
        OperatorClient client(settings_);
        Fetch<std::string>(Tr("Asking the service"), [client]() { return client.Health(); }, [this](const std::string& health) {
            SetStatus(health == "ok" ? Tr("The service answers: ok") : TrF("The service answers: {0}", health), health != "ok");
        });
    };
    buttons->AddChild(check);
    surface->AddChild(buttons);
    body->AddChild(surface);
    body->AddChild(Theme::Caption("uoS.hint", TrF("The settings are kept in {0}, readable by you alone; ULTRASTORE_OPERATOR_URL and ULTRASTORE_OPERATOR_TOKEN in the environment override them.", settingsPath_)));
}

void OperatorWindow::LoadSettingsPage() {
    if (!sUrl_) return;
    sUrl_->SetText(settings_.url);
    sToken_->SetText(settings_.token);
    sName_->SetText(settings_.name);
    settingsInfo_->SetText(settings_.Complete() ? "" : Tr("Both the URL and the token are needed before any page can ask the service."));
}

void OperatorWindow::SaveSettingsPage() {
    settings_.url = sUrl_->GetText();
    settings_.token = sToken_->GetText();
    settings_.name = sName_->GetText();
    while (!settings_.url.empty() && settings_.url.back() == '/') settings_.url.pop_back();
    if (const auto saved = settings_.Save(settingsPath_); !saved) { SetStatus(saved.error.message, true); return; }
    settingsInfo_->SetText(settings_.Complete() ? "" : Tr("Both the URL and the token are needed before any page can ask the service."));
    SetStatus(Tr("Settings saved"));
    UpdateSidebarNote("");
    Relayout();
}

} // namespace UltraStoreOperator::Ui
