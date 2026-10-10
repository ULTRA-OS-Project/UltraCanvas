// Apps/UltraCanvasStart/ui/UltraCanvasStartWindow.cpp
// Version: 0.3.0 - the assistant is a choice (Claude Code, Codex, Copilot, Gemini,
//                  other); segmented controls pick it and the platform
// Version: 0.2.0 - UltraMail's look: theme header, cards, Markdown views with
//                  links and highlighted commands, a structured System page,
//                  ShowPage for --page
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

constexpr float kWindowWidth  = 980.0f;
constexpr float kWindowHeight = 720.0f;
constexpr float kSideMargin   = Theme::kPagePadding;

// Tab order, as added in Initialize.
constexpr int kPlatformTab = 0;
constexpr int kSystemTab   = 1;
constexpr int kChoicesTab  = 2;
constexpr int kInstallTab  = 3;
constexpr int kProjectTab  = 4;
constexpr int kAiTab       = 5;
constexpr int kReportTab   = 6;

const DependencyGroup kGroups[] = {
    DependencyGroup::Toolchain, DependencyGroup::Core, DependencyGroup::Cdr,
    DependencyGroup::Pdf, DependencyGroup::Ocr, DependencyGroup::Vectorizer,
    DependencyGroup::Audio, DependencyGroup::Barcode, DependencyGroup::Net
};

std::string Join(const std::vector<std::string>& lines, const char* bullet = "") {
    std::string out;
    for (const auto& line : lines) out += bullet + line + "\n";
    return out;
}

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

// The architecture the instructions name: the machine's for its own
// platform, x86_64 for another one.
std::string GuideArchitecture(const SystemProfile& profile, Platform target) {
    return target == profile.platform && !profile.architecture.empty() ? profile.architecture : "x86_64";
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
    ai_ = DetectAi(profile_);

    WindowConfig config;
    config.title  = std::string("UltraCanvasStart ") + ULTRACANVASSTART_VERSION;
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 760;
    config.minHeight = 520;
    config.backgroundColor = Theme::kPageBackground;
    window_ = CreateWindow(config);
    if (!window_) return false;

    // ----- the header band: name, version, what was detected -----
    header_ = CreateContainer("ucsHeader", 0, 0, kWindowWidth, Theme::kHeaderHeight);
    header_->layout.SetFlexRow().SetFlexGap(Theme::kGap)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header_->SetPadding(0, Theme::kPagePadding + 6.0f);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    header_->SetContainerStyle(plain);
    auto title = Theme::MakeText("ucsTitle", "UltraCanvasStart", Theme::kSizeTitle,
                                 Theme::kTextPrimary, FontWeight::Bold);
    title->layoutItem.SetFlexShrink(0);
    header_->AddChild(title);
    auto version = Theme::MakeText("ucsVersion", ULTRACANVASSTART_VERSION, Theme::kSizeBody, Theme::kTextMuted);
    version->layoutItem.SetFlexShrink(0);
    header_->AddChild(version);
    header_->AddChild(Theme::MakeText("ucsSubtitle",
        "sets this computer up for UltraCanvas development  \xC2\xB7  " +
        PlatformName(profile_.platform) + " " + profile_.architecture + " detected",
        Theme::kSizeBody, Theme::kTextSecondary));
    window_->AddChild(header_);

    // ----- the pages -----
    tabs_ = CreateTabbedContainer("ucsTabs", kSideMargin, Theme::kHeaderHeight,
                                  kWindowWidth - 2 * kSideMargin,
                                  kWindowHeight - Theme::kHeaderHeight - Theme::kStatusHeight - kSideMargin);
    tabs_->SetTabStyle(TabStyle::Modern);
    tabs_->SetTabHeight(30);
    tabs_->fontSize = static_cast<int>(Theme::kSizeBody + 1);
    tabs_->SetActiveTabBackgroundColor(Theme::kCardBackground);
    tabs_->SetInactiveTabBackgroundColor(Theme::kPageBackground);
    tabs_->SetInactiveTabTextColor(Theme::kTextSecondary);
    tabs_->activeTabTextColor = Theme::kTextPrimary;
    tabs_->hoveredTabColor = Theme::kRowHover;
    tabs_->tabBorderColor = Theme::kCardBorder;
    tabs_->tabContentBorderColor = Theme::kCardBorder;
    tabs_->contentAreaColor = Theme::kPageBackground;
    tabs_->AddTab("Platform", BuildPlatformPage());
    tabs_->AddTab("System",   BuildSystemPage());
    tabs_->AddTab("Choices",  BuildChoicesPage());
    tabs_->AddTab("Install",  BuildInstallPage());
    tabs_->AddTab("Project",  BuildProjectPage());
    tabs_->AddTab("AI",       BuildAiPage());
    tabs_->AddTab("Report",   BuildReportPage());
    tabs_->onTabSelect = [this](int index) { OnTabEntered(index); };
    tabs_->SetActiveTab(kPlatformTab);
    window_->AddChild(tabs_);

    // ----- the status band -----
    statusBand_ = CreateContainer("ucsStatusBand", 0, kWindowHeight - Theme::kStatusHeight,
                                  kWindowWidth, Theme::kStatusHeight);
    statusBand_->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Center);
    statusBand_->SetPadding(0, Theme::kPagePadding + 6.0f);
    statusBand_->SetContainerStyle(plain);
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
    SetStatus("Choose the platform the instructions are for, then look at the System and Choices pages.");
    return true;
}

void UltraCanvasStartWindow::Show() {
    if (window_) window_->Show();
}

bool UltraCanvasStartWindow::ShowPage(const std::string& name) {
    static const std::map<std::string, int> kPages = {
        { "platform", kPlatformTab }, { "system", kSystemTab }, { "choices", kChoicesTab },
        { "install", kInstallTab }, { "project", kProjectTab }, { "ai", kAiTab }, { "report", kReportTab }
    };
    const auto found = kPages.find(name);
    if (found == kPages.end() || !tabs_) return false;
    tabs_->SetActiveTab(found->second);
    OnTabEntered(found->second);
    return true;
}

void UltraCanvasStartWindow::PreselectPlatform(Platform platform) {
    if (platformPicker_) platformPicker_->SetSelectedIndex(PlatformIndex(platform));
    SelectPlatform(platform);
}

void UltraCanvasStartWindow::PreselectAssistant(Assistant assistant) {
    if (assistantPicker_) assistantPicker_->SetSelectedIndex(AssistantIndex(assistant));
    SelectAssistant(assistant);
}

void UltraCanvasStartWindow::LayoutForSize(float width, float height) {
    if (!tabs_) return;
    const float tabsWidth  = std::max(320.0f, width - 2 * kSideMargin);
    const float tabsHeight = std::max(240.0f, height - Theme::kHeaderHeight - Theme::kStatusHeight - kSideMargin);
    if (header_) {
        header_->SetElementAbsolutePosition(Point2Df(0, 0));
        header_->SetElementSize(Size2Df(width, Theme::kHeaderHeight));
    }
    tabs_->SetElementAbsolutePosition(Point2Df(kSideMargin, Theme::kHeaderHeight));
    tabs_->SetElementSize(Size2Df(tabsWidth, tabsHeight));
    tabs_->InvalidateLayout();
    if (statusBand_) {
        statusBand_->SetElementAbsolutePosition(Point2Df(0, height - Theme::kStatusHeight));
        statusBand_->SetElementSize(Size2Df(width, Theme::kStatusHeight));
    }
    if (window_) window_->AddDirtyRectangle(
        Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

// ===== PAGE HELPERS =========================================================

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::NewPage(const std::string& id) {
    // Laid out, not positioned: no size of its own, the tabbed container
    // measures the active page (see UltraCleaner's rule page for why).
    auto page = CreateContainer(id, 0, 0, 0, 0);
    page->layout.SetFlexColumn()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(Theme::kPagePadding);
    page->SetBackgroundColor(Theme::kPageBackground);
    page->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    page->SetContainerStyle(plain);
    return page;
}

// ===== PAGES ================================================================

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildPlatformPage() {
    auto page = NewPage("ucsPlatformPage");

    auto choose = Theme::MakeCard("ucsPlatformCard");
    choose->AddChild(Theme::MakeHeading("ucsPlatformHeading", "Which platform are the instructions for?"));
    choose->AddChild(Theme::MakeHint("ucsPlatformHint",
        "Preselected to the one detected here. Pick another to read how a different "
        "operating system is set up; only the detected one can be checked and installed.", 32));
    auto row = Theme::MakeRow("ucsPlatformRow");
    platformPicker_ = Theme::MakeSegmented("ucsPlatformPicker",
        { PlatformName(Platform::Linux), PlatformName(Platform::MacOS), PlatformName(Platform::Windows) },
        PlatformIndex(profile_.platform), 330);
    // The picker lives inside the window that owns this object; a raw capture
    // of `this` is the convention (AGENTS.md: no shared_ptr in a callback).
    platformPicker_->onSegmentSelected = [this](int index) { SelectPlatform(kPlatforms[index]); };
    row->AddChild(platformPicker_);
    choose->AddChild(row);
    page->AddChild(choose);

    auto guide = Theme::MakeCard("ucsGuideCard");
    platformGuide_ = Theme::MakeMarkdownView("ucsPlatformGuide", 400);
    platformGuide_->SetText(PlatformGuide(profile_.platform, FrameworkVersion(),
                                          GuideArchitecture(profile_, profile_.platform)), false);
    Theme::Grow(platformGuide_);
    guide->AddChild(platformGuide_);
    Theme::GrowCard(guide);
    page->AddChild(guide);
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildSystemPage() {
    auto page = NewPage("ucsSystemPage");
    const std::string version = FrameworkVersion();
    const std::string archive = SdkArchiveName(profile_.platform, version, profile_.architecture);
    const std::string assetUrl = SdkReleaseAssetUrl(profile_.platform, version, profile_.architecture);

    // ----- the computer -----
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

    // ----- the tools -----
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
        if (!any) {
            row->AddChild(Theme::MakeStatusBadge("ucsSysAssistantNone", false, "none found"));
        }
        row->AddChild(Theme::MakeValue("ucsSysAssistantHint",
                                       any ? (ai_.claudeInstalled ? ai_.claudePath : std::string())
                                           : "the AI page says how to install one",
                                       any ? Theme::kTextPrimary : Theme::kTextSecondary));
        computer->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsSysGit", "git");
        row->AddChild(Theme::MakeStatusBadge("ucsSysGitBadge", ai_.gitInstalled,
                                             ai_.gitInstalled ? "installed" : "not found"));
        computer->AddChild(row);
    }
    page->AddChild(computer);

    // ----- the framework -----
    auto framework = Theme::MakeCard("ucsFrameworkCard", "The framework");
    Theme::AddKeyValue(framework, "ucsSysVersion", "UltraCanvas", version +
                       "  (this build of UltraCanvasStart, and the SDK that matches it)");
    {
        auto row = Theme::MakeKeyRow("ucsSysSdk", "Matching SDK");
        row->AddChild(Theme::MakeCodeLine("ucsSysSdkName", archive, { archive }));
        framework->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsSysRelease", "Download");
        row->AddChild(Theme::MakeLink("ucsSysReleaseLink", assetUrl, assetUrl));
        framework->AddChild(row);
    }
    {
        auto row = Theme::MakeKeyRow("ucsSysReleasePage", "Release page");
        row->AddChild(Theme::MakeLink("ucsSysReleasePageLink", SdkReleasePage(version), SdkReleasePage(version)));
        framework->AddChild(row);
    }
    {
        // The fallback while a release build is still running.
        auto row = Theme::MakeKeyRow("ucsSysArtifacts", "Still building?");
        row->AddChild(Theme::MakeLink("ucsSysArtifactsLink",
                                      "the same archive is a workflow artifact on the Actions page",
                                      SdkDownloadPage()));
        framework->AddChild(row);
    }
    Theme::AddKeyValue(framework, "ucsSysFetch", "In this app",
                       "the Project page's Download... button fetches the archive and unpacks it");
    page->AddChild(framework);

    // ----- the notes -----
    const auto notes = PlatformNotes(profile_, profile_.platform);
    if (!notes.empty()) {
        auto card = Theme::MakeCard("ucsNotesCard", "Notes");
        std::string markdown;
        for (const auto& note : notes) markdown += "- " + Linkify(note) + "\n";
        auto view = Theme::MakeMarkdownView("ucsNotesView", 90);
        view->SetText(markdown, false);
        Theme::Grow(view);
        card->AddChild(view);
        Theme::GrowCard(card);
        page->AddChild(card);
    }
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildChoicesPage() {
    auto page = NewPage("ucsChoicesPage");

    auto features = Theme::MakeCard("ucsFeaturesCard", "What do you need?");
    features->AddChild(Theme::MakeHint("ucsChoicesHint",
        "Features decide which development packages are checked and installed. "
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
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPage(); RefreshPlan(); };
        groupBoxes_[group] = box;
        features->AddChild(box);
    }
    page->AddChild(features);

    auto framework = Theme::MakeCard("ucsFrameworkChoiceCard", "The framework");
    sdkBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsUseSdk", 0, 0, 800, Theme::kRowHeight,
        "Use the prebuilt SDK (no framework build; find_package(UltraCanvas) against the unpacked archive)",
        choices_.useSdk);
    cloneBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsClone", 0, 0, 800, Theme::kRowHeight,
        "Clone the UltraCanvas repository next to the project (to read the docs and build from source)",
        choices_.cloneFramework);
    framework->AddChild(sdkBox_);
    framework->AddChild(cloneBox_);
    page->AddChild(framework);

    auto assistant = Theme::MakeCard("ucsAssistantCard", "The assistant");
    aiBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsUseAi", 0, 0, 800, Theme::kRowHeight,
        "I work with an AI assistant (the AI page says which; its instruction file goes into the project, "
        "the first prompt is prepared)",
        choices_.useAi);
    cloudBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsCloud", 0, 0, 800, Theme::kRowHeight,
        "...through GitHub only, with no compiler on this machine (Docs/GettingStarted-Cloud.md)",
        choices_.cloudOnly);
    assistant->AddChild(aiBox_);
    assistant->AddChild(cloudBox_);
    page->AddChild(assistant);

    for (auto& box : { sdkBox_, cloneBox_, aiBox_, cloudBox_ }) {
        Theme::StyleCheckbox(box);
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPage(); RefreshPlan(); };
    }
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildInstallPage() {
    auto page = NewPage("ucsInstallPage");

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
    page->AddChild(checks);

    auto output = Theme::MakeCard("ucsOutputCard", "Install output");
    installOutput_ = Theme::MakeConsole("ucsInstallOutput", 160);
    installOutput_->SetPlaceholder("The package manager's output appears here.");
    Theme::Grow(installOutput_);
    output->AddChild(installOutput_);
    Theme::GrowCard(output, 1.0f);
    page->AddChild(output);
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildProjectPage() {
    auto page = NewPage("ucsProjectPage");

    auto form = Theme::MakeCard("ucsProjectCard", "A new application");
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
    nameRow->AddChild(appNameInput_);
    form->AddChild(nameRow);

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
    form->AddChild(folderRow);

    auto sdkRow = Theme::MakeRow("ucsSdkRow");
    sdkRow->AddChild(labelFor("ucsSdkLabel", "SDK prefix"));
    sdkPrefixInput_ = CreateTextInput("ucsSdkPrefix", 0, 0, 480, Theme::kControlHeight);
    Theme::StyleInput(sdkPrefixInput_);
    sdkPrefixInput_->SetPlaceholder("the unpacked UltraCanvas-SDK-... folder (CMAKE_PREFIX_PATH)");
    sdkPrefixInput_->layoutItem.SetFlexGrow(1);
    sdkRow->AddChild(sdkPrefixInput_);
    auto findSdk = Theme::MakeButton("ucsFindSdk", "Find...", false, 90);
    findSdk->onClick = [this]() {
        const std::string chosen = UltraCanvasNativeDialogs::SelectFolder(
            "Where is the unpacked SDK?", profile_.homeDirectory, window_.get());
        if (chosen.empty()) return;
        const std::string prefix = FindSdkPrefix(chosen);
        if (prefix.empty()) {
            UltraCanvasDialogManager::ShowError(
                "No lib/cmake/UltraCanvas/UltraCanvasConfig.cmake under " + chosen +
                ".\n\nUnpack " + SdkArchiveName(profile_.platform, FrameworkVersion(), profile_.architecture) +
                " first - the Download button fetches it from\n" +
                SdkReleaseAssetUrl(profile_.platform, FrameworkVersion(), profile_.architecture),
                "Not an SDK folder", nullptr, window_.get());
            return;
        }
        sdkPrefixInput_->SetText(prefix);
        ReadChoicesFromPage();
        RefreshPlan();
    };
    sdkRow->AddChild(findSdk);
    downloadButton_ = Theme::MakeButton("ucsDownloadSdk", "Download...", false, 100);
    downloadButton_->onClick = [this]() { DownloadAndUnpackSdk(); };
    sdkRow->AddChild(downloadButton_);
    form->AddChild(sdkRow);

    auto actionRow = Theme::MakeRow("ucsProjectActions");
    auto create = Theme::MakeButton("ucsCreate", "Create the project", true, 150);
    create->onClick = [this]() { CreateProject(); };
    actionRow->AddChild(create);
    auto hint = Theme::MakeText("ucsProjectHint",
        "Writes CMakeLists.txt, main.cpp, CMakePresets.json, README.md and the assistant's instruction file. Preview below.",
        Theme::kSizeBody, Theme::kTextSecondary);
    hint->layoutItem.SetFlexGrow(1);
    actionRow->AddChild(hint);
    form->AddChild(actionRow);
    page->AddChild(form);

    auto preview = Theme::MakeCard("ucsPreviewCard", "The files");
    projectPreview_ = Theme::MakeConsole("ucsProjectPreview", 300, /*dark=*/false);
    Theme::Grow(projectPreview_);
    preview->AddChild(projectPreview_);
    Theme::GrowCard(preview);
    page->AddChild(preview);
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildAiPage() {
    auto page = NewPage("ucsAiPage");

    auto guide = Theme::MakeCard("ucsAiCard", "Working with an AI assistant");
    auto pickRow = Theme::MakeRow("ucsAssistantRow");
    auto pickLabel = Theme::MakeText("ucsAssistantLabel", "Which one?", Theme::kSizeBody, Theme::kTextSecondary);
    pickLabel->SetElementSize(Size2Df(70, Theme::kControlHeight));
    pickLabel->layoutItem.SetFlexShrink(0);
    pickRow->AddChild(pickLabel);
    std::vector<std::string> names;
    for (auto assistant : kAssistants) {
        names.push_back(assistant == Assistant::Other ? "Other" : AssistantName(assistant));
    }
    assistantPicker_ = Theme::MakeSegmented("ucsAssistantPicker", names,
                                            AssistantIndex(choices_.assistant), 540);
    assistantPicker_->onSegmentSelected = [this](int index) { SelectAssistant(kAssistants[index]); };
    pickRow->AddChild(assistantPicker_);
    guide->AddChild(pickRow);
    aiView_ = Theme::MakeMarkdownView("ucsAiView", 220);
    aiView_->SetText(AiGuide(ai_, profile_.platform, choices_.assistant), false);
    Theme::Grow(aiView_);
    guide->AddChild(aiView_);
    Theme::GrowCard(guide, 3.0f);
    page->AddChild(guide);

    auto prompt = Theme::MakeCard("ucsPromptCard", "The first prompt");
    auto row = Theme::MakeRow("ucsPromptRow");
    auto copy = Theme::MakeButton("ucsCopyPrompt", "Copy the first prompt", true, 160);
    copy->onClick = [this]() { CopyPrompt(); };
    row->AddChild(copy);
    auto hint = Theme::MakeText("ucsPromptHint",
        "Open the project folder with the assistant and paste this as the first message.",
        Theme::kSizeBody, Theme::kTextSecondary);
    hint->layoutItem.SetFlexGrow(1);
    row->AddChild(hint);
    prompt->AddChild(row);
    promptText_ = Theme::MakeConsole("ucsPromptText", 120, /*dark=*/false);
    Theme::Grow(promptText_);
    prompt->AddChild(promptText_);
    Theme::GrowCard(prompt, 2.0f);
    page->AddChild(prompt);
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildReportPage() {
    auto page = NewPage("ucsReportPage");
    auto card = Theme::MakeCard("ucsReportCard", "The report");
    auto row = Theme::MakeRow("ucsReportRow");
    auto copy = Theme::MakeButton("ucsCopyReport", "Copy the report", true, 130);
    copy->onClick = [this]() { CopyReport(); };
    row->AddChild(copy);
    auto hint = Theme::MakeText("ucsReportHint",
        "The system, the checks, the plan and the notes as text - for a colleague, an issue or the assistant.",
        Theme::kSizeBody, Theme::kTextSecondary);
    hint->layoutItem.SetFlexGrow(1);
    row->AddChild(hint);
    card->AddChild(row);
    reportText_ = Theme::MakeConsole("ucsReportText", 500, /*dark=*/false);
    Theme::Grow(reportText_);
    card->AddChild(reportText_);
    Theme::GrowCard(card);
    page->AddChild(card);
    return page;
}

// ===== ACTIONS ==============================================================

void UltraCanvasStartWindow::SelectPlatform(Platform platform) {
    choices_.platform = platform;
    if (platformGuide_) {
        platformGuide_->SetText(PlatformGuide(platform, FrameworkVersion(),
                                              GuideArchitecture(profile_, platform)), false);
    }
    const bool local = platform == profile_.platform;
    if (checkButton_) checkButton_->SetDisabled(!local);
    if (installButton_) installButton_->SetDisabled(!local);
    SetStatus(local ? "Instructions for this computer."
                    : "Reading " + PlatformName(platform) + " instructions; checks and installs only run for " +
                      PlatformName(profile_.platform) + ".");
    RefreshPlan();
}

void UltraCanvasStartWindow::SelectAssistant(Assistant assistant) {
    choices_.assistant = assistant;
    if (aiView_) aiView_->SetText(AiGuide(ai_, profile_.platform, assistant), false);
    RefreshPlan();
}

void UltraCanvasStartWindow::OnTabEntered(int index) {
    if (index == kInstallTab && checks_.empty() && !working_ &&
        choices_.platform == profile_.platform) {
        StartChecks();
    }
    if (index == kProjectTab || index == kAiTab || index == kReportTab) {
        ReadChoicesFromPage();
        RefreshPlan();
    }
}

void UltraCanvasStartWindow::StartChecks() {
    if (working_) return;
    ReadChoicesFromPage();
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
            SetStatus(missing == 0 ? "Everything the chosen features need is installed."
                                   : std::to_string(missing) + " missing; \"Install what is missing\" runs the package manager.");
        });
    });
}

void UltraCanvasStartWindow::StartInstall() {
    if (working_) return;
    ReadChoicesFromPage();
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
    ReadChoicesFromPage();
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
            ReadChoicesFromPage();
            RefreshPlan();
            for (auto& step : plan_.steps) if (step.kind == StepKind::Download) step.done = true;
            if (reportText_) reportText_->SetText(RenderReport(plan_));
            SetStatus("SDK unpacked into " + prefix);
        });
    });
}

void UltraCanvasStartWindow::CreateProject() {
    ReadChoicesFromPage();
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
    if (reportText_) reportText_->SetText(RenderReport(plan_));
    std::string next = "Written:\n" + Join(result.written, "  ") + "\nNext:\n";
    next += "  cd \"" + options.folder + "\"\n  cmake --preset default\n  cmake --build --preset default\n";
    if (options.useSdk && options.sdkPrefix.empty()) {
        next += "\nThe SDK prefix is empty: unpack the SDK and set CMAKE_PREFIX_PATH in CMakePresets.json.\n";
    }
    if (choices_.useAi) next += "\nThen open the folder with " + AssistantName(choices_.assistant) +
                                "; the AI page has the first prompt.\n";
    UltraCanvasDialogManager::ShowInformation(next, "Project created", nullptr, window_.get());
    SetStatus("Project created in " + options.folder);
}

void UltraCanvasStartWindow::CopyReport() {
    ReadChoicesFromPage();
    RefreshPlan();
    SetStatus(SetClipboardText(RenderReport(plan_)) ? "Report copied to the clipboard."
                                                    : "The clipboard could not be written.");
}

void UltraCanvasStartWindow::CopyPrompt() {
    ReadChoicesFromPage();
    SetStatus(SetClipboardText(FirstPrompt(choices_)) ? "Prompt copied to the clipboard."
                                                      : "The clipboard could not be written.");
}

// ===== VIEW UPDATES =========================================================

void UltraCanvasStartWindow::ReadChoicesFromPage() {
    for (const auto& [group, box] : groupBoxes_) choices_.Set(group, box->IsChecked());
    if (sdkBox_)   choices_.useSdk = sdkBox_->IsChecked();
    if (cloneBox_) choices_.cloneFramework = cloneBox_->IsChecked();
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
                ? "No package manager was found to install them with."
                : "Install what is missing runs " + PackageManagerName(profile_.packageManager) + " for them.");
    }
}

void UltraCanvasStartWindow::RefreshPlan() {
    const std::vector<CheckResult> checks = choices_.platform == profile_.platform ? checks_
                                                                                  : std::vector<CheckResult>();
    plan_ = BuildPlan(profile_, choices_, checks);
    if (reportText_) reportText_->SetText(RenderReport(plan_));
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
