// Apps/EmailCleaner/ui/EmailCleanerApp.h
// The EmailCleaner application manager: owns the analysis database, the
// ingest and the three views, and wires them to the account bar's filters.
//
// Accounts come from UltraMail — its LocalStore holds the configured accounts
// and its sync engine caches the message bodies. EmailCleaner mirrors the
// account list into its own database and analyses that cache, so the two apps
// share one mailbox without either reaching into the other's tables.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"

#include "EmailCleanerAccountBar.h"
#include "EmailCleanerMapView.h"
#include "EmailCleanerTimetableView.h"
#include "EmailCleanerDetailView.h"
#include "EmailCleanerActionsPanel.h"
#include "EmailCleanerRulesDialog.h"

#include "EmailCleanerAnalytics.h"
#include "EmailCleanerIngest.h"
#include "EmailCleanerAttachments.h"
#include "EmailCleanerMailBackend.h"
#include "EmailCleanerStore.h"

#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class EmailCleanerApp {
public:
    // Open the analysis database under `dataDir` and pick up the accounts and
    // cached mail UltraMail keeps under `mailDataDir`. Returns false when the
    // database cannot be opened.
    bool Initialize(const std::string& dataDir, const std::string& mailDataDir);

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateMainWindow();

    // Re-read the accounts and repaint every view from the database.
    void Refresh();

private:
    // Give the actions panel a way out to the mail server: UltraNet's IMAP
    // plug-in, plus each account's credentials from UltraMail's vault. When
    // the plug-in or the credentials are missing the panel says so and the
    // local half (blocking) still works.
    void WireMailBackend();
    // Open the keyword rule editor, and re-analyse once it has written.
    void EditRules();
    // Show one message's attachments, and open one in UltraCanvasMediaViewer.
    void ShowAttachments(const AnalyzedMessage& message);
    // The strongest keyword behind the current selection, to seed a new rule.
    std::string TopTermForSelection() const;
    void OpenAttachment(const AnalyzedMessage& message, const AttachmentRecord& record);
    // Analyse the mail UltraMail has cached for the selected account (or every
    // account when none is selected).
    void ScanMailCache();
    // Run the classifier over the stored corpus again, after a rule change.
    void Reanalyse();
    // Load the user's rule file, layered over the built-ins, if it exists.
    void LoadRules();
    // Mirror UltraMail's account list into the analysis database.
    void ImportAccounts();

    // The filter every view shares: the account bar's, plus the map selection.
    MessageFilter CurrentFilter() const;
    // What the current selection should be called in a heading.
    std::string   CurrentTitle() const;
    // The same selection as something to act on.
    ActionTarget  CurrentTarget() const;

    AnalysisStore store_;
    Analytics     analytics_{ store_ };
    Ingestor      ingestor_{ store_ };

    std::string dataDir_;
    std::string mailDataDir_;
    std::string mailCacheDir_;
    std::string rulesPath_;

    std::vector<StoredAccount> accounts_;
    std::string selectedSender_;
    std::string selectedDomain_;

    // Kept alive for as long as the backend refers to it.
    std::shared_ptr<IUltraNetPlugin>  imapPlugin_;
    std::unique_ptr<MailBackend>      mailBackend_;
    std::string                       backendUnavailable_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>          window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    AccountBar    accountBar_;
    MapView       mapView_;
    TimetableView timetableView_;
    DetailView    detailView_;
    ActionsPanel  actionsPanel_;
    RulesDialog   rulesDialog_;

    // Attachment viewers, kept alive for as long as they are on screen.
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace EmailCleaner
