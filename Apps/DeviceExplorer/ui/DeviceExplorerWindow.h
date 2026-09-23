// Apps/DeviceExplorer/ui/DeviceExplorerWindow.h
// DeviceExplorer's window: the devices IODeviceManager has found on this
// machine as a tree on the left - the computer at the root, one branch per
// group (category, connection or backend, as the toolbar chooses), a row per
// device - and everything known about the selected row on the right, in
// titled sections.
//
// Nothing is painted by hand: an UltraCanvasTreeView for the tree, an
// UltraCanvasColumnsTreeView with section bars for the details, both in an
// UltraCanvasSplitPane, and a toolbar of framework elements above them.
//
// A scan enumerates every backend, and SANE or CUPS can take seconds to
// answer, so it runs on a worker thread and parks its result in one slot a
// UI timer applies - the shape UltraNetMonitor and UltraCleaner use. Hot-plug
// changes arrive from IODeviceManager's watcher thread as a flag; the same
// timer re-reads the registry, which the manager has already re-enumerated.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DeviceExplorerModel.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace DeviceExplorer {

class DeviceExplorerWindow {
public:
    ~DeviceExplorerWindow();

    // `machine` describes the computer node; `grouping` is the initial
    // choice (--group on the command line).
    bool Initialize(const MachineSummary& machine, DeviceGrouping grouping);
    void Show();

private:
    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView> BuildDeviceTree();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildDetailsPane();

    // ===== DATA FLOW =====
    void StartScan();                   // UI thread; no-op while one runs
    void JoinScan();
    void OnTimer();                     // UI thread
    void ApplyInventory(DeviceInventory inventory);

    // ===== TREE =====
    void RebuildTree();
    void ShowDetailsFor(const std::string& nodeId);
    void ShowSections(const std::string& title, const std::string& subtitle,
                      const std::vector<PropertySection>& sections);
    std::string IconFor(const DeviceGroup& group) const;
    std::string IconFor(const UltraCanvas::IODeviceInfo& info) const;
    void RefreshStatus();
    void ShowAbout();

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> subtitleLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> page_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> groupingDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> rescanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> expandButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> collapseButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> aboutButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasSplitPane> split_;
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView> deviceTree_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> detailTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> detailSubtitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasColumnsTreeView> detailView_;

    // ===== STATE (UI thread) =====
    MachineSummary machine_;
    DeviceGrouping grouping_ = DeviceGrouping::Category;
    std::string filter_;
    DeviceInventory inventory_;
    std::vector<DeviceGroup> groups_;           // pointers into inventory_
    std::string selectedNodeId_;
    std::set<std::string> collapsedGroups_;     // by group key, survives rebuilds
    bool rebuilding_ = false;                   // ignore selection echoes
    bool monitoring_ = false;
    std::string iconsDir_;
    UltraCanvas::TimerId uiTimer_ = 0;

    // ===== WORKER =====
    std::thread scanThread_;
    std::atomic<bool> scanning_{false};
    std::mutex pendingMutex_;
    std::optional<DeviceInventory> pending_;    // guarded by pendingMutex_
    std::atomic<bool> hotplugChanged_{false};   // set on the watcher's thread
};

} // namespace DeviceExplorer
