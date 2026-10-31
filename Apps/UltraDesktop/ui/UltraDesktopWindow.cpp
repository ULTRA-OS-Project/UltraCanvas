// Apps/UltraDesktop/ui/UltraDesktopWindow.cpp
// The desktop window: bars, wallpaper, and the live data behind the items.
// See the header for the shape; the comments here say why each piece is
// wired the way it is.
// Version: 0.5.1 - the desktop on the primary monitor (area_), the others
//                  covered on X11 too; panels and toasts placed from it; a
//                  primary that moves is followed (FollowPrimaryScreen)
// Version: 0.5.0 - the clock (BuildClock, UpdateClock, the clock panel);
//                  pinned applications (PinKey, ShowPinnedMenu); the volume
//                  panel and the speaker's words (ReadSpeaker); the lock
//                  (LockScreen); sign out, restart and shut down wherever
//                  the platform can; the Super tap and Super+L on Linux
//                  (StartDesktopShortcuts); tray icons' menus (ShowTrayMenu)
// Version: 0.4.1 - from an MSIX package: the desktop switch says why it is not
//                  there, and "Close UltraDesktop" stays
// Version: 0.4.0 - Windows: instead of Explorer - the tray icons (StartTray,
//                  RefreshTray), sign out / restart / shut down, the Win key
//                  (appsShortcut_); the other screens' wallpaper
//                  (CoverOtherScreens, a closed one opened again)
// Version: 0.3.0 - Windows: in place of the Windows desktop and back (the
//                  settings button's menu, --session), no RAM disc button
//                  without a RAM disc
// Version: 0.2.0 - the notification toasts (StartNotifications, PlaceNotifications)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopWindow.h"
#include "UltraDesktopAppStarter.h"
#include "UltraDesktopCategoryBoard.h"
#include "UltraDesktopClipboardPanel.h"
#include "UltraDesktopClockPanel.h"
#include "UltraDesktopStickerboard.h"
#include "UltraDesktopTasksWindow.h"
#include "UltraDesktopVolumePanel.h"
#include "UltraDesktopWifiPanel.h"
#include "UltraDesktopBluetoothPanel.h"
#include "UltraDesktopDevicePanel.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasHardwareInfo.h"
#include "UltraCanvasVideoDevices.h"
#include "UltraCanvasWireless.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasToolbar.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWaveSeparator.h"
#include "UltraCanvasWindow.h"
#ifdef ULTRADESKTOP_HAVE_NOTIFICATIONS
#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"
#include "UltraMessage/UltraMessage.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <set>
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
// Below this height the info panel's items are smaller, and what still does
// not fit folds into the chevron's menu at the bar's foot - least used first.
constexpr int   kCompactScreenHeight = 960;
constexpr int   kInfoButtonCompact = 30;
constexpr int   kInfoIconCompact   = 18;
// The services that fold first, least used first. Webcam and microphone
// come late: their red dot says a device is on, and that must stay seen.
const char* const kInfoFoldOrder[] = {"keyboard", "vpn", "usb", "upload", "tasks", "download", "lan",
                                      "mail", "battery", "bluetooth", "webcam", "mic", "speaker", "wifi"};
constexpr int   kClockHeight    = 68;   // the face (48) over the date line
constexpr int   kClockFaceBox   = 66;   // the gauge's box: its face lands at the top, 48 px across
constexpr int   kPollsPerSpeakerRead = 5;   // the loudspeaker every 10 s
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
const Color kClockFace     (236, 236, 240, 255); // the gauge draws dark hands: a light face

const char* const kRunningPrefix = "win:";
const char* const kTrayPrefix = "tray:";

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
    trayHost_.Stop();
    appsShortcut_.Stop();
    lockShortcut_.Stop();
    clipboardShortcut_.Stop();
    clipboardPanel_.reset();
    clockPanel_.reset();
    volumePanel_.reset();
    wifiPanel_.reset();
    bluetoothPanel_.reset();
    devicePanels_.clear();
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

    // Several monitors: the primary carries the bars, and the others get
    // wallpaper windows (CoverOtherScreens). One window across the whole
    // X11 root put the bars on the far edges of the two monitors, and into
    // the dead area where the shorter one ended. One monitor, or a platform
    // that tells none apart: the screen as it is.
    bool oneOfSeveral = false;
    area_ = DesktopAreaNow(oneOfSeveral);
    window_ = CreateDesktopWindow(area_, oneOfSeveral);
    if (!window_) return false;

    BuildLayout();
    CoverOtherScreens();
    StartNotifications();

    // The clipboard history the button shows: recorded here, kept on disk.
    StartClipboardHistory();

    // Window and desktop changes arrive on the monitor's thread as a flag;
    // the device poll on its own thread as a pending reading. The UI timer
    // applies both.
    shellMonitor_.Start([this]() { windowsDirty_.store(true); });
    if (shellSession_) StartTray();
    StartDesktopShortcuts();
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
    // The tray icons live on the info panel, which is built anew.
    trayIcons_.clear();
    trayTooltips_.clear();
    trayMenus_.clear();
    if (trayHost_.IsRunning()) trayDirty_.store(true);
    running_.reset();
    organiser_.reset();
    info_.reset();
    clock_.reset();
    clockDate_.reset();
    clockMinute_ = -1;
    windowsDirty_.store(true);
    haveLastActivity_ = false;

    // The pins, against what is installed now.
    pinnedEntries_.clear();
    for (const std::string& key : settings_.pinnedApps) {
        for (const UCDesktopEntry& entry : Applications()) {
            if (PinKey(entry) == key) {
                pinnedEntries_.push_back(entry);
                break;
            }
        }
    }

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
    auto settingsButton = AddBarButton(system, "settings", "ULTRA OS settings", IconPath("ultraos.svg"),
                                       [this]() { OpenSystemSettings(); });
    settingsButton->onContextMenu = [this](int x, int y) { ShowSettingsButtonMenu(x, y); };
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

    // 3. Pinned: the RAM disc (when there is one - Windows has none of its
    // own) and UltraFiler.
    auto pinned = MakeGroup("Taskbar.Pinned", vertical, false);
    if (!settings_.ramDiscPath.empty()) {
        AddBarButton(pinned, "ramdisc", "RAM disc (" + settings_.ramDiscPath + ")", IconPath("ramdisc.svg"),
                     [this]() { OpenRamDisc(); });
    }
    AddBarButton(pinned, "filer", "UltraFiler", IconPath("ultrafiler.svg"),
                 [this]() { OpenFiler(); });
    // The user's own pins, in the order they were pinned, with the
    // applications' icons in their own colours.
    for (const UCDesktopEntry& entry : pinnedEntries_) {
        std::string text;
        if (entry.iconFile.empty() && !entry.name.empty()) {
            text = entry.name.substr(0, 1);
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        }
        const UCDesktopEntry launched = entry;
        auto button = pinned->AddButton("pin:" + PinKey(entry), text, entry.iconFile, [this, launched]() {
            std::string error;
            if (!UltraCanvasDesktopShell::LaunchApplication(launched, {}, &error)) {
                debugOutput << "UltraDesktop: could not start " << launched.name << ": " << error << std::endl;
                UltraCanvasAlert::Error("Could not start " + launched.name + ": " + error, "UltraDesktop", nullptr,
                                        window_.get());
            }
        });
        StyleBarButton(button);
        ButtonStyle style = button->GetStyle();
        style.useIconAsMask = entry.iconFile.empty();
        button->SetStyle(style);
        button->SetTooltip(entry.name);
        button->onContextMenu = [this, launched](int x, int y) { ShowPinnedMenu(launched, x, y); };
    }
    bar->AddChild(pinned);
    return bar;
}

// ===== THE CLOCK =====

std::shared_ptr<UltraCanvasContainer> UltraDesktopWindow::BuildClock() {
    auto clock = CreateContainer("Clock", 0, 0, 0, 0);
    clock->SetBackgroundColor(kBarColor);
    clock->SetElementSize(CSSLayout::Dimension::Px(kInfoPanelWidth), CSSLayout::Dimension::Px(kClockHeight));
    clock->layoutItem.SetFlexShrink(0.0f);
    clock->SetTooltip(TodayInWords());

    // The gauge element's analog clock, which ticks on a timer of its own.
    // Its face sits at the top of its box, the value zone below it empty:
    // the date goes there. A click opens the clock panel.
    clock_ = CreateGaugeDiagramElement("ClockFace", 0, 0, static_cast<float>(kInfoPanelWidth),
                                       static_cast<float>(kClockFaceBox));
    clock_->SetMode(GaugeMode::AnalogClock);
    clock_->SetShowLabels(false);
    clock_->SetBackgroundColor(kClockFace);
    if (!clock_->layoutItem.position) clock_->layoutItem.position = CSSLayout::Position();
    clock_->layoutItem.position->left = CSSLayout::Dimension::Px(0);
    clock_->layoutItem.position->top = CSSLayout::Dimension::Px(0);
    clock_->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    clock_->SetElementSize(Size2Df(static_cast<float>(kInfoPanelWidth), static_cast<float>(kClockFaceBox)));
    const auto openPanel = [this](const UCEvent& event) {
        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            OpenClockPanel();
            return true;
        }
        return false;
    };
    clock_->SetEventCallback(openPanel);
    clock->AddChild(clock_);

    clockDate_ = CreateLabel("ClockDate", 0, 0, static_cast<float>(kInfoPanelWidth), 16, TodayShort());
    clockDate_->SetFontSize(9.0f);
    clockDate_->SetTextColor(Colors::White);
    clockDate_->SetAlignment(TextAlignment::Center);
    if (!clockDate_->layoutItem.position) clockDate_->layoutItem.position = CSSLayout::Position();
    clockDate_->layoutItem.position->left = CSSLayout::Dimension::Px(0);
    clockDate_->layoutItem.position->top = CSSLayout::Dimension::Px(50);
    clockDate_->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    clockDate_->SetElementSize(Size2Df(static_cast<float>(kInfoPanelWidth), 16));
    clockDate_->SetEventCallback(openPanel);
    clock->AddChild(clockDate_);
    UpdateClock();
    return clock;
}

// The words change once a minute; the face redraws itself every second.
void UltraDesktopWindow::UpdateClock() {
    if (!clock_ || !clockDate_) return;
    const std::string time = TimeNow();
    const int minute = std::stoi(time.substr(0, 2)) * 60 + std::stoi(time.substr(3, 2));
    if (minute == clockMinute_) return;
    clockMinute_ = minute;
    const std::string words = TodayInWords() + ", " + time;
    clock_->SetTooltip(words);
    clockDate_->SetTooltip(words);
    clockDate_->SetText(TodayShort());
}

void UltraDesktopWindow::OpenClockPanel() {
    if (!window_) return;
    if (!clockPanel_) clockPanel_ = std::make_shared<UltraDesktopClockPanel>();
    if (clockPanel_->IsOpen()) {
        clockPanel_->Close();
        return;
    }
    // Beside the right bar, level with the clock.
    const Rect2Df bounds = clock_ ? clock_->GetBoundsInWindow() : Rect2Df(0, 0, 0, 0);
    clockPanel_->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    clockPanel_->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y));
}

// ===== THE LOUDSPEAKER =====

void UltraDesktopWindow::OpenVolumePanel() {
    if (!window_ || !info_) return;
    if (!volumePanel_) {
        volumePanel_ = std::make_shared<UltraDesktopVolumePanel>([this]() { ReadSpeaker(); });
    }
    if (volumePanel_->IsOpen()) {
        volumePanel_->Close();
        return;
    }
    auto speaker = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget("speaker"));
    const Rect2Df bounds = speaker ? speaker->GetBoundsInWindow() : Rect2Df(0, 0, 0, 0);
    volumePanel_->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    volumePanel_->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y - 60));
}

// ===== THE RADIOS AND THE DEVICES =====

namespace {

std::string Lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string JoinParts(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (!out.empty()) out += " · ";
        out += part;
    }
    return out;
}

std::string UsbTitle(const USBDeviceInfo& device) {
    if (!device.productName.empty()) return device.productName;
    char ids[24];
    std::snprintf(ids, sizeof(ids), "%04x:%04x", device.vendorId, device.productId);
    return "USB device " + std::string(ids);
}

DevicePanelContent ReadUsbPanel() {
    DevicePanelContent content;
    for (const USBDeviceInfo& device : UltraCanvasHardwareInfo::ListUSBDevices()) {
        if (device.isHub) continue;
        DevicePanelEntry entry;
        entry.title = UsbTitle(device);
        entry.detail = JoinParts({device.vendorName, device.deviceClass, device.speed});
        content.entries.push_back(entry);
    }
    const size_t count = content.entries.size();
    content.summary = count == 0 ? "" : std::to_string(count) + (count == 1 ? " device attached" : " devices attached");
    content.empty = "No USB devices are attached (hubs are not counted).";
    return content;
}

DevicePanelContent ReadKeyboardPanel(const std::string& layout) {
    DevicePanelContent content;
    content.summary = layout.empty() ? "The keyboard layout in use is not known."
                                     : "Keyboard layout in use: " + layout;
    for (const USBDeviceInfo& device : UltraCanvasHardwareInfo::ListUSBDevices()) {
        if (device.isHub) continue;
        const std::string words = Lowered(device.productName + " " + device.deviceClass);
        if (words.find("keyboard") == std::string::npos && words.find("keyb") == std::string::npos) continue;
        DevicePanelEntry entry;
        entry.title = UsbTitle(device);
        entry.detail = JoinParts({"USB", device.vendorName});
        content.entries.push_back(entry);
    }
    for (const WirelessBluetoothDevice& device : UltraCanvasWireless::ReadBluetooth().devices) {
        if (device.kind != "Keyboard" || !device.paired) continue;
        DevicePanelEntry entry;
        entry.title = device.name;
        entry.detail = JoinParts({"Bluetooth", device.connected ? "connected" : "not connected",
                                  device.batteryPercent ? "battery " + std::to_string(*device.batteryPercent) + "%"
                                                        : ""});
        entry.highlighted = device.connected;
        content.entries.push_back(entry);
    }
    content.empty = "No separate keyboard was recognised. A laptop's built-in keyboard, and a wireless "
                    "receiver that does not name itself a keyboard, are not listed.";
    return content;
}

DevicePanelContent ReadWebcamPanel(bool inUse) {
    DevicePanelContent content;
    content.summary = inUse ? "A program is using a camera now." : "No program is using a camera.";
    for (const VideoDeviceInfo& camera : UltraCanvasVideoDevices::ListCameras()) {
        DevicePanelEntry entry;
        entry.title = camera.name.empty() ? camera.id : camera.name;
        int best = 0, bestW = 0, bestH = 0;
        for (const CameraMode& mode : camera.supportedModes) {
            if (mode.width * mode.height > best) {
                best = mode.width * mode.height;
                bestW = mode.width;
                bestH = mode.height;
            }
        }
        entry.detail = JoinParts({camera.isDefault ? "default" : "",
                                  best > 0 ? "up to " + std::to_string(bestW) + " x " + std::to_string(bestH) : ""});
        entry.highlighted = camera.isDefault;
        content.entries.push_back(entry);
    }
    // Without a video backend in this build the cameras still show as USB
    // video devices.
    if (content.entries.empty()) {
        for (const USBDeviceInfo& device : UltraCanvasHardwareInfo::ListUSBDevices()) {
            const std::string words = Lowered(device.productName + " " + device.deviceClass);
            if (words.find("video") == std::string::npos && words.find("camera") == std::string::npos &&
                words.find("webcam") == std::string::npos) {
                continue;
            }
            DevicePanelEntry entry;
            entry.title = UsbTitle(device);
            entry.detail = JoinParts({"USB", device.vendorName});
            content.entries.push_back(entry);
        }
    }
    content.empty = UltraCanvasVideoDevices::IsAvailable()
                        ? "No camera was found."
                        : "No camera was found. (This build lists cameras through USB only.)";
    return content;
}

DevicePanelContent ReadMicrophonePanel(bool inUse) {
    DevicePanelContent content;
    content.summary = inUse ? "A program is recording from a microphone now." : "No program is recording.";
    for (const AudioDeviceInfo& input : UltraCanvasAudioDevices::ListInputDevices()) {
        DevicePanelEntry entry;
        entry.title = input.name.empty() ? input.id : input.name;
        entry.detail = JoinParts({input.isDefault ? "default input" : "",
                                  input.maxChannels == 1 ? "mono" : input.maxChannels == 2 ? "stereo" : ""});
        entry.highlighted = input.isDefault;
        content.entries.push_back(entry);
    }
    content.empty = UltraCanvasAudioDevices::IsAvailable() ? "No microphone was found."
                                                           : "This build has no audio support to list microphones.";
    return content;
}

DevicePanelContent ReadBatteryPanel(const DesktopDeviceActivity& activity) {
    DevicePanelContent content;
    if (!activity.batteryPresent) {
        content.summary = "This computer runs on mains power: it has no battery.";
        return content;
    }
    DevicePanelEntry entry;
    entry.title = activity.batteryPercent >= 0 ? std::to_string(activity.batteryPercent) + "%" : "Battery";
    entry.detail = activity.batteryCharging ? "charging" : "on battery";
    entry.highlighted = activity.batteryCharging;
    content.entries.push_back(entry);
    content.summary = activity.batteryCharging ? "Plugged in and charging." : "Running on the battery.";
    return content;
}

} // namespace

void UltraDesktopWindow::OpenWifiPanel() {
    if (!window_ || !info_) return;
    if (!wifiPanel_) {
        wifiPanel_ = std::make_shared<UltraDesktopWifiPanel>(
            nullptr, []() { UltraCanvasDesktopShell::LaunchProgram("UltraNetMonitor"); });
    }
    if (wifiPanel_->IsOpen()) {
        wifiPanel_->Close();
        return;
    }
    const Rect2Df bounds = info_->GetWidget("wifi") ? info_->GetWidget("wifi")->GetBoundsInWindow() : Rect2Df(0, 0, 0, 0);
    wifiPanel_->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    wifiPanel_->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y - 200));
}

void UltraDesktopWindow::OpenBluetoothPanel() {
    if (!window_ || !info_) return;
    if (!bluetoothPanel_) bluetoothPanel_ = std::make_shared<UltraDesktopBluetoothPanel>(nullptr);
    if (bluetoothPanel_->IsOpen()) {
        bluetoothPanel_->Close();
        return;
    }
    const Rect2Df bounds =
        info_->GetWidget("bluetooth") ? info_->GetWidget("bluetooth")->GetBoundsInWindow() : Rect2Df(0, 0, 0, 0);
    bluetoothPanel_->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    bluetoothPanel_->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y - 200));
}

void UltraDesktopWindow::OpenDevicePanel(const std::string& device) {
    if (!window_ || !info_) return;
    auto& panel = devicePanels_[device];
    if (panel && panel->IsOpen()) {
        panel->Close();
        return;
    }
    // Read afresh each time: the panel is made again with what the device
    // poll saw last.
    const DesktopDeviceActivity activity = lastActivity_;
    const DevicePanelAction explorer{"Device Explorer", "Printers, scanners and cameras in detail",
                                     []() { UltraCanvasDesktopShell::LaunchProgram("DeviceExplorer"); }};
    const DevicePanelAction tasks{"Task Manager", "The hardware page lists every device",
                                  [this]() { OpenTasks(); }};
    if (device == "usb") {
        panel = std::make_shared<UltraDesktopDevicePanel>("USB", ReadUsbPanel,
                                                          std::vector<DevicePanelAction>{explorer, tasks});
    } else if (device == "keyboard") {
        const std::string layout = activity.keyboardLayout;
        panel = std::make_shared<UltraDesktopDevicePanel>(
            "Keyboard", [layout]() { return ReadKeyboardPanel(layout); },
            std::vector<DevicePanelAction>{
                {"Bluetooth", "Pair a wireless keyboard", [this]() { OpenBluetoothPanel(); }}, tasks});
    } else if (device == "webcam") {
        const bool inUse = activity.webcamInUse;
        panel = std::make_shared<UltraDesktopDevicePanel>(
            "Webcam", [inUse]() { return ReadWebcamPanel(inUse); }, std::vector<DevicePanelAction>{explorer, tasks});
    } else if (device == "mic") {
        const bool inUse = activity.microphoneInUse;
        panel = std::make_shared<UltraDesktopDevicePanel>(
            "Microphone", [inUse]() { return ReadMicrophonePanel(inUse); },
            std::vector<DevicePanelAction>{{"Loudspeaker", "The output's volume", [this]() { OpenVolumePanel(); }},
                                           tasks});
    } else if (device == "battery") {
        panel = std::make_shared<UltraDesktopDevicePanel>(
            "Battery", [activity]() { return ReadBatteryPanel(activity); }, std::vector<DevicePanelAction>{tasks});
    } else {
        return;
    }
    const Rect2Df bounds =
        info_->GetWidget(device) ? info_->GetWidget(device)->GetBoundsInWindow() : Rect2Df(0, 0, 0, 0);
    panel->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    panel->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y - 120));
}

void UltraDesktopWindow::ReadSpeaker() {
    speaker_ = UltraCanvasDesktopShell::ReadAudioOutput();
    haveSpeaker_ = true;
    ApplySpeaker();
}

// The speaker icon: red while muted (on top of the yellow for playing), the
// volume in the words. Playing or not is the device poll's.
void UltraDesktopWindow::ApplySpeaker() {
    if (!info_) return;
    std::string words = "Loudspeaker";
    if (haveSpeaker_ && speaker_.available) {
        words += speaker_.muted ? ": muted" : ": " + std::to_string(speaker_.volumePercent) + "%";
    }
    if (haveLastActivity_ && lastActivity_.speakerPlaying) words += ", playing";
    if (haveSpeaker_ && speaker_.available && speaker_.muted) {
        info_->SetItemBadgeDot("speaker", kMarkerOn);
    } else if (haveLastActivity_ && lastActivity_.speakerPlaying) {
        info_->SetItemBadgeDot("speaker", kMarkerActive);
    } else {
        info_->ClearItemBadge("speaker");
    }
    if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget("speaker"))) {
        button->SetTooltip(words);
    }
}

// ===== THE LOCK =====

void UltraDesktopWindow::LockScreen() {
    std::string error;
    if (UltraCanvasDesktopShell::LockSession(&error)) return;
    debugOutput << "UltraDesktop: the screen was not locked: " << error << std::endl;
    UltraCanvasDialogManager::ShowWarning("The screen could not be locked.\n\n" + error, "UltraDesktop", nullptr,
                                          window_.get());
}

// ===== PINNED APPLICATIONS =====

std::string UltraDesktopWindow::PinKey(const UCDesktopEntry& entry) {
    if (!entry.shortcutPath.empty()) return entry.shortcutPath;
    if (!entry.appUserModelId.empty()) return "store:" + entry.appUserModelId;
    if (!entry.program.empty()) return entry.program;
    return entry.exec;
}

bool UltraDesktopWindow::IsPinned(const UCDesktopEntry& entry) const {
    return settings_.IsPinned(PinKey(entry));
}

void UltraDesktopWindow::SetPinned(const UCDesktopEntry& entry, bool pinned) {
    const std::string key = PinKey(entry);
    if (key.empty() || settings_.IsPinned(key) == pinned) return;
    settings_.SetPinned(key, pinned);
    SaveSettings();
    BuildLayout();
    RefreshDesktops();
}

const std::vector<UCDesktopEntry>& UltraDesktopWindow::Applications() {
    const auto now = std::chrono::steady_clock::now();
    if (applications_.empty() || now - applicationsRead_ > std::chrono::minutes(5)) {
        applications_ = UltraCanvasDesktopShell::ListApplications(kBarIcon);
        applicationsRead_ = now;
    }
    return applications_;
}

void UltraDesktopWindow::ShowPinnedMenu(const UCDesktopEntry& entry, int windowX, int windowY) {
    if (!window_) return;
    popupMenu_ = std::make_shared<UltraCanvasMenu>("PinnedMenu", 0, 0, 220, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    popupMenu_->AddItem(MenuItemData::Header(Shorten(entry.name, 40)));
    popupMenu_->AddItem(MenuItemData::Action("Open", [entry]() { UltraCanvasDesktopShell::LaunchApplication(entry); }));
    popupMenu_->AddItem(MenuItemData::Separator());
    popupMenu_->AddItem(MenuItemData::Action("Unpin from taskbar", [this, entry]() { SetPinned(entry, false); }));
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

std::shared_ptr<UltraCanvasContainer> UltraDesktopWindow::BuildRightBar() {
    auto bar = CreateContainer("RightBar", 0, 0, 0, 0);
    bar->SetBackgroundColor(kBarColor);
    bar->SetElementSize(CSSLayout::Dimension::Px(kInfoPanelWidth), CSSLayout::Dimension::Auto());
    bar->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    bar->layoutItem.SetFlexShrink(0.0f);

    // The clock first, where the eye looks for it.
    bar->AddChild(BuildClock());

    // The desktop organiser: virtual desktops, then the board, clipboard, screenshot.
    organiser_ = MakeGroup("Organiser", true, true);
    desktopCount_ = std::clamp(DesiredDesktopCount(), 1, 9);
    for (int i = 0; i < desktopCount_; ++i) {
        const std::string id = "desktop" + std::to_string(i + 1);
        AddBarToggle(organiser_, id, "Desktop " + std::to_string(i + 1), "", std::to_string(i + 1),
                     [this, i](bool) { SwitchToDesktop(i); });
    }
    organiser_->AddSeparator();
    // The pinboard is the Sticker board; the free-form notes keep a note
    // of their own.
    AddBarToggle(organiser_, "categoryboard", "Sticker board: drop files into categories",
                 IconPath("pinboard.svg"), "", [this](bool on) { ToggleCategoryBoard(on); });
    AddBarToggle(organiser_, "stickerboard", "Notes on the wallpaper", IconPath("notes.svg"), "",
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
    // Fourteen services and the tray icons do not fit under the organiser
    // on a short screen even at the compact size: what is left over goes
    // into the chevron's menu at the foot of the bar, and FitInfoPanel
    // sees to it that the least-used services are the ones that go.
    info_->SetOverflowMode(ToolbarOverflowMode::Menu);
    infoFolded_.clear();
    styledChevron_ = nullptr;
    // Tighter than the taskbar's items, still a 36 px target (30 px on a
    // short screen).
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
    AddBarButton(info_, "bluetooth", "Bluetooth",    IconPath("bluetooth.svg"), [this]() { OpenBluetoothPanel(); });
    AddBarButton(info_, "wifi",      "Wi-Fi",        IconPath("wifi.svg"),      [this]() { OpenWifiPanel(); });
    AddBarButton(info_, "usb",       "USB",          IconPath("usb.svg"),       [this]() { OpenDevicePanel("usb"); });
    AddBarButton(info_, "keyboard",  "Keyboard",     IconPath("keyboard.svg"),  [this]() { OpenDevicePanel("keyboard"); });
    AddBarButton(info_, "webcam",    "Webcam",       IconPath("webcam.svg"),    [this]() { OpenDevicePanel("webcam"); });
    AddBarButton(info_, "mic",       "Microphone",   IconPath("mic.svg"),       [this]() { OpenDevicePanel("mic"); });
    AddBarButton(info_, "speaker",   "Loudspeaker",  IconPath("speaker.svg"),   [this]() { OpenVolumePanel(); });
    AddBarButton(info_, "battery",   "Battery",      IconPath("battery.svg"),   [this]() { OpenDevicePanel("battery"); });
    AddBarButton(info_, "tasks",     "Task Manager", IconPath("tasks.svg"),     [this]() { OpenTasks(); });
    for (const auto& item : info_->GetItems()) {
        if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(item)) {
            button->SetElementSize(Size2Df(static_cast<float>(InfoButtonSize()), static_cast<float>(InfoButtonSize())));
            button->SetIconSize(InfoIconSize(), InfoIconSize());
        }
    }
    bar->AddChild(info_);
    ApplySpeaker();
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

    // The Sticker board too, above the notes: its panel is the one place it
    // answers, so the notes stay reachable around it.
    categoryBoard_ = std::make_shared<UltraDesktopCategoryBoard>("CategoryBoard", this);
    if (!categoryBoard_->layoutItem.position) categoryBoard_->layoutItem.position = CSSLayout::Position();
    categoryBoard_->layoutItem.position->left = CSSLayout::Dimension::Px(0);
    categoryBoard_->layoutItem.position->top = CSSLayout::Dimension::Px(0);
    categoryBoard_->layoutItem.position->right = CSSLayout::Dimension::Px(0);
    categoryBoard_->layoutItem.position->bottom = CSSLayout::Dimension::Px(0);
    categoryBoard_->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
    categoryBoard_->SetVisible(settings_.categoryBoard.visible);
    categoryBoard_->LoadFrom(settings_);
    area->AddChild(categoryBoard_);
    if (organiser_) {
        if (auto toggle = std::dynamic_pointer_cast<UltraCanvasButton>(organiser_->GetWidget("stickerboard"))) {
            toggle->SetPressed(settings_.stickerboardVisible);
        }
        if (auto toggle = std::dynamic_pointer_cast<UltraCanvasButton>(organiser_->GetWidget("categoryboard"))) {
            toggle->SetPressed(settings_.categoryBoard.visible);
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
    UltraDesktopAppStarter::Pins pins;
    pins.isPinned = [this](const UCDesktopEntry& entry) { return IsPinned(entry); };
    pins.setPinned = [this](const UCDesktopEntry& entry, bool pinned) { SetPinned(entry, pinned); };
    appStarter_ = std::make_shared<UltraDesktopAppStarter>(std::move(pins));
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

void UltraDesktopWindow::ToggleCategoryBoard(bool visible) {
    settings_.categoryBoard.visible = visible;
    if (categoryBoard_) {
        categoryBoard_->SetVisible(visible);
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
    // The other screens show the same picture.
    for (const auto& wallpaper : screenWallpapers_) {
        if (wallpaper) wallpaper->LoadFromFile(WallpaperPath());
    }
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
    // Top right of the desktop's own monitor, beside the right bar, and
    // below the taskbar when it runs along the top. The toasts measure from
    // the whole screen's edges, so the monitors beyond the desktop's are
    // part of the margins.
    const TaskbarEdge edge = settings_.taskbarEdge;
    int screenW = 0, screenH = 0;
    UltraCanvasDesktopShell::GetScreenSize(screenW, screenH);
    const auto logical = [this](int physical) { return window_ ? window_->PhysicalToLogical(physical) : physical; };
    const int beyondLeft = logical(std::max(0, area_.x));
    const int beyondTop = logical(std::max(0, area_.y));
    const int beyondRight = logical(std::max(0, screenW - (area_.x + area_.width)));
    const int beyondBottom = logical(std::max(0, screenH - (area_.y + area_.height)));
    toasts_->SetCorner(NotificationToastCorner::TopRight);
    toasts_->SetScreenMargins(beyondLeft + (edge == TaskbarEdge::Left ? kBarThickness : 0),
                              beyondTop + (edge == TaskbarEdge::Top ? kBarThickness : 0),
                              beyondRight + kInfoPanelWidth,
                              beyondBottom + (edge == TaskbarEdge::Bottom ? kBarThickness : 0));
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

// ===== THE INFO PANEL ON A SHORT SCREEN =====

bool UltraDesktopWindow::CompactInfoPanel() const {
    return area_.height > 0 && area_.height < kCompactScreenHeight;
}

int UltraDesktopWindow::InfoButtonSize() const {
    return CompactInfoPanel() ? kInfoButtonCompact : kInfoButton;
}

int UltraDesktopWindow::InfoIconSize() const {
    return CompactInfoPanel() ? kInfoIconCompact : kInfoIcon;
}

// The toolbar's Menu mode takes the items off the end of the bar; this
// makes those the least-used services. It moves the `n` least-used ones to
// the end - in their usual order - where `n` is how many the menu holds,
// and is done once the menu holds exactly them. A bar that no longer
// overflows (a taller screen) keeps the order until the bars are built again.
void UltraDesktopWindow::FitInfoPanel() {
    if (!info_) return;
    // The chevron is the toolbar's own button: dress it like the bar's.
    if (auto chevron = info_->GetOverflowButton(); chevron && chevron.get() != styledChevron_) {
        styledChevron_ = chevron.get();
        StyleBarButton(chevron);
        chevron->SetElementSize(Size2Df(static_cast<float>(InfoButtonSize()), static_cast<float>(InfoButtonSize())));
        chevron->SetIconSize(InfoIconSize(), InfoIconSize());
        chevron->SetTooltip("More");
    }
    const std::vector<std::string> overflow = info_->GetOverflowItemIds();
    if (overflow.empty()) return;
    std::set<std::string> wanted;
    for (const char* id : kInfoFoldOrder) {
        if (wanted.size() >= overflow.size()) break;
        if (info_->GetItemIndex(id) >= 0) wanted.insert(id);
    }
    if (std::set<std::string>(overflow.begin(), overflow.end()) == wanted || wanted == infoFolded_) return;
    infoFolded_ = wanted;
    const std::vector<std::string> order = info_->GetItemOrder();
    const int last = static_cast<int>(order.size()) - 1;
    for (const std::string& id : order) {
        if (wanted.count(id) == 0) continue;
        const int from = info_->GetItemIndex(id);
        if (from >= 0 && from != last) info_->MoveItem(from, last);
    }
    info_->InvalidateLayout();
    info_->RequestRedraw();
}

void UltraDesktopWindow::OnTimer() {
    CheckSettingsFile();
    CheckDesktopSwitch();
    {
        // A monitor plugged in or out: every few seconds is soon enough.
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextScreenCheck_) {
            nextScreenCheck_ = now + std::chrono::seconds(3);
            FollowPrimaryScreen();
            CoverOtherScreens();
        }
    }
    if (windowsDirty_.exchange(false)) {
        RefreshDesktops();
        RefreshWindows();
    }
    if (trayDirty_.exchange(false)) RefreshTray();
    FitInfoPanel();
    UpdateClock();

    bool apply = false;
    bool applySpeaker = false;
    DesktopDeviceActivity now;
    {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        if (devicePending_) {
            now = pending_;
            devicePending_ = false;
            apply = true;
        }
        if (speakerPending_) {
            speaker_ = pendingSpeaker_;
            haveSpeaker_ = true;
            speakerPending_ = false;
            applySpeaker = true;
        }
    }
    if (apply) {
        ApplyDeviceActivity(now, lastActivity_, haveLastActivity_);
        lastActivity_ = now;
        haveLastActivity_ = true;
        applySpeaker = true;
    }
    if (applySpeaker) ApplySpeaker();

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
            // The application's own icon in its own colours, as the pins
            // and the tray show it; only the bar's glyphs and the letter
            // fallback are tinted through the mask.
            button->SetUseIconAsMask(w.iconFile.empty());
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

    // Yellow: activity. (The speaker's marker and words are ApplySpeaker's,
    // which adds the volume.)
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
            // The loudspeaker now and then: a change made elsewhere (a
            // keyboard's volume keys, another program) shows within seconds.
            const bool readSpeaker = pollsSinceSpeaker_ == 0;
            DesktopAudioOutput speaker;
            if (readSpeaker) speaker = UltraCanvasDesktopShell::ReadAudioOutput();
            lock.lock();
            if (!devicePollRunning_) break;
            pending_ = std::move(reading);
            devicePending_ = true;
            if (readSpeaker) {
                pendingSpeaker_ = std::move(speaker);
                speakerPending_ = true;
            }
            pollsSinceSpeaker_ = (pollsSinceSpeaker_ + 1) % kPollsPerSpeakerRead;
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
    // Its application, when one is installed under its class: pinned from here.
    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
        if (w.id != windowId) continue;
        if (const UCDesktopEntry* entry = UltraCanvasDesktopShell::MatchApplication(w, Applications())) {
            const UCDesktopEntry application = *entry;
            const bool pinned = IsPinned(application);
            popupMenu_->AddItem(MenuItemData::Separator());
            popupMenu_->AddItem(MenuItemData::Action(pinned ? "Unpin from taskbar" : "Pin to taskbar",
                                                     [this, application, pinned]() { SetPinned(application, !pinned); }));
        }
        break;
    }
    popupMenu_->AddItem(MenuItemData::Separator());
    popupMenu_->AddItem(MenuItemData::Action("Close window", [windowId]() { UltraCanvasDesktopShell::CloseWindow(windowId); }));
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

void UltraDesktopWindow::ShowSettingsButtonMenu(int windowX, int windowY) {
    if (!window_) return;
    popupMenu_ = std::make_shared<UltraCanvasMenu>("SettingsButtonMenu", 0, 0, 300, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    popupMenu_->AddItem(MenuItemData::Action("ULTRA OS settings", [this]() { OpenSystemSettings(); }));
    // Only Windows has a desktop of its own to switch from, and back to.
    if (UltraCanvasDesktopShell::CanReplaceSystemDesktop()) {
        popupMenu_->AddItem(MenuItemData::Separator());
        const DesktopReplacement mode = UltraCanvasDesktopShell::GetSystemDesktopReplacement();
        if (mode == DesktopReplacement::Off) {
            popupMenu_->AddItem(MenuItemData::Action("Use as the Windows desktop (beside Explorer)",
                                                     [this]() { UseAsSystemDesktop(DesktopReplacement::Beside); }));
        }
        if (mode != DesktopReplacement::Instead &&
            UltraCanvasDesktopShell::CanReplaceSystemDesktop(DesktopReplacement::Instead)) {
            popupMenu_->AddItem(MenuItemData::Action("Use instead of Explorer (from the next sign-in)",
                                                     [this]() { UseAsSystemDesktop(DesktopReplacement::Instead); }));
        }
        if (mode == DesktopReplacement::Instead) {
            popupMenu_->AddItem(MenuItemData::Action("Keep Explorer running beside UltraDesktop",
                                                     [this]() { UseAsSystemDesktop(DesktopReplacement::Beside); }));
        }
        if (mode != DesktopReplacement::Off) {
            popupMenu_->AddItem(MenuItemData::Action("Switch back to the Windows desktop",
                                                     [this]() { SwitchBackToSystemDesktop(); }));
        } else if (!sessionDesktop_) {
            // A trial run beside the Windows desktop has no title bar to close.
            popupMenu_->AddItem(MenuItemData::Action("Close UltraDesktop", []() {
                if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->RequestExit();
            }));
        }
    } else if (const std::string why = UltraCanvasDesktopShell::GetSystemDesktopSwitchUnavailableReason();
               !why.empty()) {
        // Windows, but a copy that cannot switch - the Microsoft Store one,
        // whose registry changes sign-in never sees. Say so where the switch
        // would be, rather than leave it out without a word; and this is
        // always a trial run, with no title bar to close.
        popupMenu_->AddItem(MenuItemData::Separator());
        popupMenu_->AddItem(MenuItemData::Action("Use as the Windows desktop...", [this, why]() {
            UltraCanvasDialogManager::ShowInformation(why, "UltraDesktop", nullptr, window_.get());
        }));
        popupMenu_->AddItem(MenuItemData::Action("Close UltraDesktop", []() {
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->RequestExit();
        }));
    }
    // The lock and the end of the session: wherever this desktop is the
    // one that offers them - as the shell on Windows, and on Linux, where
    // the session has no other desktop to switch to.
    const bool ownsSession = shellSession_ || !UltraCanvasDesktopShell::CanReplaceSystemDesktop();
    if (ownsSession && UltraCanvasDesktopShell::CanLockSession()) {
        popupMenu_->AddItem(MenuItemData::Separator());
        popupMenu_->AddItem(MenuItemData::ActionWithShortcut("Lock screen", "Super+L", [this]() { LockScreen(); }));
    }
    if (ownsSession && UltraCanvasDesktopShell::CanEndSession()) {
        popupMenu_->AddItem(MenuItemData::Separator());
        popupMenu_->AddItem(MenuItemData::Action("Sign out...", [this]() { ConfirmEndSession(SessionEnd::SignOut); }));
        popupMenu_->AddItem(MenuItemData::Action("Restart...", [this]() { ConfirmEndSession(SessionEnd::Restart); }));
        popupMenu_->AddItem(MenuItemData::Action("Shut down...", [this]() { ConfirmEndSession(SessionEnd::ShutDown); }));
    }
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX, windowY), *window_, settings);
}

// ===== EVERY SCREEN =====

// The monitor the desktop belongs on right now: the primary of several (the
// one the session marks, else the one listed first - at the origin), the
// whole screen of one or where the platform tells none apart.
DesktopScreenInfo UltraDesktopWindow::DesktopAreaNow(bool& oneOfSeveral) {
    const std::vector<DesktopScreenInfo> screens = UltraCanvasDesktopShell::ListScreens();
    const DesktopScreenInfo* primary = nullptr;
    for (const DesktopScreenInfo& screen : screens) {
        if (screen.primary && !primary) primary = &screen;
    }
    if (!primary && !screens.empty()) primary = &screens.front();
    oneOfSeveral = primary && screens.size() > 1;
    DesktopScreenInfo area;
    if (oneOfSeveral) {
        area = *primary;
    } else {
        UltraCanvasDesktopShell::GetScreenSize(area.width, area.height);
    }
    area.primary = true;
    return area;
}

std::shared_ptr<UltraCanvasWindow> UltraDesktopWindow::CreateDesktopWindow(const DesktopScreenInfo& area,
                                                                           bool oneOfSeveral) {
    WindowConfig config;
    config.title = std::string("UltraDesktop ") + ULTRADESKTOP_VERSION;
    config.type = WindowType::Desktop;
    config.width = 1280;    // replaced by the screen size for a desktop window
    config.height = 800;
    config.backgroundColor = kBarInsetColor;
    if (oneOfSeveral) config.desktopArea = Rect2Di(area.x, area.y, area.width, area.height);
    return CreateWindow(config);
}

// The primary moved (xrandr --output ... --primary, the display settings),
// or the monitors changed so that the desktop's own is no longer the
// primary's place: a desktop window cannot be moved to another monitor in
// place, so a new one is made there, the bars built into it, and the old one
// closed - the new one first, so the application never sees itself without
// a window. The panels and the toasts follow through area_.
void UltraDesktopWindow::FollowPrimaryScreen() {
    bool oneOfSeveral = false;
    const DesktopScreenInfo area = DesktopAreaNow(oneOfSeveral);
    if (area.width <= 0 || area.height <= 0) return;
    if (area.x == area_.x && area.y == area_.y && area.width == area_.width && area.height == area_.height) return;
    auto fresh = CreateDesktopWindow(area, oneOfSeveral);
    if (!fresh) {
        debugOutput << "UltraDesktop: no window for the primary monitor at " << area.x << "," << area.y << std::endl;
        return;
    }
    debugOutput << "UltraDesktop: the primary monitor is now " << area.width << "x" << area.height << " at "
                << area.x << "," << area.y << "; moving the desktop there" << std::endl;
    auto old = window_;
    // The old window's tree goes with it; the bars are built anew for the
    // new one (BuildLayout removes root_ from window_, which is the new one
    // by then, so it is taken off the old one here).
    if (old && root_) old->RemoveChild(root_);
    root_.reset();
    window_ = fresh;
    area_ = area;
    BuildLayout();
    RefreshDesktops();
    window_->Show();
    PlaceNotifications();
    if (old) {
        old->onWindowResize = nullptr;
        old->Close();
    }
}

int UltraDesktopWindow::ScreenX(float logicalX) const {
    return area_.x + (window_ ? window_->LogicalToPhysical(static_cast<int>(logicalX)) : static_cast<int>(logicalX));
}

int UltraDesktopWindow::ScreenY(float logicalY) const {
    return area_.y + (window_ ? window_->LogicalToPhysical(static_cast<int>(logicalY)) : static_cast<int>(logicalY));
}

void UltraDesktopWindow::CoverOtherScreens() {
    // The monitors outside what the desktop window covers (area_). Beside
    // Explorer, Explorer's desktop shows on them, with its icons; as the
    // shell on Windows, and on X11, where the session has no other desktop,
    // each gets a wallpaper window.
    const bool systemCovers = UltraCanvasDesktopShell::CanReplaceSystemDesktop() &&
                              UltraCanvasDesktopShell::IsSystemDesktopRunning();
    std::vector<DesktopScreenInfo> others;
    if (!systemCovers) {
        for (const DesktopScreenInfo& screen : UltraCanvasDesktopShell::ListScreens()) {
            const bool inside = screen.x >= area_.x && screen.y >= area_.y &&
                                screen.x + screen.width <= area_.x + area_.width &&
                                screen.y + screen.height <= area_.y + area_.height;
            if (!inside) others.push_back(screen);
        }
    }
    const bool changed = others != otherScreens_;
    if (changed) {
        for (const auto& window : screenWindows_) {
            if (window && window->IsCreated()) window->Close();
        }
        otherScreens_ = others;
        screenWindows_.assign(others.size(), nullptr);
        screenWallpapers_.assign(others.size(), nullptr);
    }
    // A window closed since (Alt+F4) opens again, as does one that would not
    // open the last time - the others stay as they are.
    for (size_t i = 0; i < otherScreens_.size(); ++i) {
        if (screenWindows_[i] && screenWindows_[i]->IsCreated()) continue;
        OpenScreenWindow(i, changed);
    }
}

void UltraDesktopWindow::OpenScreenWindow(size_t index, bool report) {
    const DesktopScreenInfo& screen = otherScreens_[index];
    WindowConfig config;
    config.title = std::string("UltraDesktop ") + ULTRADESKTOP_VERSION;
    config.type = WindowType::Desktop;
    config.desktopArea = Rect2Di(screen.x, screen.y, screen.width, screen.height);
    config.backgroundColor = kBarInsetColor;
    auto window = CreateWindow(config);
    if (!window) {
        // Tried again at every check; said once.
        if (report) {
            debugOutput << "UltraDesktop: no window for the screen at " << screen.x << "," << screen.y << std::endl;
        }
        return;
    }
    auto wallpaper = std::make_shared<UltraCanvasImageElement>("ScreenWallpaper" + std::to_string(index), 0, 0, 0, 0);
    wallpaper->SetFitMode(ImageFitMode::Cover);
    wallpaper->LoadFromFile(WallpaperPath());
    int width = 0, height = 0;
    window->GetWindowSize(width, height);
    wallpaper->SetElementAbsolutePosition(Point2Df(0, 0));
    wallpaper->SetElementSize(Size2Df(static_cast<float>(std::max(1, width)), static_cast<float>(std::max(1, height))));
    window->AddChild(wallpaper);
    UltraCanvasImageElement* picture = wallpaper.get();
    window->onWindowResize = [picture](int w, int h) {
        picture->SetElementSize(Size2Df(static_cast<float>(std::max(1, w)), static_cast<float>(std::max(1, h))));
    };
    window->Show();
    screenWindows_[index] = window;
    screenWallpapers_[index] = wallpaper;
}

// ===== IN PLACE OF THE SYSTEM'S DESKTOP =====

void UltraDesktopWindow::UseAsSystemDesktop(DesktopReplacement how) {
    std::string error;
    const bool instead = how == DesktopReplacement::Instead;
    if (UltraCanvasDesktopShell::ReplaceSystemDesktop(UltraCanvasDesktopShell::FindProgram("UltraDesktop"),
                                                      {instead ? "--shell" : "--session"}, &error, how)) {
        sessionDesktop_ = true;
        if (instead && !shellSession_) {
            UltraCanvasDialogManager::ShowInformation(
                    "From the next sign-in UltraDesktop is the desktop, and Explorer does not start: sign out and "
                    "in again to see it.\n\nShould UltraDesktop ever fail to start, Explorer starts in its place "
                    "for that session. \"Switch back to the Windows desktop\", here or in UOS-Settings, undoes it.",
                    "UltraDesktop", nullptr, window_.get());
        }
        return;
    }
    debugOutput << "UltraDesktop: not the desktop at sign-in: " << error << std::endl;
    UltraCanvasDialogManager::ShowWarning("UltraDesktop could not take the place of the Windows desktop.\n\n" + error,
                                          "UltraDesktop", nullptr, window_.get());
}

void UltraDesktopWindow::SwitchBackToSystemDesktop() {
    std::string error;
    if (!UltraCanvasDesktopShell::RestoreSystemDesktop(&error)) {
        debugOutput << "UltraDesktop: the Windows desktop not restored: " << error << std::endl;
        UltraCanvasDialogManager::ShowWarning("The Windows desktop could not be restored.\n\n" + error,
                                              "UltraDesktop", nullptr, window_.get());
        return;
    }
    // Explorer's taskbar is back - or, as the shell, the guard starts
    // Explorer the moment this desktop has closed.
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->RequestExit();
}

void UltraDesktopWindow::ConfirmEndSession(SessionEnd how) {
    const char* question = how == SessionEnd::SignOut ? "Sign out now? Programs that are open are asked to close."
                         : how == SessionEnd::Restart ? "Restart the computer now? Programs that are open are asked "
                                                        "to close."
                                                      : "Shut down the computer now? Programs that are open are "
                                                        "asked to close.";
    UltraCanvasDialogManager::ShowConfirmation(question, "UltraDesktop", [this, how](bool confirmed) {
        if (!confirmed) return;
        std::string error;
        if (UltraCanvasDesktopShell::EndSession(how, &error)) return;
        UltraCanvasDialogManager::ShowWarning("The session did not end.\n\n" + error, "UltraDesktop", nullptr,
                                              window_.get());
    }, window_.get());
}

// ===== THE NOTIFICATION AREA =====

void UltraDesktopWindow::StartTray() {
    std::string error;
    if (!trayHost_.Start([this]() { trayDirty_.store(true); }, &error)) {
        debugOutput << "UltraDesktop: the tray icons are not shown: " << error << std::endl;
    }
}

void UltraDesktopWindow::StartDesktopShortcuts() {
    // Beside Explorer the Win key is the Start menu's and Win+L is Windows'
    // own; as the shell, and on Linux, they are this desktop's.
    const bool ownsSession = shellSession_ || !UltraCanvasDesktopShell::CanReplaceSystemDesktop();
    if (!ownsSession) return;
    std::string error;
    const bool apps = appsShortcut_.Start("Super", [this]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([this]() { OpenAppStarter(); });
        }
    }, &error);
    if (!apps) debugOutput << "UltraDesktop: the Super key does not open the applications: " << error << std::endl;
    if (!UltraCanvasDesktopShell::CanLockSession()) return;
    error.clear();
    const bool lock = lockShortcut_.Start("Super+L", [this]() {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([this]() { LockScreen(); });
        }
    }, &error);
    if (!lock) debugOutput << "UltraDesktop: Super+L does not lock the screen: " << error << std::endl;
}

void UltraDesktopWindow::RefreshTray() {
    if (!info_ || !trayHost_.IsRunning()) return;
    const std::vector<DesktopTrayIcon> icons = trayHost_.ListIcons();
    std::set<std::string> wanted;
    for (const DesktopTrayIcon& icon : icons) wanted.insert(kTrayPrefix + icon.id);

    // Gone icons leave the panel.
    for (auto it = trayIcons_.begin(); it != trayIcons_.end();) {
        if (wanted.count(it->first) == 0) {
            info_->RemoveItem(it->first);
            trayTooltips_.erase(it->first);
            trayMenus_.erase(it->first);
            it = trayIcons_.erase(it);
        } else {
            ++it;
        }
    }

    for (const DesktopTrayIcon& icon : icons) {
        const std::string itemId = kTrayPrefix + icon.id;
        std::shared_ptr<UltraCanvasButton> button;
        auto known = trayIcons_.find(itemId);
        if (known == trayIcons_.end()) {
            // A program without a picture still gets a button: its initial.
            std::string text;
            if (icon.iconFile.empty() && !icon.appClass.empty()) {
                text = icon.appClass.substr(0, 1);
                std::transform(text.begin(), text.end(), text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
            const std::string id = icon.id;
            button = info_->AddButton(itemId, text, icon.iconFile, [this, id, itemId]() {
                if (info_) info_->ClearItemBadge(itemId);   // the balloon was seen
                trayHost_.Click(id, TrayClick::Left);
            });
            StyleBarButton(button);
            // In their own colours: a tray icon's colour often is its state.
            ButtonStyle style = button->GetStyle();
            style.useIconAsMask = false;
            button->SetStyle(style);
            button->SetElementSize(Size2Df(static_cast<float>(InfoButtonSize()), static_cast<float>(InfoButtonSize())));
            button->SetIconSize(InfoIconSize(), InfoIconSize());
            // A program that hands its menu over as data gets it drawn here;
            // the others open their own on the click.
            button->onContextMenu = [this, id, itemId](int x, int y) {
                const auto menu = trayMenus_.find(itemId);
                if (menu != trayMenus_.end() && !menu->second.empty()) ShowTrayMenu(id, itemId, x, y);
                else trayHost_.Click(id, TrayClick::Right);
            };
            // Above the services, in the order the programs added them.
            const int from = info_->GetItemIndex(itemId);
            const int to = info_->GetItemIndex("mail");
            if (to >= 0 && from > to) info_->MoveItem(from, to);
            trayIcons_[itemId] = icon.iconFile;
        } else {
            button = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget(itemId));
            if (button && known->second != icon.iconFile) {
                button->SetIcon(icon.iconFile);
                known->second = icon.iconFile;
            }
        }
        if (!button) continue;
        trayMenus_[itemId] = icon.menu;
        const std::string tooltip = icon.tooltip.empty() ? icon.appClass : icon.tooltip;
        if (trayTooltips_[itemId] != tooltip) {
            trayTooltips_[itemId] = tooltip;
            button->SetTooltip(tooltip);
        }
        if (icon.balloonSerial != 0 && trayBalloons_[icon.id] != icon.balloonSerial) {
            trayBalloons_[icon.id] = icon.balloonSerial;
            ShowTrayBalloon(icon);
        }
    }
    info_->InvalidateLayout();
    info_->RequestRedraw();
}

namespace {

// A tray icon's menu as the framework's menu items, submenus and all: a
// choice goes back to the program by the entry's id.
std::vector<MenuItemData> TrayMenuItems(const std::vector<DesktopTrayMenuItem>& items,
                                        const std::function<void(int32_t)>& activate) {
    std::vector<MenuItemData> out;
    for (const DesktopTrayMenuItem& item : items) {
        if (item.separator) {
            out.push_back(MenuItemData::Separator());
            continue;
        }
        const int32_t id = item.id;
        MenuItemData data;
        if (!item.children.empty()) {
            data = MenuItemData::Submenu(item.label, item.iconFile, TrayMenuItems(item.children, activate));
        } else if (item.checkable) {
            data = MenuItemData::Checkbox(item.label, item.checked, [activate, id](bool) { activate(id); });
        } else {
            data = MenuItemData::Action(item.label, item.iconFile, [activate, id]() { activate(id); });
        }
        data.enabled = item.enabled;
        out.push_back(std::move(data));
    }
    return out;
}

} // namespace

void UltraDesktopWindow::ShowTrayMenu(const std::string& iconId, const std::string& itemId, int windowX, int windowY) {
    if (!window_) return;
    const auto menu = trayMenus_.find(itemId);
    if (menu == trayMenus_.end() || menu->second.empty()) return;
    popupMenu_ = std::make_shared<UltraCanvasMenu>("TrayMenu", 0, 0, 240, 0);
    popupMenu_->SetMenuType(MenuType::PopupMenu);
    const std::string id = iconId;
    for (MenuItemData& item : TrayMenuItems(menu->second, [this, id](int32_t entry) {
             trayHost_.ActivateMenuItem(id, entry);
         })) {
        popupMenu_->AddItem(std::move(item));
    }
    PopupElementSettings settings;
    popupMenu_->OpenMenu(Point2Di(windowX - 250, windowY), *window_, settings);
}

void UltraDesktopWindow::ShowTrayBalloon(const DesktopTrayIcon& icon) {
    const std::string title = icon.balloonTitle.empty() ? icon.appClass : icon.balloonTitle;
#ifdef ULTRADESKTOP_HAVE_NOTIFICATIONS
    // A balloon is a notification: the toasts draw it.
    if (toasts_) {
        UltraMessage::SystemNotification notification;
        notification.appId = icon.appClass;
        notification.appName = icon.appClass;
        notification.summary = title;
        notification.body = icon.balloonText;
        UltraMsgMessage message;
        message.envelope.id = "tray-" + icon.id + "-" + std::to_string(icon.balloonSerial);
        message.envelope.topic = UltraMsgTopics::SystemNotification;
        message.envelope.from.appId = "org.ultraos.ultradesktop.tray";
        message.body = UltraMessage::MakeSystemNotification(notification);
        if (toasts_->Ingest(message)) return;
    }
#endif
    // No toasts: a yellow marker on the icon, the words in its tooltip
    // until it is clicked.
    if (!info_) return;
    const std::string itemId = kTrayPrefix + icon.id;
    info_->SetItemBadgeDot(itemId, kMarkerActive);
    if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(info_->GetWidget(itemId))) {
        const std::string text = title + (icon.balloonText.empty() ? "" : ": " + icon.balloonText);
        trayTooltips_[itemId] = text;
        button->SetTooltip(text);
    }
}

void UltraDesktopWindow::CheckDesktopSwitch() {
    if (!sessionDesktop_) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < nextSwitchCheck_) return;
    nextSwitchCheck_ = now + std::chrono::seconds(1);
    if (!UltraCanvasDesktopShell::CanReplaceSystemDesktop() || UltraCanvasDesktopShell::IsSystemDesktopReplaced())
        return;
    debugOutput << "UltraDesktop: switched back to the Windows desktop elsewhere; closing" << std::endl;
    sessionDesktop_ = false;
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) app->RequestExit();
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
        target = PasteTargetFor(*pasteWindow, Applications());
    }
    // On the desktop's own monitor: beside the button, or centred for Super+V.
    clipboardPanel_->SetScreenArea(area_.x, area_.y, area_.width, area_.height);
    if (besideButton && clipboardButton_) {
        const Rect2Df bounds = clipboardButton_->GetBoundsInWindow();
        clipboardPanel_->Open(ScreenX(bounds.x - 10), ScreenY(bounds.y - 8), std::move(target));
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
