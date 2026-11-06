// Apps/UltraStore/ui/StoreWindow.h
// The store's window after the UltraStore shop design: a header with the
// wordmark, the sections as pills (Discover, Categories, Installed,
// Updates), the search pill and Settings; the page it selects; the download
// queue in a collapsible panel along the bottom; a status line. Every
// operation goes through the core (StoreEngine, CatalogClient), run on one
// worker thread at a time, so the window and the command line never
// disagree; the window only shows what the core reports and asks it for
// the next thing.
//
// Pages (StoreWindow.cpp):
//  - Discover: the featured app (the newest release in the catalog) as a
//    hero, the categories as pills, "New and updated" as a numbered list,
//    "Free essentials" as cards, the four reasons for the store, and the
//    partner / discount code. A search in the header turns the page into
//    the results, as cards.
//  - Categories: the filters on the left (the catalog's categories with
//    their counts, free or paid, open source only, hide installed), the
//    heading with the count and the sort order, and the cards in a grid.
//  - Installed: the installed apps as cards, the runtimes and libraries
//    under them, and the clean-up (garbage collection).
//  - Updates: the installed apps a newer release exists for, one by one or
//    all at once; the header pill carries the count.
//  - Details: one app, the shop's app page - the breadcrumb, the icon, the
//    name, the facts, the actions, what this machine downloads, the
//    screenshots, the description, what is new and the version history,
//    what it installs, the information column with the links, more from
//    the same publisher.
//  - Settings (StoreSettingsPage.cpp): channel, language, servers, the keys
//    the user trusts beside the compiled-in ones, the machine the store
//    detected, the catalog's state, the store folder.
//
// Jobs (StoreJobs.cpp): Refresh (the catalog from the servers), Reload
// (what is installed), Install, Update, UpdateAll, Remove, Collect; queued,
// one running, each a row of the Downloads panel with its progress bar and
// Pause / Resume / Cancel. The worker posts to the UI thread
// (UltraCanvasApplication::PostToUIThread) and never touches an element.
//
// Pictures (icons, screenshots) come through AssetCache on a second thread
// and are set on the cards when they arrive.
// Version: 0.5.0 - the shop design: the header, the Discover sections, the Categories filters, the app page
// Version: 0.4.0 - the open order resumed, the update terms
// Version: 0.3.0 - Buy through the payment service; the licences
// Version: 0.2.0 - codes and ultrastore:// links
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AssetCache.h"
#include "Catalog.h"
#include "CatalogClient.h"
#include "PackageResolver.h"
#include "PlatformInfo.h"
#include "Purchase.h"
#include "StoreEngine.h"
#include "StoreQuery.h"
#include "StoreSettings.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasChipGroup.h"
#include "UltraCanvasCollapsiblePanel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace UltraStore::Ui {

struct WindowOptions {
    std::string catalogFile;   // show this catalog.json instead of the servers' (no signature check)
    std::string root;          // the store folder; empty: the platform's
    std::string channel;       // "stable" or "beta"; empty: the setting
};

class StoreWindow {
public:
    StoreWindow(UltraCanvas::UltraCanvasApplication& app, WindowOptions options);
    ~StoreWindow();

    StoreWindow(const StoreWindow&) = delete;
    StoreWindow& operator=(const StoreWindow&) = delete;

    bool Create();
    void Show();

    // A link from a QR code or a page: ultrastore://code/<CODE> enters the
    // code; ultrastore://app/<id>[?code=<CODE>] opens the app's details,
    // with the code when it carries one. Anything else is said in the
    // status line.
    void OpenLink(const std::string& url);
    // The code the person typed or arrived with: checked against the
    // catalog, kept in force for StoreSettings::kCodeDays, shown in the
    // prices. An empty text forgets the code.
    void ApplyCode(const std::string& text);

private:
    // ===== shell (StoreWindow.cpp) =====
    void BuildShell();
    void BuildHeader();
    std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeNavButton(const std::string& id, const std::string& text, float width);
    // A page: a column with the gutter, scrolled as one; the body is where
    // the sections go.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildScrollPage(const std::string& id,
                                                                       std::shared_ptr<UltraCanvas::UltraCanvasContainer>& body);
    void ShowPage(const std::string& id);
    void LayoutForSize(float width, float height);
    void SetStatus(const std::string& text, bool isError = false);
    void Alert(const std::string& message, bool isError = true);
    bool OnClosing();

    // ===== data =====
    void OpenStore();                       // the engine, the client, the cached or local catalog
    void SetCatalog(std::shared_ptr<const Catalog> catalog, const std::string& note);
    void SetInstalled(std::vector<InstalledRecord> records);
    ListingFilter BaseFilter() const;
    std::string Lang() const { return lang_; }
    std::string ServerNote() const;         // why nothing can be fetched, or empty
    std::string Today() const;              // "YYYY-MM-DD", UTC
    const PromoCode* CodeInForce() const;   // the settings' code when the catalog knows it and it is valid today
    const PromoCode* CodeFor(const Component& app) const;   // CodeInForce() when it applies to the app
    // The apps of the catalog this machine lists, with the filter.
    std::vector<AppListing> Listings(const ListingFilter& filter) const;
    // Sorted: 0 newest release first, 1 by name, 2 price low to high.
    static void SortListings(std::vector<AppListing>& apps, int mode);
    // The app the Discover hero shows: the newest release in the catalog.
    std::optional<AppListing> FeaturedApp() const;
    // What this machine would download for the app's release, with what it
    // already has; nothing when there is no package for it.
    std::optional<InstallPlan> PlanFor(const AppListing& l) const;
    std::string CategoryOf(const Component& app) const;    // the first category, or ""
    std::string PublisherName(const Component& app) const; // the catalog's name for the publisher id, else the id
    const Publisher* PublisherOf(const Component& app) const;
    std::string LanguagesOf(const Component& app) const;   // "English, Deutsch, +6"
    std::string PriceText(const Component& app) const;     // "Free", "29.90 EUR", with the code in force; no trial note

    // ===== pages =====
    void RebuildAllPages();
    void RebuildDiscover();
    void RebuildCategories();
    void RebuildInstalled();
    void RebuildUpdates();
    // The cards of the apps into a wrapping grid, or the empty text.
    void FillGrid(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& grid, const std::vector<AppListing>& apps,
                  const std::string& idPrefix, const std::string& emptyText);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> MakeAppCard(const AppListing& l, const std::string& id);
    // A numbered row of "New and updated": the icon, the name, the version
    // line, the action.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeAppRow(const AppListing& l, const std::string& id, int number);
    // A compact row of "More from the publisher".
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeAppLink(const AppListing& l, const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeHero(const AppListing& l);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeCategoryPills();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeWhyStrip();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeCodeRow();
    // A picture element that loads from the cache, or asks for the URL.
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> MakePicture(const std::string& id, const std::string& url, float w, float h);
    // Install / Try / Buy, Open, Update, Remove - what fits the app's state.
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasButton>> PrimaryActions(const AppListing& l, const std::string& id);
    void ShowDetails(const std::string& appId);
    void HideDetails();
    void RebuildDetails();
    std::string StateLine(const AppListing& l) const;
    // The facts under an app's name: the trial, the licence, the age, the
    // account, the update period.
    std::vector<std::string> FactsOf(const AppListing& l) const;

    // ===== actions =====
    void InstallApp(const std::string& appId);
    void BuyApp(const std::string& appId);     // through the catalog's payment service; else the publisher's page
    void UpdateApp(const std::string& appId);
    void UpdateAll();
    void RemoveApp(const std::string& appId);
    void LaunchApp(const AppListing& l);
    void Collect();
    void RefreshCatalog();
    void ReloadInstalled();

    // ===== pictures =====
    void RequestPicture(const std::string& url, const std::weak_ptr<UltraCanvas::UltraCanvasUIElement>& target);
    void PictureReady(const std::string& url, const std::string& path);
    void StopPictureThread();

    // ===== jobs (StoreJobs.cpp) =====
    enum class JobKind { Refresh, Reload, Install, Update, UpdateAll, Remove, Collect, Buy };
    struct Job {
        uint64_t id = 0;
        JobKind kind = JobKind::Reload;
        std::string appId;
        std::string title;
        bool showInQueue = true;
        bool resume = false;   // Buy: the settings' open order, not a new one
    };
    struct JobResult {
        bool ok = true;
        std::string message;                       // for the status line
        bool haveInstalled = false;
        std::vector<InstalledRecord> installed;
        bool haveLicences = false;
        std::vector<LicenceRecord> licences;
        std::shared_ptr<const Catalog> catalog;    // Refresh: the new catalog
        CatalogState state;
    };
    enum class QueueState { Waiting, Running, Done, Failed, Cancelled };
    struct QueueEntry {
        uint64_t jobId = 0;
        std::string title;
        std::string status;
        QueueState state = QueueState::Waiting;
        double fraction = -1.0;                    // < 0: unknown
        bool paused = false;
        std::weak_ptr<UltraCanvas::UltraCanvasGaugeDiagramElement> gauge;
        std::weak_ptr<UltraCanvas::UltraCanvasLabel> statusLabel;
        std::weak_ptr<UltraCanvas::UltraCanvasButton> pauseButton;
    };
    void Enqueue(Job job);
    void RunNextJob();
    JobResult RunJob(const Job& job, const std::shared_ptr<const Catalog>& catalog, const std::string& channel,
                     const std::function<void(const std::string&, double)>& progress);
    void JobProgress(uint64_t jobId, const std::string& status, double fraction);
    void JobFinished(uint64_t jobId, JobResult result);
    void CancelJob(uint64_t jobId);
    void TogglePause(uint64_t jobId);
    void DismissFinished();
    void RebuildQueuePanel();
    void UpdateQueueSummary();
    QueueEntry* FindEntry(uint64_t jobId);
    bool Busy() const { return running_.has_value(); }
    bool HasJobFor(const std::string& appId) const;
    void StopJobThread();

    // ===== settings page (StoreSettingsPage.cpp) =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSettingsPage();
    void LoadSettingsIntoPage();
    void SaveSettingsFromPage();
    void RefreshSettingsInfo();
    void ApplySettings();                   // the client follows the settings

    // ===== members =====
    UltraCanvas::UltraCanvasApplication& app_;
    WindowOptions options_;
    StoreSettings settings_;
    std::string lang_ = "en";
    std::string channel_ = "stable";
    PlatformInfo platform_;

    std::unique_ptr<StoreEngine> engine_;
    std::unique_ptr<CatalogClient> client_;
    std::unique_ptr<AssetCache> assets_;
    std::string storeError_;                // the engine could not open
    std::shared_ptr<const Catalog> catalog_;
    std::string catalogNote_;               // where the catalog came from, or why there is none
    CatalogState catalogState_;
    std::vector<InstalledRecord> installed_;
    std::vector<LicenceRecord> licences_;      // the apps bought here
    const LicenceRecord* Bought(const std::string& appId) const;
    // The licence's terms for an app bought here, else an empty Licence.
    Licence PurchaseOf(const std::string& appId) const;
    bool resumedOrder_ = false;                // the settings' open order was looked at once
    bool localCatalog_ = false;
    bool autoExpanding_ = false;            // the panel opening for a job, not the user's fold

    std::shared_ptr<int> alive_ = std::make_shared<int>(1);   // posted tasks check it

    // the shell
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> logoButton_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasButton>> navButtons_;   // page id -> pill
    std::shared_ptr<UltraCanvas::UltraCanvasBadge> updatesBadge_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> searchInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> pageHost_;
    std::shared_ptr<UltraCanvas::UltraCanvasCollapsiblePanel> downloads_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> queueList_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> pages_;
    std::string currentPage_ = "discover";
    std::string pageBeforeDetails_ = "discover";

    // Discover
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> discoverBody_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> codeInput_;
    std::string search_;
    // Categories
    std::shared_ptr<UltraCanvas::UltraCanvasChipGroup> categoryChips_;
    std::vector<std::string> categories_;
    std::string category_;
    std::string priceFilter_ = "all";       // "all", "free", "paid"
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasButton>> priceButtons_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> openSourceBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> hideInstalledBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> sortDropdown_;
    int sortMode_ = 0;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> categoryHeading_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> categoryGrid_;
    // Installed
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> installedBody_;
    // Updates
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> updatesBody_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> updateAllButton_;
    // Details
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> detailsPage_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> detailsBody_;
    std::string detailsAppId_;
    // Settings
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> channelDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> languageInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> serversArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> keysArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> refreshOnStartBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> settingsInfo_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> builtinInfo_;

    // jobs
    std::deque<Job> pending_;
    std::optional<Job> running_;
    std::vector<QueueEntry> queue_;
    std::thread jobThread_;
    std::atomic<bool> cancelRunning_{false};
    uint64_t nextJobId_ = 1;

    // pictures
    std::thread pictureThread_;
    std::mutex pictureMutex_;
    std::condition_variable pictureWake_;
    std::deque<std::string> pictureQueue_;
    bool pictureStop_ = false;
    std::map<std::string, std::string> picturePaths_;                                            // url -> file
    std::map<std::string, std::vector<std::weak_ptr<UltraCanvas::UltraCanvasUIElement>>> pictureWaiters_;

    static constexpr int kMinWidth = 900;
    static constexpr int kMinHeight = 560;
};

} // namespace UltraStore::Ui
