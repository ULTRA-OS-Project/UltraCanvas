// UltraCanvas/core/UltraMessage/UltraMessageAdapter.h
// The adapter interface (proposal §9): a broker-side plugin that bridges a
// platform-native channel onto UltraMessage topics — inbound (a native
// application's notification becomes a `system.notification`) and, for the
// notifications it produced, back out (the feed invoking one of its actions).
// Adapters live under UltraCanvas/OS/<Platform>/UltraMessage/ (platform code)
// or UltraCanvas/Plugins/UltraMessage/<name>/ (portable ones on UltraNet) and
// are registered by RegisterBuiltinAdapters below.
// Version: 0.2.1 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageEndpoint.h"

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

// ---- shared by the notification adapters (§9.1 mirrors) --------------------

// What kind of application produced a native notification, guessed from its
// identity (desktop entry, AppUserModelId, bundle id, display name) for the
// platforms and applications that give no category hint.
enum class AppKind { Unknown, Messenger, Mail };
AppKind GuessAppKind(const std::string& appId, const std::string& appName);
// The freedesktop category the mirror rules key on: "im.received",
// "email.arrived", or "" for Unknown.
std::string CategoryForAppKind(AppKind kind);

// A chat toast (category `im.received*`) also becomes a `messaging.message`,
// a mail toast (`email*`) a `mail.message`, so the feed groups them with the
// first-class sources. What a toast carries is heuristic: the summary is the
// sender (chat) or the subject line (mail). `notificationId` is the id of the
// `system.notification` the mirror points back to (`mirrorOf`). Returns the
// mirror's id, empty when the category mirrors to nothing.
std::string PublishMirror(IAdapterHost& host, const std::string& adapterName,
                          const SystemNotification& n, const std::string& notificationId);

std::string Lowercase(std::string text);
std::string FirstLine(const std::string& text);

} // namespace Internal
} // namespace UltraMessage
