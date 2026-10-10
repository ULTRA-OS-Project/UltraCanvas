// Apps/UltraCanvasStart/ui/UltraCanvasStartWindow.cpp
// Version: 0.3.0 - the stepper: five steps, Back / Next, the Guide and the
//                  Report as dialogs (the workflow proposal, implemented)
// Version: 0.2.0 - UltraMail's look: theme header, cards, Markdown views
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasStartWindow.h"

#include "StartChecks.h"
#include "StartGuide.h"
#include "StartPackages.h"
#include "StartPlan.h"
#include "StartProject.h"
#include "StartRunner.h"
#include "StartSdk.h"
#include "StartSystem.h"
#include "UltraCanvasStartTheme.h"

#ifndef ULTRACANVASSTART_VERSION
#error "ULTRACANVASSTART_VERSION is not defined: build through CMake, which reads it from Docs/UltraCanvasStart/CHANGELOG.md"
#endif

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>

using namespace UltraCanvas;

namespace UltraCanvasStart {
namespace {

constexpr float kWindowWidth   = 980.0f;
constexpr float kWindowHeight  = 720.0f;
constexpr float kSideMargin    = Theme::kPagePadding;
constexpr float kStepperHeight = 66.0f;
constexpr float kNavHeight     = 40.0f;
constexpr int   kStepCount     = static_cast<int>(Step::Count);

const DependencyGroup kGroups[] = {
    DependencyGroup::Toolchain, DependencyGroup::Core, DependencyGroup::Cdr,
    DependencyGroup::Pdf, DependencyGroup::Ocr, DependencyGroup::Vectorizer,
    DependencyGroup::Audio, DependencyGroup::Barcode, DependencyGroup::Net
};

// The pickers' segments, in order.
const Platform kPlatforms[] = { Platform::Linux, Platform::MacOS, Platform::Windows };
const Assistant kAssistants[] = { Assistant::ClaudeCode, Assistant::Codex, Assistant::Copilot,
                                  Assistant::Gemini, Assistant::Other };

int PlatformIndex(Platform platform) {
    for (int i = 0; i < 3; ++i) if (kPlatforms[i] == platform) return i;
    return 0;
}

int AssistantIndex(Assistant assistant) {
    for (int i = 0; i < 5; ++i) if (kAssistants[i] == assistant) return i;
    return 0;
}

// What each step decides, under its heading.
const char* kStepHints[kStepCount] = {
    "What this computer has, and the way in for any platform. Nothing to decide here.",
    "Which features the application needs, and which assistant you work with.",
    "Whether the tools and libraries are there; what is missing can be installed from here.",
    "Where find_package(UltraCanvas) will look: the prebuilt SDK, or a clone built from source.",
    "The application: its name and folder, then the files are written."
};

std::string Join(const std::vector<std::string>& lines, const char* bullet = "") {
    std::string out;
    for (const auto& line : lines) out += bullet + line + "\n";
    return out;
}

// The architecture the instructions name: the machine's for its own
// platform, x86_64 for another one.
std::string GuideArchitecture(const SystemProfile& profile, Platform target) {
    return target == profile.platform && !profile.architecture.empty() ? profile.architecture : "x86_64";
}

ContainerStyle PlainContainer() {
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    return plain;
}

} // namespace

UltraCanvasStartWindow::~UltraCanvasStartWindow() {
    if (worker_.joinable()) worker_.join();
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->StopTimer(uiTimer_);
    }
}

bool UltraCanvasStartWindow::Initialize() {
    profile_ = DetectSystem();
    choices_.platform = profile_.platform;
    guidePlatform_ = profile_.platform;
    ai_ = DetectAi(profile_);

    WindowConfig config;
    config.title  = std::string("UltraCanvasStart ") + ULTRACANVASSTART_VERSION;
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 760;
    config.minHeight = 560;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    header_ = BuildHeader();
    window_->AddChild(header_);

    stepper_ = BuildStepper();
    window_->AddChild(stepper_);

    // One pane per step; only the current one is visible, and the host lays
    // out the visible one over its whole area.
    paneHost_ = CreateContainer("ucsPanes", 0, 0, kWindowWidth, 400);
    paneHost_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    paneHost_->SetContainerStyle(PlainContainer());
    panes_ = { BuildComputerStep(), BuildFeaturesStep(), BuildToolsStep(),
               BuildFrameworkStep(), BuildProjectStep() };
    for (auto& pane : panes_) {
        pane->SetVisible(false);
        paneHost_->AddChild(pane);
    }
    window_->AddChild(paneHost_);

    navigation_ = BuildNavigation();
    window_->AddChild(navigation_);

    statusBand_ = CreateContainer("ucsStatusBand", 0, kWindowHeight - Theme::kStatusHeight,
                                  kWindowWidth, Theme::kStatusHeight);
    statusBand_->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    statusBand_->SetPadding(0, Theme::kPagePadding + 6.0f);
    statusBand_->SetContainerStyle(PlainContainer());
    statusLabel_ = Theme::MakeText("ucsStatus", "", Theme::kSizeBody, Theme::kTextSecondary);
    statusLabel_->layoutItem.SetFlexGrow(1);
    statusBand_->AddChild(statusLabel_);
    window_->AddChild(statusBand_);

    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(100, /*periodic=*/true, [this](TimerId) { DrainUiQueue(); });
    }

    RefreshPlan();
    ShowStepPane(0);
    SetStatus("Step 1 of 5: this is what was found. Next goes on to the features.");
    return true;
}

void UltraCanvasStartWindow::Show() {
    if (window_) window_->Show();
}

bool UltraCanvasStartWindow::ShowStep(int number) {
    if (number < 1 || number > kStepCount || !stepper_) return false;
    // SetCurrentStep marks the earlier steps completed; a jump from the
    // command line skips their checks.
    stepper_->SetCurrentStep(number - 1, /*runCallback=*/false);
    ShowStepPane(number - 1);
    return true;
}

bool UltraCanvasStartWindow::ShowPage(const std::string& name) {
    static const std::map<std::string, int> kPages = {
        { "platform", 1 }, { "system", 1 }, { "choices", 2 }, { "ai", 2 },
        { "install", 3 }, { "framework", 4 }, { "project", 5 }
    };
    if (name == "guide")  { ShowGuideDialog();  return true; }
    if (name == "report") { ShowReportDialog(); return true; }
    const auto found = kPages.find(name);
    return found != kPages.end() && ShowStep(found->second);
}

void UltraCanvasStartWindow::PreselectPlatform(Platform platform) {
    if (guidePicker_) guidePicker_->SetSelectedIndex(PlatformIndex(platform));
    SelectGuidePlatform(platform);
}

void UltraCanvasStartWindow::PreselectAssistant(Assistant assistant) {
    if (assistantPicker_) assistantPicker_->SetSelectedIndex(AssistantIndex(assistant));
    SelectAssistant(assistant);
}

void UltraCanvasStartWindow::LayoutForSize(float width, float height) {
    if (!paneHost_) return;
    const float contentWidth = std::max(320.0f, width - 2 * kSideMargin);
    float y = 0;
    if (header_) {
        header_->SetElementAbsolutePosition(Point2Df(0, 0));
        header_->SetElementSize(Size2Df(width, Theme::kHeaderHeight));
    }
    y += Theme::kHeaderHeight;
    if (stepper_) {
        stepper_->SetElementAbsolutePosition(Point2Df(kSideMargin, y));
        stepper_->SetElementSize(Size2Df(contentWidth, kStepperHeight));
    }
    y += kStepperHeight + Theme::kGap;
    const float navTop = height - Theme::kStatusHeight - kNavHeight;
    const float paneHeight = std::max(200.0f, navTop - y - Theme::kGap);
    paneHost_->SetElementAbsolutePosition(Point2Df(kSideMargin, y));
    paneHost_->SetElementSize(Size2Df(contentWidth, paneHeight));
    paneHost_->InvalidateLayout();
    if (navigation_) {
        navigation_->SetElementAbsolutePosition(Point2Df(kSideMargin, navTop));
        navigation_->SetElementSize(Size2Df(contentWidth, kNavHeight));
    }
    if (statusBand_) {
        statusBand_->SetElementAbsolutePosition(Point2Df(0, height - Theme::kStatusHeight));
        statusBand_->SetElementSize(Size2Df(width, Theme::kStatusHeight));
    }
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== CONSTRUCTION =========================================================

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildHeader() {
    auto header = CreateContainer("ucsHeader", 0, 0, kWindowWidth, Theme::kHeaderHeight);
    header->layout.SetFlexRow().SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->SetPadding(0, Theme::kPagePadding + 6.0f);
    header->SetContainerStyle(PlainContainer());
    auto title = Theme::MakeText("ucsTitle", "UltraCanvasStart", Theme::kSizeTitle,
                                 Theme::kTextPrimary, FontWeight::Bold);
    title->layoutItem.SetFlexShrink(0);
    header->AddChild(title);
    auto version = Theme::MakeText("ucsVersion", ULTRACANVASSTART_VERSION, Theme::kSizeBody, Theme::kTextMuted);
    version->layoutItem.SetFlexShrink(0);
    header->AddChild(version);
    auto subtitle = Theme::MakeText("ucsSubtitle",
        "sets this computer up for UltraCanvas development  \xC2\xB7  " +
        PlatformName(profile_.platform) + " " + profile_.architecture + " detected",
        Theme::kSizeBody, Theme::kTextSecondary);
    subtitle->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->AddChild(subtitle);
    // The reference panes: reading matter and the report, at any time.
    auto guide = Theme::MakeButton("ucsGuideButton", "Guide", false, 70);
    guide->SetTooltip("The way in for Linux, macOS or Windows, step by step");
    guide->onClick = [this]() { ShowGuideDialog(); };
    header->AddChild(guide);
    auto report = Theme::MakeButton("ucsReportButton", "Report", false, 70);
    report->SetTooltip("The system, the checks, the plan and the notes as text");
    report->onClick = [this]() { ShowReportDialog(); };
    header->AddChild(report);
    return header;
}

std::shared_ptr<UltraCanvasStepper> UltraCanvasStartWindow::BuildStepper() {
    auto stepper = CreateStepper("ucsStepper", kSideMargin, Theme::kHeaderHeight,
                                 kWindowWidth - 2 * kSideMargin, kStepperHeight);
    stepper->SetSteps({
        { "Your computer", "what was found" },
        { "Features",      "what for, which assistant" },
        { "Tools",         "checks and install" },
        { "Framework",     "SDK or source" },
        { "Project",       "name, folder, create" },
    });
    stepper->SetNavigation(StepperNavigation::Linear);
    stepper->SetMarkerStyle(StepMarkerStyle::NumberedCircle);
    auto& style = stepper->GetStyle();
    style.completedColor          = Theme::kAccent;
    style.activeColor             = Theme::kAccent;
    style.connectorCompletedColor = Theme::kAccent;
    style.upcomingColor           = Theme::kCardBorder;
    style.upcomingGlyphColor      = Theme::kTextSecondary;
    style.markerBackground        = Theme::kCardBackground;
    style.connectorColor          = Theme::kCardBorder;
    style.errorColor              = Theme::kWarnText;
    style.titleColor              = Theme::kTextSecondary;
    style.activeTitleColor        = Theme::kTextPrimary;
    style.descriptionColor        = Theme::kTextMuted;
    style.markerSize              = 24.0f;
    style.titleFontSize           = Theme::kSizeBody + 1.0f;
    style.descriptionFontSize     = Theme::kSizeSecondary;
    // The stepper lives inside the window that owns this object; a raw
    // capture of `this` is the convention (AGENTS.md: no shared_ptr in a
    // callback).
    stepper->onStepChanged = [this](int index) { ShowStepPane(index); };
    return stepper;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildNavigation() {
    auto row = CreateContainer("ucsNavigation", 0, 0, kWindowWidth, kNavHeight);
    row->layout.SetFlexRow().SetFlexGap(Theme::kGap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->SetContainerStyle(PlainContainer());
    stepHint_ = Theme::MakeText("ucsStepHint", "", Theme::kSizeBody, Theme::kTextSecondary);
    stepHint_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(stepHint_);
    backButton_ = Theme::MakeButton("ucsBack", "Back", false, 90);
    backButton_->onClick = [this]() { GoBack(); };
    row->AddChild(backButton_);
    nextButton_ = Theme::MakeButton("ucsNext", "Next", true, 110);
    nextButton_->onClick = [this]() { GoNext(); };
    row->AddChild(nextButton_);
    return row;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::NewPane(const std::string& id) {
    auto pane = CreateContainer(id, 0, 0, 0, 0);
    pane->layout.SetFlexColumn()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    pane->SetBackgroundColor(Theme::kPageBackground);
    pane->SetContainerStyle(PlainContainer());
    pane->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildGuideCard(
    const std::string& id, std::shared_ptr<UltraCanvasSegmentedControl>& picker,
    std::shared_ptr<UltraCanvasTextArea>& view, float height) {
    auto card = Theme::MakeCard(id);
    auto row = Theme::MakeRow(id + ".row");
    auto label = Theme::MakeText(id + ".label", "Read the guide for", Theme::kSizeBody, Theme::kTextSecondary);
    label->layoutItem.SetFlexShrink(0);
    row->AddChild(label);
    picker = Theme::MakeSegmented(id + ".picker",
        { PlatformName(Platform::Linux), PlatformName(Platform::MacOS), PlatformName(Platform::Windows) },
        PlatformIndex(guidePlatform_), 300);
    row->AddChild(picker);
    auto hint = Theme::MakeText(id + ".hint",
        "Preselected to this computer's platform; only that one can be checked and installed.",
        Theme::kSizeBody, Theme::kTextMuted);
    hint->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(hint);
    card->AddChild(row);
    view = Theme::MakeMarkdownView(id + ".view", height);
    view->SetText(PlatformGuide(guidePlatform_, FrameworkVersion(),
                                GuideArchitecture(profile_, guidePlatform_)), false);
    Theme::Grow(view);
    card->AddChild(view);
    Theme::GrowCard(card);
    return card;
}

// ===== STEPS ================================================================

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildComputerStep() {
    auto pane = NewPane("ucsComputerStep");

    auto computer = Theme::MakeCard("ucsComputerCard", "This computer");
    Theme::AddKeyValue(computer, "ucsSysPlatform", "Platform", PlatformName(profile_.platform));
    Theme::AddKeyValue(computer, "ucsSysOs", "Operating system",
                       profile_.osName + (profile_.osVersion.empty() ? "" : " " + profile_.osVersion));
    Theme::AddKeyValue(computer, "ucsSysArch", "Architecture", profile_.architecture);
    if (!profile_.distributionId.empty()) {
        Theme::AddKeyValue(computer, "ucsSysDistro", "Distribution",
                           profile_.distributionId +
                           (profile_.distributionLike.empty() ? "" : " (like " + profile_.distributionLike + ")"));
    }
    {
        auto row = Theme::MakeKeyRow("ucsSysManager", "Package manager");
        const bool found = !profile_.packageManagerPath.empty();
        row->AddChild(Theme::MakeStatusBadge("ucsSysManagerBadge", found,
                                             found ? PackageManagerName(profile_.packageManager) : "none on PATH"));
        row->AddChild(Theme::MakeValue("ucsSysManagerPath",
                                       found ? profile_.packageManagerPath
                                             : (profile_.packageManager == PackageManager::None
                                                    ? "no apt, dnf, pacman, zypper or brew was found"
                                                    : PackageManagerName(profile_.packageManager) + " was not found on PATH"),
                                       found ? Theme::kTextPrimary : Theme::kTextSecondary));
        computer->AddChild(row);
    }
    if (profile_.platform == Platform::Windows) {
        auto row = Theme::MakeKeyRow("ucsSysMsys", "MSYS2");
        const bool found = !profile_.msysPrefix.empty();
        row->AddChild(Theme::MakeStatusBadge("ucsSysMsysBadge", found));
        if (found) {
            row->AddChild(Theme::MakeValue("ucsSysMsysPath", profile_.msysPrefix));
        } else {
            row->AddChild(Theme::MakeLink("ucsSysMsysLink", "install it from https://www.msys2.org",
                                          "https://www.msys2.org"));
        }
        computer->AddChild(row);
        if (!profile_.msystem.empty()) Theme::AddKeyValue(computer, "ucsSysMsystem", "MSYSTEM", profile_.msystem);
    }
    Theme::AddKeyValue(computer, "ucsSysHome", "Home", profile_.homeDirectory);
    {
        auto row = Theme::MakeKeyRow("ucsSysAssistants", "AI assistants");
        bool any = false;
        for (auto assistant : kAssistants) {
            const AssistantStatus* status = ai_.Status(assistant);
            if (!status || !status->installed) continue;
            any = true;
            row->AddChild(Theme::MakeStatusBadge("ucsSysAssistant" + AssistantName(assistant), true,
                                                 AssistantName(assistant) +
                                                 (status->version.empty() ? "" : " " + status->version)));
        }
        if (!any) row->AddChild(Theme::MakeStatusBadge("ucsSysAssistantNone", false, "none found"));
        row->AddChild(Theme::MakeValue("ucsSysAssistantHint",
                                       any ? (ai_.claudeInstalled ? ai_.claudePath : std::string())
                                           : "step 2 says how to install one",
                                       any ? Theme::kTextPrimary : Theme::kTextSecondary));
        computer->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsSysGit", "git");
        row->AddChild(Theme::MakeStatusBadge("ucsSysGitBadge", ai_.gitInstalled,
                                             ai_.gitInstalled ? "installed" : "not found"));
        computer->AddChild(row);
    }
    Theme::AddKeyValue(computer, "ucsSysVersion", "UltraCanvas", FrameworkVersion() +
                       "  (this build of UltraCanvasStart, and the SDK that matches it; step 4 fetches it)");
    pane->AddChild(computer);

    auto guide = BuildGuideCard("ucsGuide", guidePicker_, guideView_, 200);
    guidePicker_->onSegmentSelected = [this](int index) { SelectGuidePlatform(kPlatforms[index]); };
    pane->AddChild(guide);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildFeaturesStep() {
    auto pane = NewPane("ucsFeaturesStep");

    auto features = Theme::MakeCard("ucsFeaturesCard", "What does the application need?");
    features->AddChild(Theme::MakeHint("ucsChoicesHint",
        "Features decide which development packages step 3 checks and installs. "
        "The toolchain and the framework core are always needed.", 20));
    for (auto group : kGroups) {
        const bool mandatory = group == DependencyGroup::Toolchain || group == DependencyGroup::Core;
        auto box = UltraCanvasCheckbox::CreateCheckbox(
            "ucsGroup" + DependencyGroupTitle(group), 0, 0, 800, Theme::kRowHeight,
            DependencyGroupTitle(group) + "  -  " + DependencyGroupDescription(group),
            choices_.Has(group));
        Theme::StyleCheckbox(box);
        if (mandatory) box->SetDisabled(true);
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPanes(); RefreshPlan(); };
        groupBoxes_[group] = box;
        features->AddChild(box);
    }
    pane->AddChild(features);

    auto assistant = Theme::MakeCard("ucsAssistantCard", "Which assistant do you work with?");
    auto pickRow = Theme::MakeRow("ucsAssistantRow");
    std::vector<std::string> names;
    for (auto a : kAssistants) names.push_back(a == Assistant::Other ? "Other" : AssistantName(a));
    assistantPicker_ = Theme::MakeSegmented("ucsAssistantPicker", names,
                                            AssistantIndex(choices_.assistant), 540);
    assistantPicker_->onSegmentSelected = [this](int index) { SelectAssistant(kAssistants[index]); };
    pickRow->AddChild(assistantPicker_);
    assistant->AddChild(pickRow);
    aiBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsUseAi", 0, 0, 800, Theme::kRowHeight,
        "Write its instruction file into the project and prepare the first prompt",
        choices_.useAi);
    cloudBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsCloud", 0, 0, 800, Theme::kRowHeight,
        "...through GitHub only, with no compiler on this machine (Docs/GettingStarted-Cloud.md)",
        choices_.cloudOnly);
    for (auto& box : { aiBox_, cloudBox_ }) {
        Theme::StyleCheckbox(box);
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPanes(); RefreshPlan(); };
        assistant->AddChild(box);
    }
    aiView_ = Theme::MakeMarkdownView("ucsAiView", 160);
    aiView_->SetText(AiGuide(ai_, profile_.platform, choices_.assistant), false);
    Theme::Grow(aiView_);
    assistant->AddChild(aiView_);
    Theme::GrowCard(assistant);
    pane->AddChild(assistant);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildToolsStep() {
    auto pane = NewPane("ucsToolsStep");

    auto checks = Theme::MakeCard("ucsChecksCard", "Tools and libraries");
    auto row = Theme::MakeRow("ucsInstallRow");
    installButton_ = Theme::MakeButton("ucsInstall", "Install what is missing", true, 170);
    installButton_->onClick = [this]() { StartInstall(); };
    checkButton_ = Theme::MakeButton("ucsCheck", "Check again", false, 110);
    checkButton_->onClick = [this]() { StartChecks(); };
    installBadge_ = Theme::MakeStatusBadge("ucsInstallBadge", false, "not checked yet");
    installBadge_->SetVariant(BadgeVariant::Neutral);
    installSummary_ = Theme::MakeText("ucsInstallSummary", "", Theme::kSizeBody, Theme::kTextSecondary);
    installSummary_->layoutItem.SetFlexGrow(1);
    row->AddChild(installButton_);
    row->AddChild(checkButton_);
    row->AddChild(installBadge_);
    row->AddChild(installSummary_);
    checks->AddChild(row);
    checksView_ = Theme::MakeMarkdownView("ucsChecksView", 260);
    checksView_->SetText(ChecksMarkdown({}), false);
    Theme::Grow(checksView_);
    checks->AddChild(checksView_);
    Theme::GrowCard(checks, 2.0f);
    pane->AddChild(checks);

    auto output = Theme::MakeCard("ucsOutputCard", "Install output");
    installOutput_ = Theme::MakeConsole("ucsInstallOutput", 160);
    installOutput_->SetPlaceholder("The package manager's output appears here.");
    Theme::Grow(installOutput_);
    output->AddChild(installOutput_);
    Theme::GrowCard(output, 1.0f);
    pane->AddChild(output);
    return pane;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildFrameworkStep() {
    auto pane = NewPane("ucsFrameworkStep");
    const std::string version = FrameworkVersion();
    const std::string archive = SdkArchiveName(profile_.platform, version, profile_.architecture);
    const std::string assetUrl = SdkReleaseAssetUrl(profile_.platform, version, profile_.architecture);

    auto card = Theme::MakeCard("ucsFrameworkCard", "How does the application get the framework?");
    auto wayRow = Theme::MakeRow("ucsWayRow");
    wayPicker_ = Theme::MakeSegmented("ucsWayPicker", { "The prebuilt SDK", "A clone, built from source" },
                                      choices_.useSdk ? 0 : 1, 360);
    wayPicker_->onSegmentSelected = [this](int index) { SelectFrameworkWay(index == 0); };
    wayRow->AddChild(wayPicker_);
    frameworkBadge_ = Theme::MakeStatusBadge("ucsFrameworkBadge", false, "not set yet");
    frameworkBadge_->SetVariant(BadgeVariant::Neutral);
    wayRow->AddChild(frameworkBadge_);
    card->AddChild(wayRow);

    // ----- the SDK -----
    Theme::AddKeyValue(card, "ucsFwVersion", "UltraCanvas", version +
                       "  (this build of UltraCanvasStart, and the SDK that matches it)");
    {
        auto row = Theme::MakeKeyRow("ucsFwSdk", "Matching SDK");
        row->AddChild(Theme::MakeCodeLine("ucsFwSdkName", archive, { archive }));
        card->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsFwRelease", "Download");
        row->AddChild(Theme::MakeLink("ucsFwReleaseLink", assetUrl, assetUrl));
        card->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsFwReleasePage", "Release page");
        row->AddChild(Theme::MakeLink("ucsFwReleasePageLink", SdkReleasePage(version), SdkReleasePage(version)));
        card->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsFwArtifacts", "Still building?");
        row->AddChild(Theme::MakeLink("ucsFwArtifactsLink",
                                      "the same archive is a workflow artifact on the Actions page",
                                      SdkDownloadPage()));
        card->AddChild(row);
    }
    sdkRow_ = Theme::MakeRow("ucsSdkRow");
    auto prefixLabel = Theme::MakeText("ucsSdkLabel", "SDK prefix", Theme::kSizeBody, Theme::kTextSecondary);
    prefixLabel->SetElementSize(Size2Df(Theme::kKeyWidth, Theme::kControlHeight));
    prefixLabel->layoutItem.SetFlexShrink(0);
    sdkRow_->AddChild(prefixLabel);
    sdkPrefixInput_ = CreateTextInput("ucsSdkPrefix", 0, 0, 480, Theme::kControlHeight);
    Theme::StyleInput(sdkPrefixInput_);
    sdkPrefixInput_->SetPlaceholder("the unpacked UltraCanvas-SDK-... folder (CMAKE_PREFIX_PATH)");
    sdkPrefixInput_->layoutItem.SetFlexGrow(1);
    sdkPrefixInput_->onTextChanged = [this](const std::string&) { ReadChoicesFromPanes(); RefreshFrameworkStep(); };
    sdkRow_->AddChild(sdkPrefixInput_);
    downloadButton_ = Theme::MakeButton("ucsDownloadSdk", "Download and unpack...", true, 150);
    downloadButton_->onClick = [this]() { DownloadAndUnpackSdk(); };
    sdkRow_->AddChild(downloadButton_);
    findButton_ = Theme::MakeButton("ucsFindSdk", "Find...", false, 80);
    findButton_->onClick = [this]() { FindSdk(); };
    sdkRow_->AddChild(findButton_);
    card->AddChild(sdkRow_);

    // ----- either way: what the choice means, in words -----
    frameworkView_ = Theme::MakeMarkdownView("ucsFrameworkView", 120);
    Theme::Grow(frameworkView_);
    card->AddChild(frameworkView_);
    Theme::GrowCard(card);
    pane->AddChild(card);
    RefreshFrameworkStep();
    return pane;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildProjectStep() {
    auto pane = NewPane("ucsProjectStep");

    projectCard_ = Theme::MakeCard("ucsProjectCard", "A new application");
    auto labelFor = [](const std::string& id, const std::string& text) {
        auto label = Theme::MakeText(id, text, Theme::kSizeBody, Theme::kTextSecondary);
        label->SetElementSize(Size2Df(110, Theme::kControlHeight));
        label->layoutItem.SetFlexShrink(0);
        return label;
    };
    auto nameRow = Theme::MakeRow("ucsNameRow");
    nameRow->AddChild(labelFor("ucsNameLabel", "Application name"));
    appNameInput_ = CreateTextInput("ucsAppName", 0, 0, 240, Theme::kControlHeight);
    Theme::StyleInput(appNameInput_);
    appNameInput_->SetText(choices_.appName);
    appNameInput_->onTextChanged = [this](const std::string&) { ReadChoicesFromPanes(); RefreshPlan(); };
    nameRow->AddChild(appNameInput_);
    projectCard_->AddChild(nameRow);

    auto folderRow = Theme::MakeRow("ucsFolderRow");
    folderRow->AddChild(labelFor("ucsFolderLabel", "Project folder"));
    folderInput_ = CreateTextInput("ucsFolder", 0, 0, 480, Theme::kControlHeight);
    Theme::StyleInput(folderInput_);
    folderInput_->SetPlaceholder("an empty folder for the application");
    folderInput_->layoutItem.SetFlexGrow(1);
    if (!profile_.homeDirectory.empty()) {
        folderInput_->SetText(profile_.homeDirectory + "/" + choices_.appName);
    }
    folderRow->AddChild(folderInput_);
    auto browse = Theme::MakeButton("ucsBrowse", "Choose...", false, 90);
    browse->onClick = [this]() { ChooseProjectFolder(); };
    folderRow->AddChild(browse);
    projectCard_->AddChild(folderRow);

    auto actionRow = Theme::MakeRow("ucsProjectActions");
    createButton_ = Theme::MakeButton("ucsCreate", "Create the project", true, 150);
    createButton_->onClick = [this]() { CreateProject(); };
    actionRow->AddChild(createButton_);
    auto hint = Theme::MakeText("ucsProjectHint",
        "Writes CMakeLists.txt, main.cpp, CMakePresets.json, README.md and the assistant's instruction file. Preview below.",
        Theme::kSizeBody, Theme::kTextSecondary);
    hint->layoutItem.SetFlexGrow(1);
    actionRow->AddChild(hint);
    projectCard_->AddChild(actionRow);
    pane->AddChild(projectCard_);

    previewCard_ = Theme::MakeCard("ucsPreviewCard", "The files");
    projectPreview_ = Theme::MakeConsole("ucsProjectPreview", 300, /*dark=*/false);
    Theme::Grow(projectPreview_);
    previewCard_->AddChild(projectPreview_);
    Theme::GrowCard(previewCard_);
    pane->AddChild(previewCard_);

    // ----- Done: shown instead of the two cards above once the project exists -----
    doneCard_ = Theme::MakeCard("ucsDoneCard", "Done");
    doneView_ = Theme::MakeMarkdownView("ucsDoneView", 200);
    Theme::Grow(doneView_);
    doneCard_->AddChild(doneView_);
    auto doneRow = Theme::MakeRow("ucsDoneRow");
    auto copyReport = Theme::MakeButton("ucsCopyReport", "Copy the report", false, 130);
    copyReport->onClick = [this]() { CopyReport(); };
    doneRow->AddChild(copyReport);
    auto doneHint = Theme::MakeText("ucsDoneHint",
        "The system, the checks, the plan and the notes as text - for a colleague, an issue or the assistant.",
        Theme::kSizeBody, Theme::kTextSecondary);
    doneHint->layoutItem.SetFlexGrow(1);
    doneRow->AddChild(doneHint);
    doneCard_->AddChild(doneRow);
    Theme::GrowCard(doneCard_);
    doneCard_->SetVisible(false);
    pane->AddChild(doneCard_);

    promptCard_ = Theme::MakeCard("ucsPromptCard", "The first prompt");
    auto promptRow = Theme::MakeRow("ucsPromptRow");
    auto copyPrompt = Theme::MakeButton("ucsCopyPrompt", "Copy the first prompt", true, 160);
    copyPrompt->onClick = [this]() { CopyPrompt(); };
    promptRow->AddChild(copyPrompt);
    auto promptHint = Theme::MakeText("ucsPromptHint",
        "Open the project folder with the assistant and paste this as the first message.",
        Theme::kSizeBody, Theme::kTextSecondary);
    promptHint->layoutItem.SetFlexGrow(1);
    promptRow->AddChild(promptHint);
    promptCard_->AddChild(promptRow);
    promptText_ = Theme::MakeConsole("ucsPromptText", 120, /*dark=*/false);
    Theme::Grow(promptText_);
    promptCard_->AddChild(promptText_);
    Theme::GrowCard(promptCard_);
    promptCard_->SetVisible(false);
    pane->AddChild(promptCard_);
    return pane;
}

// ===== NAVIGATION ===========================================================

void UltraCanvasStartWindow::ShowStepPane(int index) {
    if (index < 0 || index >= static_cast<int>(panes_.size())) return;
    if (index != currentStep_) OnLeaveStep(currentStep_);
    currentStep_ = index;
    for (int i = 0; i < static_cast<int>(panes_.size()); ++i) panes_[i]->SetVisible(i == index);
    if (paneHost_) paneHost_->InvalidateLayout();
    OnEnterStep(index);
    UpdateNavigation();
    if (window_) window_->RequestRedraw();
}

void UltraCanvasStartWindow::GoNext() {
    if (!stepper_ || working_) return;
    if (currentStep_ + 1 >= kStepCount) return;
    stepper_->NextStep();   // onStepChanged shows the pane
}

void UltraCanvasStartWindow::GoBack() {
    if (!stepper_ || working_) return;
    if (currentStep_ == 0) return;
    stepper_->PrevStep();
}

void UltraCanvasStartWindow::OnLeaveStep(int index) {
    if (!stepper_) return;
    ReadChoicesFromPanes();
    RefreshPlan();
    if (index == static_cast<int>(Step::Tools)) {
        // Going on with something missing is allowed (a programmer may
        // install by hand); the marker says so until the checks pass.
        const bool missing = !checks_.empty() && plan_.MissingCount() > 0 &&
                             choices_.platform == profile_.platform;
        stepper_->SetStepError(index, missing);
    } else if (index == static_cast<int>(Step::Framework)) {
        const bool unset = choices_.useSdk && (!sdkPrefixInput_ || sdkPrefixInput_->GetText().empty());
        stepper_->SetStepError(index, unset);
    }
}

void UltraCanvasStartWindow::OnEnterStep(int index) {
    if (index == static_cast<int>(Step::Tools) && checks_.empty() && !working_ &&
        choices_.platform == profile_.platform) {
        StartChecks();
    }
    if (index == static_cast<int>(Step::Framework) || index == static_cast<int>(Step::Project)) {
        ReadChoicesFromPanes();
        RefreshPlan();
    }
    if (index == static_cast<int>(Step::Project) && doneCard_ && promptCard_) {
        doneCard_->SetVisible(projectCreated_);
        promptCard_->SetVisible(projectCreated_ && choices_.useAi);
        projectCard_->SetVisible(!projectCreated_);
        previewCard_->SetVisible(!projectCreated_);
    }
    static const char* kStatus[kStepCount] = {
        "Step 1 of 5: this is what was found. Next goes on to the features.",
        "Step 2 of 5: tick what the application needs and pick your assistant.",
        "Step 3 of 5: the checks run now; Install what is missing runs the package manager.",
        "Step 4 of 5: fetch the SDK here, or choose to build the framework from source.",
        "Step 5 of 5: name the application and create it."
    };
    SetStatus(kStatus[index]);
}

void UltraCanvasStartWindow::UpdateNavigation() {
    if (stepHint_) stepHint_->SetText(kStepHints[currentStep_]);
    if (backButton_) backButton_->SetDisabled(currentStep_ == 0 || working_);
    if (nextButton_) {
        const bool last = currentStep_ + 1 >= kStepCount;
        nextButton_->SetVisible(!last);
        nextButton_->SetDisabled(last || working_);
    }
}

// ===== ACTIONS ==============================================================

void UltraCanvasStartWindow::SelectGuidePlatform(Platform platform) {
    guidePlatform_ = platform;
    if (guideView_) {
        guideView_->SetText(PlatformGuide(platform, FrameworkVersion(),
                                          GuideArchitecture(profile_, platform)), false);
    }
}

void UltraCanvasStartWindow::SelectAssistant(Assistant assistant) {
    choices_.assistant = assistant;
    if (aiView_) aiView_->SetText(AiGuide(ai_, profile_.platform, assistant), false);
    RefreshPlan();
}

void UltraCanvasStartWindow::SelectFrameworkWay(bool useSdk) {
    choices_.useSdk = useSdk;
    choices_.cloneFramework = !useSdk;
    RefreshFrameworkStep();
    RefreshPlan();
}

void UltraCanvasStartWindow::StartChecks() {
    if (working_) return;
    ReadChoicesFromPanes();
    if (worker_.joinable()) worker_.join();
    working_ = true;
    SetBusy(true);
    SetStatus("Checking the tools and libraries...");
    if (installOutput_) installOutput_->SetText("");
    const SystemProfile profile = profile_;
    const Choices choices = choices_;
    worker_ = std::thread([this, profile, choices]() {
        auto results = RunChecks(profile, choices, [this](const std::string& title) {
            RunOnUiThread([this, title]() { SetStatus("Checking " + title + "..."); });
        });
        RunOnUiThread([this, results]() {
            checks_ = results;
            working_ = false;
            SetBusy(false);
            RefreshChecksText();
            RefreshPlan();
            const size_t missing = plan_.MissingCount();
            if (stepper_) stepper_->SetStepError(static_cast<int>(Step::Tools), missing > 0);
            SetStatus(missing == 0 ? "Everything the chosen features need is installed. Next goes on to the framework."
                                   : std::to_string(missing) + " missing; \"Install what is missing\" runs the package manager.");
        });
    });
}

void UltraCanvasStartWindow::StartInstall() {
    if (working_) return;
    ReadChoicesFromPanes();
    RefreshPlan();
    const PlanStep* install = nullptr;
    for (const auto& step : plan_.steps) {
        if (step.kind == StepKind::Install) { install = &step; break; }
    }
    if (!install || install->argv.empty()) {
        UltraCanvasDialogManager::ShowInformation(
            "Nothing to install: every package the chosen features need is present, "
            "or no package manager was recognised.", "Install", nullptr, window_.get());
        return;
    }
    const PlanStep step = *install;
    const std::string command = DisplayCommand(step.argv, step.needsElevation);
    UltraCanvasDialogManager::ShowConfirmation(
        "This runs the package manager:\n\n" + command +
        (step.needsElevation ? "\n\nIt needs administrator rights; the system will ask for your password." : "") +
        (profile_.packageManager == PackageManager::Msys2Pacman
             ? "\n\nOn Windows the packages are installed into the MSYS2 tree." : ""),
        "Install the development packages?",
        [this, step](bool confirmed) {
            if (!confirmed) return;
            if (worker_.joinable()) worker_.join();
            working_ = true;
            SetBusy(true);
            SetStatus("Installing... this can take a few minutes.");
            if (installOutput_) installOutput_->SetText("$ " + DisplayCommand(step.argv, step.needsElevation) + "\n");
            const SystemProfile profile = profile_;
            worker_ = std::thread([this, step, profile]() {
                const RunResult result = RunStep(step, profile);
                RunOnUiThread([this, result]() {
                    working_ = false;
                    SetBusy(false);
                    if (installOutput_) {
                        installOutput_->AppendText(result.output);
                        if (!result.started) installOutput_->AppendText("\n" + result.error + "\n");
                        installOutput_->AppendText(result.Succeeded() ? "\n[done]\n"
                                                   : "\n[failed, exit " + std::to_string(result.exitCode) + "]\n");
                    }
                    if (result.Succeeded()) {
                        SetStatus("Installed. Checking again...");
                        StartChecks();
                    } else {
                        SetStatus(result.started ? "The package manager reported an error; see the output."
                                                 : result.error);
                    }
                });
            });
        },
        window_.get());
}

void UltraCanvasStartWindow::ChooseProjectFolder() {
    const std::string chosen = UltraCanvasNativeDialogs::SelectFolder(
        "Where should the application live?", profile_.homeDirectory, window_.get());
    if (chosen.empty()) return;
    if (folderInput_) folderInput_->SetText(chosen);
    ReadChoicesFromPanes();
    RefreshPlan();
}

void UltraCanvasStartWindow::FindSdk() {
    const std::string chosen = UltraCanvasNativeDialogs::SelectFolder(
        "Where is the unpacked SDK?", profile_.homeDirectory, window_.get());
    if (chosen.empty()) return;
    const std::string prefix = FindSdkPrefix(chosen);
    if (prefix.empty()) {
        UltraCanvasDialogManager::ShowError(
            "No lib/cmake/UltraCanvas/UltraCanvasConfig.cmake under " + chosen +
            ".\n\nUnpack " + SdkArchiveName(profile_.platform, FrameworkVersion(), profile_.architecture) +
            " first - \"Download and unpack...\" fetches it from\n" +
            SdkReleaseAssetUrl(profile_.platform, FrameworkVersion(), profile_.architecture),
            "Not an SDK folder", nullptr, window_.get());
        return;
    }
    if (sdkPrefixInput_) sdkPrefixInput_->SetText(prefix);
    ReadChoicesFromPanes();
    RefreshFrameworkStep();
    RefreshPlan();
}

void UltraCanvasStartWindow::DownloadAndUnpackSdk() {
    if (working_) return;
    const std::string version = FrameworkVersion();
    const std::string url = SdkReleaseAssetUrl(profile_.platform, version, profile_.architecture);
    const std::string archiveName = SdkArchiveName(profile_.platform, version, profile_.architecture);
    if (!SdkDownloadAvailable()) {
        // No network module in this build: hand the address over instead.
        SetClipboardText(url);
        UltraCanvasDialogManager::ShowInformation(
            "This build of UltraCanvasStart cannot download. The address is on the clipboard:\n\n" + url +
            "\n\nFetch it in a browser, unpack it, and use Find... to point at the folder.",
            "Download the SDK", nullptr, window_.get());
        return;
    }
    const std::string folder = UltraCanvasNativeDialogs::SelectFolder(
        "Where should the SDK be unpacked?", profile_.homeDirectory, window_.get());
    if (folder.empty()) return;
    const std::string archive = folder + "/" + archiveName;

    if (worker_.joinable()) worker_.join();
    working_ = true;
    SetBusy(true);
    SetStatus("Downloading " + archiveName + "...");
    const SystemProfile profile = profile_;
    worker_ = std::thread([this, url, archive, folder, profile, archiveName]() {
        std::string error;
        bool ok = DownloadSdk(url, archive, error);
        if (ok) {
            RunOnUiThread([this, archiveName]() { SetStatus("Unpacking " + archiveName + "..."); });
            const RunResult unpack = RunStep(UnpackStep(archive, folder), profile);
            ok = unpack.Succeeded();
            if (!ok) error = unpack.started ? "tar could not unpack the archive:\n" + unpack.output
                                           : unpack.error;
        }
        std::string prefix;
        if (ok) {
            prefix = FindSdkPrefix(folder);
            if (prefix.empty()) {
                ok = false;
                error = "The archive was unpacked, but no lib/cmake/UltraCanvas/UltraCanvasConfig.cmake "
                        "was found under " + folder;
            }
        }
        RunOnUiThread([this, ok, error, prefix]() {
            working_ = false;
            SetBusy(false);
            if (!ok) {
                SetStatus("The SDK download did not finish.");
                UltraCanvasDialogManager::ShowError(error, "Download the SDK", nullptr, window_.get());
                return;
            }
            if (sdkPrefixInput_) sdkPrefixInput_->SetText(prefix);
            ReadChoicesFromPanes();
            RefreshFrameworkStep();
            RefreshPlan();
            for (auto& step : plan_.steps) if (step.kind == StepKind::Download) step.done = true;
            if (stepper_) stepper_->SetStepError(static_cast<int>(Step::Framework), false);
            SetStatus("SDK unpacked into " + prefix + ". Next goes on to the project.");
        });
    });
}

void UltraCanvasStartWindow::CreateProject() {
    ReadChoicesFromPanes();
    if (choices_.projectFolder.empty()) {
        UltraCanvasDialogManager::ShowError("Choose a project folder first.", "No folder", nullptr, window_.get());
        return;
    }
    ProjectOptions options;
    options.folder = choices_.projectFolder;
    options.appName = choices_.appName;
    options.useSdk = choices_.useSdk;
    options.sdkPrefix = sdkPrefixInput_ ? sdkPrefixInput_->GetText() : "";
    options.withAiNotes = choices_.useAi;
    options.assistant = choices_.assistant;
    const ProjectResult result = ScaffoldProject(options);
    if (!result.ok) {
        UltraCanvasDialogManager::ShowError(result.error, "The project was not created", nullptr, window_.get());
        return;
    }
    for (auto& step : plan_.steps) if (step.kind == StepKind::Scaffold) step.done = true;
    projectCreated_ = true;

    // The Done page: what was written, the commands, the assistant.
    std::string done = "### Written\n\n";
    for (const auto& path : result.written) done += "- `" + path + "`\n";
    done += "\n### Next\n\n";
    done += "1. `cd \"" + options.folder + "\"`\n";
    done += "2. `cmake --preset default`\n";
    done += "3. `cmake --build --preset default`\n";
    if (options.useSdk && options.sdkPrefix.empty()) {
        done += "\nThe SDK prefix is empty: unpack the SDK (step 4) and set `CMAKE_PREFIX_PATH` in `CMakePresets.json`.\n";
    }
    if (choices_.useAi) {
        done += "\nThen open the folder with **" + AssistantName(choices_.assistant) +
                "** and paste the first prompt below as the first message.\n";
    }
    if (doneView_) doneView_->SetText(done, false);
    OnEnterStep(static_cast<int>(Step::Project));
    if (paneHost_) paneHost_->InvalidateLayout();
    SetStatus("Project created in " + options.folder + ". That was the last step.");
}

void UltraCanvasStartWindow::CopyReport() {
    ReadChoicesFromPanes();
    RefreshPlan();
    SetStatus(SetClipboardText(RenderReport(plan_)) ? "Report copied to the clipboard."
                                                    : "The clipboard could not be written.");
}

void UltraCanvasStartWindow::CopyPrompt() {
    ReadChoicesFromPanes();
    SetStatus(SetClipboardText(FirstPrompt(choices_)) ? "Prompt copied to the clipboard."
                                                      : "The clipboard could not be written.");
}

void UltraCanvasStartWindow::ShowGuideDialog() {
    DialogConfig config;
    config.title      = "The way in";
    config.width      = 820;
    config.height     = 560;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(Theme::kPagePadding);
    dialog->SetBackgroundColor(Theme::kPageBackground);

    std::shared_ptr<UltraCanvasSegmentedControl> picker;
    std::shared_ptr<UltraCanvasTextArea> view;
    auto card = BuildGuideCard("ucsGuideDialog", picker, view, 400);
    // The dialog's own view follows its own picker; raw pointers, since the
    // dialog owns both (AGENTS.md: no shared_ptr in a callback).
    auto* viewRaw = view.get();
    const SystemProfile profile = profile_;
    picker->onSegmentSelected = [viewRaw, profile](int index) {
        const Platform platform = kPlatforms[index];
        viewRaw->SetText(PlatformGuide(platform, FrameworkVersion(), GuideArchitecture(profile, platform)), false);
    };
    dialog->AddChild(card);

    auto row = Theme::MakeRow("ucsGuideDialogRow");
    row->AddStretchSpacer(1);
    auto close = Theme::MakeButton("ucsGuideDialogClose", "Close", true, 90);
    close->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    row->AddChild(close);
    dialog->AddChild(row);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, window_.get());
}

void UltraCanvasStartWindow::ShowReportDialog() {
    ReadChoicesFromPanes();
    RefreshPlan();
    DialogConfig config;
    config.title      = "The report";
    config.width      = 820;
    config.height     = 560;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;
    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn().SetFlexGap(Theme::kGap)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(Theme::kPagePadding);
    dialog->SetBackgroundColor(Theme::kPageBackground);

    auto card = Theme::MakeCard("ucsReportDialogCard");
    auto text = Theme::MakeConsole("ucsReportDialogText", 420, /*dark=*/false);
    text->SetText(RenderReport(plan_));
    Theme::Grow(text);
    card->AddChild(text);
    Theme::GrowCard(card);
    dialog->AddChild(card);

    auto row = Theme::MakeRow("ucsReportDialogRow");
    auto hint = Theme::MakeText("ucsReportDialogHint",
        "The system, the checks, the plan and the notes as text - for a colleague, an issue or the assistant.",
        Theme::kSizeBody, Theme::kTextSecondary);
    hint->layoutItem.SetFlexGrow(1);
    row->AddChild(hint);
    auto copy = Theme::MakeButton("ucsReportDialogCopy", "Copy", true, 90);
    copy->onClick = [this]() { CopyReport(); };
    row->AddChild(copy);
    auto close = Theme::MakeButton("ucsReportDialogClose", "Close", false, 90);
    close->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    row->AddChild(close);
    dialog->AddChild(row);
    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, window_.get());
}

// ===== VIEW UPDATES =========================================================

void UltraCanvasStartWindow::ReadChoicesFromPanes() {
    for (const auto& [group, box] : groupBoxes_) choices_.Set(group, box->IsChecked());
    if (wayPicker_) {
        choices_.useSdk = wayPicker_->GetSelectedIndex() == 0;
        choices_.cloneFramework = !choices_.useSdk;
    }
    if (aiBox_)    choices_.useAi = aiBox_->IsChecked();
    if (cloudBox_) choices_.cloudOnly = cloudBox_->IsChecked() && choices_.useAi;
    if (appNameInput_ && !appNameInput_->GetText().empty()) choices_.appName = appNameInput_->GetText();
    if (folderInput_) choices_.projectFolder = folderInput_->GetText();
}

void UltraCanvasStartWindow::RefreshChecksText() {
    if (!checksView_) return;
    checksView_->SetText(ChecksMarkdown(checks_), false);
    size_t missing = 0;
    for (const auto& c : checks_) {
        if (c.checked && !(c.present && c.versionOk)) ++missing;
    }
    if (installBadge_) {
        if (checks_.empty()) {
            installBadge_->SetText("not checked yet");
            installBadge_->SetVariant(BadgeVariant::Neutral);
        } else if (missing == 0) {
            installBadge_->SetText("everything installed");
            installBadge_->SetVariant(BadgeVariant::Successful);
        } else {
            installBadge_->SetText(std::to_string(missing) + " missing");
            installBadge_->SetVariant(BadgeVariant::Warning);
        }
    }
    if (installButton_) installButton_->SetDisabled(checks_.empty() || missing == 0 || working_);
    if (installSummary_) {
        installSummary_->SetText(
            checks_.empty() ? ""
            : missing == 0 ? "Everything the chosen features need is installed."
            : profile_.packageManagerPath.empty()
                ? "No package manager was found to install them with; Next goes on anyway."
                : "Install what is missing runs " + PackageManagerName(profile_.packageManager) +
                  " for them; Next goes on anyway.");
    }
}

void UltraCanvasStartWindow::RefreshFrameworkStep() {
    const bool sdk = !wayPicker_ || wayPicker_->GetSelectedIndex() == 0;
    const std::string prefix = sdkPrefixInput_ ? sdkPrefixInput_->GetText() : "";
    if (sdkRow_) sdkRow_->SetVisible(sdk);
    if (frameworkBadge_) {
        if (!sdk) {
            frameworkBadge_->SetText("clone and build");
            frameworkBadge_->SetVariant(BadgeVariant::Info);
        } else if (prefix.empty()) {
            frameworkBadge_->SetText("no SDK yet");
            frameworkBadge_->SetVariant(BadgeVariant::Warning);
        } else {
            frameworkBadge_->SetText("SDK found");
            frameworkBadge_->SetVariant(BadgeVariant::Successful);
        }
    }
    if (frameworkView_) {
        std::string text;
        if (sdk) {
            text += "The SDK is the framework already built: the headers, the libraries, the plug-ins and the "
                    "CMake package that `find_package(UltraCanvas)` reads "
                    "([Docs/UltraCanvasSDK.md](https://github.com/ULTRA-OS-Project/UltraCanvas/blob/main/Docs/UltraCanvasSDK.md)).\n\n";
            text += "1. **Download and unpack...** fetches the archive above into a folder you pick and unpacks it there.\n";
            text += "2. Or unpack it yourself and use **Find...** to point at the folder.\n";
            text += "3. The prefix goes into the project's `CMakePresets.json` as `CMAKE_PREFIX_PATH`.\n";
            if (!prefix.empty()) text += "\nThe prefix is `" + prefix + "`.\n";
            else text += "\nWithout a prefix the project is still written; set `CMAKE_PREFIX_PATH` in `CMakePresets.json` later.\n";
        } else {
            const std::string destination = choices_.projectFolder.empty()
                ? std::string("UltraCanvas") : choices_.projectFolder + "/../UltraCanvas";
            const PlanStep clone = CloneStep(destination);
            text += "The framework is cloned next to the project and built with CMake; the project's "
                    "`CMakeLists.txt` expects the checkout at `../UltraCanvas`.\n\n";
            text += "1. `" + DisplayCommand(clone.argv, false) + "`\n";
            text += "2. Build it the way step 1 of [Docs/GettingStarted.md]"
                    "(https://github.com/ULTRA-OS-Project/UltraCanvas/blob/main/Docs/GettingStarted.md) says for " +
                    PlatformName(profile_.platform) + ".\n";
            text += "3. Create the project (step 5); it finds the checkout by that path.\n";
        }
        frameworkView_->SetText(text, false);
    }
    if (paneHost_) paneHost_->InvalidateLayout();
}

void UltraCanvasStartWindow::RefreshPlan() {
    const std::vector<CheckResult> checks = choices_.platform == profile_.platform ? checks_
                                                                                  : std::vector<CheckResult>();
    plan_ = BuildPlan(profile_, choices_, checks);
    if (projectPreview_) {
        ProjectOptions options;
        options.folder = choices_.projectFolder;
        options.appName = choices_.appName;
        options.useSdk = choices_.useSdk;
        options.sdkPrefix = sdkPrefixInput_ ? sdkPrefixInput_->GetText() : "";
        options.withAiNotes = choices_.useAi;
        options.assistant = choices_.assistant;
        projectPreview_->SetText("--- CMakeLists.txt\n" + ProjectCMakeLists(options) +
                                 "\n--- CMakePresets.json\n" + ProjectPresets(options) +
                                 "\n--- main.cpp\n" + ProjectMainCpp(options));
    }
    if (promptText_) promptText_->SetText(FirstPrompt(choices_));
    if (cloudBox_) cloudBox_->SetDisabled(!choices_.useAi);
}

void UltraCanvasStartWindow::SetBusy(bool busy) {
    if (checkButton_) checkButton_->SetDisabled(busy);
    // The install button is for what the checks found missing; nothing to
    // install keeps it off (RefreshChecksText decides).
    if (installButton_) installButton_->SetDisabled(busy || checks_.empty() || plan_.MissingCount() == 0);
    if (downloadButton_) downloadButton_->SetDisabled(busy);
    if (findButton_) findButton_->SetDisabled(busy);
    if (createButton_) createButton_->SetDisabled(busy);
    UpdateNavigation();
}

void UltraCanvasStartWindow::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

// ===== THREAD PLUMBING ======================================================

void UltraCanvasStartWindow::RunOnUiThread(std::function<void()> action) {
    std::lock_guard<std::mutex> lock(uiQueueMutex_);
    uiQueue_.push_back(std::move(action));
}

void UltraCanvasStartWindow::DrainUiQueue() {
    std::vector<std::function<void()>> actions;
    {
        std::lock_guard<std::mutex> lock(uiQueueMutex_);
        actions.swap(uiQueue_);
    }
    for (auto& action : actions) action();
}

} // namespace UltraCanvasStart
