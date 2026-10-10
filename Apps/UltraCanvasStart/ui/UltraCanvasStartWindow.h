// Apps/UltraCanvasStart/ui/UltraCanvasStartWindow.h
// UltraCanvasStart's window: a stepper (Docs/UltraCanvasStart/WorkflowProposal.md).
// Five steps in the order the work happens - Your computer (what was found,
// with the guide for any platform under it), Features (what for, and which
// assistant), Tools (the checks and the install), Framework (the prebuilt
// SDK or a clone), Project (name, folder, create, and the Done page with the
// first prompt) - an UltraCanvasStepper above one pane that shows the current
// step, Back / Next below it. The header offers the Guide and the Report as
// dialogs at any time, so neither is a step. Nothing is painted by hand:
// every pane is assembled from catalogue elements and laid out with the CSS
// flex layout, as the other applications do, and styled through
// UltraCanvasStartTheme.h (UltraMail's values).
//
// The panes' prose comes from the engine as Markdown (StartGuide) and is
// shown in read-only Markdown views: links open in the browser, commands
// stand out, one action per step.
//
// The checks and the install run a package manager and block on it, so they
// run on a worker thread; results come back through a queue a UI timer
// drains, the pattern UltraCleaner and UltraSocial use.
// Version: 0.3.0 - the stepper: five steps, Back / Next, the Guide and the
//                  Report as dialogs (the workflow proposal, implemented)
// Version: 0.2.0 - UltraMail's look: theme header, cards, Markdown views
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartAi.h"
#include "StartTypes.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasStepper.h"
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

// The steps, in order.
enum class Step : int {
    Computer = 0,
    Features,
    Tools,
    Framework,
    Project,
    Count
};

class UltraCanvasStartWindow {
public:
    ~UltraCanvasStartWindow();

    bool Initialize();
    void Show();

    // Opens a step (1 to 5; --step). False for another number.
    bool ShowStep(int number);
    // The page names of versions before 0.3.0 (--page): "platform" and
    // "system" are step 1, "choices" and "ai" step 2, "install" step 3,
    // "project" step 5; "guide" and "report" open the dialogs. False for a
    // name that is no page. Call it after Show(): a dialog needs the window.
    bool ShowPage(const std::string& name);
    // Selects the platform the guide is for (--for in window mode).
    void PreselectPlatform(Platform platform);
    // Selects the assistant (--assistant).
    void PreselectAssistant(Assistant assistant);

private:
    // ===== CONSTRUCTION =====
    void LayoutForSize(float width, float height);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> NewPane(const std::string& id);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildHeader();
    std::shared_ptr<UltraCanvas::UltraCanvasStepper> BuildStepper();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildNavigation();

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildComputerStep();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildFeaturesStep();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolsStep();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildFrameworkStep();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildProjectStep();

    // The guide for a platform (the picker and the Markdown view), used by
    // step 1 and by the Guide dialog.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildGuideCard(
        const std::string& id, std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>& picker,
        std::shared_ptr<UltraCanvas::UltraCanvasTextArea>& view, float height);

    // ===== NAVIGATION =====
    void ShowStepPane(int index);        // the stepper's onStepChanged
    void GoNext();
    void GoBack();
    void OnLeaveStep(int index);         // marks the step's error state
    void OnEnterStep(int index);         // runs the checks, refreshes the plan
    void UpdateNavigation();

    // ===== ACTIONS =====
    void SelectGuidePlatform(Platform platform);
    void SelectAssistant(Assistant assistant);
    void SelectFrameworkWay(bool useSdk);
    void StartChecks();
    void StartInstall();
    void ChooseProjectFolder();
    void FindSdk();
    // Downloads this version's SDK from its GitHub release into a folder the
    // user picks, unpacks it there and fills in the SDK prefix.
    void DownloadAndUnpackSdk();
    void CreateProject();
    void CopyReport();
    void CopyPrompt();
    void ShowGuideDialog();
    void ShowReportDialog();

    // ===== VIEW UPDATES =====
    void ReadChoicesFromPanes();
    void RefreshChecksText();
    void RefreshFrameworkStep();
    void RefreshPlan();
    void SetBusy(bool busy);
    void SetStatus(const std::string& text);

    // ===== THREAD PLUMBING =====
    void RunOnUiThread(std::function<void()> action);
    void DrainUiQueue();

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> header_;
    std::shared_ptr<UltraCanvas::UltraCanvasStepper> stepper_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> paneHost_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasContainer>> panes_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> navigation_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> stepHint_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> backButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> nextButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> statusBand_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;

    // Step 1: the computer, and the guide
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> guidePicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> guideView_;

    // Step 2: features and the assistant
    std::map<DependencyGroup, std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>> groupBoxes_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> aiBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> cloudBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> assistantPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> aiView_;

    // Step 3: tools
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> checksView_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> installOutput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> checkButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> installButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasBadge> installBadge_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> installSummary_;

    // Step 4: the framework
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> wayPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> sdkRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> sdkPrefixInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> findButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> downloadButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasBadge> frameworkBadge_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> frameworkView_;

    // Step 5: the project, then Done
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> projectCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> appNameInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> folderInput_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> createButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> previewCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> projectPreview_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> doneCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> doneView_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> promptCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> promptText_;

    SystemProfile profile_;
    Choices choices_;
    std::vector<CheckResult> checks_;
    Plan plan_;
    AiStatus ai_;
    Platform guidePlatform_ = Platform::Unknown;
    int currentStep_ = 0;
    bool projectCreated_ = false;

    std::atomic<bool> working_{ false };
    std::thread worker_;
    std::mutex uiQueueMutex_;
    std::vector<std::function<void()>> uiQueue_;
    UltraCanvas::TimerId uiTimer_ = 0;
};

} // namespace UltraCanvasStart
