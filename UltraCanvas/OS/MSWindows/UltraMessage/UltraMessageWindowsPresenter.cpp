// UltraCanvas/OS/MSWindows/UltraMessage/UltraMessageWindowsPresenter.cpp
// The windows-presenter adapter: the way out to the screen on Windows. A
// `system.notification` an application posts on the bus (UltraMail's "new
// mail", ...) is shown as a notification-area balloon (Shell_NotifyIconW
// with NIF_INFO), which Windows 10 and 11 present as a toast and keep in the
// Action Center. A click on it comes back as a `system.notification.action`
// (actionId "default") naming the bus message, so the application that
// posted it can act; a dismissal or action posted on the bus withdraws it.
//
// Balloons need no package identity, no Start-menu shortcut and no
// registration, so every desktop build has the presenter; the price is that
// Windows names the toast after the process that hosts the broker, so a
// notification from another application carries that application's name
// in its title. The windows-notification-listener adapter reads the toast
// back from the Action Center; it skips the ones noted here (NotePresented).
//
// A hidden window on the adapter's own thread owns the tray icon and pumps
// its messages; the icon exists only while a notification may still be
// clicked. Focus assist (quiet hours) is respected.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "../../../core/UltraMessage/UltraMessageAdapter.h"
#include "../../../core/UltraMessage/UltraMessageInternal.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace UltraMessage {
namespace Internal {

namespace {

constexpr const char* kAdapterName = "windows-presenter";
constexpr const wchar_t* kWindowClass = L"UltraMessagePresenterWindow";
constexpr UINT kIconId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;    // the tray icon's events (NOTIFYICON_VERSION_4)
constexpr UINT kShowMessage = WM_APP + 2;    // lParam: a PendingBalloon to show
constexpr UINT kWithdrawMessage = WM_APP + 3;// lParam: a std::string* naming the notification
constexpr UINT_PTR kRemoveIconTimer = 1;
// How long a notification stays clickable in the Action Center: the tray
// icon, which the click needs, goes this long after the last one.
constexpr UINT kIconLifetimeMs = 10 * 60 * 1000;

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), length, nullptr, nullptr);
    return out;
}

// The text as it fits a fixed NOTIFYICONDATAW field (with its terminator),
// never cutting a surrogate pair in half.
std::wstring Fit(const std::wstring& text, size_t capacity) {
    if (text.size() < capacity) return text;
    size_t length = capacity - 1;
    if (length > 0 && text[length - 1] >= 0xD800 && text[length - 1] <= 0xDBFF) --length;
    return text.substr(0, length);
}

template <size_t N>
void CopyField(wchar_t (&field)[N], const std::wstring& text) {
    const std::wstring fitted = Fit(text, N);
    std::copy(fitted.begin(), fitted.end(), field);
    field[fitted.size()] = L'\0';
}

// What one Present hands the window thread.
struct PendingBalloon {
    std::string notificationId;
    std::wstring title;
    std::wstring text;
    std::string urgency;
};

class WindowsPresenterAdapter final : public IAdapter {
public:
    ~WindowsPresenterAdapter() override { Stop(); }

    std::string Name() const override { return kAdapterName; }
    std::string Description() const override {
        return "Applications' notifications on screen as Windows notifications (a notification-area balloon, "
               "shown as a toast and kept in the Action Center), with the click reported back";
    }
    std::string Platform() const override { return "windows"; }

    UltraMsgAdapterState Start(IAdapterHost& host) override {
        Stop();
        host_ = &host;
        SetState(UltraMsgAdapterStatus::Starting, "creating the notification window", "", "");
        ready_ = false;
        thread_ = std::thread([this] { Run(); });
        std::unique_lock<std::mutex> lock(readyMutex_);
        readyCv_.wait_for(lock, std::chrono::seconds(5), [&] { return ready_; });
        return State();
    }

    void Stop() override {
        HWND window = window_.load();
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (thread_.joinable()) thread_.join();
        window_.store(nullptr);
        host_ = nullptr;
        SetState(UltraMsgAdapterStatus::Disabled, "", "", "");
    }

    UltraMsgAdapterState State() const override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return state_;
    }

    bool Present(const UltraMsgMessage& notification) override {
        HWND window = window_.load();
        if (!window) return false;
        SystemNotification n;
        if (!ParseSystemNotification(notification.body, n)) return false;
        auto* pending = new PendingBalloon;
        pending->notificationId = notification.envelope.id;
        // Windows heads the toast with the name of the process that hosts the
        // broker; a notification from any other application says whose it is.
        std::string title = n.summary;
        const int poster = notification.envelope.from.processId;
        if (poster > 0 && static_cast<DWORD>(poster) != GetCurrentProcessId() && !n.appName.empty())
            title = n.appName + ": " + title;
        pending->title = Widen(title);
        pending->text = Widen(n.body.empty() ? n.summary : n.body);
        pending->urgency = n.urgency;
        if (!PostMessageW(window, kShowMessage, 0, reinterpret_cast<LPARAM>(pending))) {
            delete pending;
            return false;
        }
        return true;
    }

    bool HandleAction(const UltraMsgMessage& action) override {
        if (!action.body.IsObject()) return false;
        const JSONValue* id = action.body.Find("notificationId");
        if (!id || !id->IsString()) return false;
        {
            std::lock_guard<std::mutex> lock(currentMutex_);
            if (current_.empty() || current_ != id->GetString()) return false;
        }
        HWND window = window_.load();
        if (!window) return true;
        auto* which = new std::string(id->GetString());
        if (!PostMessageW(window, kWithdrawMessage, 0, reinterpret_cast<LPARAM>(which))) delete which;
        return true;
    }

private:
    void SetState(UltraMsgAdapterStatus status, const std::string& message, const std::string& remedy,
                  const std::string& mode) {
        UltraMsgAdapterState state;
        state.status = status;
        state.message = message;
        state.remedy = remedy;
        state.mode = mode;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = state;
        }
        if (host_ && status != UltraMsgAdapterStatus::Starting) host_->ReportState(kAdapterName, state);
    }

    void SignalReady() {
        {
            std::lock_guard<std::mutex> lock(readyMutex_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    // ---- the window thread -------------------------------------------------

    void Run() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);   // fails harmlessly when a restart registered it already
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        LoadIcons();

        // A hidden top-level window rather than a message-only one: only a
        // top-level window hears TaskbarCreated when Explorer restarts.
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"UltraMessage", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, instance, this);
        if (!window) {
            SetState(UltraMsgAdapterStatus::Error,
                     "cannot create the notification window (error " + std::to_string(GetLastError()) + ")", "", "");
            SignalReady();
            FreeIcons();
            return;
        }
        window_.store(window);
        SetState(UltraMsgAdapterStatus::Running, "applications' notifications are shown as Windows notifications",
                 "", "balloon");
        SignalReady();

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Balloons still queued for the window never reach it now.
        while (PeekMessageW(&msg, nullptr, kShowMessage, kWithdrawMessage, PM_REMOVE)) {
            if (msg.message == kShowMessage) delete reinterpret_cast<PendingBalloon*>(msg.lParam);
            else if (msg.message == kWithdrawMessage) delete reinterpret_cast<std::string*>(msg.lParam);
        }
        window_.store(nullptr);
        FreeIcons();
    }

    // The tray and balloon icons: the host executable's own, else the stock
    // information icon.
    void LoadIcons() {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            // ("small" is a macro in some Windows headers.)
            HICON bigIcon = nullptr;
            HICON trayIcon = nullptr;
            ExtractIconExW(path, 0, &bigIcon, &trayIcon, 1);
            largeIcon_ = bigIcon;
            ownLarge_ = bigIcon != nullptr;
            smallIcon_ = trayIcon;
            ownSmall_ = trayIcon != nullptr;
        }
        if (!smallIcon_) {
            smallIcon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));   // IDI_INFORMATION, shared
            ownSmall_ = false;
        }
    }

    void FreeIcons() {
        if (ownLarge_ && largeIcon_) DestroyIcon(largeIcon_);
        if (ownSmall_ && smallIcon_) DestroyIcon(smallIcon_);
        largeIcon_ = smallIcon_ = nullptr;
        ownLarge_ = ownSmall_ = false;
    }

    NOTIFYICONDATAW IconData(HWND window) const {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kIconId;
        return data;
    }

    bool EnsureIcon(HWND window) {
        if (iconAdded_) return true;
        NOTIFYICONDATAW data = IconData(window);
        data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = smallIcon_;
        CopyField(data.szTip, L"Notifications");
        if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
        iconAdded_ = true;
        return true;
    }

    void RemoveIcon(HWND window) {
        KillTimer(window, kRemoveIconTimer);
        if (iconAdded_) {
            NOTIFYICONDATAW data = IconData(window);
            Shell_NotifyIconW(NIM_DELETE, &data);
        }
        iconAdded_ = false;
        std::lock_guard<std::mutex> lock(currentMutex_);
        current_.clear();
    }

    void ShowBalloon(HWND window, const PendingBalloon& balloon) {
        if (!EnsureIcon(window)) {
            SetState(UltraMsgAdapterStatus::Running,
                     "the notification area refused the icon a notification needs (is Explorer running?)", "",
                     "balloon");
            return;
        }
        NOTIFYICONDATAW data = IconData(window);
        data.uFlags = NIF_INFO;
        const std::wstring title = Fit(balloon.title, sizeof(data.szInfoTitle) / sizeof(wchar_t));
        const std::wstring text = Fit(balloon.text, sizeof(data.szInfo) / sizeof(wchar_t));
        CopyField(data.szInfoTitle, title);
        CopyField(data.szInfo, text);
        data.dwInfoFlags = NIIF_RESPECT_QUIET_TIME;
        if (balloon.urgency == "critical") {
            data.dwInfoFlags |= NIIF_WARNING;
        } else if (largeIcon_) {
            data.dwInfoFlags |= NIIF_USER | NIIF_LARGE_ICON;
            data.hBalloonIcon = largeIcon_;
        } else {
            data.dwInfoFlags |= NIIF_INFO;
        }
        if (balloon.urgency == "low") data.dwInfoFlags |= NIIF_NOSOUND;
        // Noted before it shows: the listener may poll the Action Center the
        // moment it appears.
        NotePresented(Narrow(title), Narrow(text));
        if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
            SetState(UltraMsgAdapterStatus::Running, "Windows refused a notification", "", "balloon");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(currentMutex_);
            current_ = balloon.notificationId;
        }
        SetTimer(window, kRemoveIconTimer, kIconLifetimeMs, nullptr);
    }

    void OnTrayEvent(HWND window, UINT event) {
        if (event != NIN_BALLOONUSERCLICK) return;
        std::string clicked;
        {
            std::lock_guard<std::mutex> lock(currentMutex_);
            clicked.swap(current_);
        }
        // Clicked: nothing more to click, so the icon goes.
        RemoveIcon(window);
        if (clicked.empty() || !host_) return;
        JSONValue body = JSONValue::MakeObject();
        body.Set("notificationId", clicked);
        body.Set("actionId", "default");
        body.Set("adapter", kAdapterName);
        host_->Publish(kAdapterName, UltraMsgTopics::SystemNotificationAction, body);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<WindowsPresenterAdapter*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wParam, lParam);

        if (message == kShowMessage) {
            std::unique_ptr<PendingBalloon> balloon(reinterpret_cast<PendingBalloon*>(lParam));
            if (balloon) self->ShowBalloon(window, *balloon);
            return 0;
        }
        if (message == kWithdrawMessage) {
            std::unique_ptr<std::string> which(reinterpret_cast<std::string*>(lParam));
            bool shown = false;
            {
                std::lock_guard<std::mutex> lock(self->currentMutex_);
                shown = which && *which == self->current_;
            }
            // Removing the icon takes the toast out of the Action Center too.
            if (shown) self->RemoveIcon(window);
            return 0;
        }
        if (message == kTrayMessage) {
            self->OnTrayEvent(window, LOWORD(lParam));
            return 0;
        }
        if (message == WM_TIMER && wParam == kRemoveIconTimer) {
            self->RemoveIcon(window);
            return 0;
        }
        if (self->taskbarCreated_ && message == self->taskbarCreated_) {
            // Explorer restarted and forgot the icon; the next notification
            // adds it again.
            self->iconAdded_ = false;
            return 0;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            self->RemoveIcon(window);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    IAdapterHost* host_ = nullptr;
    std::thread thread_;
    std::atomic<HWND> window_{nullptr};

    mutable std::mutex stateMutex_;
    UltraMsgAdapterState state_;
    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;

    // Window thread only.
    UINT taskbarCreated_ = 0;
    bool iconAdded_ = false;
    HICON smallIcon_ = nullptr;   // the tray icon
    HICON largeIcon_ = nullptr;   // the balloon's, when the executable has one
    bool ownSmall_ = false;
    bool ownLarge_ = false;

    // The notification on screen (or in the Action Center), read by
    // HandleAction on a broker thread.
    std::mutex currentMutex_;
    std::string current_;
};

} // namespace

std::unique_ptr<IAdapter> CreateWindowsPresenterAdapter() {
    return std::make_unique<WindowsPresenterAdapter>();
}

} // namespace Internal
} // namespace UltraMessage
