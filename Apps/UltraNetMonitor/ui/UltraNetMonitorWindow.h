// Apps/UltraNetMonitor/ui/UltraNetMonitorWindow.h
// UltraNetMonitor's window. Four tabs: *Live* - the processes on the left,
// every connection on the right, a filter box and a pause button above them
// - *History* - the flows the activity store has recorded, over a chosen
// range - *Names* - every address the name sources have put a domain name
// to, with the source and how far to trust it - and *Events* - connections
// as they open and close, from the event sources, live or as recorded. A
// *Record* toggle on the toolbar writes every snapshot the worker takes,
// every DNS observation and every connection event the sources report,
// into the store at the platform's per-user data path. Nothing is painted
// by hand: five UltraCanvasListViews over the models in
// UltraNetMonitorModels, each behind an UltraCanvasListSortFilterProxy.
//
// The socket table is read on a worker thread once a second (walking /proc
// is I/O, and it must never stall a repaint); the result is parked in one
// slot that a UI timer applies on the main thread - the same shape
// UltraCleaner uses for its scanner. Recording happens on that worker too,
// and on the name sources' threads, under a mutex the History tab's
// queries share, so the single SQLite connection is never used from two
// threads at once. The name and event sources themselves are started by
// main.cpp before the window opens; the window only reports them.
// Version: 0.5.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetMonitorModels.h"

#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorEvents.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorStore.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace UltraNetMonitor {

class UltraNetMonitorWindow {
public:
    ~UltraNetMonitorWindow();

    // `nameNotes` are what main.cpp has to say about the name and event
    // sources it started or could not (a proxy port that needed privilege,
    // a tracker that needs root); the Names and Events tabs show them.
    bool Initialize(std::vector<std::string> nameNotes = {});
    void Show();

private:
    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildLivePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildHistoryPage();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildProcessList();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildConnectionList();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildFlowList();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildNamesPage();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildNameList();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildEventsPage();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildEventList();

    // ===== DATA FLOW =====
    void StartWorker();
    void StopWorker();
    void ApplyPendingSnapshot();       // UI thread, from the timer
    void RefreshStatus();
    void SetPaused(bool paused);

    // ===== FILTERING =====
    // The connection list narrows to one PID when a process row is selected;
    // the selection is remembered by PID, not row, because every snapshot
    // re-sorts the rows under it.
    void SelectProcess(std::optional<uint32_t> pid);
    void ReapplyProcessSelection();
    void RebuildConnectionFilter();

    // ===== THE STORE =====
    // Opens the store at the default path on first use. Caller holds
    // storeMutex_. False, with storeError_ set, when it cannot be opened.
    bool EnsureStoreOpen();
    void ToggleRecording();
    void RefreshHistory();
    void PurgeHistory();
    int64_t HistoryRangeSeconds() const;

    // ===== NAMES =====
    void RefreshNames();
    std::string NameSourcesSummary() const;

    // ===== EVENTS =====
    // Live shows the registry's ring; Recorded queries the store over the
    // History tab's range. The toggle switches, Refresh re-reads.
    void RefreshEvents();
    void ToggleEventsRecorded();
    std::string EventSourcesSummary() const;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> allButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> pauseButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> recordButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> subtitleLabel_;

    std::shared_ptr<ProcessListModel> processModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> processProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> processView_;

    std::shared_ptr<ConnectionListModel> connectionModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> connectionProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> connectionView_;

    // History tab
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> rangeDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> historyFilter_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> refreshButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> purgeButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> historyStatus_;
    std::shared_ptr<FlowListModel> flowModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> flowProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> flowView_;
    // "Purge" asks twice: the first click arms it, the second acts. A refresh
    // or a tab change disarms it.
    bool purgeArmed_ = false;

    // Names tab
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> namesFilter_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> namesRefreshButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> namesStatus_;
    std::shared_ptr<NameListModel> nameModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> nameProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> nameView_;
    std::vector<std::string> nameNotes_;
    UltraCanvas::NameListenerId nameListener_ = 0;
    std::atomic<int64_t> recordedObservations_{0};
    unsigned snapshotsApplied_ = 0;

    // Events tab
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> eventsFilter_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> eventsModeButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> eventsRefreshButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> eventsClearButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> eventsStatus_;
    std::shared_ptr<EventListModel> eventModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> eventProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> eventView_;
    bool eventsRecorded_ = false;
    UltraCanvas::EventListenerId eventListener_ = 0;
    std::atomic<int64_t> recordedEvents_{0};

    // Worker -> UI: one slot, overwritten, never queued.
    std::thread worker_;
    std::atomic<bool> stopWorker_{false};
    std::atomic<bool> paused_{false};
    std::mutex pendingMutex_;
    std::vector<UltraCanvas::NetworkConnection> pendingConnections_;
    UltraCanvas::NetworkMonitorResult pendingResult_;
    bool pendingDirty_ = false;

    // The store: opened lazily, used by the worker (recording) and the UI
    // thread (history) under one mutex.
    std::mutex storeMutex_;
    UltraCanvas::NetworkMonitorStoreHandle store_ = UltraCanvas::NetworkMonitorInvalidStore;
    std::string storePath_;
    std::string storeError_;
    std::atomic<bool> recording_{false};
    std::atomic<int64_t> recordedSnapshots_{0};
    std::string recordError_;   // worker -> UI, under storeMutex_

    UltraCanvas::NetworkMonitorCapabilities capabilities_;
    UltraCanvas::NetworkMonitorResult lastResult_;
    std::size_t lastConnectionCount_ = 0;
    std::size_t lastProcessCount_ = 0;
    std::optional<uint32_t> selectedPid_;
    // Set while the window itself changes the process selection, so the
    // selection callback does not treat it as the user's click.
    bool adjustingSelection_ = false;
    UltraCanvas::TimerId uiTimer_ = 0;
};

} // namespace UltraNetMonitor
