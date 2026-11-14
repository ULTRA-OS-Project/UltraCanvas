// Apps/UltraDesktop/ui/UltraDesktopWindow.h
// The ULTRA OS desktop: one screen-sized window at the bottom of the stack
// holding the wallpaper, the taskbar on the edge the settings name, and the
// right bar with the desktop organiser and the info panel.
//
// Everything on the bars is an UltraCanvasToolbar item; the groups on one
// bar are joined by UltraCanvasWaveSeparator; what the items show comes from
// UltraCanvasDesktopShell (windows, desktops, device activity, notices) and
// what they do goes back through it (activate, minimize, switch desktop,
// screenshot, launch). The window itself never touches the window system.
//
// Two background sources feed the bars, both hand over through flags and a
// mutex and are applied on the UI timer: the shell monitor (a window opened
// or closed, the active window or the current desktop changed) and the
// device poll (every two seconds, since a USB and network listing is not
// free).
//
// The clipboard: the desktop records every copy into the persistent history
// (UltraCanvasClipboardHistory) while it runs, and its clipboard button -
// or Super+V anywhere - opens the quick panel on that history. The
// UltraClipboard application edits the same history. Without a database in
// the build the button shows the framework's in-memory list instead.
//
// It is also the screen for notifications: an UltraCanvasNotificationToastHost
// on the UltraMessage bus draws, in the top-right corner beside the right bar,
// every notification that nothing else draws - on ULTRA OS UltraMessage
// itself is the notification server, so every application's notifications
// (and UltraMail's new mail) would otherwise reach the message feed only.
//
// As the session's shell on Windows (--shell: instead of Explorer) it also
// hosts the notification area - the programs' tray icons sit on the info
// panel, above its services, and their balloons become toasts - its
// settings button offers sign out, restart and shut down, which Explorer's
// Start menu would otherwise, and a tap of the Win key opens the
// Applications window.
// Version: 0.9.0 - files dropped on the wallpaper go to the Sticker board's
//                  "Unsorted" category or into the Desktop folder, shown as
//                  icons (DropFiles, SetDropTarget, UltraDesktopDesktopIcons)
// Version: 0.8.0 - context menus on the wallpaper (wallpaper, theme, the
//                  taskbar's edge, display settings) and on the bars (the
//                  taskbar's edge); a light theme (BarPalette, Bars); the
//                  clock back at the top of the right bar
// Version: 0.7.0 - an application's windows share one taskbar button with a
//                  count; resting the pointer on it shows the windows'
//                  pictures to choose from (UltraDesktopWindowPreviews)
// Version: 0.5.2 - the screenshot button opens UltraSnip (OpenSnip, its menu of
//                  shapes and the recording, Super+Shift+S), the plain capture as the fallback
// Version: 0.6.0 - the Wi-Fi and Bluetooth panels (join, pair, connect),
//                  and the USB, keyboard, webcam, microphone and battery
//                  panels behind the info panel's icons; the USB drives'
//                  "Safely remove"; on a short screen the info panel's items
//                  shrink and the least used fold into a menu (FitInfoPanel);
//                  programs that start with the desktop (StartStartupPrograms)
// Version: 0.5.1 - on X11 the desktop covers the primary monitor and puts
//                  wallpaper windows on the others, as it does as the
//                  Windows shell; panels and toasts follow it, and so does
//                  the desktop itself when the primary moves
// Version: 0.5.0 - the clock and the date, the clock panel; pinned
//                  applications; the volume panel; the lock; sign out,
//                  restart and shut down wherever the platform can; the
//                  Super tap and Super+L on Linux; tray icons' menus
// Version: 0.4.0 - Windows: instead of Explorer - the tray icons, sign out,
//                  restart and shut down, the Win key, every screen
// Version: 0.3.0 - Windows: in place of the Windows desktop and back
// Version: 0.2.0 - the notification toasts
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopSettings.h"

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasTimer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <map>
#include "UltraCanvasWindow.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasNotificationToastHost;
    class UltraCanvasClipboardHistory;
    class UltraCanvasClipboardRecorder;
    class UltraCanvasContainer;
    class UltraCanvasToolbar;
    class UltraCanvasWaveSeparator;
    class UltraCanvasImageElement;
    class UltraCanvasMenu;
    struct MenuItemData;
    class UltraCanvasButton;
    class UltraCanvasGaugeDiagramElement;
    class UltraCanvasLabel;
}

namespace UltraDesktop {

class UltraDesktopAppStarter;
class UltraDesktopClipboardPanel;
class UltraDesktopClockPanel;
class UltraDesktopVolumePanel;
class UltraDesktopWifiPanel;
class UltraDesktopBluetoothPanel;
class UltraDesktopDevicePanel;
class UltraDesktopStickerboard;
class UltraDesktopDesktopIcons;
class UltraDesktopCategoryBoard;
class UltraDesktopTasksWindow;
class UltraDesktopWindowPreviews;
struct BarPalette;

class UltraDesktopWindow {
public:
    UltraDesktopWindow();
    ~UltraDesktopWindow();

    // `settingsPath` is where the settings are read from and written to; ""
    // keeps them for this session only. `edgeOverride` (from --edge) wins
    // over the file for this run without being written back.
    bool Initialize(const std::string& settingsPath, const TaskbarEdge* edgeOverride);
    void Show();

    // ===== IN PLACE OF THE SYSTEM'S DESKTOP (Windows) =====
    // Started by the sign-in (--session, or just switched over): the desktop
    // closes when the switch is turned off elsewhere - UOS-Settings, the
    // uninstaller, --restore-desktop - so the Windows desktop is back at once.
    void SetSessionDesktop(bool session) { sessionDesktop_ = session; }
    // Started as the session's shell (--shell): Explorer does not run, so
    // the desktop hosts the tray icons and ends the session itself.
    void SetShellSession(bool shell) { shellSession_ = shell; }
    // The settings button's menu: switch over - beside Explorer now, or
    // instead of it from the next sign-in - or back. Back closes the desktop;
    // Explorer's taskbar returns as it was, or Explorer itself starts.
    void UseAsSystemDesktop(UltraCanvas::DesktopReplacement how = UltraCanvas::DesktopReplacement::Beside);
    void SwitchBackToSystemDesktop();

    // ===== WHAT THE BARS DO (also reachable from the windows this opens) =====
    // The ULTRA OS settings button: starts UOS-Settings, the system's settings
    // application, where the desktop has its page (taskbar edge, wallpaper,
    // RAM disc, file manager, virtual desktops).
    // `page`: the UOS-Settings page to open on ("desktop/info-bar").
    void OpenSystemSettings(const std::string& page = std::string());
    void OpenAppStarter();
    // An application was started from the Apps window or a taskbar pin:
    // first in the Apps window's Recent (DesktopSettings::recentApps).
    void NoteAppStarted(const UltraCanvas::UCDesktopEntry& entry);
    void OpenTasks();
    // The RAM disc button: UltraFiler shows its RAM disc, or opens the New
    // RAM disc dialog when there is none (DesktopSettings::ramDiscPath is
    // kRamDiscFromFiler); a folder set in the settings is opened instead.
    void OpenRamDisc();
    void OpenFiler(const std::string& path = "");
    void TakeScreenshot();
    // UltraSnip with these arguments (--rectangle, --window, --screen,
    // --video); the plain whole-screen capture (TakeScreenshot) when the
    // program is not installed
    void OpenSnip(const std::vector<std::string>& arguments);
    void ShowScreenshotMenu(int windowX, int windowY);
    void StartSnipShortcut();
    void ToggleStickerboard(bool visible);
    void ToggleCategoryBoard(bool visible);
    // The Sticker board's window: over the work area, below the application
    // that was in front.
    void ShowCategoryBoard();
    // Its window and cards made, not shown ("Start with the desktop").
    void PrepareCategoryBoard();
    // settings_.startWithDesktop: each started unseen (minimized, or with no
    // window) unless running already; the Sticker board made ready.
    void StartStartupPrograms();
    void PlaceCategoryBoard();
    void SwitchToDesktop(int index);
    // The clock's panel (a click on the clock) and the loudspeaker's (a
    // click on the speaker icon).
    void OpenClockPanel();
    void OpenVolumePanel();
    // The info panel's radios: Wi-Fi and Bluetooth, operated from a panel
    // (UltraCanvasWireless).
    void OpenWifiPanel();
    void OpenBluetoothPanel();
    // Opens it (never closes it) - for a pairing a device starts.
    void ShowBluetoothPanel();
    void EnsureBluetoothPanel();
    // The info panel's devices: "usb", "keyboard", "webcam", "mic",
    // "battery" - what is attached or in use, with a way on to
    // DeviceExplorer or the Task Manager.
    void OpenDevicePanel(const std::string& device);
    // Lock the screen - the settings menu's "Lock screen" and Super+L.
    void LockScreen();
    // The taskbar's pinned applications, kept in the settings by key: the
    // program's path, or on Windows the shortcut or the Store application's
    // id - what stays the same across sessions.
    static std::string PinKey(const UltraCanvas::UCDesktopEntry& entry);
    bool IsPinned(const UltraCanvas::UCDesktopEntry& entry) const;
    void SetPinned(const UltraCanvas::UCDesktopEntry& entry, bool pinned);
    // Save the settings object and rebuild the bars from it.
    void ApplySettings();
    // UOS-Settings writes the desktop's settings file from its own process:
    // when the file changed under us, take the settings it owns (edge,
    // wallpaper, RAM disc, file manager, desktops) and rebuild. The sticky
    // notes stay the ones in memory - they are this process's.
    void CheckSettingsFile();

    DesktopSettings& Settings() { return settings_; }
    // The desktop's right-click menu, on the wallpaper and the notes'
    // ground: the wallpaper, the theme, the taskbar's edge, the display
    // settings and the desktop's settings. Window coordinates.
    void ShowDesktopMenu(int windowX, int windowY);
    // Files dropped on the wallpaper (or a desktop icon), at `at` in the
    // work area: onto the Sticker board's "Unsorted" category, which
    // shows, or into the Desktop folder (DesktopSettings::dropFilesOn).
    void DropFiles(const std::vector<std::string>& files, const UltraCanvas::Point2Df& at);
    // "board" or "desktop": where dropped files go; the desktop icons show
    // with "desktop".
    void SetDropTarget(const std::string& target);
    // Dragging desktop icons: the other programs' windows on this desktop
    // are read once when a drag starts, and the drag leaves the desktop as
    // a native file drag when the pointer (window coordinates) is over one.
    void NoteIconDragStart();
    bool PointOverOtherWindow(const UltraCanvas::Point2Di& windowPoint) const;
    // How many desktops the organiser shows right now: the window manager's
    // count where there is one, the settings' where there is none.
    int DesktopCount() const { return desktopCount_; }
    void SaveSettings();

private:
    // Starts the file manager (DesktopSettings::filerProgram) with these arguments.
    void LaunchFiler(const std::vector<std::string>& arguments);
    // ===== CONSTRUCTION =====
    void BuildLayout();
    void LayoutForSize(float width, float height);
    // Tell the window manager where the bars are, so maximised windows stop short of them.
    void ReserveBarEdges();
    int  DesiredDesktopCount() const;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTaskbar(bool vertical);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildRightBar();
    // The bars' colours in the settings' theme (dark or light).
    const BarPalette& Bars() const;
    // The menu entries that move the taskbar to another edge, and the
    // theme's; shared by the desktop's menu and the bars'.
    std::vector<UltraCanvas::MenuItemData> TaskbarEdgeItems();
    std::vector<UltraCanvas::MenuItemData> ThemeItems();
    // A right click on the bars' ground (not on a button): the taskbar's
    // edge, the theme and the desktop's settings.
    void ShowBarMenu(int windowX, int windowY);
    void InstallBarMenu(const std::shared_ptr<UltraCanvas::UltraCanvasToolbar>& group);
    void SetTaskbarEdge(TaskbarEdge edge);
    void SetTheme(const std::string& theme);
    // A picture for the wallpaper, through FileLoader's Open dialog.
    void ChooseWallpaper();
    void SetWallpaper(const std::string& path);   // "" = the default picture
    void OpenDisplaySettings();
    // The clock at the top of the right bar, white on black: the gauge
    // element's analog clock or the digital time, with or without the date
    // (right-click to choose), the whole date and time in the tooltip.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildClock();
    // The clock's right-click menu: its display types.
    void ShowClockMenu(int windowX, int windowY);
    void UpdateClock();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildWorkArea();
    // The installed applications, read once and again every five minutes:
    // the pins, the running apps' entries and the clipboard panel's targets.
    const std::vector<UltraCanvas::UCDesktopEntry>& Applications();
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> MakeGroup(const std::string& id, bool vertical,
                                                                bool inset);
    std::shared_ptr<UltraCanvas::UltraCanvasButton> AddBarButton(
            const std::shared_ptr<UltraCanvas::UltraCanvasToolbar>& group, const std::string& id,
            const std::string& tooltip, const std::string& icon, std::function<void()> onClick);
    std::shared_ptr<UltraCanvas::UltraCanvasButton> AddBarToggle(
            const std::shared_ptr<UltraCanvas::UltraCanvasToolbar>& group, const std::string& id,
            const std::string& tooltip, const std::string& icon, const std::string& text,
            std::function<void(bool)> onToggle);
    void StyleBarButton(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button);
    // Brings a bar's wave to kWaveGap from the icon of `group` next to it:
    // no padding on that side of the group, the wave over the button's margin.
    void TuckAgainstWave(const std::shared_ptr<UltraCanvas::UltraCanvasToolbar>& group,
                         const std::shared_ptr<UltraCanvas::UltraCanvasWaveSeparator>& wave,
                         bool vertical, bool waveAfterGroup);
    std::string IconPath(const std::string& name) const;
    // The settings' wallpaper, else DefaultWallpaperPath.
    std::string WallpaperPath() const;
    // ULTRA OS's own picture (media/images/desktop/ULTRA OS A.avif), else
    // the framework's landscape.
    static std::string DefaultWallpaperPath();
    // WallpaperPath into `wallpaper`; the landscape when it cannot be read.
    void LoadWallpaper(UltraCanvas::UltraCanvasImageElement& wallpaper) const;

    // ===== LIVE DATA =====
    void OnTimer();
    void RefreshWindows();
    // Running applications that do not fit the bar at the usual spacing move
    // closer together, and when that is not enough get a smaller highlight
    // around their icons; back to normal once they fit again.
    void FitRunningButtons();
    void RefreshDesktops();
    // The battery's icon: its level as a fill, a bolt while charging, green
    // while charging and red below 25 %; only redrawn when that changes.
    void UpdateBatteryIcon(const UltraCanvas::DesktopDeviceActivity& now);
    // The battery's icon, badge (unless switched off) and tooltip.
    void ApplyBattery(const UltraCanvas::DesktopDeviceActivity& now);
    // The battery's right-click menu: the percentage badge on or off, and
    // the battery panel. SetBatteryPercentShown saves the choice and shows
    // it at once (DesktopSettings::batteryPercent).
    void ShowBatteryMenu(int windowX, int windowY);
    // A right click on the info panel: "Hide this icon" over a service's
    // icon, "Info bar icons..." (UOS-Settings' page), the theme.
    void ShowInfoBarMenu(int windowX, int windowY, const std::string& itemId);
    void AddInfoBarItems(UltraCanvas::UltraCanvasMenu& menu, const std::string& itemId);
    // Switches one of the info panel's icons on or off, saves, rebuilds the bars.
    void SetInfoItemShown(const std::string& id, bool shown);
    void SetBatteryPercentShown(bool shown);
    void ApplyDeviceActivity(const UltraCanvas::DesktopDeviceActivity& now,
                             const UltraCanvas::DesktopDeviceActivity& before, bool haveBefore);
    void ApplyNotices();
    // The toasts: connected at start, kept clear of the bars wherever the
    // taskbar is.
    void StartNotifications();
    void PlaceNotifications();
    void StartDevicePoll();
    void StopDevicePoll();

    // ===== RUNNING APPS =====
    // One button per application (RunningGroupKey): a click on the button of
    // a single window raises or minimizes it; with several the previews
    // open, and the person picks the window there.
    void OnRunningToggled(const std::string& key, bool on);
    void HoverRunning(const std::string& key);
    void UnhoverRunning(const std::string& key);
    void ShowPreviews(const std::string& key);
    void HidePreviews();
    void SchedulePreviewsClose();
    void StopPreviewTimers();
    void ShowWindowMenu(uint64_t windowId, int windowX, int windowY);
    // An application's button with several windows: its windows, minimize
    // all, close all, pin.
    void ShowGroupMenu(const std::string& key, int windowX, int windowY);
    void ShowPinnedMenu(const UltraCanvas::UCDesktopEntry& entry, int windowX, int windowY);
    void ShowClipboardMenu(int windowX, int windowY);
    void ShowSettingsButtonMenu(int windowX, int windowY);
    // The shortcuts that reach the desktop from any window: a tap of the
    // Super key for the Applications window and Super+L for the lock -
    // wherever no other desktop owns them (Linux; Windows as the shell).
    void StartDesktopShortcuts();
    // The loudspeaker's volume and mute, for the speaker icon's words and
    // marker: read on the device poll's thread now and then, and here after
    // the volume panel changed it.
    void ReadSpeaker();
    // The info panel on a short screen: smaller items, and the least-used
    // services folded into the chevron's menu (kInfoFoldOrder).
    bool CompactInfoPanel() const;
    int InfoButtonSize() const;
    int InfoIconSize() const;
    void FitInfoPanel();
    void ApplySpeaker();
    // Once a second: a session desktop whose switch was turned off closes.
    void CheckDesktopSwitch();
    // Sign out, restart, shut down - asked first.
    void ConfirmEndSession(UltraCanvas::SessionEnd how);

    // ===== EVERY SCREEN =====
    // The desktop window covers the primary monitor (`area_`), or the whole
    // screen where the platform tells no monitors apart. While no other
    // desktop runs - Explorer beside this one - a monitor outside it gets a
    // desktop window of its own with the wallpaper, so no screen stays
    // black; followed as monitors come and go, and a window closed (Alt+F4)
    // opens again at the next check. A primary that moves is followed too:
    // the desktop window is made anew on the new one.
    static UltraCanvas::DesktopScreenInfo DesktopAreaNow(bool& oneOfSeveral);
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> CreateDesktopWindow(const UltraCanvas::DesktopScreenInfo& area,
                                                                        bool oneOfSeveral);
    void FollowPrimaryScreen();
    void CoverOtherScreens();
    // A point of the desktop window, in logical px, as a screen position in
    // physical px: what the panels beside the bars are placed by.
    int ScreenX(float logicalX) const;
    int ScreenY(float logicalY) const;
    // The window for otherScreens_[index]; `report` says a failure once.
    void OpenScreenWindow(size_t index, bool report);

    // ===== THE NOTIFICATION AREA (the shell session) =====
    void StartTray();
    // The tray icons on the info panel follow the host's list.
    void RefreshTray();
    void ShowTrayBalloon(const UltraCanvas::DesktopTrayIcon& icon);
    // An icon whose program hands its menu over as data (Linux): drawn here.
    void ShowTrayMenu(const std::string& iconId, const std::string& itemId, int windowX, int windowY);

    // ===== CLIPBOARD HISTORY =====
    void StartClipboardHistory();
    // From the bar (beside the button) or Super+V (centred).
    void ToggleClipboardPanel(bool besideButton);
    void ShowClipboardButtonMenu(int windowX, int windowY);
    void CopyHistoryEntry(int64_t entryId);
    void OpenClipboardApplication(const std::vector<std::string>& arguments);
    void SetClipboardRecording(bool on);
    void UpdateClipboardButton();
    void CheckClipboardHistory();

    // ===== STATE =====
    std::string settingsPath_;
    DesktopSettings settings_;
    // The settings file's time stamp as of our own last read or write; a
    // different one means another program wrote it.
    std::filesystem::file_time_type settingsFileTime_{};
    std::chrono::steady_clock::time_point nextSettingsCheck_{};
    bool sessionDesktop_ = false;
    bool shellSession_ = false;
    std::chrono::steady_clock::time_point nextSwitchCheck_{};
    std::string iconsDir_;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    // What the desktop window covers, in the screen's physical pixels.
    UltraCanvas::DesktopScreenInfo area_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> workArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> wallpaper_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> running_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> organiser_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> info_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> popupMenu_;
    std::shared_ptr<UltraDesktopStickerboard> stickerboard_;
    std::shared_ptr<UltraDesktopDesktopIcons> desktopIcons_;
    int desktopIconTicks_ = 0;
    std::vector<UltraCanvas::Rect2Di> otherWindowRects_;   // NoteIconDragStart
    std::shared_ptr<UltraDesktopCategoryBoard> categoryBoard_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> boardWindow_;
    UltraCanvas::Rect2Di boardRect_;    // where PlaceCategoryBoard last put it
    std::shared_ptr<UltraDesktopAppStarter> appStarter_;
    std::shared_ptr<UltraDesktopTasksWindow> tasks_;
    std::unique_ptr<UltraCanvas::UltraCanvasNotificationToastHost> toasts_;

    // The clock: the analog face or the digital time, and the date under it
    // (as DesktopSettings::clockStyle says), refreshed when the time shown
    // changes.
    std::shared_ptr<UltraCanvas::UltraCanvasGaugeDiagramElement> clock_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> clockTime_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> clockDate_;
    std::string clockStamp_;
    std::shared_ptr<UltraDesktopClockPanel> clockPanel_;
    std::shared_ptr<UltraDesktopVolumePanel> volumePanel_;
    // The services FitInfoPanel last moved to the end of the info panel.
    std::set<std::string> infoFolded_;
    // The overflow chevron last given the bar's look (it is the toolbar's).
    const UltraCanvas::UltraCanvasButton* styledChevron_ = nullptr;
    std::shared_ptr<UltraDesktopWifiPanel> wifiPanel_;
    std::shared_ptr<UltraDesktopBluetoothPanel> bluetoothPanel_;
    // One per device icon, by its item id; made on the first click.
    std::map<std::string, std::shared_ptr<UltraDesktopDevicePanel>> devicePanels_;
    UltraCanvas::UltraCanvasGlobalShortcut lockShortcut_;
    UltraCanvas::UltraCanvasGlobalShortcut snipShortcut_;       // Super+Shift+S: UltraSnip
    // The pinned applications as resolved against the installed ones at the
    // last rebuild; a pin whose application is gone is kept, and shows again
    // when it is back.
    std::vector<UltraCanvas::UCDesktopEntry> pinnedEntries_;
    // The loudspeaker as last read; `haveSpeaker_` false until the first reading.
    UltraCanvas::DesktopAudioOutput speaker_;
    bool haveSpeaker_ = false;

    // The clipboard history; null when the build has no database.
    std::unique_ptr<UltraCanvas::UltraCanvasClipboardHistory> history_;
    std::unique_ptr<UltraCanvas::UltraCanvasClipboardRecorder> recorder_;
    std::shared_ptr<UltraDesktopClipboardPanel> clipboardPanel_;
    UltraCanvas::UltraCanvasGlobalShortcut clipboardShortcut_;
    UltraCanvas::UltraCanvasButton* clipboardButton_ = nullptr;   // owned by the organiser
    uint64_t historyGeneration_ = 0;
    bool clipboardPaused_ = false;
    int ticksSinceHistoryCheck_ = 0;
    int ticksSincePrune_ = 0;
    // What the quick panel is opened for: the last window of another program
    // to have had the focus (the bar's button takes it from that window), and
    // the installed applications, to know what that program takes.
    uint64_t lastPasteWindow_ = 0;
    std::vector<UltraCanvas::UCDesktopEntry> applications_;
    std::chrono::steady_clock::time_point applicationsRead_{};

    // An application's windows on the bar: the group key (RunningGroupKey)
    // -> its toolbar item and its windows in ListWindows' order.
    struct RunningWindow {
        uint64_t id = 0;
        std::string title;
        std::string iconFile;
        bool active = false;
        bool minimized = false;
    };
    struct RunningGroup {
        std::string itemId;
        std::string name;        // the application, for the tooltip
        std::string letter;      // the button's text when there is no icon
        std::vector<RunningWindow> windows;
        int badge = 0;           // the count the button shows, 0 for none
        bool active = false;     // one of them has the focus
    };
    std::map<std::string, RunningGroup> runningGroups_;
    // How tightly the running applications are packed (FitRunningButtons):
    // 0 as usual, 1 closer together, 2 also in a smaller highlight.
    int runningDensity_ = 0;
    // What the battery's icon was last made for (UpdateBatteryIcon).
    std::string batteryIconKey_;
    std::map<uint64_t, std::string> runningTitles_;
    uint64_t nextRunningItem_ = 0;
    std::shared_ptr<UltraDesktopWindowPreviews> previews_;
    std::string hoveredGroup_;
    UltraCanvas::TimerId previewOpenTimer_ = 0;
    UltraCanvas::TimerId previewCloseTimer_ = 0;
    int currentDesktop_ = -1;
    int desktopCount_ = 0;
    int requestedDesktops_ = 0;   // the settings' choice, for the rebuild that applies it

    UltraCanvas::UltraCanvasDesktopShellMonitor shellMonitor_;
    std::atomic<bool> windowsDirty_{true};

    // The tray: the host's thread raises the flag, the UI timer applies it.
    // Item id -> the icon file and tooltip it shows; icon id -> the last
    // balloon shown.
    UltraCanvas::UltraCanvasTrayHost trayHost_;
    std::atomic<bool> trayDirty_{false};
    // As the shell: a tap of the Win key opens the Applications window, as
    // it would open Explorer's Start menu.
    UltraCanvas::UltraCanvasGlobalShortcut appsShortcut_;

    // The other screens' windows, each with its wallpaper.
    std::vector<UltraCanvas::DesktopScreenInfo> otherScreens_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasWindow>> screenWindows_;
    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasImageElement>> screenWallpapers_;
    std::chrono::steady_clock::time_point nextScreenCheck_{};
    std::map<std::string, std::string> trayIcons_;
    std::map<std::string, std::string> trayTooltips_;
    std::map<std::string, uint64_t> trayBalloons_;
    // Item id -> the icon's menu as data, for the right click.
    std::map<std::string, std::vector<UltraCanvas::DesktopTrayMenuItem>> trayMenus_;
    UltraCanvas::TimerId uiTimer_ = 0;
    int ticksSinceNotices_ = 0;
    int screenshotBadgeTicks_ = 0;

    // The device poll: a worker fills `pending_`, the UI timer takes it.
    std::thread devicePoll_;
    std::mutex deviceMutex_;
    std::condition_variable deviceWake_;
    bool devicePollRunning_ = false;
    bool devicePending_ = false;
    UltraCanvas::DesktopDeviceActivity pending_;
    // The speaker is read on the same thread every fifth poll (a sound
    // server round trip is dearer than a sysfs read).
    bool speakerPending_ = false;
    UltraCanvas::DesktopAudioOutput pendingSpeaker_;
    int pollsSinceSpeaker_ = 0;
    UltraCanvas::DesktopDeviceActivity lastActivity_;
    bool haveLastActivity_ = false;
};

} // namespace UltraDesktop
