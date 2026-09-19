// Apps/UltraNetMonitor/ui/UltraNetMonitorWindow.h
// UltraNetMonitor's window: the processes on the left, every connection on
// the right, a filter box and a pause button above them, and a status line
// that says what the backend can and cannot see. Nothing is painted by hand
// - it is two UltraCanvasListViews over the models in UltraNetMonitorModels,
// each behind an UltraCanvasListSortFilterProxy for header-click sorting and
// filtering.
//
// The socket table is read on a worker thread once a second (walking /proc
// is I/O, and it must never stall a repaint); the result is parked in one
// slot that a UI timer applies on the main thread - the same shape
// UltraCleaner uses for its scanner.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetMonitorModels.h"

#include "NetworkMonitor/NetworkMonitor.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
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

    bool Initialize();
    void Show();

private:
    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildProcessList();
    std::shared_ptr<UltraCanvas::UltraCanvasListView> BuildConnectionList();

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

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> allButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> pauseButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> subtitleLabel_;

    std::shared_ptr<ProcessListModel> processModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> processProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> processView_;

    std::shared_ptr<ConnectionListModel> connectionModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasListSortFilterProxy> connectionProxy_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> connectionView_;

    // Worker -> UI: one slot, overwritten, never queued.
    std::thread worker_;
    std::atomic<bool> stopWorker_{false};
    std::atomic<bool> paused_{false};
    std::mutex pendingMutex_;
    std::vector<UltraCanvas::NetworkConnection> pendingConnections_;
    UltraCanvas::NetworkMonitorResult pendingResult_;
    bool pendingDirty_ = false;

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
