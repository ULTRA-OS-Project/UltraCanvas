// UltraCanvas/core/UltraMessage/UltraMessageAdapter.h
// The adapter interface (proposal §9): a broker-side plugin that bridges a
// platform-native channel onto UltraMessage topics — inbound (a native
// application's notification becomes a `system.notification`) and, for the
// notifications it produced, back out (the feed invoking one of its actions).
// Adapters live under UltraCanvas/OS/<Platform>/UltraMessage/ (platform code)
// or UltraCanvas/Plugins/UltraMessage/<name>/ (portable ones on UltraNet) and
// are registered by RegisterBuiltinAdapters below.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage/UltraMessage.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraMessage {
namespace Internal {

using UltraCanvas::JSONValue;

// What the broker offers an adapter.
class IAdapterHost {
public:
    virtual ~IAdapterHost() = default;

    // Publishes a notice on the bus under the adapter's own identity
    // (`org.ultraos.ultramessage.adapter.<name>`); journaled by the usual
    // rules. Returns the message id, empty when the broker refused it.
    virtual std::string Publish(const std::string& adapterName, const std::string& topic,
                                const JSONValue& body, const UltraMsgSendOptions& options = {}) = 0;

    // The adapter's state changed on its own (a name was lost, a permission
    // was granted); the broker records it for UltraMsg_GetAdapterState.
    virtual void ReportState(const std::string& adapterName, const UltraMsgAdapterState& state) = 0;
};

class IAdapter {
public:
    virtual ~IAdapter() = default;

    virtual std::string Name() const = 0;          // "freedesktop-notifications"
    virtual std::string Description() const = 0;
    virtual std::string Platform() const = 0;      // "linux" | "windows" | "macos" | "any"
    virtual bool EnabledByDefault() const { return true; }

    // Starts the adapter; it may run its own thread. The returned state is
    // the initial one (Running, NeedsPermission, Unavailable, ...). The host
    // outlives the adapter.
    virtual UltraMsgAdapterState Start(IAdapterHost& host) = 0;
    virtual void Stop() = 0;
    virtual UltraMsgAdapterState State() const = 0;

    // The feed acted on a notification: a `system.notification.action` or
    // `system.notification.dismissed` message whose `notificationId` names a
    // message this adapter may have published. Return false when it is not
    // one of this adapter's.
    virtual bool HandleAction(const UltraMsgMessage& action) { (void)action; return false; }
};

// Every adapter compiled into this build, in registration order. Defined in
// UltraMessageAdapters.cpp; each platform file contributes a factory.
std::vector<std::unique_ptr<IAdapter>> CreateBuiltinAdapters();

// The sender identity the host gives an adapter's messages.
UltraMsgSender AdapterSender(const std::string& adapterName);

} // namespace Internal
} // namespace UltraMessage
