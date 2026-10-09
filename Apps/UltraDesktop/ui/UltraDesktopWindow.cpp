// Apps/UltraDesktop/ui/UltraDesktopWindow.cpp
// The desktop window: bars, wallpaper, and the live data behind the items.
// See the header for the shape; the comments here say why each piece is
// wired the way it is.
// Version: 0.2.0 - the notification toasts (StartNotifications, PlaceNotifications)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopWindow.h"
#include "UltraDesktopAppStarter.h"
#include "UltraDesktopClipboardPanel.h"
#include "UltraDesktopStickerboard.h"
#include "UltraDesktopTasksWindow.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWaveSeparator.h"
#include "UltraCanvasWindow.h"
#ifdef ULTRADESKTOP_HAVE_NOTIFICATIONS
#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <type_traits>

#ifndef ULTRADESKTOP_VERSION
#error "ULTRADESKTOP_VERSION is not defined: build through CMake, which reads it from Docs/UltraDesktop/CHANGELOG.md"
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace UltraDesktop {
namespace {

// ===== THE LOOK =====
constexpr int   kBarThickness   = 52;   // the taskbar's width (height when horizontal)
constexpr int   kInfoPanelWidth = 48;   // the right bar: icons only, labels are tooltips
constexpr int   kBarButton      = 40;   // one item's box
constexpr int   kBarIcon        = 24;
constexpr int   kInfoButton     = 36;   // the info panel's items, fourteen of them
constexpr int   kInfoIcon       = 20;
constexpr float kWaveLength     = 36;
constexpr unsigned kUiTimerMs   = 250;
constexpr int   kNoticeTicks    = 20;   // notices re-read every 5 s
constexpr int   kScreenshotTicks = 12;  // the "saved" marker stays 3 s
constexpr int   kDevicePollSeconds = 2;
constexpr uint64_t kActivityBytesPerPoll = 4096;   // below this a link is idle

const Color kBarColor      (74, 74, 74, 255);
const Color kBarInsetColor (20, 20, 20, 255);    // the carved-in groups
const Color kBarHover      (110, 110, 110, 255);
const Color kBarActive     (150, 150, 150, 255); // the active app / current desktop
const Color kMarkerOn      (220, 30, 30, 255);   // red: a device is on
const Color kMarkerActive  (240, 200, 0, 255);   // yellow: activity
const Color kMarkerCount   (90, 90, 90, 255);    // grey pill for counts
const Color kMarkerSaved   (60, 170, 90, 255);   // green: the screenshot landed

const char* const kRunningPrefix = "win:";

// The window's native handle as the id the desktop shell module takes: an
// X11 Window is an integer, a HWND or NSWindow a pointer. A template, so the
// branch for the other kind is discarded rather than compiled: a static_cast
// from HWND to an integer is an error, not merely dead code.
template <typename Handle>
uint64_t NativeHandleId(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
    } else {
        return static_cast<uint64_t>(handle);
    }
}

std::string Shorten(const std::string& text, size_t max) {
    if (text.size() <= max) return text;
    return text.substr(0, max - 1) + "…";
}

} // namespace

// ===== LIFETIME =====

UltraDesktopWindow::UltraDesktopWindow() = default;

UltraDesktopWindow::~UltraDesktopWindow() {
    toasts_.reset();   // its windows, its bus connection
    // The poll thread, the shell monitor and the shortcut's thread all hand
    // over through members of this object: stop them before anything else goes.
    StopDevicePoll();
    shellMonitor_.Stop();
    clipboardShortcut_.Stop();
    clipboardPanel_.reset();
    if (recorder_) recorder_->Detach();
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(uiTimer_);
    }
}

bool UltraDesktopWindow::Initialize(const std::string& settingsPath, const TaskbarEdge* edgeOverride) {
    settingsPath_ = settingsPath;
    std::string error;
    if (!settings_.Load(settingsPath_, &error)) {
        debugOutput << "UltraDesktop: settings not read (" << error << "), using the defaults" << std::endl;
    }
    if (edgeOverride) settings_.taskbarEdge = *edgeOverride;
    {
        std::error_code ec;
        settingsFileTime_ = std::filesystem::last_write_time(PathFromUtf8(settingsPath_), ec);
    }
    iconsDir_ = NormalizePath(GetResourcesDir() + "media/icons/desktop/");

    WindowConfig config;
    config.title = std::string("UltraDesktop ") + ULTRADESKTOP_VERSION;
    config.type = WindowType::Desktop;
    config.width = 1280;    // replaced by the screen size for a desktop window
    config.height = 800;
    config.backgroundColor = kBarInsetColor;
    window_ = CreateWindow(config);
    if (!window_) return false;

    BuildLayout();
    StartNotifications();

    // The clipboard history the button shows: recorded here, kept on disk.
    StartClipboardHistory();

    // Window and desktop changes arrive on the monitor's thread as a flag;
    // the device poll on its own thread as a pending reading. The UI timer
    // applies both.
    shellMonitor_.Start([this]() { windowsDirty_.store(true); });
    StartDevicePoll();
    if (auto* app = UltraCanvasApplication::GetInstance()) {
        uiTimer_ = app->StartTimer(kUiTimerMs, /*periodic=*/true, [this](TimerId) { OnTimer(); });
    }
    return true;
}

void UltraDesktopWindow::Show() {
    if (window_) window_->Show();
}

// ===== CONSTRUCTION =====

std::string UltraDesktopWindow::IconPath(const std::string& name) const {
    return iconsDir_ + name;
}

std::string UltraDesktopWindow::WallpaperPath() const {
    if (!settings_.wallpaper.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(PathFromUtf8(settings_.wallpaper), ec)) return settings_.wallpaper;
        debugOutput << "UltraDesktop: wallpaper \"" << settings_.wallpaper
                    << "\" is not there, showing the default" << std::endl;
    }
    return NormalizePath(GetResourcesDir() + "media/images/landscape.jpg");
}

void UltraDesktopWindow::BuildLayout() {
    if (root_) {
        window_->RemoveChild(root_);
        root_.reset();
    }
    runningItems_.clear();
    runningTitles_.clear();
    running_.reset();
    organiser_.reset();
    info_.reset();
    windowsDirty_.store(true);
    haveLastActivity_ = false;

    root_ = CreateContainer("Desktop", 0, 0, 0, 0);
    root_->SetBackgroundColor(kBarInsetColor);

    const TaskbarEdge edge = settings_.taskbarEdge;
    auto taskbar  = BuildTaskbar(edge == TaskbarEdge::Left);
    auto rightBar = BuildRightBar();
    workArea_ = BuildWorkArea();

    if (edge == TaskbarEdge::Left) {
        // [taskbar] [work area] [right bar]
        root_->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        workArea_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f)
                             .SetFlexBasis(CSSLayout::Dimension::Px(0));
        root_->AddChild(taskbar);
        root_->AddChild(workArea_);
        root_->AddChild(rightBar);
    } else {
        // [top taskbar] / [work area | right bar] / [bottom taskbar]
        root_->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        auto middle = CreateContainer("Middle", 0, 0, 0, 0);
        middle->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        middle->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f)
                          .SetFlexBasis(CSSLayout::Dimension::Px(0));
        workArea_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f)
                             .SetFlexBasis(CSSLayout::Dimension::Px(0));
        middle->AddChild(workArea_);
        middle->AddChild(rightBar);
        if (edge == TaskbarEdge::Top) root_->AddChild(taskbar);
        root_->AddChild(middle);
        if (edge == TaskbarEdge::Bottom) root_->AddChild(taskbar);
    }

    // The root is sized to the window explicitly (a percentage would resolve
    // against nothing: the window's children are laid out from their own
    // size) and follows every resize.
    window_->AddChild(root_);
    int width = 0, height = 0;
    window_->GetWindowSize(width, height);
    LayoutForSize(static_cast<float>(width), static_cast<float>(height));
    window_->onWindowResize = [this](int w, int h) {
        LayoutForSize(static_cast<float>(w), static_cast<float>(h));
    };
    ReserveBarEdges();
}

void UltraDesktopWindow::ReserveBarEdges() {
    if (!window_) return;
    // The taskbar's edge and the right bar, in the physical pixels the window
    // manager measures in. Through the module, so the desktop never touches
    // the window system itself.
    const int taskbar = window_->LogicalToPhysical(kBarThickness);
    const int right = window_->LogicalToPhysical(kInfoPanelWidth);
    const TaskbarEdge edge = settings_.taskbarEdge;
    UltraCanvasDesktopShell::ReserveScreenEdges(
            NativeHandleId(window_->GetNativeHandle()),
            edge == TaskbarEdge::Left ? taskbar : 0,
            right,
            edge == TaskbarEdge::Top ? taskbar : 0,
            edge == TaskbarEdge::Bottom ? taskbar : 0);
}

void UltraDesktopWindow::LayoutForSize(float width, float height) {
    if (!root_ || !window_) return;
    root_->SetElementAbsolutePosition(Point2Df(0, 0));
    root_->SetElementSize(Size2Df(std::max(1.0f, width), std::max(1.0f, height)));
    root_->InvalidateLayout();
    window_->AddDirtyRectangle(Rect2Di(0, 0, static_cast<int>(width), static_cast<int>(height)));
}

std::shared_ptr<UltraCanvasToolbar> UltraDesktopWindow::MakeGroup(const std::string& id, bool vertical,
                                                                  bool inset) {
    auto group = std::make_shared<UltraCanvasToolbar>(id, 0, 0, 0, 0);
    group->SetOrientation(vertical ? ToolbarOrientation::Vertical : ToolbarOrientation::Horizontal);
    ToolbarAppearance look = ToolbarAppearance::Flat();
    look.backgroundColor = inset ? kBarInsetColor : kBarColor;
    look.foregroundColor = Colors::White;
    look.hoverBackgroundColor = kBarHover;
    look.activeBackgroundColor = kBarActive;
    look.separatorColor = Color(120, 120, 120, 255);
    look.iconSize = ToolbarIconSize::Large;
    look.showIconLabels = false;
    look.centerIcons = true;
    look.itemSpacing = 6.0f;
    group->SetAppearance(look);
    group->SetPadding(6, 4);
    group->layoutItem.SetFlexShrink(0.0f);
    group->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return group;
}

void UltraDesktopWindow::StyleBarButton(const std::shared_ptr<UltraCanvasButton>& button) {
    if (!button) return;
    ButtonStyle style = button->GetStyle();
    style.normalColor = Colors::Transparent;
    style.hoverColor = kBarHover;
    style.pressedColor = kBarActive;
    style.normalTextColor = Colors::White;
    style.hoverTextColor = Colors::White;
    style.pressedTextColor = Colors::White;
    style.borderWidth = 0;
    style.cornerRadius = 8;
    style.fontSize = 15;
    style.useIconAsMask = true;
    button->SetStyle(style);
    button->SetIconSize(kBarIcon, kBarIcon);
    button->SetElementSize(Size2Df(static_cast<float>(kBarButton), static_cast<float>(kBarButton)));
    button->SetPadding(0);
}

std::shared_ptr<UltraCanvasButton> UltraDesktopWindow::AddBarButton(
        const std::shared_ptr<UltraCanvasToolbar>& group, const std::string& id,
        const std::string& tooltip, const std::string& icon, std::function<void()> onClick) {
    auto button = group->AddButton(id, "", icon, std::move(onClick));
    StyleBarButton(button);
    button->SetTooltip(tooltip);
    return button;
}

std::shared_ptr<UltraCanvasButton> UltraDesktopWindow::AddBarToggle(
        const std::shared_ptr<UltraCanvasToolbar>& group, const std::string& id,
        const std::string& tooltip, const std::string& icon, const std::string& text,
        std::function<void(bool)> onToggle) {
    auto button = group->AddToggleButton(id, text, icon, std::move(onToggle));
    StyleBarButton(button);
    button->SetTooltip(tooltip);
    return button;
}

std::shared_ptr<UltraCanvasContainer> UltraDesktopWindow::BuildTaskbar(bool vertical) {
    auto bar = CreateContainer("Taskbar", 0, 0, 0, 0);
    bar->SetBackgroundColor(kBarColor);
    if (vertical) {
        bar->SetElementSize(CSSLayout::Dimension::Px(kBarThickness), CSSLayout::Dimension::Auto());
        bar->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    } else {
        bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kBarThickness));
        bar->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    }
    bar->layoutItem.SetFlexShrink(0.0f);

    // 1. System: ULTRA OS settings and the app starter.
    auto system = MakeGroup("Taskbar.System", vertical, false);
    AddBarButton(system, "settings", "ULTRA OS settings", IconPath("ultraos.svg"),
                 [this]() { OpenSystemSettings(); });
    AddBarButton(system, "apps", "Applications", IconPath("apps.svg"),
                 [this]() { OpenAppStarter(); });
    bar->AddChild(system);

    bar->AddChild(CreateWaveSeparator("Taskbar.Wave1", vertical, kBarColor, kBarInsetColor, kWaveLength));

    // 2. Running apps: fills the bar, scrolls when full, drag to reorder.
    running_ = MakeGroup("Taskbar.Running", vertical, true);
    running_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    running_->SetOverflowMode(ToolbarOverflowMode::Scroll);
    running_->EnableItemReordering(true);
    bar->AddChild(running_);

    bar->AddChild(CreateWaveSeparator("Taskbar.Wave2", vertical, kBarInsetColor, kBarColor, kWaveLength));

    // 3. Pinned: the RAM disc and UltraFiler.
    auto pinned = MakeGroup("Taskbar.Pinned", vertical, false);
    AddBarButton(pinned, "ramdisc", "RAM disc (" + settings_.ramDiscPath + ")", IconPath("ramdisc.svg"),
                 [this]() { OpenRamDisc(); });
    AddBarButton(pinned, "filer", "UltraFiler", IconPath("ultrafiler.svg"),
                 [this]() { OpenFiler(); });
    bar->AddChild(pinned);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraDesktopWindow::BuildRightBar() {
    auto bar = CreateContainer("RightBar", 0, 0, 0, 0);
    bar->SetBackgroundColor(kBarColor);
    bar->SetElementSize(CSSLayout::Dimension::Px(kInfoPanelWidth), CSSLayout::Dimension::Auto());
    bar->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    bar->layoutItem.SetFlexShrink(0.0f);

    // The desktop organiser: virtual desktops, then the board, clipboard, screenshot.
    organiser_ = MakeGroup("Organiser", true, true);
    desktopCount_ = std::clamp(DesiredDesktopCount(), 1, 9);
    for (int i = 0; i < desktopCount_; ++i) {
        const std::string id = "desktop" + std::to_string(i + 1);
        AddBarToggle(organiser_, id, "Desktop " + std::to_string(i + 1), "", std::to_string(i + 1),
                     [this, i](bool) { SwitchToDesktop(i); });
    }
    organiser_->AddSeparator();
    AddBarToggle(organiser_, "stickerboard", "Stickerboard", IconPath("pinboard.svg"), "",
                 [this](bool on) { ToggleStickerboard(on); });
    auto clipboard = AddBarButton(organiser_, "clipboard", "Clipboard history (Super+V)",
                                  IconPath("clipboard.svg"), nullptr);
    clipboardButton_ = clipboard.get();
    clipboard->onClick = [this, button = clipboard.get()]() {
        if (history_) {
            ToggleClipboardPanel(true);
            return;
        }
        const Rect2Df bounds = button->GetBoundsInWindow();
        ShowClipboardMenu(static_cast<int>(bounds.x), static_cast<int>(bounds.y + bounds.height));
    };
    clipboard->onContextMenu = [this](int windowX, int windowY) {
        if (history_) ShowClipboardButtonMenu(windowX, windowY);
    };
    UpdateClipboardButton();
    AddBarButton(organiser_, "screenshot", "Screenshot of the screen", IconPath("screenshot.svg"),
                 [this]() { TakeScreenshot(); });
    bar->AddChild(organiser_);

    bar->AddChild(CreateWaveSeparator("RightBar.Wave", true, kBarInsetColor, kBarColor, kWaveLength));

    // The info panel, anchored to the bottom by the stretch at its top.
    info_ = MakeGroup("InfoPanel", true, false);
    info_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    info_->SetOverflowMode(ToolbarOverflowMode::Scroll);
    // Fourteen services have to fit under the organiser on a 768 px screen:
    // tighter than the taskbar's items, still a 36 px target.
    ToolbarAppearance tight = info_->GetAppearance();
    tight.itemSpacing = 2.0f;
    info_->SetAppearance(tight);
    info_->SetPadding(4, 4);
    info_->AddStretch(1.0f);
    AddBarButton(info_, "mail",      "Email",        IconPath("mail.svg"),      [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraMail"); });
    AddBarButton(info_, "upload",    "Upload",       IconPath("upload.svg"),    [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    AddBarButton(info_, "download",  "Download",     IconPath("download.svg"),  [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    AddBarButton(info_, "lan",       "Internet / LAN", IconPath("lan.svg"),     [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    AddBarButton(info_, "vpn",       "VPN",          IconPath("vpn.svg"),       [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    AddBarButton(info_, "bluetooth", "Bluetooth",    IconPath("bluetooth.svg"), [this]() { OpenTasks(); });
    AddBarButton(info_, "wifi",      "Wi-Fi",        IconPath("wifi.svg"),      [this]() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    AddBarButton(info_, "usb",       "USB",          IconPath("usb.svg"),       [this]() { UltraCanvasDesktopShell::LaunchProgram("DeviceExplorer"); });
    AddBarButton(info_, "keyboard",  "Keyboard",     IconPath("keyboard.svg"),  [this]() { UltraCanvasDesktopShell::LaunchProgram("DeviceExplorer"); });
    AddBarButton(info_, "webcam",    "Webcam",       IconPath("webcam.svg"),    [this]() { UltraCanvasDesktopShell::LaunchProgram("DeviceExplorer"); });
    AddBarButton(info_, "mic",       "Microphone",   IconPath("mic.svg"),       [this]() { OpenTasks(); });
    AddBarButton(info_, "speaker",   "Loudspeaker",  IconPath("speaker.svg"),   [this]() { OpenTasks(); });
    AddBarButton(info_, "battery",   "Battery",      IconPath("battery.svg"),   [this]() { OpenTasks(); });
    AddBarButton(info_, "tasks",     "Task Manager", IconPath("tasks.svg"),     [this]() { OpenTasks(); });
    for (const auto& item : info_->GetItems()) {
        if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(item)) {
            button->SetElementSize(Size2Df(static_cast<float>(kInfoButton), static_cast<float>(kInfoButton)));
            button->SetIconSize(kInfoIcon, kInfoIcon);
        }
    }
    bar->AddChild(info_);
    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraDesktopWindow::BuildWorkArea() {
    auto area = CreateContainer("WorkArea", 0, 0, 0, 0);
    area->SetBackgroundColor(kBarInsetColor);
    area->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // The wallpaper fills the area; application windows float above the
    // desktop window itself, so nothing else lives here but the board.
    wallpaper_ = std::make_shared<UltraCanvasImageElement>("Wallpaper", 0, 0, 0, 0);
    wallpaper_->SetFitMode(ImageFitMode::Cover);
    wallpaper_->LoadFromFile(WallpaperPath());
    wallpaper_->layoutItem.SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(CSSLayout::Dimension::Px(0));
    wallpaper_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    area->AddChild(wallpaper_);

    // The Stickerboard lies over the wallpaper, out of the flow, pinned to
    // every edge of the area.
    stickerboard_ = std::make_shared<UltraDesktopStickerboard>("Stickerboard", this);
    if (!stickerboard_->layoutItem.position) stickerboard_->layoutItem.position = CSSLayout::Position();
    stickerboard_->layoutItem.position->left = CSSLayout::Dimension::Px(0);
    stickerboard_->layoutItem.position->top = CSSLayout::Dimension::Px(0);
    stickerboard_->layoutItem.position->right = CSSLayout::Dimension::Px(0);
    stickerboard_->layoutItem.position->bottom = CSSLayout::Dimension::Px(0);
    stickerboard_->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    stickerboard_->SetVisible(settings_.stickerboardVisible);
    stickerboard_->LoadFrom(settings_);
    area->AddChild(stickerboard_);
    if (organiser_) {
        if (auto toggle = std::dynamic_pointer_cast<UltraCanvasButton>(organiser_->GetWidget("stickerboard"))) {
            toggle->SetPressed(settings_.stickerboardVisible);
        }
    }
    return area;
}

// ===== WHAT THE BARS DO =====

void UltraDesktopWindow::OpenSystemSettings() {
    std::string error;
    if (UltraCanvasDesktopShell::LaunchProgram("UOS-Settings", {}, &error)) return;
    debugOutput << "UltraDesktop: UOS-Settings: " << error << std::endl;
    UltraCanvasDialogManager::ShowWarning(
            "UOS-Settings, the ULTRA OS settings application, could not be started. "
            "It holds the desktop's settings too: install it beside UltraDesktop or "
            "on the PATH.\n\n" + error,
            "ULTRA OS settings", nullptr, window_.get());
}

void UltraDesktopWindow::OpenAppStarter() {
    if (appStarter_ && appStarter_->IsOpen()) {
        appStarter_->Raise();
        return;
    }
    appStarter_ = std::make_shared<UltraDesktopAppStarter>();
    appStarter_->Open();
}

void UltraDesktopWindow::OpenTasks() {
    if (tasks_ && tasks_->IsOpen()) {
        tasks_->Raise();
        return;
    }
    tasks_ = std::make_shared<UltraDesktopTasksWindow>();
    tasks_->Open();
}

void UltraDesktopWindow::OpenRamDisc() {
    OpenFiler(settings_.ramDiscPath);
}

void UltraDesktopWindow::OpenFiler(const std::string& path) {
    std::vector<std::string> arguments;
    if (!path.empty()) arguments.push_back(path);
    std::string error;
    if (!UltraCanvasDesktopShell::LaunchProgram(settings_.filerProgram, arguments, &error)) {
        debugOutput << "UltraDesktop: " << error << std::endl;
    }
}

void UltraDesktopWindow::TakeScreenshot() {
    const std::string path = UltraCanvasDesktopShell::DefaultScreenshotPath();
    std::string error;
    auto screenshot = organiser_ ? std::dynamic_pointer_cast<UltraCanvasButton>(organiser_->GetWidget("screenshot")) : nullptr;
    if (UltraCanvasDesktopShell::CaptureScreen(path, &error)) {
        if (organiser_) organiser_->SetItemBadgeDot("screenshot", kMarkerSaved);
        if (screenshot) screenshot->SetTooltip("Saved " + path);
        screenshotBadgeTicks_ = kScreenshotTicks;
    } else {
        debugOutput << "UltraDesktop: screenshot failed: " << error << std::endl;
        if (organiser_) organiser_->SetItemBadgeDot("screenshot", kMarkerOn);
        if (screenshot) screenshot->SetTooltip("Screenshot failed: " + error);
        screenshotBadgeTicks_ = kScreenshotTicks;
    }
}

void UltraDesktopWindow::ToggleStickerboard(bool visible) {
    settings_.stickerboardVisible = visible;
    if (stickerboard_) {
        stickerboard_->SetVisible(visible);
        if (workArea_) workArea_->InvalidateLayout();
        window_->RequestRedraw();
    }
    SaveSettings();
}

int UltraDesktopWindow::DesiredDesktopCount() const {
    // What the user just chose in the settings wins for the rebuild that
    // follows it (the manager answers the request asynchronously); otherwise
    // the window manager's own count, since it is the one that has the
    // desktops; the settings value only where there is no manager to ask.
    if (requestedDesktops_ > 0) return requestedDesktops_;
    const int managed = UltraCanvasDesktopShell::GetVirtualDesktopCount();
    if (managed > 0) return managed;
    return settings_.virtualDesktops;
}

void UltraDesktopWindow::SwitchToDesktop(int index) {
    if (index < 0) return;
    // The window manager may offer fewer desktops than the organiser shows:
    // ask it for as many first, then switch.
    if (UltraCanvasDesktopShell::GetVirtualDesktopCount() <= index) {
        UltraCanvasDesktopShell::SetVirtualDesktopCount(desktopCount_);
    }
    UltraCanvasDesktopShell::SetCurrentVirtualDesktop(index);
    currentDesktop_ = index;
    RefreshDesktops();
    windowsDirty_.store(true);
}

void UltraDesktopWindow::ApplySettings() {
    SaveSettings();
    // The chosen number of desktops goes to the window manager, which owns
    // them; the organiser is rebuilt with that number now and follows the
    // manager afterwards, should it answer with another.
    if (settings_.virtualDesktops != desktopCount_) {
        UltraCanvasDesktopShell::SetVirtualDesktopCount(settings_.virtualDesktops);
        requestedDesktops_ = settings_.virtualDesktops;
    }
    BuildLayout();
    requestedDesktops_ = 0;
    RefreshDesktops();
    PlaceNotifications();   // the taskbar may have moved
}

// ===== NOTIFICATIONS =====

void UltraDesktopWindow::StartNotifications() {
#ifdef ULTRADESKTOP_HAVE_NOTIFICATIONS
    // The desktop is the first to start in an ULTRA OS session, so it usually
    // hosts the UltraMessage broker too - and with it the adapter that serves
    // org.freedesktop.Notifications for every application.
    toasts_ = std::make_unique<UltraCanvasNotificationToastHost>();
    PlaceNotifications();
    if (!toasts_->Connect()) {
        debugOutput << "UltraDesktop: notifications are not shown: " << toasts_->LastError() << std::endl;
    }
#endif
}

void UltraDesktopWindow::PlaceNotifications() {
#ifdef ULTRADESKTOP_HAVE_NOTIFICATIONS
    if (!toasts_) return;
    // Top right, beside the right bar, and below the taskbar when it runs
    // along the top.
    const TaskbarEdge edge = settings_.taskbarEdge;
    toasts_->SetCorner(NotificationToastCorner::TopRight);
    toasts_->SetScreenMargins(edge == TaskbarEdge::Left ? kBarThickness : 0,
                              edge == TaskbarEdge::Top ? kBarThickness : 0,
                              kInfoPanelWidth,
                              edge == TaskbarEdge::Bottom ? kBarThickness : 0);
#endif
}

void UltraDesktopWindow::SaveSettings() {
    if (settingsPath_.empty()) return;
    std::string error;
    if (!settings_.Save(settingsPath_, &error)) {
        debugOutput << "UltraDesktop: settings not saved: " << error << std::endl;
    }
    std::error_code ec;
    settingsFileTime_ = std::filesystem::last_write_time(PathFromUtf8(settingsPath_), ec);
}

void UltraDesktopWindow::CheckSettingsFile() {
    if (settingsPath_.empty()) return;
    // Once a second is plenty for a setting the user just changed elsewhere.
    const auto now = std::chrono::steady_clock::now();
    if (now < nextSettingsCheck_) return;
    nextSettingsCheck_ = now + std::chrono::seconds(1);

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(PathFromUtf8(settingsPath_), ec);
    if (ec || stamp == settingsFileTime_) return;
    settingsFileTime_ = stamp;

    DesktopSettings written;
    std::string error;
    if (!written.Load(settingsPath_, &error)) {
        debugOutput << "UltraDesktop: changed settings not read: " << error << std::endl;
        return;
    }
    // Only what UOS-Settings edits; the notes and the Stickerboard switch are
    // this process's and the copy in memory is the current one.
    const bool changed = written.taskbarEdge != settings_.taskbarEdge ||
                         written.wallpaper != settings_.wallpaper ||
                         written.ramDiscPath != settings_.ramDiscPath ||
                         written.filerProgram != settings_.filerProgram ||
                         written.virtualDesktops != settings_.virtualDesktops;
    if (!changed) return;
    settings_.taskbarEdge = written.taskbarEdge;
    settings_.wallpaper = written.wallpaper;
    settings_.ramDiscPath = written.ramDiscPath;
    settings_.filerProgram = written.filerProgram;
    settings_.virtualDesktops = written.virtualDesktops;
    // Written back with the notes as they are now, should a note have
    // changed between UOS-Settings reading the file and writing it.
    ApplySettings();
}

// ===== LIVE DATA =====

void UltraDesktopWindow::OnTimer() {
    CheckSettingsFile();
    if (windowsDirty_.exchange(false)) {
        RefreshDesktops();
        RefreshWindows();
    }

    bool apply = false;
    DesktopDeviceActivity now;
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (devicePending_) {
            now = pending_;
            devicePending_ = false;
            apply = true;
        }
    }
    if (apply) {
        ApplyDeviceActivity(now, lastActivity_, haveLastActivity_);
        lastActivity_ = now;
        haveLastActivity_ = true;
    }

    if (++ticksSinceNotices_ >= kNoticeTicks) {
        ticksSinceNotices_ = 0;
        ApplyNotices();
    }

    if (screenshotBadgeTicks_ > 0 && --screenshotBadgeTicks_ == 0 && organiser_) {
        organiser_->ClearItemBadge("screenshot");
    }

    CheckClipboardHistory();
}

void UltraDesktopWindow::RefreshDesktops() {
    // The window manager added or removed a desktop (a keyboard shortcut,
    // another pager, its own settings): the organiser follows it.
    const int managed = UltraCanvasDesktopShell::GetVirtualDesktopCount();
    if (managed > 0 && std::clamp(managed, 1, 9) != desktopCount_ && organiser_) {
        BuildLayout();
    }
    const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
    if (current >= 0) currentDesktop_ = current;
    if (!organiser_) return;
    for (int i = 0; i < desktopCount_; ++i) {
        auto button = std::dynamic_pointer_cast<UltraCanvasButton>(
                organiser_->GetWidget("desktop" + std::to_string(i + 1)));
        if (button) button->SetPressed(i == currentDesktop_);
    }
}

std::string UltraDesktopWindow::RunningItemId(uint64_t windowId) const {
    return kRunningPrefix + std::to_string(windowId);
}

void UltraDesktopWindow::RefreshWindows() {
    if (!running_) return;
    const std::vector<DesktopWindowInfo> windows = UltraCanvasDesktopShell::ListWindows();

    std::map<uint64_t, const DesktopWindowInfo*> wanted;
    for (const DesktopWindowInfo& w : windows) {
        // Where a paste from the clipboard panel goes when it is opened from
        // the bar: clicking the bar does not change it.
        if (w.active && w.appClass != "UltraDesktop") lastPasteWindow_ = w.id;
        if (w.skipTaskbar) continue;
        if (w.appClass == "UltraDesktop") continue;   // ourselves, whatever the manager says
        if (currentDesktop_ >= 0 && w.virtualDesktop >= 0 && w.virtualDesktop != currentDesktop_) continue;
        wanted[w.id] = &w;
    }

    // Gone windows leave the bar.
    for (auto it = runningItems_.begin(); it != runningItems_.end();) {
        if (wanted.count(it->first) == 0) {
            running_->RemoveItem(it->second);
            runningTitles_.erase(it->first);
            it = runningItems_.erase(it);
        } else {
            ++it;
        }
    }

    // New windows join it, next to their application's other windows when
    // it has any, at the end otherwise. Existing ones only update.
    for (const DesktopWindowInfo& w : windows) {
        auto found = wanted.find(w.id);
        if (found == wanted.end()) continue;
        const std::string itemId = RunningItemId(w.id);
        auto existing = runningItems_.find(w.id);
        std::shared_ptr<UltraCanvasButton> button;
        if (existing == runningItems_.end()) {
            std::string text;
            if (w.iconFile.empty()) {
                const std::string& source = !w.appClass.empty() ? w.appClass : w.title;
                if (!source.empty()) text = source.substr(0, 1);
                std::transform(text.begin(), text.end(), text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
            const uint64_t id = w.id;
            button = AddBarToggle(running_, itemId, w.title, w.iconFile, text, [this, id](bool on) {
                if (on) UltraCanvasDesktopShell::ActivateWindow(id);
                else UltraCanvasDesktopShell::MinimizeWindow(id);
            });
            button->onContextMenu = [this, id](int x, int y) { ShowWindowMenu(id, x, y); };
            runningItems_[w.id] = itemId;

            // Group with the same application's windows.
            int place = -1;
            const auto order = running_->GetItemOrder();
            for (size_t i = 0; i < order.size(); ++i) {
                for (const auto& [otherId, otherItem] : runningItems_) {
                    if (otherItem != order[i] || otherId == w.id) continue;
                    const auto other = wanted.find(otherId);
                    if (other != wanted.end() && other->second->appClass == w.appClass) place = static_cast<int>(i);
                }
            }
            if (place >= 0) {
                const int from = running_->GetItemIndex(itemId);
                if (from > place + 1) running_->MoveItem(from, place + 1);
            }
        } else {
            button = std::dynamic_pointer_cast<UltraCanvasButton>(running_->GetWidget(itemId));
        }
        if (!button) continue;
        if (runningTitles_[w.id] != w.title) {
            runningTitles_[w.id] = w.title;
            button->SetTooltip(w.title.empty() ? w.appClass : w.title);
        }
        button->SetPressed(w.active && !w.minimized);
    }
    running_->InvalidateLayout();
    running_->RequestRedraw();
}

void UltraDesktopWindow::ApplyDeviceActivity(const DesktopDeviceActivity& now,
                                             const DesktopDeviceActivity& before, bool haveBefore) {
    if (!info_) return;
    const auto marker = [this](const char* id, bool on, const Color& color) {
        if (on) info_->SetItemBadgeDot(id, color);
        else info_->ClearItemBadge(id);
    };
    const auto tooltip = [this](const char* id, const std::string& text) {
        if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget(id))) {
            button->SetTooltip(text);
        }
    };

    // Red: a device is on.
    marker("webcam", now.webcamInUse, kMarkerOn);
    tooltip("webcam", now.webcamInUse ? "Webcam: in use" : "Webcam");
    marker("mic", now.microphoneInUse, kMarkerOn);
    tooltip("mic", now.microphoneInUse ? "Microphone: recording" : "Microphone");
    marker("bluetooth", now.bluetoothPowered, kMarkerOn);
    tooltip("bluetooth", !now.bluetoothPresent ? "Bluetooth: no adapter"
            : now.bluetoothPowered ? "Bluetooth: on, " + std::to_string(now.bluetoothConnectedDevices) + " connected"
                                   : "Bluetooth: off");

    // Yellow: activity.
    marker("speaker", now.speakerPlaying, kMarkerActive);
    tooltip("speaker", now.speakerPlaying ? "Loudspeaker: playing" : "Loudspeaker");
    const bool downloading = haveBefore && now.bytesReceived > before.bytesReceived + kActivityBytesPerPoll;
    const bool uploading = haveBefore && now.bytesSent > before.bytesSent + kActivityBytesPerPoll;
    marker("download", downloading, kMarkerActive);
    marker("upload", uploading, kMarkerActive);
    if (haveBefore) {
        const uint64_t down = (now.bytesReceived - std::min(now.bytesReceived, before.bytesReceived)) / kDevicePollSeconds;
        const uint64_t up = (now.bytesSent - std::min(now.bytesSent, before.bytesSent)) / kDevicePollSeconds;
        tooltip("download", "Download: " + FormatFileSize(static_cast<size_t>(down)) + "/s");
        tooltip("upload", "Upload: " + FormatFileSize(static_cast<size_t>(up)) + "/s");
    }

    // Links: a red marker means the link is down, none means fine.
    marker("lan", !now.lanConnected && !now.wifiConnected && !now.internetInterfaceUp, kMarkerOn);
    tooltip("lan", now.lanConnected ? "LAN: connected" : now.internetInterfaceUp ? "Internet: connected" : "Internet / LAN: no link");
    marker("wifi", now.wifiPresent && !now.wifiConnected, kMarkerOn);
    tooltip("wifi", !now.wifiPresent ? "Wi-Fi: no adapter"
            : now.wifiConnected ? ("Wi-Fi: " + (now.wifiSsid.empty() ? std::string("connected") : now.wifiSsid))
                                : "Wi-Fi: not connected");
    marker("vpn", now.vpnConnected, kMarkerActive);
    tooltip("vpn", now.vpnConnected ? "VPN: connected" : "VPN: off");

    // Counts.
    if (now.usbDeviceCount > 0) info_->SetItemBadgeCount("usb", now.usbDeviceCount, kMarkerCount);
    else info_->ClearItemBadge("usb");
    tooltip("usb", "USB: " + std::to_string(now.usbDeviceCount) + " device" + (now.usbDeviceCount == 1 ? "" : "s"));

    if (now.batteryPresent && now.batteryPercent >= 0) {
        const Color color = now.batteryCharging ? kMarkerSaved : now.batteryPercent <= 15 ? kMarkerOn : kMarkerCount;
        info_->SetItemBadge("battery", std::to_string(now.batteryPercent), color);
        tooltip("battery", "Battery: " + std::to_string(now.batteryPercent) + "%" +
                           (now.batteryCharging ? ", charging" : ""));
    } else {
        info_->ClearItemBadge("battery");
        tooltip("battery", "Battery: none (mains power)");
    }

    if (!now.keyboardLayout.empty()) {
        std::string layout = now.keyboardLayout;
        std::transform(layout.begin(), layout.end(), layout.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        info_->SetItemBadge("keyboard", Shorten(layout, 3), kMarkerCount);
        tooltip("keyboard", "Keyboard layout: " + layout);
    }
}

void UltraDesktopWindow::ApplyNotices() {
    if (!info_) return;
    const DesktopNotice mail = UltraCanvasDesktopShell::ReadNotice("UltraMail");
    if (mail.updatedUnixSeconds > 0 && mail.count > 0) {
        info_->SetItemBadgeCount("mail", mail.count, kMarkerCount);
    } else {
        info_->ClearItemBadge("mail");
    }
    if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget("mail"))) {
        if (mail.updatedUnixSeconds == 0) button->SetTooltip("Email");
        else if (!mail.text.empty()) button->SetTooltip("Email: " + mail.text);
        else button->SetTooltip("Email: " + std::to_string(mail.count) + " unread");
    }
}

void UltraDesktopWindow::StartDevicePoll() {
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (devicePollRunning_) return;
        devicePollRunning_ = true;
    }
    devicePoll_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(deviceMutex_);
        while (devicePollRunning_) {
            lock.unlock();
            DesktopDeviceActivity reading = UltraCanvasDesktopShell::ReadDeviceActivity();
            lock.lock();
            if (!devicePollRunning_) break;
            pending_ = std::move(reading);
            devicePending_ = true;
            deviceWake_.wait_for(lock, std::chrono::seconds(kDevicePollSeconds),
                                 [this]() { return !devicePollRunning_; });
        }
    });
}

void UltraDesktopWindow::StopDevicePoll() {
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (!devicePollRunning_) return;
        devicePollRunning_ = false;
    }
    deviceWake_.notify_all();
    if (devicePoll_.joinable()) devicePoll_.join();
}

// ===== MENUS =====

void UltraDesktopWindow::ShowWindowMenu(uint64_t windowId, int windowX, int windowY) {
    if (!window_) return;
    popupMenu_ = std::make_shared<UltraCanvasMenu>("WindowMenu", 0, 0, 220, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    auto title = runningTitles_.find(windowId);
    if (title != runningTitles_.end() && !title->second.empty()) {
        popupMenu_->AddItem(MenuItemData::Header(Shorten(title->second, 40)));
    }
    popupMenu_->AddItem(MenuItemData::Action("Activate", [windowId]() { UltraCanvasDesktopShell::ActivateWindow(windowId); }));
    popupMenu_->AddItem(MenuItemData::Action("Minimize", [windowId]() { UltraCanvasDesktopShell::MinimizeWindow(windowId); }));
    if (desktopCount_ > 1) {
        std::vector<MenuItemData> desktops;
        for (int i = 0; i < desktopCount_; ++i) {
            desktops.push_back(MenuItemData::Action("Desktop " + std::to_string(i + 1), [this, windowId, i]() {
                UltraCanvasDesktopShell::MoveWindowToVirtualDesktop(windowId, i);
                windowsDirty_.store(true);
            }));
        }
        desktops.push_back(MenuItemData::Action("Every desktop", [this, windowId]() {
            UltraCanvasDesktopShell::MoveWindowToVirtualDesktop(windowId, -1);
            windowsDirty_.store(true);
        }));
        popupMenu_->AddItem(MenuItemData::Submenu("Move to", desktops));
    }
    popupMenu_->AddItem(MenuItemData::Separator());
    popupMenu_->AddItem(MenuItemData::Action("Close window", [windowId]() { UltraCanvasDesktopShell::CloseWindow(windowId); }));
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

void UltraDesktopWindow::ShowClipboardMenu(int windowX, int windowY) {
    if (!window_) return;
    auto* clipboard = GetClipboard();
    popupMenu_ = std::make_shared<UltraCanvasMenu>("ClipboardMenu", 0, 0, 320, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    popupMenu_->AddItem(MenuItemData::Header("Clipboard history"));
    size_t shown = 0;
    if (clipboard) {
        const auto& entries = clipboard->GetEntries();
        // The store keeps the newest first (AddEntry inserts at the front),
        // which is the order a menu reads best in.
        for (auto it = entries.begin(); it != entries.end() && shown < 15; ++it) {
            const ClipboardData entry = *it;
            std::string label = entry.type == ClipboardDataType::Text
                    ? entry.preview : (entry.GetTypeString() + ": " + entry.preview);
            if (label.empty()) label = entry.GetTypeString();
            popupMenu_->AddItem(MenuItemData::Action(Shorten(label, 48), [entry]() {
                if (auto* cb = GetClipboard()) {
                    if (entry.type == ClipboardDataType::Text) cb->SetText(entry.content);
                    else if (entry.type == ClipboardDataType::FilePath) cb->SetFiles({entry.content});
                    else if (!entry.rawData.empty()) cb->SetImage(entry.rawData, entry.mimeType);
                }
            }));
            ++shown;
        }
    }
    if (shown == 0) {
        MenuItemData empty = MenuItemData::Action("Nothing copied yet", []() {});
        empty.enabled = false;
        popupMenu_->AddItem(empty);
    } else {
        popupMenu_->AddItem(MenuItemData::Separator());
        popupMenu_->AddItem(MenuItemData::Action("Clear history", []() {
            if (auto* cb = GetClipboard()) cb->ClearHistory();
        }));
    }
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

// ===== CLIPBOARD HISTORY =====

namespace {

// The program a copy came from: the window that has the focus when the copy
// is noticed - the one the person copied in. The desktop's own panel is not
// a source.
std::string ActiveApplicationName() {
    const uint64_t active = UltraCanvasDesktopShell::GetActiveWindow();
    if (active == 0) return "";
    for (const auto& window : UltraCanvasDesktopShell::ListWindows()) {
        if (window.id != active) continue;
        std::string name = !window.appName.empty() ? window.appName : window.appClass;
        if (name == "UltraDesktop" || window.title == "Clipboard") return "";
        return name;
    }
    return "";
}

// What a paste into `window` takes, from its application's desktop entry.
UltraDesktopClipboardPanel::Target PasteTargetFor(const DesktopWindowInfo& window,
                                                  const std::vector<UCDesktopEntry>& applications) {
    UltraDesktopClipboardPanel::Target target;
    const UCDesktopEntry* entry = UltraCanvasDesktopShell::MatchApplication(window, applications);
    if (!entry) return target;
    target.kinds = PreferredClipboardKinds(entry->categories, entry->mimeTypes);
    // "GNU Image Manipulation Program" does not fit a section header.
    target.name = entry->name.size() <= 18 || window.appClass.empty() ? entry->name : window.appClass;
    return target;
}

} // namespace

void UltraDesktopWindow::StartClipboardHistory() {
    history_ = std::make_unique<UltraCanvasClipboardHistory>();
    if (!history_->Open()) {
        debugOutput << "UltraDesktop: no clipboard history (" << history_->GetLastError()
                    << "); the clipboard button shows this session's copies" << std::endl;
        history_.reset();
    }
    if (!history_) {
        if (auto* clipboard = GetClipboard()) clipboard->StartMonitoring();
        return;
    }
    historyGeneration_ = history_->GetGeneration();
    clipboardPaused_ = history_->GetPolicy().recordingPaused;
    recorder_ = std::make_unique<UltraCanvasClipboardRecorder>();
    recorder_->sourceProvider = []() { return ActiveApplicationName(); };
    recorder_->onRecorded = [this](int64_t, ClipboardRecordResult) {
        if (clipboardPanel_) clipboardPanel_->Refresh();
    };
    if (auto* clipboard = GetClipboard()) recorder_->Attach(history_.get(), clipboard, "desktop", 10);

    UltraDesktopClipboardPanel::Actions actions;
    actions.copy = [this](int64_t id) { CopyHistoryEntry(id); };
    actions.edit = [this](int64_t id) { OpenClipboardApplication({"--edit", std::to_string(id)}); };
    actions.openApplication = [this](const std::string& text) {
        if (text.empty()) OpenClipboardApplication({});
        else OpenClipboardApplication({"--search", text});
    };
    clipboardPanel_ = std::make_shared<UltraDesktopClipboardPanel>(history_.get(), std::move(actions));

    // Super+V from any window. The shortcut's thread hands the press to the UI thread.
    std::string error;
    const bool started = clipboardShortcut_.Start("Super+V", [this]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([this]() { ToggleClipboardPanel(false); });
        }
    }, &error);
    if (!started) debugOutput << "UltraDesktop: Super+V does not open the clipboard: " << error << std::endl;
}

void UltraDesktopWindow::CheckClipboardHistory() {
    if (!history_) return;
    if (recorder_) recorder_->Tick();
    // Changes from another process (UltraClipboard): once a second.
    if (++ticksSinceHistoryCheck_ >= 4) {
        ticksSinceHistoryCheck_ = 0;
        const uint64_t generation = history_->GetGeneration();
        if (generation != historyGeneration_) {
            historyGeneration_ = generation;
            const bool paused = history_->GetPolicy().recordingPaused;
            if (paused != clipboardPaused_) {
                clipboardPaused_ = paused;
                UpdateClipboardButton();
            }
            if (clipboardPanel_) clipboardPanel_->Refresh();
        }
    }
    // Removals past their undo window are finished once a minute.
    if (++ticksSincePrune_ >= 240) {
        ticksSincePrune_ = 0;
        history_->Prune();
    }
}

void UltraDesktopWindow::ToggleClipboardPanel(bool besideButton) {
    if (!clipboardPanel_ || !window_) return;
    if (clipboardPanel_->IsOpen()) {
        clipboardPanel_->Close();
        return;
    }
    // The window the paste goes to: the focused one for Super+V, the last
    // focused one of another program when the bar was clicked.
    UltraDesktopClipboardPanel::Target target;
    const uint64_t active = UltraCanvasDesktopShell::GetActiveWindow();
    const DesktopWindowInfo* pasteWindow = nullptr;
    const std::vector<DesktopWindowInfo> windows = UltraCanvasDesktopShell::ListWindows();
    for (const DesktopWindowInfo& w : windows) {
        if (w.id == active && w.appClass != "UltraDesktop") pasteWindow = &w;
    }
    for (const DesktopWindowInfo& w : windows) {
        if (!pasteWindow && w.id == lastPasteWindow_) pasteWindow = &w;
    }
    if (pasteWindow) {
        lastPasteWindow_ = pasteWindow->id;
        const auto now = std::chrono::steady_clock::now();
        if (applications_.empty() || now - applicationsRead_ > std::chrono::minutes(5)) {
            applications_ = UltraCanvasDesktopShell::ListApplications(16);
            applicationsRead_ = now;
        }
        target = PasteTargetFor(*pasteWindow, applications_);
    }
    if (besideButton && clipboardButton_) {
        // The desktop window covers the screen from its top left corner, so
        // its coordinates are the screen's, scaled to physical pixels.
        const Rect2Df bounds = clipboardButton_->GetBoundsInWindow();
        clipboardPanel_->Open(window_->LogicalToPhysical(static_cast<int>(bounds.x) - 10),
                              window_->LogicalToPhysical(static_cast<int>(bounds.y) - 8), std::move(target));
    } else {
        clipboardPanel_->Open(-1, -1, std::move(target));
    }
}

void UltraDesktopWindow::CopyHistoryEntry(int64_t entryId) {
    auto* clipboard = GetClipboard();
    if (!history_ || !clipboard) return;
    std::vector<ClipboardFormat> formats;
    if (!history_->ReadFormats(entryId, formats) || !RestoreToClipboard(*clipboard, formats)) {
        UltraCanvasAlert::Error("This copy can no longer be read from the clipboard history.", "Clipboard",
                                nullptr, window_.get());
        return;
    }
    history_->MarkUsed(entryId);
}

void UltraDesktopWindow::OpenClipboardApplication(const std::vector<std::string>& arguments) {
    std::string error;
    if (UltraCanvasDesktopShell::LaunchProgram("UltraClipboard", arguments, &error)) return;
    UltraCanvasAlert::Error("UltraClipboard could not be started: " + error, "Clipboard", nullptr, window_.get());
}

void UltraDesktopWindow::SetClipboardRecording(bool on) {
    if (!history_) return;
    ClipboardHistoryPolicy policy = history_->GetPolicy();
    policy.recordingPaused = !on;
    history_->SetPolicy(policy);
    clipboardPaused_ = !on;
    historyGeneration_ = history_->GetGeneration();
    UpdateClipboardButton();
    if (clipboardPanel_) clipboardPanel_->Refresh();
}

// Crossed out while recording is paused, so the bar says it.
void UltraDesktopWindow::UpdateClipboardButton() {
    if (!clipboardButton_) return;
    clipboardButton_->SetIcon(IconPath(clipboardPaused_ ? "clipboard-paused.svg" : "clipboard.svg"));
    clipboardButton_->SetTooltip(clipboardPaused_ ? "Clipboard history - recording is paused (Super+V)"
                                                  : "Clipboard history (Super+V)");
}

void UltraDesktopWindow::ShowClipboardButtonMenu(int windowX, int windowY) {
    if (!window_ || !history_) return;
    popupMenu_ = std::make_shared<UltraCanvasMenu>("ClipboardButtonMenu", 0, 0, 240, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    popupMenu_->AddItem(MenuItemData::Header("Clipboard history"));
    popupMenu_->AddItem(MenuItemData::Action(clipboardPaused_ ? "Resume recording" : "Pause recording",
                                             [this]() { SetClipboardRecording(clipboardPaused_); }));
    popupMenu_->AddItem(MenuItemData::Action("Open UltraClipboard", [this]() { OpenClipboardApplication({}); }));
    popupMenu_->AddItem(MenuItemData::Separator());
    popupMenu_->AddItem(MenuItemData::Action("Clear history\xE2\x80\xA6", [this]() {
        UltraCanvasAlert::Confirm("Remove every entry from the clipboard history? Pinned entries stay.",
                                  "Clear clipboard history", [this](bool yes) {
                                      if (!yes || !history_) return;
                                      history_->Clear(false);
                                      if (clipboardPanel_) clipboardPanel_->Refresh();
                                  }, window_.get());
    }));
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX - 250, windowY), *window_, settings);
}

} // namespace UltraDesktop
