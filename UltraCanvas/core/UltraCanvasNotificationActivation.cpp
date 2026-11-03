// core/UltraCanvasNotificationActivation.cpp
// UltraCanvasNotificationActivation.h where there is nothing to register: a
// notification's click comes back over the bus while the application runs.
// The Windows backend (OS/MSWindows/UltraCanvasWindowsNotificationActivation.cpp)
// is picked up by the platform glob.
// Version: 1.1.0 - ListNotificationRegistrations
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraCanvasNotificationActivation.h"

namespace UltraCanvas {

#if !defined(_WIN32) && !defined(_WIN64)
    bool RegisterNotificationActivator(const std::string&, std::function<void(const NotificationActivation&)>) {
        return false;
    }

    std::vector<NotificationRegistration> ListNotificationRegistrations() { return {}; }

    size_t RemoveNotificationRegistrations() { return 0; }
#endif

} // namespace UltraCanvas
