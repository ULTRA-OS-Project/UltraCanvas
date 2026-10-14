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
// panel, above its services, and their balloons become toasts - and its
// settings button offers sign out, restart and shut down, which Explorer's
// Start menu would otherwise.
// Version: 0.4.0 - Windows: instead of Explorer - the tray icons, sign out,
//                  restart and shut down
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
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasNotificationToastHost;
    class UltraCanvasClipboardHistory;
    class UltraCanvasClipboardRecorder;
    class UltraCanvasContainer;
    class UltraCanvasToolbar;
    class UltraCanvasImageElement;
    class UltraCanvasMenu;
    class UltraCanvasButton;
}

namespace UltraDesktop {

class UltraDesktopAppStarter;
class UltraDesktopClipboardPanel;
class UltraDesktopStickerboard;
class UltraDesktopCategoryBoard;
class UltraDesktopTasksWindow;

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
    void OpenSystemSettings();
    void OpenAppStarter();
    void OpenTasks();
    void OpenRamDisc();
    void OpenFiler(const std::string& path = "");
    void TakeScreenshot();
    void ToggleStickerboard(bool visible);
    void ToggleCategoryBoard(bool visible);
    void SwitchToDesktop(int index);
    // Save the settings object and rebuild the bars from it.
    void ApplySettings();
    // UOS-Settings writes the desktop's settings file from its own process:
    // when the file changed under us, take the settings it owns (edge,
    // wallpaper, RAM disc, file manager, desktops) and rebuild. The sticky
    // notes stay the ones in memory - they are this process's.
    void CheckSettingsFile();

    DesktopSettings& Settings() { return settings_; }
    // How many desktops the organiser shows right now: the window manager's
    // count where there is one, the settings' where there is none.
    int DesktopCount() const { return desktopCount_; }
    void SaveSettings();

private:
    // ===== CONSTRUCTION =====
    void BuildLayout();
    void LayoutForSize(float width, float height);
    // Tell the window manager where the bars are, so maximised windows stop short of them.
    void ReserveBarEdges();
    int  DesiredDesktopCount() const;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTaskbar(bool vertical);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildRightBar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildWorkArea();
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
    std::string IconPath(const std::string& name) const;
    std::string WallpaperPath() const;

    // ===== LIVE DATA =====
    void OnTimer();
    void RefreshWindows();
    void RefreshDesktops();
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
    void ShowWindowMenu(uint64_t windowId, int windowX, int windowY);
    void ShowClipboardMenu(int windowX, int windowY);
    void ShowSettingsButtonMenu(int windowX, int windowY);
    // Once a second: a session desktop whose switch was turned off closes.
    void CheckDesktopSwitch();
    // Sign out, restart, shut down - asked first.
    void ConfirmEndSession(UltraCanvas::SessionEnd how);

    // ===== THE NOTIFICATION AREA (the shell session) =====
    void StartTray();
    // The tray icons on the info panel follow the host's list.
    void RefreshTray();
    void ShowTrayBalloon(const UltraCanvas::DesktopTrayIcon& icon);

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
    std::string RunningItemId(uint64_t windowId) const;

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
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> workArea_;
    std::shared_ptr<UltraCanvas::UltraCanvasImageElement> wallpaper_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> running_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> organiser_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> info_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> popupMenu_;
    std::shared_ptr<UltraDesktopStickerboard> stickerboard_;
    std::shared_ptr<UltraDesktopCategoryBoard> categoryBoard_;
    std::shared_ptr<UltraDesktopAppStarter> appStarter_;
    std::shared_ptr<UltraDesktopTasksWindow> tasks_;
    std::unique_ptr<UltraCanvas::UltraCanvasNotificationToastHost> toasts_;

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

    // window id -> the toolbar item for it, in the order the user keeps
    std::map<uint64_t, std::string> runningItems_;
    std::map<uint64_t, std::string> runningTitles_;
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
    std::map<std::string, std::string> trayIcons_;
    std::map<std::string, std::string> trayTooltips_;
    std::map<std::string, uint64_t> trayBalloons_;
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
    UltraCanvas::DesktopDeviceActivity lastActivity_;
    bool haveLastActivity_ = false;
};

} // namespace UltraDesktop
