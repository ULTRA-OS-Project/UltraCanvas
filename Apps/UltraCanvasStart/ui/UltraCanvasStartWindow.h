// Apps/UltraCanvasStart/ui/UltraCanvasStartWindow.h
// UltraCanvasStart's window: a tabbed wizard. Platform (which OS the
// instructions are for, preselected to the detected one), System (what was
// found), Choices (which features, SDK or source, AI or not), Install (the
// checks and the install step), Project (the skeleton), AI (Claude Code) and
// Report (the whole plan as text, for the clipboard). Nothing is painted by
// hand: every page is assembled from catalogue elements and laid out with the
// CSS flex layout, as the other applications do.
//
// The checks and the install run a package manager and block on it, so they
// run on a worker thread; results come back through a queue a UI timer
// drains, the pattern UltraCleaner and UltraSocial use.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartAi.h"
#include "StartTypes.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvasStart {

class UltraCanvasStartWindow {
public:
    ~UltraCanvasStartWindow();

    bool Initialize();
    void Show();

private:
    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> NewPage(const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> Heading(const std::string& id,
                                                           const std::string& text);
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> ReadOnlyText(const std::string& id,
                                                                   float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> ButtonRow(const std::string& id);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildPlatformPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildSystemPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildChoicesPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildInstallPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildProjectPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildAiPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildReportPage();

    // ===== ACTIONS =====
    void SelectPlatform(Platform platform);
    void StartChecks();
    void StartInstall();
    void ChooseProjectFolder();
    // Downloads this version's SDK from its GitHub release into a folder the
    // user picks, unpacks it there and fills in the SDK prefix.
    void DownloadAndUnpackSdk();
    void CreateProject();
    void CopyReport();
    void CopyPrompt();
    void OnTabEntered(int index);

    // ===== VIEW UPDATES =====
    void ReadChoicesFromPage();
    void RefreshSystemText();
    void RefreshChecksText();
    void RefreshPlan();
    void RefreshAiText();
    void SetBusy(bool busy);
    void SetStatus(const std::string& text);

    // ===== THREAD PLUMBING =====
    void RunOnUiThread(std::function<void()> action);
    void DrainUiQueue();

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTabbedContainer> tabs_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;

    // Platform page
    UltraCanvas::UltraCanvasRadioGroup platformGroup_;
    std::map<Platform, std::shared_ptr<UltraCanvas::UltraCanvasRadio>> platformRadios_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> platformNotes_;

    // System page
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> systemText_;

    // Choices page
    std::map<DependencyGroup, std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>> groupBoxes_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> sdkBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> cloneBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> aiBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> cloudBox_;

    // Install page
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> checksText_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> installOutput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> checkButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> installButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> downloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> installSummary_;

    // Project page
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> appNameInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> folderInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> sdkPrefixInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> projectPreview_;

    // AI page
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> aiText_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> promptText_;

    // Report page
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> reportText_;

    SystemProfile profile_;
    Choices choices_;
    std::vector<CheckResult> checks_;
    Plan plan_;
    AiStatus ai_;

    std::atomic<bool> working_{ false };
    std::thread worker_;
    std::mutex uiQueueMutex_;
    std::vector<std::function<void()>> uiQueue_;
    UltraCanvas::TimerId uiTimer_ = 0;
};

} // namespace UltraCanvasStart
