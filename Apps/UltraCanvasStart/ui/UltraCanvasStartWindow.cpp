// Apps/UltraCanvasStart/ui/UltraCanvasStartWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasStartWindow.h"

#include "StartChecks.h"
#include "StartPackages.h"
#include "StartPlan.h"
#include "StartProject.h"
#include "StartRunner.h"
#include "StartSdk.h"
#include "StartSystem.h"

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
constexpr float kPagePadding  = 12.0f;
constexpr float kSectionGap   = 8.0f;

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

// The per-platform instructions of Docs/GettingStarted.md step 1, in brief,
// for the platform page: enough to read another OS's way in without leaving
// the app. The guide itself is the full text.
std::string PlatformSummary(Platform platform) {
    switch (platform) {
        case Platform::Linux:
            return "Linux\n\n"
                   "1. Install the development packages with the distribution's package manager "
                   "(apt, dnf, pacman or zypper; the Install page lists the exact names).\n"
                   "2. A C++20 compiler: clang 14+ or GCC 11+.\n"
                   "3. Either unpack the prebuilt SDK (UltraCanvas-SDK-Linux-<version>-<arch>.tar.gz) "
                   "or clone the repository and build it with CMake.\n"
                   "4. Run an application with LD_LIBRARY_PATH pointing at the SDK's lib/, or let "
                   "package-linux.sh bundle the libraries next to the executable.\n";
        case Platform::MacOS:
            return "macOS\n\n"
                   "1. xcode-select --install for the compiler and git.\n"
                   "2. Homebrew (https://brew.sh), then the formulae the Install page lists: "
                   "cmake pkg-config cairo pango harfbuzz vips glib freetype tinyxml2 and the optional ones.\n"
                   "3. Either unpack the prebuilt SDK (UltraCanvas-SDK-MacOS-<version>-<arch>.tar.gz; "
                   "arm64 for Apple silicon, x86_64 for Intel) or clone and build with CMake.\n"
                   "4. package-macos.sh makes the application bundle.\n";
        case Platform::Windows:
            return "Windows\n\n"
                   "1. Install MSYS2 (https://www.msys2.org) and open its CLANG64 shell "
                   "(CLANGARM64 on an ARM machine). Run pacman -Syu first.\n"
                   "2. pacman -S the mingw-w64-clang-x86_64-* packages the Install page lists "
                   "(clang, cmake, ninja, pkgconf, cairo, pango, ...).\n"
                   "3. Either unpack the prebuilt SDK (UltraCanvas-SDK-Windows-<version>-<arch>.zip; "
                   "the core is bin/libUltraCanvas.dll) or clone and run build-win.cmd.\n"
                   "4. package-win.sh makes the standalone zip with every DLL next to the exe.\n";
        default:
            return "Choose the platform the instructions are for.";
    }
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
    window_ = CreateWindow(config);
    if (!window_) return false;

    auto title = CreateLabel("ucsTitle", 16, 10, 260, 26, "UltraCanvasStart");
    title->SetFontSize(18);
    window_->AddChild(title);
    window_->AddChild(CreateLabel(
        "ucsSubtitle", 200, 14, 700, 22,
        std::string(ULTRACANVASSTART_VERSION) + " · sets this computer up for UltraCanvas development · " +
        PlatformName(profile_.platform) + " " + profile_.architecture + " detected"));

    tabs_ = CreateTabbedContainer("ucsTabs", 8, 44, kWindowWidth - 16, kWindowHeight - 100);
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

    statusLabel_ = CreateLabel("ucsStatus", 16, kWindowHeight - 30, kWindowWidth - 32, 22, "");
    window_->AddChild(statusLabel_);

    LayoutForSize(kWindowWidth, kWindowHeight);
    window_->onWindowResize = [this](int width, int height) {
        LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    };

    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(100, /*periodic=*/true, [this](TimerId) { DrainUiQueue(); });
    }

    RefreshSystemText();
    RefreshAiText();
    RefreshPlan();
    SetStatus("Choose the platform the instructions are for, then look at the System and Choices pages.");
    return true;
}

void UltraCanvasStartWindow::Show() {
    if (window_) window_->Show();
}

void UltraCanvasStartWindow::LayoutForSize(float width, float height) {
    if (!tabs_) return;
    constexpr float kSideMargin = 8.0f;
    constexpr float kTabsTop    = 44.0f;
    constexpr float kStatusBand = 30.0f;
    const float tabsWidth  = std::max(320.0f, width - 2 * kSideMargin);
    const float tabsHeight = std::max(240.0f, height - kTabsTop - kStatusBand - kSideMargin);
    tabs_->SetElementAbsolutePosition(Point2Df(kSideMargin, kTabsTop));
    tabs_->SetElementSize(Size2Df(tabsWidth, tabsHeight));
    tabs_->InvalidateLayout();
    if (statusLabel_) {
        statusLabel_->SetElementAbsolutePosition(Point2Df(16.0f, height - kStatusBand + 4.0f));
        statusLabel_->SetElementSize(Size2Df(std::max(200.0f, width - 32.0f), 22.0f));
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
                .SetFlexGap(kSectionGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kPagePadding);
    page->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    page->SetContainerStyle(plain);
    return page;
}

std::shared_ptr<UltraCanvasLabel> UltraCanvasStartWindow::Heading(const std::string& id,
                                                                  const std::string& text) {
    auto label = CreateLabel(id, 0, 0, 600, 26, text);
    label->SetFontSize(15);
    label->layoutItem.SetFlexShrink(0);
    return label;
}

std::shared_ptr<UltraCanvasTextArea> UltraCanvasStartWindow::ReadOnlyText(const std::string& id,
                                                                          float height) {
    auto area = std::make_shared<UltraCanvasTextArea>(id, 0, 0, 600, height);
    area->SetReadOnly(true);
    area->SetWordWrap(true);
    area->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    area->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return area;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::ButtonRow(const std::string& id) {
    auto row = CreateContainer(id, 0, 0, 0, 0);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(36));
    row->layoutItem.SetFlexShrink(0);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    row->SetContainerStyle(plain);
    return row;
}

// ===== PAGES ================================================================

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildPlatformPage() {
    auto page = NewPage("ucsPlatformPage");
    page->AddChild(Heading("ucsPlatformHeading", "Which platform are the instructions for?"));
    page->AddChild(CreateLabel("ucsPlatformHint", 0, 0, 800, 22,
        "Preselected to the one detected here. Pick another to read how a different "
        "operating system is set up; only the detected one can be checked and installed."));

    auto row = ButtonRow("ucsPlatformRow");
    for (auto platform : { Platform::Linux, Platform::MacOS, Platform::Windows }) {
        auto radio = UltraCanvasRadio::Create("ucsPlatform" + PlatformName(platform), 0, 0,
                                              PlatformName(platform), platform == profile_.platform);
        radio->SetElementSize(Size2Df(140, 26));
        platformGroup_.AddRadioButton(radio);
        if (platform == profile_.platform) platformGroup_.SelectButton(radio);
        platformRadios_[platform] = radio;
        row->AddChild(radio);
    }
    // The radios live inside the window that owns this object; a raw capture
    // of `this` is the convention (AGENTS.md: no shared_ptr in a callback).
    platformGroup_.onSelectionChanged = [this](std::shared_ptr<UltraCanvasRadio> selected) {
        for (const auto& [platform, radio] : platformRadios_) {
            if (radio == selected) { SelectPlatform(platform); break; }
        }
    };
    page->AddChild(row);

    platformNotes_ = ReadOnlyText("ucsPlatformNotes", 420);
    platformNotes_->SetText(PlatformSummary(profile_.platform));
    page->AddChild(platformNotes_);
    platformNotes_->layoutItem.SetFlexGrow(1);
    platformNotes_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildSystemPage() {
    auto page = NewPage("ucsSystemPage");
    page->AddChild(Heading("ucsSystemHeading", "This computer"));
    systemText_ = ReadOnlyText("ucsSystemText", 460);
    page->AddChild(systemText_);
    systemText_->layoutItem.SetFlexGrow(1);
    systemText_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildChoicesPage() {
    auto page = NewPage("ucsChoicesPage");
    page->AddChild(Heading("ucsChoicesHeading", "What do you need?"));
    page->AddChild(CreateLabel("ucsChoicesHint", 0, 0, 800, 22,
        "Features decide which development packages are checked and installed. "
        "The toolchain and the framework core are always needed."));

    for (auto group : kGroups) {
        const bool mandatory = group == DependencyGroup::Toolchain || group == DependencyGroup::Core;
        auto box = UltraCanvasCheckbox::CreateCheckbox(
            "ucsGroup" + DependencyGroupTitle(group), 0, 0, 800, 24,
            DependencyGroupTitle(group) + " - " + DependencyGroupDescription(group),
            choices_.Has(group));
        if (mandatory) box->SetDisabled(true);
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPage(); RefreshPlan(); };
        groupBoxes_[group] = box;
        page->AddChild(box);
    }

    page->AddChild(Heading("ucsFrameworkHeading", "The framework"));
    sdkBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsUseSdk", 0, 0, 800, 24,
        "Use the prebuilt SDK (no framework build; find_package(UltraCanvas) against the unpacked archive)",
        choices_.useSdk);
    cloneBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsClone", 0, 0, 800, 24,
        "Clone the UltraCanvas repository next to the project (to read the docs and build from source)",
        choices_.cloneFramework);
    page->AddChild(sdkBox_);
    page->AddChild(cloneBox_);

    page->AddChild(Heading("ucsAiHeading", "The assistant"));
    aiBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsUseAi", 0, 0, 800, 24,
        "I work with Claude Code (writes CLAUDE.md into the project, prepares the first prompt)",
        choices_.useAi);
    cloudBox_ = UltraCanvasCheckbox::CreateCheckbox("ucsCloud", 0, 0, 800, 24,
        "...through GitHub only, with no compiler on this machine (Docs/GettingStarted-Cloud.md)",
        choices_.cloudOnly);
    page->AddChild(aiBox_);
    page->AddChild(cloudBox_);
    for (auto& box : { sdkBox_, cloneBox_, aiBox_, cloudBox_ }) {
        box->layoutItem.SetFlexShrink(0);
        box->onStateChanged = [this](CheckedState, CheckedState) { ReadChoicesFromPage(); RefreshPlan(); };
    }
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildInstallPage() {
    auto page = NewPage("ucsInstallPage");
    page->AddChild(Heading("ucsInstallHeading", "Tools and libraries"));

    auto row = ButtonRow("ucsInstallRow");
    checkButton_ = CreateButton("ucsCheck", 0, 0, 150, 30, "Check again");
    checkButton_->onClick = [this]() { StartChecks(); };
    installButton_ = CreateButton("ucsInstall", 0, 0, 170, 30, "Install what is missing");
    installButton_->onClick = [this]() { StartInstall(); };
    installSummary_ = CreateLabel("ucsInstallSummary", 0, 0, 500, 22, "Not checked yet.");
    row->AddChild(checkButton_);
    row->AddChild(installButton_);
    row->AddChild(installSummary_);
    page->AddChild(row);

    checksText_ = ReadOnlyText("ucsChecksText", 260);
    checksText_->SetText("Press \"Check again\" to look for the tools and libraries the chosen features need.");
    page->AddChild(checksText_);
    checksText_->layoutItem.SetFlexGrow(2);
    checksText_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

    page->AddChild(Heading("ucsOutputHeading", "Install output"));
    installOutput_ = ReadOnlyText("ucsInstallOutput", 160);
    page->AddChild(installOutput_);
    installOutput_->layoutItem.SetFlexGrow(1);
    installOutput_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildProjectPage() {
    auto page = NewPage("ucsProjectPage");
    page->AddChild(Heading("ucsProjectHeading", "A new application"));

    auto nameRow = ButtonRow("ucsNameRow");
    nameRow->AddChild(CreateLabel("ucsNameLabel", 0, 0, 120, 22, "Application name"));
    appNameInput_ = CreateTextInput("ucsAppName", 0, 0, 240, 28);
    appNameInput_->SetText(choices_.appName);
    nameRow->AddChild(appNameInput_);
    page->AddChild(nameRow);

    auto folderRow = ButtonRow("ucsFolderRow");
    folderRow->AddChild(CreateLabel("ucsFolderLabel", 0, 0, 120, 22, "Project folder"));
    folderInput_ = CreateTextInput("ucsFolder", 0, 0, 480, 28);
    folderInput_->SetPlaceholder("an empty folder for the application");
    if (!profile_.homeDirectory.empty()) {
        folderInput_->SetText(profile_.homeDirectory + "/" + choices_.appName);
    }
    folderRow->AddChild(folderInput_);
    auto browse = CreateButton("ucsBrowse", 0, 0, 100, 28, "Choose...");
    browse->onClick = [this]() { ChooseProjectFolder(); };
    folderRow->AddChild(browse);
    page->AddChild(folderRow);

    auto sdkRow = ButtonRow("ucsSdkRow");
    sdkRow->AddChild(CreateLabel("ucsSdkLabel", 0, 0, 120, 22, "SDK prefix"));
    sdkPrefixInput_ = CreateTextInput("ucsSdkPrefix", 0, 0, 480, 28);
    sdkPrefixInput_->SetPlaceholder("the unpacked UltraCanvas-SDK-... folder (CMAKE_PREFIX_PATH)");
    sdkRow->AddChild(sdkPrefixInput_);
    auto findSdk = CreateButton("ucsFindSdk", 0, 0, 100, 28, "Find...");
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
    downloadButton_ = CreateButton("ucsDownloadSdk", 0, 0, 110, 28, "Download...");
    downloadButton_->onClick = [this]() { DownloadAndUnpackSdk(); };
    sdkRow->AddChild(downloadButton_);
    page->AddChild(sdkRow);

    auto actionRow = ButtonRow("ucsProjectActions");
    auto create = CreateButton("ucsCreate", 0, 0, 160, 30, "Create the project");
    create->onClick = [this]() { CreateProject(); };
    actionRow->AddChild(create);
    actionRow->AddChild(CreateLabel("ucsProjectHint", 0, 0, 600, 22,
        "Writes CMakeLists.txt, main.cpp, CMakePresets.json, README.md and CLAUDE.md. Preview below."));
    page->AddChild(actionRow);

    projectPreview_ = ReadOnlyText("ucsProjectPreview", 300);
    page->AddChild(projectPreview_);
    projectPreview_->layoutItem.SetFlexGrow(1);
    projectPreview_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildAiPage() {
    auto page = NewPage("ucsAiPage");
    page->AddChild(Heading("ucsAiPageHeading", "Working with Claude Code"));
    aiText_ = ReadOnlyText("ucsAiText", 220);
    page->AddChild(aiText_);
    aiText_->layoutItem.SetFlexGrow(1);
    aiText_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

    auto row = ButtonRow("ucsPromptRow");
    auto copy = CreateButton("ucsCopyPrompt", 0, 0, 170, 30, "Copy the first prompt");
    copy->onClick = [this]() { CopyPrompt(); };
    row->AddChild(copy);
    row->AddChild(CreateLabel("ucsPromptHint", 0, 0, 600, 22,
        "Open the project folder with Claude Code and paste this as the first message."));
    page->AddChild(row);

    promptText_ = ReadOnlyText("ucsPromptText", 200);
    page->AddChild(promptText_);
    promptText_->layoutItem.SetFlexGrow(1);
    promptText_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

std::shared_ptr<UltraCanvasContainer> UltraCanvasStartWindow::BuildReportPage() {
    auto page = NewPage("ucsReportPage");
    auto row = ButtonRow("ucsReportRow");
    auto copy = CreateButton("ucsCopyReport", 0, 0, 150, 30, "Copy the report");
    copy->onClick = [this]() { CopyReport(); };
    row->AddChild(copy);
    row->AddChild(CreateLabel("ucsReportHint", 0, 0, 700, 22,
        "The system, the checks, the plan and the notes as text - for a colleague, an issue or the assistant."));
    page->AddChild(row);

    reportText_ = ReadOnlyText("ucsReportText", 500);
    page->AddChild(reportText_);
    reportText_->layoutItem.SetFlexGrow(1);
    reportText_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    return page;
}

// ===== ACTIONS ==============================================================

void UltraCanvasStartWindow::SelectPlatform(Platform platform) {
    choices_.platform = platform;
    if (platformNotes_) platformNotes_->SetText(PlatformSummary(platform));
    const bool local = platform == profile_.platform;
    if (checkButton_) checkButton_->SetDisabled(!local);
    if (installButton_) installButton_->SetDisabled(!local);
    SetStatus(local ? "Instructions for this computer."
                    : "Reading " + PlatformName(platform) + " instructions; checks and installs only run for " +
                      PlatformName(profile_.platform) + ".");
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
    if (choices_.useAi) next += "\nThen open the folder with Claude Code; the AI page has the first prompt.\n";
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

void UltraCanvasStartWindow::RefreshSystemText() {
    if (!systemText_) return;
    std::string text;
    text += "Platform:         " + PlatformName(profile_.platform) + "\n";
    text += "Operating system: " + profile_.osName + (profile_.osVersion.empty() ? "" : " " + profile_.osVersion) + "\n";
    text += "Architecture:     " + profile_.architecture + "\n";
    if (!profile_.distributionId.empty()) {
        text += "Distribution:     " + profile_.distributionId +
                (profile_.distributionLike.empty() ? "" : " (like " + profile_.distributionLike + ")") + "\n";
    }
    text += "Package manager:  " + PackageManagerName(profile_.packageManager) +
            (profile_.packageManagerPath.empty() ? "  (not found on PATH)" : "  " + profile_.packageManagerPath) + "\n";
    if (profile_.platform == Platform::Windows) {
        text += "MSYS2:            " + (profile_.msysPrefix.empty() ? std::string("not found - install it from https://www.msys2.org")
                                                                    : profile_.msysPrefix) + "\n";
        if (!profile_.msystem.empty()) text += "MSYSTEM:          " + profile_.msystem + "\n";
    }
    text += "Home:             " + profile_.homeDirectory + "\n";
    text += "\nFramework:        UltraCanvas " + FrameworkVersion() + "\n";
    text += "Matching SDK:     " + SdkArchiveName(profile_.platform, FrameworkVersion(), profile_.architecture) + "\n";
    text += "                  " + SdkReleaseAssetUrl(profile_.platform, FrameworkVersion(), profile_.architecture) + "\n";
    text += "                  (the Project page's Download button fetches it; the workflow artifacts are at\n";
    text += "                  " + SdkDownloadPage() + ")\n";
    text += "\nClaude Code:      " + (ai_.claudeInstalled ? "installed" + (ai_.claudeVersion.empty() ? "" : ", " + ai_.claudeVersion) +
                                                            " (" + ai_.claudePath + ")" : std::string("not found")) + "\n";
    text += "git:              " + std::string(ai_.gitInstalled ? "installed" : "not found") + "\n";
    const auto notes = PlatformNotes(profile_, profile_.platform);
    if (!notes.empty()) text += "\nNotes\n" + Join(notes, "  * ");
    systemText_->SetText(text);
}

void UltraCanvasStartWindow::RefreshChecksText() {
    if (!checksText_) return;
    if (checks_.empty()) {
        checksText_->SetText("Nothing checked yet.");
        return;
    }
    std::string text;
    DependencyGroup current = DependencyGroup::Toolchain;
    bool first = true;
    size_t missing = 0;
    for (const auto& c : checks_) {
        if (first || c.group != current) {
            if (!first) text += "\n";
            text += DependencyGroupTitle(c.group) + "\n";
            current = c.group;
            first = false;
        }
        const bool ok = c.present && c.versionOk;
        if (c.checked && !ok) ++missing;
        const char* mark = !c.checked ? "  ?  " : (ok ? "  +  " : "  -  ");
        std::string line = std::string(mark) + c.title;
        if (!c.version.empty()) line += "  " + c.version;
        if (!ok || !c.checked) line += "  (" + c.detail + ")";
        if (!c.packageName.empty() && (!ok)) line += "  ->  " + c.packageName;
        text += line + "\n";
    }
    checksText_->SetText(text);
    if (installSummary_) {
        installSummary_->SetText(missing == 0 ? "Everything is installed."
                                              : std::to_string(missing) + " missing" +
                                                (profile_.packageManagerPath.empty() ? ", and no package manager was found" : ""));
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
        projectPreview_->SetText("--- CMakeLists.txt\n" + ProjectCMakeLists(options) +
                                 "\n--- CMakePresets.json\n" + ProjectPresets(options) +
                                 "\n--- main.cpp\n" + ProjectMainCpp(options));
    }
    if (promptText_) promptText_->SetText(FirstPrompt(choices_));
    if (cloudBox_) cloudBox_->SetDisabled(!choices_.useAi);
}

void UltraCanvasStartWindow::RefreshAiText() {
    if (!aiText_) return;
    std::string text;
    if (ai_.claudeInstalled) {
        text += "Claude Code is installed" + (ai_.claudeVersion.empty() ? "" : " (" + ai_.claudeVersion + ")") +
                " at " + ai_.claudePath + ".\n\n";
    } else {
        text += "Claude Code was not found on this computer. To install it:\n" +
                Join(ClaudeInstallInstructions(profile_.platform), "  ") + "\n";
    }
    text += "How the assistant works on an UltraCanvas application (Docs/GettingStarted.md):\n"
            "  1. It reads CLAUDE.md, which points at the framework's AGENTS.md and the element catalogue.\n"
            "  2. One bounded change per session; name the elements and the docs to read.\n"
            "  3. It builds and runs the check scripts before it reports back; read the ## Delivery block.\n"
            "  4. Every change gets a changelog entry; the version comes from the changelog.\n\n";
    text += "Without a compiler on this machine (Docs/GettingStarted-Cloud.md), the pull request is the compiler:\n" +
            Join(CloudChecklist(), "  - ");
    aiText_->SetText(text);
}

void UltraCanvasStartWindow::SetBusy(bool busy) {
    if (checkButton_) checkButton_->SetDisabled(busy);
    if (installButton_) installButton_->SetDisabled(busy);
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
