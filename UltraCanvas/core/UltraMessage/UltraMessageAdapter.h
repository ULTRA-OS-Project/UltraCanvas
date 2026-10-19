// UltraCanvas/core/UltraMessage/UltraMessageAdapter.h
// The adapter interface (proposal §9): a broker-side plugin that bridges a
// platform-native channel onto UltraMessage topics — inbound (a native
// application's notification becomes a `system.notification`) and, for the
// notifications it produced, back out (the feed invoking one of its actions).
// Adapters live under UltraCanvas/OS/<Platform>/UltraMessage/ (platform code)
// or UltraCanvas/Plugins/UltraMessage/<name>/ (portable ones on UltraNet) and
// are registered by RegisterBuiltinAdapters below. A *presenter* adapter works
// the other way round: it puts the `system.notification`s applications post
// on screen through the platform's own notification service (Present):
// freedesktop-presenter, windows-presenter, macos-presenter.
// Version: 0.4.0 - the presenters' shared half: content, responses, what is on screen
// Version: 0.3.0 - Present: applications' notifications shown on screen
// Version: 0.2.1 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMessage/UltraMessage.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
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

    // An application on the bus posted a `system.notification` (not Silent):
    // put it on screen through the platform's notification service, and
    // report what the user does with it as `system.notification.action` /
    // `system.notification.dismissed` naming `notification.envelope.id`. A
    // notification that replaces an earlier one (UltraMsgFlag_Replace) updates
    // it where the platform can. Called on a broker thread, outside the
    // routing lock, before the message is journaled or delivered; must not
    // block on the screen. Return true when this adapter shows it (the broker
    // then asks no other and sets the body's `displayed` to this adapter's
    // name), false when it does not present or nothing on this desktop can
    // display it. Notifications adapters publish themselves are never
    // presented: they came from the screen already - an adapter that reads
    // them from a platform service which drew them sets `displayed` itself.
    virtual bool Present(const UltraMsgMessage& notification) { (void)notification; return false; }
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

// ---- shared by the presenters ----------------------------------------------

// A notification a presenter put on screen reappears in the platform's own
// notification list, where a listening adapter would read it back as a
// second `system.notification`. The presenter notes what it showed; the
// listener skips a toast with the same text for a few minutes. Thread-safe.
void NotePresented(const std::string& title, const std::string& text);
bool WasPresented(const std::string& title, const std::string& text);

// What a presenter shows for one notification, read from the message in one
// place so the platform-neutral half of a presenter is the same - and tested
// - on every platform, whichever presenter the build has.
constexpr size_t kMaxPresentedButtons = 3;
struct PresentedContent {
    std::string notificationId;   // the bus message (envelope.id)
    std::string replacesId;       // the notification it updates (UltraMsgFlag_Replace), else ""
    std::string appId;            // the posting application; groups its notifications
    std::string appName;
    std::string title;            // the summary
    // The posting application's name when another process posted it: the
    // platform heads the notification with the presenting process's name.
    std::string subtitle;
    std::string body;
    std::string iconFile;         // the absolute path the icon names (a path or file:// URI), else ""
    std::string urgency;          // "low" | "normal" | "critical"
    bool hasDefaultAction = false;              // a click on it is its "default" action
    std::vector<NotificationAction> buttons;    // its other actions, at most kMaxPresentedButtons
};
// False when the message is no system.notification.
bool ReadPresentedContent(const UltraMsgMessage& notification, int presenterProcessId, PresentedContent& out);

// A name for a set of buttons, the same for the same ids and labels in the
// same order, "plain" for none. A platform that registers each set once
// (macOS notification categories) keys it by this.
std::string ButtonSetKey(const std::vector<NotificationAction>& buttons);

// What the user did with a notification a presenter showed.
enum class PresenterResponse { Activated, Action, Dismissed };
// Publishes it on the bus: a click (Activated) as the `default` action when
// the notification has one, a button as its action, a dismissal as
// `system.notification.dismissed` (reason "dismissed"). Returns false when
// nothing is published (a click on a notification without a default action).
bool PublishPresenterResponse(IAdapterHost& host, const std::string& adapterName,
                              const std::string& notificationId, PresenterResponse response,
                              const std::string& actionId, bool hasDefaultAction);

// The notifications a presenter has on screen, by bus id and by the platform
// identifier each shows under. An update (UltraMsgFlag_Replace) keeps the
// identifier of the notification it replaces, so the platform changes that
// one in place. Thread-safe.
class PresentedNotifications {
public:
    // Records `notificationId` on screen and returns the platform identifier
    // it shows under: the replaced one's when `replacesId` is on screen, else
    // `proposed`.
    std::string Show(const std::string& notificationId, const std::string& replacesId,
                     const std::string& proposed);
    // The bus id showing under `nativeId`, forgotten; "" when none.
    std::string TakeByNative(const std::string& nativeId);
    // The platform identifier `notificationId` shows under, forgotten; ""
    // when it is not on screen.
    std::string TakeById(const std::string& notificationId);
    size_t Size() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::string> byId_;       // bus id -> platform identifier
    std::map<std::string, std::string> byNative_;   // platform identifier -> bus id
    std::deque<std::string> order_;                  // bus ids, oldest first
};

// The macOS presenter's fallback for a process that is no application bundle:
// the argument vector that runs `display notification` through osascript.
// The texts travel as script arguments, never inside the script, so nothing
// in them is AppleScript.
std::vector<std::string> AppleScriptNotificationCommand(const PresentedContent& content);

} // namespace Internal
} // namespace UltraMessage
