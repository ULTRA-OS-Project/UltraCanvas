// UltraCanvas/OS/MSWindows/UltraMessage/UltraMessageWindowsToasts.h
// The windows-presenter's toasts: an application's notification shown as a
// Windows toast under that application's own AppUserModelID, so Windows heads
// it with the application's name and icon - "UltraMail" - and not with the
// process that hosts the broker (UltraFiler, say), as the notification-area
// balloon it replaces did.
//
// The ID is registered for the current user under
// HKCU\Software\Classes\AppUserModelId\<id> (DisplayName, IconUri), which
// Windows 10 1903 and later accept from a desktop program with no package
// identity and no Start-menu shortcut. Clicks, buttons and dismissals come back
// through the toast's own events while the broker runs. Internal to the
// presenter; the platform-neutral half (the ID, the XML, the arguments) is in
// UltraMessageAdapter.h, where the tests reach it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../../../core/UltraMessage/UltraMessageAdapter.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMessage {
namespace Internal {

class WindowsToasts {
public:
    // What the user did with a toast, on a WinRT thread.
    using ResponseHandler = std::function<void(const std::string& notificationId, PresenterResponse response,
                                               const std::string& actionId, bool hasDefaultAction)>;

    virtual ~WindowsToasts() = default;
    // True when the notification is handled: shown, or not shown because the
    // user turned the application's notifications off. False when it could
    // not be shown as a toast, for the presenter to show a balloon instead.
    virtual bool Show(const PresentedContent& content) = 0;
    // Takes the toast off the screen and out of the Action Center; false when
    // `notificationId` is no toast of these. Thread-safe.
    virtual bool Withdraw(const std::string& notificationId) = 0;
    // Thread-safe.
    virtual bool IsShowing(const std::string& notificationId) const = 0;
    // "toast" on success - the adapter's mode.
    static constexpr const char* kMode = "toast";
};

// Null where toasts cannot be shown: a build without C++/WinRT, or Windows
// before 10 1903. Call it on the thread that will show and withdraw them, and
// destroy it there: it joins that thread to the WinRT multithreaded apartment.
std::unique_ptr<WindowsToasts> CreateWindowsToasts(WindowsToasts::ResponseHandler onResponse);

} // namespace Internal
} // namespace UltraMessage
