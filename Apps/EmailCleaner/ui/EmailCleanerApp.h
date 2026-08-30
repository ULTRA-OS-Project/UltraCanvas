// Apps/EmailCleaner/ui/EmailCleanerApp.h
// The EmailCleaner application manager: owns the analysis database, the
// ingest and the three views, and wires them to the account bar's filters.
//
// Accounts come from UltraMail — its LocalStore holds the configured accounts
// and its sync engine caches the message bodies. EmailCleaner mirrors the
// account list into its own database and analyses that cache, so the two apps
// share one mailbox without either reaching into the other's tables.
// Version: 0.1.0 (Phase 1)
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

#include "EmailCleanerAnalytics.h"
#include "EmailCleanerIngest.h"
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

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>          window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    AccountBar    accountBar_;
    MapView       mapView_;
    TimetableView timetableView_;
    DetailView    detailView_;
};

} // namespace EmailCleaner
