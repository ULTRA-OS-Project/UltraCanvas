// Apps/UltraWinManager/UltraWinManagerWindow.h
// UltraWin Manager — the graphical front-end for Windows-application
// support in ULTRA OS. Three tabs over the UltraWin API:
//   Environments — the isolated Wine prefixes: their components, verb
//                  installs, launches into a chosen environment, deletion.
//   Programs     — installed Start-Menu programs across environments,
//                  launchable by double-click.
//   Windows VM   — the tier-2 machine: capability/state panel, provision
//                  from install media, start/suspend/resume/stop.
// Every slow UltraWin call runs on a worker thread; results reach the UI
// through PostToUIThread guarded by an alive flag (the UltraFiler
// pattern). Assembled entirely from catalogue elements.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include "UltraWin/UltraWin.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraWinManager {

class ManagerWindow {
public:
    explicit ManagerWindow(UltraCanvas::UltraCanvasApplication& app);
    ~ManagerWindow();

    bool Create();
    void Show();

private:
    // Tab construction (each returns the tab's content container).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildEnvironmentsTab();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildProgramsTab();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildVmTab();

    // Data refreshes (UI thread; cheap filesystem reads).
    void RefreshEnvironments();
    void RefreshComponents();     // for the selected environment
    void RefreshPrograms();
    void RefreshVmPanel();        // spawns a worker (QMP/probe can block)

    // Selected environment name, "" when none.
    std::string SelectedEnvironment() const;

    // Slow operations — each runs on a detached worker and reports into
    // the status bar; `refreshAfter` re-runs the matching refresh.
    void RunWorker(const std::string& busyText,
                   std::function<UltraWinResult()> work,
                   std::function<void()> refreshAfter);

    void SetStatus(const std::string& text);

    UltraCanvas::UltraCanvasApplication& app_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    // Environments tab
    std::shared_ptr<UltraCanvas::UltraCanvasListView> envList_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> envModel_;
    std::vector<std::string> envNames_;   // row -> name
    std::shared_ptr<UltraCanvas::UltraCanvasListView> compList_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> compModel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> compInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> exeInput_;

    // Programs tab
    std::shared_ptr<UltraCanvas::UltraCanvasListView> progList_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> progModel_;
    std::vector<std::string> progPaths_;  // row -> shortcut path
    std::vector<std::string> progEnvs_;   // row -> owning environment

    // VM tab
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> vmStateLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> vmDetailLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> winIsoInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> driversIsoInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> vmRunInput_;

    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;

    // Guards cross-thread UI updates after destruction; busy_ is shared
    // for the same reason (a worker outliving the window clears it).
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> busy_ =
        std::make_shared<std::atomic<bool>>(false);

    static constexpr long kW = 940;
    static constexpr long kH = 640;
};

}  // namespace UltraWinManager
