// Apps/EmailCleaner/ui/EmailCleanerApp.h
// The EmailCleaner application manager: owns the analysis database, the
// ingest and the three views, and wires them to the account bar's filters.
//
// Accounts come from two places. UltraMail's are shared: its LocalStore holds
// them and its sync engine caches the message bodies, which EmailCleaner
// mirrors and analyses without writing to either. Accounts added under
// "Accounts…" are EmailCleaner's own: their own list, vault and body cache
// under EmailCleaner's data directory, fetched by the same UltraMail
// SyncEngine (EmailCleanerAccounts.h). Both land in the one analysis database.
// Version: 0.3.0 - own accounts beside UltraMail's
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
#include "EmailCleanerAccountsDialog.h"

#include "EmailCleanerAccounts.h"
#include "EmailCleanerAnalytics.h"
#include "EmailCleanerIngest.h"
#include "EmailCleanerAttachments.h"
#include "EmailCleanerMailBackend.h"
#include "EmailCleanerStore.h"

#include <map>
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
    // plug-in, plus each account's credentials — from UltraMail's vault for
    // the shared accounts, from EmailCleaner's own for the rest. When the
    // plug-in or the credentials are missing the panel says so and the local
    // half (blocking) still works.
    void WireMailBackend();
    // The UltraMail half of WireMailBackend; returns how many were usable and
    // says in `problem` why none were, when that is the vault's fault.
    int  RegisterUltraMailAccounts(std::string& problem);
    // The own half: every own account with a saved password.
    int  RegisterOwnAccounts();
    void RegisterOwnAccount(const UltraMail::Account& account, const std::string& password);

    // ---- Own accounts ------------------------------------------------------
    // Open the "Accounts…" dialog.
    void ManageAccounts();
    // The dialog's rows: every account, with where its mail comes from.
    std::vector<AccountsDialog::Row> AccountRows() const;
    // Check the sign-in off the UI thread, then save the account and its
    // password and start downloading its mail. `done` gets "" or the reason.
    void AddOwnAccount(const NewAccountRequest& request,
                       std::function<void(const std::string&)> done);
    void RemoveOwnAccount(const StoredAccount& account);
    // Download the own accounts in `accountIds` (off the UI thread), then
    // analyse every in-scope cache. Empty = nothing to download: analyse now.
    void FetchThenAnalyse(const std::vector<std::string>& accountIds);
    // Analyse the cache of every account in scope (the account bar's choice).
    void AnalyseCaches(bool skipExisting, const std::string& fetchReport);
    // Where an account's cached bodies are: UltraMail's cache, or EmailCleaner's.
    std::string CacheDirFor(const std::string& accountId) const;
    // The saved password of an own account ("" when there is none).
    std::string OwnPassword(const std::string& accountId) const;
    // Open the keyword rule editor, and re-analyse once it has written.
    void EditRules();
    // Show one message's attachments, and open one in UltraCanvasMediaViewer.
    void ShowAttachments(const AnalyzedMessage& message);
    // The strongest keyword behind the current selection, to seed a new rule.
    std::string TopTermForSelection() const;
    void OpenAttachment(const AnalyzedMessage& message, const AttachmentRecord& record);
    // "Load mail": download what is new for the own accounts in scope, then
    // analyse the cache of the selected account (or every account when none
    // is selected) — UltraMail's accounts as UltraMail last synced them.
    void ScanMailCache();
    // Run the classifier over the stored corpus again, after a rule change.
    void Reanalyse();
    // Load the user's rule file, layered over the built-ins, if it exists.
    void LoadRules();
    // Mirror UltraMail's account list — and EmailCleaner's own — into the
    // analysis database.
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
    std::string pluginDir_;   // where UltraNet's plug-ins were looked for

    std::vector<StoredAccount> accounts_;
    // The full records behind accounts_ (servers included), by account id —
    // what the mail backend and the fetch need and the analysis rows omit.
    std::map<std::string, UltraMail::Account> ultraMailAccounts_;
    OwnAccounts   ownAccounts_;
    bool          fetching_ = false;   // an own-account download is running
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
    AccountsDialog accountsDialog_;

    // Attachment viewers, kept alive for as long as they are on screen.
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> viewerWindows_;
};

} // namespace EmailCleaner
