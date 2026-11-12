// Apps/UltraStoreOperator/ui/OperatorWindow.h
// The operator's window: the design's Super* pages on the marketplace
// service - a dark sidebar (Dashboard, Merchants, App review with the
// queue's count, Reports with the open count, Terms and notices, Audit
// log, Settings) and on the right the page. Every page is a client of
// OperatorClient, the same the command line uses, so the two never
// disagree. The service is asked on a worker thread (one job at a time)
// and the page is filled through UltraCanvasApplication::PostToUIThread.
// Every action that cannot be taken back - a release, a rejection, a
// suspension, a publish - asks once.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Operator.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSidebarNavigation.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace UltraStoreOperator::Ui {

class OperatorWindow {
public:
    // startPage: the page to open on (dashboard, merchants, review, reports,
    // terms, audit, settings); the Dashboard when empty or unknown.
    OperatorWindow(UltraCanvas::UltraCanvasApplication& app, std::string settingsPathUtf8, std::string startPage = {});
    ~OperatorWindow();
    OperatorWindow(const OperatorWindow&) = delete;
    OperatorWindow& operator=(const OperatorWindow&) = delete;

    bool Create();
    void Show();

private:
    using Container = UltraCanvas::UltraCanvasContainer;
    using Label = UltraCanvas::UltraCanvasLabel;
    using Button = UltraCanvas::UltraCanvasButton;
    using TextInput = UltraCanvas::UltraCanvasTextInput;
    using TextArea = UltraCanvas::UltraCanvasTextArea;
    using Dropdown = UltraCanvas::UltraCanvasDropdown;

    // ----- shell (OperatorWindow.cpp) -----
    void BuildShell();
    std::shared_ptr<Container> BuildPage(const std::string& id, const std::string& title, const std::string& lead,
                                         std::shared_ptr<Container>& header, std::shared_ptr<Container>& body);
    void ShowPage(const std::string& id);
    void LayoutForSize(float width, float height);
    void SetStatus(const std::string& text, bool isError = false);
    void Alert(const std::string& message, bool isError = true);
    void Confirm(const std::string& message, std::function<void()> then);
    bool OnClosing();
    void Relayout();
    void UpdateSidebarNote(const std::string& numbers);
    OperatorClient Client() const;   // on the settings as they are now
    bool Ready(bool say = true);     // the settings complete, no job running

    // ----- the pages -----
    void BuildDashboardPage();
    void RefreshDashboard();
    void BuildSettingsPage();
    void LoadSettingsPage();
    void SaveSettingsPage();
    void BuildAuditPage();
    void RefreshAudit();
    // (OperatorPages.cpp)
    void BuildMerchantsPage();
    void RefreshMerchants();
    void ShowMerchants();
    void OpenMerchant(const std::string& id);
    void ShowMerchantFile();
    void BuildReviewPage();
    void RefreshReview();
    void ShowReview();
    void OpenSubmission(const std::string& id);
    void ShowSubmission();
    void DecideNow(const std::string& action);
    void PublishNow();
    void BuildReportsPage();
    void RefreshReports();
    void ShowReports();
    void OpenReport(const std::string& id);
    void ShowReport();
    void BuildTermsPage();
    void RefreshTerms();
    void ShowTerms();

    // ----- jobs -----
    struct JobOutcome {
        bool ok = true;
        std::string message;
        std::function<void()> apply;   // on the UI thread, before `done`
    };
    void RunJob(const std::string& title, std::function<JobOutcome()> work, std::function<void(const JobOutcome&)> done = nullptr);
    // One request on a worker, its value shown on the UI thread; a failure
    // goes to the status line and an alert.
    template <class T>
    void Fetch(const std::string& title, std::function<Result<T>()> work, std::function<void(const T&)> show, std::function<void()> after = nullptr) {
        RunJob(title, [work, show]() {
            JobOutcome out;
            Result<T> r = work();
            if (!r) { out.ok = false; out.message = r.error.message + (r.error.detail.empty() ? "" : " (" + r.error.detail + ")"); return out; }
            out.apply = [show, value = std::move(r.Value())]() { show(value); };
            return out;
        }, [after](const JobOutcome& out) { if (out.ok && after) after(); });
    }

    // ----- members -----
    UltraCanvas::UltraCanvasApplication& app_;
    std::string settingsPath_, startPage_;
    OperatorSettings settings_;
    std::string settingsError_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(1);
    std::thread worker_;
    std::atomic<bool> busy_{false};

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<Container> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasSidebarNavigation> sidebar_;
    std::shared_ptr<Container> pageHost_, sidebarNoteBox_;
    std::shared_ptr<Label> statusLabel_, sidebarNote_;
    std::map<std::string, std::shared_ptr<Container>> pages_;
    std::string currentPage_;

    // what was fetched
    std::vector<Merchant> merchants_;
    std::vector<Submission> submissions_;
    std::vector<Report> reports_;
    std::vector<Notice> notices_;
    TermsList terms_;
    Policy policy_;
    Released released_;
    std::vector<AuditRow> audit_;
    bool haveMerchantFile_ = false, haveSubmission_ = false, haveReport_ = false;
    MerchantFile merchantFile_;
    Submission submission_;
    Report report_;
    std::string reviewFilter_ = "waiting", reportsFilter_ = "open";

    // dashboard
    std::shared_ptr<Container> stats_, dashQueue_, dashReports_, dashCompliance_, dashActions_;
    // merchants
    std::shared_ptr<Container> merchantsList_, merchantFileBox_;
    std::shared_ptr<TextInput> mId_, mName_, mContact_, mKey_;
    std::shared_ptr<TextInput> mClause_, mReason_, mDays_, mSubject_;
    std::shared_ptr<TextArea> mText_;
    std::shared_ptr<Dropdown> mKind_;
    // review
    std::shared_ptr<Container> reviewFilters_, reviewList_, submissionBox_;
    std::shared_ptr<Label> releasedLabel_;
    std::shared_ptr<TextArea> decisionMessage_;
    std::shared_ptr<TextInput> decisionSequence_;
    // reports
    std::shared_ptr<Container> reportsFilters_, reportsList_, reportBox_;
    std::shared_ptr<TextInput> rAssignee_, rOutcome_, rTarget_, rKind_, rReporter_;
    std::shared_ptr<TextArea> rFindings_, rText_;
    std::shared_ptr<Dropdown> rTargetKind_;
    // terms and notices
    std::shared_ptr<Container> termsList_, noticesList_;
    std::shared_ptr<TextInput> tInForce_, tChanges_, tVersion_;
    std::shared_ptr<TextArea> tText_, nText_;
    std::shared_ptr<TextInput> pExpiry_, pStrikes_, pDays_, pDeadline_, pClauses_;
    std::shared_ptr<Dropdown> nKind_;
    std::shared_ptr<TextInput> nTo_, nSubject_, nClause_;
    // audit
    std::shared_ptr<Container> auditList_;
    std::shared_ptr<Label> auditCheck_;
    // settings
    std::shared_ptr<TextInput> sUrl_, sToken_, sName_;
    std::shared_ptr<Label> settingsInfo_;

    static constexpr int kMinWidth = 960;
    static constexpr int kMinHeight = 600;
};

} // namespace UltraStoreOperator::Ui
