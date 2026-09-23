// UltraCanvas/OS/MSWindows/UltraMessage/UltraMessageWindowsNotificationListener.cpp
// The Windows notification listener adapter (proposal §9.2): every toast in
// the Action Center becomes a `system.notification`, with the chat and mail
// mirrors the other notification adapters produce. Built on the WinRT
// `Windows.UI.Notifications.Management.UserNotificationListener` through
// C++/WinRT (the MSYS2 `cppwinrt` package or the Windows SDK).
//
// The listener is read-only: a toast's buttons cannot be pressed from here,
// so a `system.notification.action` from the feed only clears the toast from
// the Action Center, as a dismissal does. Windows delivers no change event to
// a desktop process, so the adapter polls the listener (every 2 s) and
// publishes what is new, and a `system.notification.dismissed` for what went
// away. Access is the user's to grant: Settings > Privacy & security >
// Notifications; until then the state is `needs-permission` and the adapter
// re-checks every few seconds, so granting it needs no restart. On a
// process without package identity the listener may be unavailable
// altogether; the state says so.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "../../../core/UltraMessage/UltraMessageAdapter.h"
#include "../../../core/UltraMessage/UltraMessageInternal.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraMessage {
namespace Internal {

namespace {

namespace WF = winrt::Windows::Foundation;
namespace WUN = winrt::Windows::UI::Notifications;
namespace WUNM = winrt::Windows::UI::Notifications::Management;

constexpr const char* kAdapterName = "windows-notification-listener";
constexpr auto kPollInterval = std::chrono::seconds(2);
constexpr auto kAccessRecheckInterval = std::chrono::seconds(5);

std::string Utf8(const winrt::hstring& text) {
    return winrt::to_string(text);
}

std::string ErrorText(const winrt::hresult_error& error) {
    char code[16];
    std::snprintf(code, sizeof code, "0x%08X", static_cast<unsigned>(error.code()));
    std::string message = Utf8(error.message());
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
        message.pop_back();
    return message.empty() ? std::string(code) : message + " (" + code + ")";
}

class WindowsNotificationListenerAdapter final : public IAdapter {
public:
    ~WindowsNotificationListenerAdapter() override { Stop(); }

    std::string Name() const override { return kAdapterName; }
    std::string Description() const override {
        return "Every toast in the Action Center (Windows.UI.Notifications.Management): read through the "
               "notification listener once the user allowed it in Settings";
    }
    std::string Platform() const override { return "windows"; }

    UltraMsgAdapterState Start(IAdapterHost& host) override {
        Stop();
        host_ = &host;
        SetState(UltraMsgAdapterStatus::Starting, "opening the notification listener", "", "");
        stopping_.store(false);
        ready_ = false;
        thread_ = std::thread([this] { Run(); });
        std::unique_lock<std::mutex> lock(readyMutex_);
        readyCv_.wait_for(lock, std::chrono::seconds(5), [&] { return ready_; });
        return State();
    }

    void Stop() override {
        stopping_.store(true);
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            byId_.clear();
            byUlid_.clear();
        }
        host_ = nullptr;
        SetState(UltraMsgAdapterStatus::Disabled, "", "", "");
    }

    UltraMsgAdapterState State() const override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return state_;
    }

    // The feed acted on or dismissed one of ours: the toast leaves the
    // Action Center. Its buttons cannot be pressed through the listener.
    bool HandleAction(const UltraMsgMessage& action) override {
        if (!action.body.IsObject()) return false;
        const JSONValue* idValue = action.body.Find("notificationId");
        if (!idValue || !idValue->IsString()) return false;
        uint32_t id = 0;
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            auto it = byUlid_.find(idValue->GetString());
            if (it == byUlid_.end()) return false;
            id = it->second;
        }
        Forget(id);
        std::lock_guard<std::mutex> lock(listenerMutex_);
        if (listener_) {
            try {
                listener_.RemoveNotification(id);
            } catch (const winrt::hresult_error&) {
                // Already gone: nothing to clear.
            }
        }
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

    // Sleeps `duration` unless Stop() wakes it. Returns false when stopping.
    bool Pause(std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wake_.wait_for(lock, duration, [&] { return stopping_.load(); });
        return !stopping_.load();
    }

    // Applies an access status; true when notifications may be read.
    bool ApplyAccess(WUNM::UserNotificationListenerAccessStatus status) {
        switch (status) {
            case WUNM::UserNotificationListenerAccessStatus::Allowed:
                SetState(UltraMsgAdapterStatus::Running, "reading the Action Center through the notification listener",
                         "", "listener");
                return true;
            case WUNM::UserNotificationListenerAccessStatus::Denied:
                SetState(UltraMsgAdapterStatus::NeedsPermission,
                         "the user denied notification access to this application",
                         "Settings > Privacy & security > Notifications: allow this application to access notifications",
                         "");
                return false;
            case WUNM::UserNotificationListenerAccessStatus::Unspecified:
            default:
                SetState(UltraMsgAdapterStatus::NeedsPermission, "notification access has not been granted yet",
                         "Settings > Privacy & security > Notifications: allow this application to access notifications",
                         "");
                return false;
        }
    }

    // ---- the adapter thread ------------------------------------------------

    void Run() {
        bool allowed = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error&) {
            // Already initialized on this thread with another model: usable.
        }
        try {
            WUNM::UserNotificationListener listener = WUNM::UserNotificationListener::Current();
            {
                std::lock_guard<std::mutex> lock(listenerMutex_);
                listener_ = listener;
            }
            allowed = ApplyAccess(listener.RequestAccessAsync().get());
        } catch (const winrt::hresult_error& error) {
            SetState(UltraMsgAdapterStatus::Unavailable,
                     "the notification listener is not available to this process: " + ErrorText(error),
                     "the listener needs Windows 10 1607 or later and, on some builds, an application with package "
                     "identity (MSIX)",
                     "");
            SignalReady();
            std::lock_guard<std::mutex> lock(listenerMutex_);
            listener_ = nullptr;
            return;
        }
        SignalReady();

        while (!stopping_.load()) {
            if (!allowed) {
                if (!Pause(kAccessRecheckInterval)) break;
                try {
                    allowed = ApplyAccess(listener_.GetAccessStatus());
                } catch (const winrt::hresult_error& error) {
                    SetState(UltraMsgAdapterStatus::Error, "cannot read the notification access status: " + ErrorText(error),
                             "", "");
                }
                continue;
            }
            try {
                Poll();
            } catch (const winrt::hresult_error& error) {
                SetState(UltraMsgAdapterStatus::Error, "reading the Action Center failed: " + ErrorText(error), "",
                         "listener");
            }
            if (!Pause(kPollInterval)) break;
        }
        std::lock_guard<std::mutex> lock(listenerMutex_);
        listener_ = nullptr;
    }

    // One pass over the Action Center: publish what is new, report what went
    // away. The first pass publishes what already sits there — those are the
    // unread toasts the feed has not seen.
    void Poll() {
        WUNM::UserNotificationListener listener{nullptr};
        {
            std::lock_guard<std::mutex> lock(listenerMutex_);
            listener = listener_;
        }
        if (!listener) return;
        WF::Collections::IVectorView<WUN::UserNotification> toasts =
            listener.GetNotificationsAsync(WUN::NotificationKinds::Toast).get();
        std::set<uint32_t> present;
        for (const WUN::UserNotification& toast : toasts) {
            const uint32_t id = toast.Id();
            present.insert(id);
            bool known = false;
            {
                std::lock_guard<std::mutex> lock(idsMutex_);
                known = byId_.count(id) != 0;
            }
            if (!known) Publish(toast);
        }
        std::vector<std::pair<uint32_t, std::string>> gone;
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            for (const auto& [id, ulid] : byId_)
                if (!present.count(id)) gone.push_back({id, ulid});
        }
        for (const auto& [id, ulid] : gone) {
            Forget(id);
            if (!host_) continue;
            JSONValue body = JSONValue::MakeObject();
            body.Set("notificationId", ulid);
            body.Set("reason", "removed");
            host_->Publish(kAdapterName, UltraMsgTopics::SystemNotificationDismissed, body);
        }
    }

    void Publish(const WUN::UserNotification& toast) {
        if (!host_) return;
        SystemNotification n;
        n.origin = "windows-listener";
        try {
            winrt::Windows::ApplicationModel::AppInfo info = toast.AppInfo();
            if (info) {
                n.appId = Utf8(info.AppUserModelId());
                auto display = info.DisplayInfo();
                if (display) n.appName = Utf8(display.DisplayName());
            }
        } catch (const winrt::hresult_error&) {
            // An application without display info: the toast still counts.
        }
        std::vector<std::string> texts;
        try {
            WUN::Notification notification = toast.Notification();
            WUN::NotificationVisual visual = notification ? notification.Visual() : nullptr;
            WUN::NotificationBinding binding =
                visual ? visual.GetBinding(WUN::KnownNotificationBindings::ToastGeneric()) : nullptr;
            if (binding) {
                for (const WUN::AdaptiveNotificationText& text : binding.GetTextElements()) {
                    std::string line = Utf8(text.Text());
                    if (!line.empty()) texts.push_back(line);
                }
            }
        } catch (const winrt::hresult_error&) {
        }
        if (!texts.empty()) {
            n.summary = texts.front();
            for (size_t i = 1; i < texts.size(); ++i) {
                if (!n.body.empty()) n.body += "\n";
                n.body += texts[i];
            }
        }
        if (n.summary.empty()) n.summary = n.appName.empty() ? "Notification" : n.appName;
        n.category = CategoryForAppKind(GuessAppKind(n.appId, n.appName));

        JSONValue body = MakeSystemNotification(n);
        body.Set("adapter", kAdapterName);
        body.Set("nativeId", static_cast<int64_t>(toast.Id()));
        try {
            const time_t created = winrt::clock::to_time_t(toast.CreationTime());
            if (created > 0) body.Set("createdMs", static_cast<int64_t>(created) * 1000);
        } catch (const winrt::hresult_error&) {
        }

        const std::string ulid = host_->Publish(kAdapterName, UltraMsgTopics::SystemNotification, body);
        if (ulid.empty()) return;
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            byId_[toast.Id()] = ulid;
            byUlid_[ulid] = toast.Id();
        }
        PublishMirror(*host_, kAdapterName, n, ulid);
    }

    void Forget(uint32_t id) {
        std::lock_guard<std::mutex> lock(idsMutex_);
        auto it = byId_.find(id);
        if (it == byId_.end()) return;
        byUlid_.erase(it->second);
        byId_.erase(it);
    }

    IAdapterHost* host_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex stateMutex_;
    UltraMsgAdapterState state_;

    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;

    std::mutex wakeMutex_;
    std::condition_variable wake_;

    std::mutex listenerMutex_;
    WUNM::UserNotificationListener listener_{nullptr};

    std::mutex idsMutex_;
    std::map<uint32_t, std::string> byId_;     // Action Center id -> journal / bus id
    std::map<std::string, uint32_t> byUlid_;
};

} // namespace

std::unique_ptr<IAdapter> CreateWindowsNotificationListenerAdapter() {
    return std::make_unique<WindowsNotificationListenerAdapter>();
}

} // namespace Internal
} // namespace UltraMessage
