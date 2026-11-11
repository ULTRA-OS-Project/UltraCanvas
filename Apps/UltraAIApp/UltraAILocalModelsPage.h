// Apps/UltraAIApp/UltraAILocalModelsPage.h
// The settings window's "Local models" page: what the local runtime
// (UltraAILocalRuntime.h) has on this computer and what it offers. The
// engine and its accelerator; a table of the catalogue's models and the
// user's own with their size, fit and state (installed, running,
// downloading, failed); Install / Remove / Stop / Forget on the selected
// row; the progress of the install in flight with Cancel; "Add model
// file..." (a GGUF the user has, used in place), "Add from Hugging
// Face...", "Refresh catalogue" and "Open folder"; the download policy and
// the idle time; disk use. Every change applies and is saved at once, as
// on the other pages.
//
// The page talks to LocalRuntime::Instance() directly. Installs run on the
// runtime's worker and report through callbacks that are marshalled onto
// the UI thread; removals, stops and the gallery refresh, which can block
// for seconds, run on a thread of their own and refresh the page when they
// return.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraAI Module
#pragma once

#include "UltraAILocalRuntime.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListView.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
class UltraCanvasWindowBase;
}

namespace UltraAIApp {

class UltraAILocalModelsPage {
public:
    // `alive` is the settings window's flag: a callback that arrives after
    // the window closed does nothing.
    explicit UltraAILocalModelsPage(std::shared_ptr<std::atomic<bool>> alive);
    ~UltraAILocalModelsPage();

    // Build the page (the settings window's page skeleton) and return it.
    // `parent` is the window the file and input dialogs open on.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(
        UltraCanvas::UltraCanvasWindowBase* parent);

    // Re-read the runtime: the engine line, the table, the running engines,
    // the disk line. Cheap (local folders only); called when the page is
    // shown and after every action.
    void Refresh();

    // The page's "Restore defaults": Ask policy, Auto accelerator, 15 min idle.
    void RestoreDefaults();

private:
    void BuildHardwareLine(UltraCanvas::UltraCanvasContainer& body);
    void BuildEngineRow(UltraCanvas::UltraCanvasContainer& body);
    void BuildModelTable(UltraCanvas::UltraCanvasContainer& body);
    void BuildActionRow(UltraCanvas::UltraCanvasContainer& body);
    void BuildProgressRow(UltraCanvas::UltraCanvasContainer& body);
    void BuildAddRow(UltraCanvas::UltraCanvasContainer& body);
    void BuildSettingsRows(UltraCanvas::UltraCanvasContainer& body);

    void RefreshEngine();
    void RefreshTable();
    void RefreshButtons();
    void RefreshDisk();
    void ShowProgress(const UltraAI::InstallProgress& p);
    void HideProgress();

    const UltraAI::LocalModelInfo* SelectedModel() const;
    void OnInstall();
    void OnRemove();
    void OnStop();
    void OnForget();
    void OnInstallEngine();
    void OnAddFile();
    void OnAddHuggingFace();
    void OnRefreshCatalogue();
    void OnOpenFolder();
    void SetStatus(const std::string& text);

    // Run `work` off the UI thread, then `after` (and Refresh) on it.
    void RunThenRefresh(std::function<std::string()> work);
    // An install job's progress, from the runtime's worker.
    UltraAI::InstallProgressCallback ProgressSink();

    std::shared_ptr<std::atomic<bool>> alive_;
    UltraCanvas::UltraCanvasWindowBase* parent_ = nullptr;

    // Widgets.
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    hardwareLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    engineLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> accelDropdown_;
    std::vector<std::string>                          accelIds_;      // aligned with the dropdown
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   engineButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> table_;
    std::shared_ptr<UltraCanvas::UltraCanvasMultiColumnListModel> tableModel_;
    std::vector<UltraAI::LocalModelInfo>              rows_;          // aligned with the table
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   installButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   removeButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   forgetButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> progressRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    progressLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasGaugeDiagramElement> progressBar_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>   cancelButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> policyDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> idleDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>    diskLabel_;

    std::string activeJobId_;     // the install in flight, "" when none
    bool busy_ = false;           // a remove / stop / refresh on its thread
};

} // namespace UltraAIApp
