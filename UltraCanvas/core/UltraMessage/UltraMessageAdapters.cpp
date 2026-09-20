// UltraCanvas/core/UltraMessage/UltraMessageAdapters.cpp
// The registry of the adapters compiled into this build (§9). Each platform
// or plugin file contributes a factory; the build defines which exist.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageAdapter.h"
#include "UltraMessageInternal.h"

namespace UltraMessage {
namespace Internal {

#ifdef ULTRAMESSAGE_HAVE_GIO
// OS/Linux/UltraMessage/UltraMessageFreedesktopNotifications.cpp
std::unique_ptr<IAdapter> CreateFreedesktopNotificationsAdapter();
#endif

std::vector<std::unique_ptr<IAdapter>> CreateBuiltinAdapters() {
    std::vector<std::unique_ptr<IAdapter>> adapters;
#ifdef ULTRAMESSAGE_HAVE_GIO
    adapters.push_back(CreateFreedesktopNotificationsAdapter());
#endif
    return adapters;
}

UltraMsgSender AdapterSender(const std::string& adapterName) {
    UltraMsgSender sender;
    sender.appId = "org.ultraos.ultramessage.adapter." + adapterName;
    sender.instanceId = "adapter:" + adapterName;
    sender.processId = CurrentProcessId();
    sender.verified = true;
    sender.displayName = adapterName;
    return sender;
}

} // namespace Internal
} // namespace UltraMessage
