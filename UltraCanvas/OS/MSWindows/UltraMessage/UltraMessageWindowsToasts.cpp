// UltraCanvas/OS/MSWindows/UltraMessage/UltraMessageWindowsToasts.cpp
// See UltraMessageWindowsToasts.h. Built where the C++/WinRT headers are (the
// MSYS2 `cppwinrt` package or the Windows SDK), with the notification listener.
// Version: 0.2.0 - the activations carry the notification's id and launch
//                 (UltraCanvasNotificationActivation.h), for the application's
//                 own activator; the ID is listed for the uninstaller
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "UltraMessageWindowsToasts.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace UltraMessage {
namespace Internal {

namespace {

namespace WF = winrt::Windows::Foundation;
namespace WDX = winrt::Windows::Data::Xml::Dom;
namespace WUN = winrt::Windows::UI::Notifications;

// Windows 10 1903: the first to take an AppUserModelID registered in
// HKCU\Software\Classes\AppUserModelId from a program with no shortcut.
constexpr DWORD kFirstToastBuild = 18362;
constexpr const wchar_t* kGroup = L"ultramessage";
// The toasts whose events are still listened to; the oldest go first.
constexpr size_t kToastsKept = 64;

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}

// The build of Windows 10 or later, 0 before it. RtlGetVersion, because
// GetVersionEx answers 6.2 to a program without a compatibility manifest.
DWORD WindowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    auto rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!rtlGetVersion) return 0;
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) return 0;
    return info.dwMajorVersion >= 10 ? info.dwBuildNumber : 0;
}

bool SetRegistryText(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// The application's name and icon under its AppUserModelID, for the user:
// what Windows heads the application's toasts with.
bool RegisterToastApplication(const std::wstring& appUserModelId, const std::string& displayName,
                              const std::string& iconFile) {
    const std::wstring path = L"Software\\Classes\\AppUserModelId\\" + appUserModelId;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    bool ok = SetRegistryText(key, L"DisplayName", Widen(displayName));
    if (ok && !iconFile.empty()) ok = SetRegistryText(key, L"IconUri", Widen(iconFile));
    RegCloseKey(key);
    // Listed for the uninstaller (UltraCanvasNotificationActivation.h); the
    // application's own activator, when it registers one, puts its CLSID there.
    HKEY index = nullptr;
    if (ok && RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\UltraCanvas\\Notifications", 0, nullptr, 0,
                              KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &index, nullptr) == ERROR_SUCCESS) {
        if (RegQueryValueExW(index, appUserModelId.c_str(), nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            SetRegistryText(index, appUserModelId.c_str(), std::wstring());
        RegCloseKey(index);
    }
    return ok;
}

struct ShownToast {
    std::string notificationId;
    std::wstring appUserModelId;
    WUN::ToastNotifier notifier{nullptr};
    WUN::ToastNotification toast{nullptr};
};

// What the toasts' events reach, kept alive by them: an event may arrive
// after the toasts are gone, and then finds `stopped`.
struct ToastState {
    std::mutex mutex;
    bool stopped = false;
    WindowsToasts::ResponseHandler onResponse;
    std::map<std::string, std::string> tagById;   // bus id -> tag
    std::map<std::string, ShownToast> byTag;
    std::deque<std::string> order;                // tags, oldest first

    // Forgets the toast; true when it was still listened to.
    bool Forget(const std::string& notificationId) {
        auto it = tagById.find(notificationId);
        if (it == tagById.end()) return false;
        byTag.erase(it->second);
        tagById.erase(it);
        return true;
    }

    void Respond(const std::string& notificationId, PresenterResponse response, const std::string& actionId,
                 bool hasDefaultAction) {
        WindowsToasts::ResponseHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped || !Forget(notificationId)) return;
            handler = onResponse;
        }
        if (handler) handler(notificationId, response, actionId, hasDefaultAction);
    }
};

class WinRTToasts final : public WindowsToasts {
public:
    WinRTToasts(ResponseHandler onResponse, bool ownsApartment)
        : state_(std::make_shared<ToastState>()), ownsApartment_(ownsApartment) {
        state_->onResponse = std::move(onResponse);
    }

    ~WinRTToasts() override {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stopped = true;
            state_->byTag.clear();
            state_->tagById.clear();
            state_->order.clear();
        }
        if (ownsApartment_) winrt::uninit_apartment();
    }

    bool Show(const PresentedContent& content) override {
        const std::string appId = WindowsToastAppUserModelId(content.appId);
        if (appId.empty() || content.notificationId.empty()) return false;
        try {
            const std::wstring appUserModelId = Widen(appId);
            if (registered_.count(appId) == 0) {
                if (!RegisterToastApplication(appUserModelId, content.appName.empty() ? appId : content.appName,
                                              content.iconFile))
                    return false;
                registered_.insert(appId);
            }
            WUN::ToastNotifier notifier =
                WUN::ToastNotificationManager::CreateToastNotifier(winrt::hstring(appUserModelId));
            // Turned off - for the application, for the user, or by policy:
            // their choice, and no balloon goes round it.
            if (notifier.Setting() != WUN::NotificationSetting::Enabled) return true;

            WDX::XmlDocument document;
            document.LoadXml(winrt::to_hstring(WindowsToastXml(content)));
            WUN::ToastNotification toast(document);

            // An update shows under the tag of the toast it replaces, so
            // Windows changes that one in place. A bus id (26 characters) fits
            // the tag's 64.
            std::string tag = content.notificationId;
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                auto replaced = state_->tagById.find(content.replacesId);
                if (!content.replacesId.empty() && replaced != state_->tagById.end()) {
                    tag = replaced->second;
                    state_->tagById.erase(replaced);
                }
            }
            if (tag.size() > 64) tag.resize(64);
            toast.Tag(winrt::to_hstring(tag));
            toast.Group(kGroup);

            std::weak_ptr<ToastState> weak = state_;
            const std::string id = content.notificationId;
            const bool hasDefault = content.hasDefaultAction;
            toast.Activated([weak, id, hasDefault](const WUN::ToastNotification&, const WF::IInspectable& args) {
                std::string actionId;
                PresenterResponse response = PresenterResponse::Activated;
                if (auto activated = args.try_as<WUN::ToastActivatedEventArgs>())
                    response = WindowsToastResponse(winrt::to_string(activated.Arguments()), actionId);
                if (auto state = weak.lock()) state->Respond(id, response, actionId, hasDefault);
            });
            toast.Dismissed([weak, id, hasDefault](const WUN::ToastNotification&,
                                                   const WUN::ToastDismissedEventArgs& args) {
                // Timed out: it waits in the Action Center, still clickable.
                // Hidden by the application: withdrawn, already forgotten.
                if (args.Reason() != WUN::ToastDismissalReason::UserCanceled) return;
                if (auto state = weak.lock()) state->Respond(id, PresenterResponse::Dismissed, "", hasDefault);
            });

            // Noted before it shows: the notification listener may read the
            // Action Center the moment it appears.
            NotePresented(content.title, content.body);
            notifier.Show(toast);

            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->tagById[id] = tag;
            ShownToast& shown = state_->byTag[tag];
            shown.notificationId = id;
            shown.appUserModelId = appUserModelId;
            shown.notifier = notifier;
            shown.toast = toast;
            state_->order.push_back(tag);
            while (state_->order.size() > kToastsKept) {
                const std::string oldest = state_->order.front();
                state_->order.pop_front();
                auto it = state_->byTag.find(oldest);
                // A tag shown again since is the newer toast's.
                if (it != state_->byTag.end() &&
                    std::find(state_->order.begin(), state_->order.end(), oldest) == state_->order.end()) {
                    state_->tagById.erase(it->second.notificationId);
                    state_->byTag.erase(it);
                }
            }
            return true;
        } catch (const winrt::hresult_error&) {
            return false;
        }
    }

    bool Withdraw(const std::string& notificationId) override {
        ShownToast shown;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto it = state_->tagById.find(notificationId);
            if (it == state_->tagById.end()) return false;
            auto toast = state_->byTag.find(it->second);
            if (toast != state_->byTag.end()) shown = toast->second;
            state_->Forget(notificationId);
        }
        if (!shown.toast) return true;
        try {
            shown.notifier.Hide(shown.toast);
        } catch (const winrt::hresult_error&) {
            // Already off the screen.
        }
        try {
            WUN::ToastNotificationManager::History().Remove(shown.toast.Tag(), kGroup,
                                                            winrt::hstring(shown.appUserModelId));
        } catch (const winrt::hresult_error&) {
            // Not in the Action Center.
        }
        return true;
    }

    bool IsShowing(const std::string& notificationId) const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->tagById.count(notificationId) != 0;
    }

private:
    std::shared_ptr<ToastState> state_;
    std::set<std::string> registered_;   // the IDs registered by this process
    bool ownsApartment_ = false;
};

} // namespace

std::unique_ptr<WindowsToasts> CreateWindowsToasts(WindowsToasts::ResponseHandler onResponse) {
    if (WindowsBuild() < kFirstToastBuild) return nullptr;
    bool ownsApartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        ownsApartment = true;
    } catch (const winrt::hresult_error&) {
        // The thread is in an apartment already, which serves as well.
    }
    try {
        // The activation factory answers now, not at the first notification.
        (void)WUN::ToastNotificationManager::History();
    } catch (const winrt::hresult_error&) {
        if (ownsApartment) winrt::uninit_apartment();
        return nullptr;
    }
    return std::make_unique<WinRTToasts>(std::move(onResponse), ownsApartment);
}

} // namespace Internal
} // namespace UltraMessage
