// Apps/UltraStore/ui/StoreWindow.h
// The store's window: a sidebar (Discover, Categories, Installed, Updates;
// Settings), the page it selects, the download queue in a collapsible panel
// along the bottom, a status line. Every operation goes through the core
// (StoreEngine, CatalogClient), run on one worker thread at a time, so the
// window and the command line never disagree; the window only shows what
// the core reports and asks it for the next thing.
//
// Pages (StoreWindow.cpp):
//  - Discover: a search field, every app of the catalog as a card with its
//    icon, publisher, summary, price or state, and the action that fits
//    (Install, Open, Update, Remove). The card's surface opens the details.
//  - Categories: the catalog's categories as a single-select chip group
//    over the same cards.
//  - Installed: the installed apps, the runtimes and libraries under them,
//    and the clean-up (garbage collection).
//  - Updates: the installed apps a newer release exists for, one by one or
//    all at once.
//  - Details: one app - icon, texts, screenshots, release notes, what it
//    needs, links - with a way back.
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
// Version: 0.4.0 - the open order resumed, the update terms
// Version: 0.3.0 - Buy through the payment service; the licences
// Version: 0.2.0 - codes and ultrastore:// links
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AssetCache.h"
#include "Catalog.h"
#include "CatalogClient.h"
#include "PlatformInfo.h"
#include "Purchase.h"
#include "StoreEngine.h"
#include "StoreQuery.h"
#include "StoreSettings.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCard.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasChipGroup.h"
#include "UltraCanvasCollapsiblePanel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSidebarNavigation.h"
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
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildListPage(const std::string& id, const std::string& title,
                                                                     std::shared_ptr<UltraCanvas::UltraCanvasContainer>& header,
                                                                     std::shared_ptr<UltraCanvas::UltraCanvasContainer>& list);
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

    // ===== pages =====
    void RebuildAllPages();
    void RebuildDiscover();
    void RebuildCategories();
    void RebuildInstalled();
    void RebuildUpdates();
    void FillList(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& list, const std::vector<AppListing>& apps,
                  const std::string& idPrefix, const std::string& emptyText);
    std::shared_ptr<UltraCanvas::UltraCanvasCard> MakeAppCard(const AppListing& l, const std::string& id);
    // Install / Try / Buy, Open, Update, Remove - what fits the app's state.
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasButton>> PrimaryActions(const AppListing& l, const std::string& id);
    void ShowDetails(const std::string& appId);
    void HideDetails();
    void RebuildDetails();
    std::string StateLine(const AppListing& l) const;

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
    std::shared_ptr<UltraCanvas::UltraCanvasSidebarNavigation> sidebar_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> pageHost_;
    std::shared_ptr<UltraCanvas::UltraCanvasCollapsiblePanel> downloads_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> queueList_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> pages_;
    std::string currentPage_ = "discover";
    std::string pageBeforeDetails_ = "discover";

    // Discover
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> searchInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> discoverList_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> discoverNote_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> codeInput_;
    std::string search_;
    // Categories
    std::shared_ptr<UltraCanvas::UltraCanvasChipGroup> categoryChips_;
    std::vector<std::string> categories_;
    std::string category_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> categoryList_;
    // Installed
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> installedList_;
    // Updates
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> updatesList_;
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

    static constexpr int kMinWidth = 760;
    static constexpr int kMinHeight = 480;
};

} // namespace UltraStore::Ui
