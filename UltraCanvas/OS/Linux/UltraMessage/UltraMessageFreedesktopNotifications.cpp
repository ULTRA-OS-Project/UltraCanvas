// UltraCanvas/OS/Linux/UltraMessage/UltraMessageFreedesktopNotifications.cpp
// The freedesktop-notifications adapter (proposal §9.1): on the session bus
// it owns `org.freedesktop.Notifications` and so *is* the notification
// server — every desktop application's toast (Telegram Desktop, Signal,
// Thunderbird, Slack, ...) arrives as a Notify() call and becomes a
// `system.notification` on the bus, mirrored to `messaging.message` or
// `mail.message` when the application set the freedesktop category.
// Where a desktop already runs a notification server (GNOME, KDE) the name
// cannot be taken; the adapter then falls back to monitor mode
// (org.freedesktop.DBus.Monitoring.BecomeMonitor on a private connection)
// and reads Notify() calls passively — no actions, no replies.
//
// GDBus (GIO), on a private GMainContext in the adapter's own thread, so an
// application's own GLib main loop is never touched.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "../../../core/UltraMessage/UltraMessageAdapter.h"
#include "../../../core/UltraMessage/UltraMessageInternal.h"
#include "UltraMessage/UltraMessageEndpoint.h"

#include <gio/gio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace UltraMessage {
namespace Internal {

namespace {

constexpr const char* kAdapterName = "freedesktop-notifications";
constexpr const char* kBusName = "org.freedesktop.Notifications";
constexpr const char* kObjectPath = "/org/freedesktop/Notifications";
constexpr const char* kInterface = "org.freedesktop.Notifications";
constexpr const char* kSpecVersion = "1.2";

constexpr const char* kIntrospectionXml =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg type='s' name='app_name' direction='in'/>"
    "      <arg type='u' name='replaces_id' direction='in'/>"
    "      <arg type='s' name='app_icon' direction='in'/>"
    "      <arg type='s' name='summary' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='as' name='actions' direction='in'/>"
    "      <arg type='a{sv}' name='hints' direction='in'/>"
    "      <arg type='i' name='expire_timeout' direction='in'/>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg type='as' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg type='s' name='name' direction='out'/>"
    "      <arg type='s' name='vendor' direction='out'/>"
    "      <arg type='s' name='version' direction='out'/>"
    "      <arg type='s' name='spec_version' direction='out'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='u' name='reason'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='s' name='action_key'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

// NotificationClosed reasons, per the specification.
[[maybe_unused]] constexpr guint kReasonExpired = 1;
constexpr guint kReasonDismissed = 2;
constexpr guint kReasonClosedByCall = 3;

std::string StripDesktopSuffix(std::string entry) {
    const std::string suffix = ".desktop";
    if (entry.size() > suffix.size() && entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) == 0)
        entry.erase(entry.size() - suffix.size());
    return entry;
}

std::string FirstLine(const std::string& text) {
    const size_t nl = text.find('\n');
    return nl == std::string::npos ? text : text.substr(0, nl);
}

std::string Lowercase(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// One Notify() call, decoded.
struct Toast {
    std::string appName;
    guint32 replacesId = 0;
    std::string appIcon;
    std::string summary;
    std::string body;
    std::vector<std::pair<std::string, std::string>> actions;   // (key, label)
    std::string category;
    std::string desktopEntry;
    std::string imagePath;
    int urgency = 1;
    int senderPid = 0;
    gint32 expireTimeout = -1;
};

bool DecodeToast(GVariant* parameters, Toast& out) {
    if (!parameters || !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(susssasa{sv}i)"))) return false;
    const gchar* appName = nullptr;
    const gchar* appIcon = nullptr;
    const gchar* summary = nullptr;
    const gchar* body = nullptr;
    GVariantIter* actions = nullptr;
    GVariantIter* hints = nullptr;
    g_variant_get(parameters, "(&su&s&s&sasa{sv}i)", &appName, &out.replacesId, &appIcon, &summary, &body,
                  &actions, &hints, &out.expireTimeout);
    out.appName = appName ? appName : "";
    out.appIcon = appIcon ? appIcon : "";
    out.summary = summary ? summary : "";
    out.body = body ? body : "";

    const gchar* item = nullptr;
    std::vector<std::string> flat;
    while (g_variant_iter_next(actions, "&s", &item)) flat.emplace_back(item ? item : "");
    g_variant_iter_free(actions);
    for (size_t i = 0; i + 1 < flat.size(); i += 2) out.actions.emplace_back(flat[i], flat[i + 1]);

    const gchar* key = nullptr;
    GVariant* value = nullptr;
    while (g_variant_iter_next(hints, "{&sv}", &key, &value)) {
        const std::string name = key ? key : "";
        if (name == "category" && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            out.category = g_variant_get_string(value, nullptr);
        else if (name == "desktop-entry" && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            out.desktopEntry = g_variant_get_string(value, nullptr);
        else if (name == "image-path" && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            out.imagePath = g_variant_get_string(value, nullptr);
        else if (name == "urgency" && g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE))
            out.urgency = static_cast<int>(g_variant_get_byte(value));
        else if (name == "sender-pid") {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) out.senderPid = static_cast<int>(g_variant_get_int64(value));
            else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) out.senderPid = g_variant_get_int32(value);
            else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) out.senderPid = static_cast<int>(g_variant_get_uint32(value));
        }
        g_variant_unref(value);
    }
    g_variant_iter_free(hints);
    return true;
}

class FreedesktopNotificationsAdapter final : public IAdapter {
public:
    ~FreedesktopNotificationsAdapter() override { Stop(); }

    std::string Name() const override { return kAdapterName; }
    std::string Description() const override {
        return "Every desktop application's notification (org.freedesktop.Notifications): served by "
               "UltraMessage where the name is free, read passively where another server owns it";
    }
    std::string Platform() const override { return "linux"; }

    UltraMsgAdapterState Start(IAdapterHost& host) override {
        Stop();
        host_ = &host;
        SetState(UltraMsgAdapterStatus::Starting, "connecting to the session bus", "", "");
        stopping_.store(false);
        ready_ = false;
        thread_ = std::thread([this] { Run(); });
        std::unique_lock<std::mutex> lock(readyMutex_);
        readyCv_.wait_for(lock, std::chrono::seconds(5), [&] { return ready_; });
        return State();
    }

    void Stop() override {
        stopping_.store(true);
        if (loop_) g_main_loop_quit(loop_);
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

    bool HandleAction(const UltraMsgMessage& action) override {
        if (!action.body.IsObject()) return false;
        const JSONValue* idValue = action.body.Find("notificationId");
        if (!idValue || !idValue->IsString()) return false;
        guint32 id = 0;
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            auto it = byUlid_.find(idValue->GetString());
            if (it == byUlid_.end()) return false;
            id = it->second;
        }
        if (mode_ != Mode::Server || !connection_) return true;   // ours, but nothing to signal
        if (action.envelope.topic == UltraMsgTopics::SystemNotificationAction) {
            const JSONValue* key = action.body.Find("actionId");
            EmitSignal("ActionInvoked", g_variant_new("(us)", id, key && key->IsString() ? key->GetString().c_str() : ""));
            EmitSignal("NotificationClosed", g_variant_new("(uu)", id, kReasonDismissed));
        } else {
            EmitSignal("NotificationClosed", g_variant_new("(uu)", id, kReasonDismissed));
        }
        Forget(id);
        return true;
    }

private:
    enum class Mode { None, Server, Monitor };

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

    // ---- the adapter thread ------------------------------------------------

    void Run() {
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);
        loop_ = g_main_loop_new(context_, FALSE);

        GError* error = nullptr;
        connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!connection_) {
            SetState(UltraMsgAdapterStatus::Unavailable,
                     std::string("no session bus: ") + (error ? error->message : "unknown error"),
                     "start the desktop session bus (dbus-daemon --session) or set DBUS_SESSION_BUS_ADDRESS", "");
            if (error) g_error_free(error);
            SignalReady();
            Cleanup();
            return;
        }

        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospectionXml, &error);
        if (node) {
            static const GDBusInterfaceVTable vtable = {&OnMethodCall, nullptr, nullptr, {nullptr}};
            registrationId_ = g_dbus_connection_register_object(connection_, kObjectPath, node->interfaces[0],
                                                                &vtable, this, nullptr, &error);
            g_dbus_node_info_unref(node);
        }
        if (!registrationId_) {
            SetState(UltraMsgAdapterStatus::Error,
                     std::string("cannot register the notification object: ") + (error ? error->message : ""), "", "");
            if (error) g_error_free(error);
            SignalReady();
            Cleanup();
            return;
        }

        ownerId_ = g_bus_own_name_on_connection(connection_, kBusName, G_BUS_NAME_OWNER_FLAGS_NONE,
                                                &OnNameAcquired, &OnNameLost, this, nullptr);
        g_main_loop_run(loop_);
        Cleanup();
    }

    void Cleanup() {
        if (monitorConnection_) {
            if (filterId_) g_dbus_connection_remove_filter(monitorConnection_, filterId_);
            filterId_ = 0;
            g_object_unref(monitorConnection_);
            monitorConnection_ = nullptr;
        }
        if (ownerId_) {
            g_bus_unown_name(ownerId_);
            ownerId_ = 0;
        }
        if (connection_) {
            if (registrationId_) g_dbus_connection_unregister_object(connection_, registrationId_);
            registrationId_ = 0;
            g_object_unref(connection_);
            connection_ = nullptr;
        }
        if (loop_) {
            g_main_loop_unref(loop_);
            loop_ = nullptr;
        }
        if (context_) {
            g_main_context_pop_thread_default(context_);
            g_main_context_unref(context_);
            context_ = nullptr;
        }
        mode_ = Mode::None;
    }

    static void OnNameAcquired(GDBusConnection*, const gchar*, gpointer userData) {
        auto* self = static_cast<FreedesktopNotificationsAdapter*>(userData);
        self->mode_ = Mode::Server;
        self->SetState(UltraMsgAdapterStatus::Running,
                       "serving org.freedesktop.Notifications: every application's notifications arrive here",
                       "", "server");
        self->SignalReady();
    }

    static void OnNameLost(GDBusConnection* connection, const gchar*, gpointer userData) {
        auto* self = static_cast<FreedesktopNotificationsAdapter*>(userData);
        if (self->stopping_.load()) return;
        if (self->mode_ == Mode::Server) {
            // Lost after owning it: another server took over. Fall back.
            self->mode_ = Mode::None;
        }
        if (!connection) {
            self->SetState(UltraMsgAdapterStatus::Unavailable, "the session bus connection closed", "", "");
            self->SignalReady();
            return;
        }
        self->StartMonitorMode();
        self->SignalReady();
    }

    // Monitor mode: a second, private connection that the bus turns into a
    // monitor. From then on it only receives, so signals and replies stay on
    // the first connection (which has no name and is otherwise idle).
    void StartMonitorMode() {
        GError* error = nullptr;
        gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!address) {
            SetState(UltraMsgAdapterStatus::Unavailable,
                     std::string("another notification server owns the name and the bus address is unknown: ") +
                         (error ? error->message : ""), "", "");
            if (error) g_error_free(error);
            return;
        }
        monitorConnection_ = g_dbus_connection_new_for_address_sync(
            address,
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        g_free(address);
        if (!monitorConnection_) {
            SetState(UltraMsgAdapterStatus::Unavailable,
                     std::string("another notification server owns the name; cannot open a monitor connection: ") +
                         (error ? error->message : ""), "", "");
            if (error) g_error_free(error);
            return;
        }
        filterId_ = g_dbus_connection_add_filter(monitorConnection_, &OnMonitorMessage, this, nullptr);

        GVariantBuilder rules;
        g_variant_builder_init(&rules, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&rules, "s", "type='method_call',interface='org.freedesktop.Notifications',member='Notify'");
        GVariant* reply = g_dbus_connection_call_sync(
            monitorConnection_, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring",
            "BecomeMonitor", g_variant_new("(asu)", &rules, 0u), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
        if (!reply) {
            SetState(UltraMsgAdapterStatus::NeedsPermission,
                     std::string("another notification server owns the name; the bus refused monitor mode: ") +
                         (error ? error->message : ""),
                     "allow this user to monitor the session bus, or stop the other notification server", "");
            if (error) g_error_free(error);
            g_dbus_connection_remove_filter(monitorConnection_, filterId_);
            filterId_ = 0;
            g_object_unref(monitorConnection_);
            monitorConnection_ = nullptr;
            return;
        }
        g_variant_unref(reply);
        mode_ = Mode::Monitor;
        SetState(UltraMsgAdapterStatus::Running,
                 "another notification server owns org.freedesktop.Notifications; reading its Notify calls "
                 "passively — actions are not available",
                 "", "monitor");
    }

    static GDBusMessage* OnMonitorMessage(GDBusConnection*, GDBusMessage* message, gboolean incoming,
                                          gpointer userData) {
        auto* self = static_cast<FreedesktopNotificationsAdapter*>(userData);
        if (!incoming || g_dbus_message_get_message_type(message) != G_DBUS_MESSAGE_TYPE_METHOD_CALL)
            return message;   // our own replies (BecomeMonitor) must get through
        if (g_strcmp0(g_dbus_message_get_interface(message), kInterface) == 0 &&
            g_strcmp0(g_dbus_message_get_member(message), "Notify") == 0) {
            Toast toast;
            if (DecodeToast(g_dbus_message_get_body(message), toast)) {
                // Monitor mode cannot know the id the real server assigned;
                // the sender's serial keeps replaces_id chains distinct enough.
                self->PublishToast(toast, /*assignedId=*/0);
            }
        }
        // Swallow every eavesdropped call: left to GDBus it would answer
        // UnknownMethod on the other server's behalf, and a monitor that
        // sends anything is disconnected by the daemon.
        g_object_unref(message);
        return nullptr;
    }

    static void OnMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* methodName,
                             GVariant* parameters, GDBusMethodInvocation* invocation, gpointer userData) {
        auto* self = static_cast<FreedesktopNotificationsAdapter*>(userData);
        const std::string method = methodName ? methodName : "";
        if (method == "Notify") {
            Toast toast;
            if (!DecodeToast(parameters, toast)) {
                g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.InvalidArgs",
                                                           "malformed Notify");
                return;
            }
            guint32 id = toast.replacesId;
            if (id == 0) id = self->nextId_++;
            self->PublishToast(toast, id);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
        } else if (method == "CloseNotification") {
            guint32 id = 0;
            g_variant_get(parameters, "(u)", &id);
            std::string ulid;
            {
                std::lock_guard<std::mutex> lock(self->idsMutex_);
                auto it = self->byId_.find(id);
                if (it != self->byId_.end()) ulid = it->second;
            }
            // Forget first: the dismissed notice below comes straight back
            // through HandleAction, which must not signal a second close.
            self->Forget(id);
            if (!ulid.empty() && self->host_) {
                JSONValue body = JSONValue::MakeObject();
                body.Set("notificationId", ulid);
                body.Set("reason", "closed");
                self->host_->Publish(kAdapterName, UltraMsgTopics::SystemNotificationDismissed, body);
            }
            self->EmitSignal("NotificationClosed", g_variant_new("(uu)", id, kReasonClosedByCall));
            g_dbus_method_invocation_return_value(invocation, nullptr);
        } else if (method == "GetCapabilities") {
            GVariantBuilder builder;
            g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
            for (const char* capability : {"body", "actions", "persistence", "action-icons"})
                g_variant_builder_add(&builder, "s", capability);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &builder));
        } else if (method == "GetServerInformation") {
            const std::string version = UltraMsg_GetVersion();
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(ssss)", "UltraMessage", "ULTRA OS", version.c_str(), kSpecVersion));
        } else {
            g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod",
                                                       "unknown method");
        }
    }

    // ---- translation -------------------------------------------------------

    void PublishToast(const Toast& toast, guint32 assignedId) {
        if (!host_) return;
        SystemNotification n;
        n.appName = toast.appName;
        n.appId = StripDesktopSuffix(toast.desktopEntry);
        n.category = toast.category;
        n.summary = toast.summary;
        n.body = toast.body;
        n.icon = !toast.imagePath.empty() ? toast.imagePath : toast.appIcon;
        n.urgency = toast.urgency <= 0 ? "low" : toast.urgency >= 2 ? "critical" : "normal";
        for (const auto& [key, label] : toast.actions) n.actions.push_back({key, label});
        n.origin = mode_ == Mode::Monitor ? "freedesktop-monitor" : "freedesktop";
        JSONValue body = MakeSystemNotification(n);
        body.Set("adapter", kAdapterName);
        if (assignedId) body.Set("nativeId", static_cast<int64_t>(assignedId));
        if (toast.senderPid) body.Set("senderPid", static_cast<int64_t>(toast.senderPid));

        UltraMsgSendOptions options;
        if (toast.urgency >= 2) options.flags |= UltraMsgFlag_Urgent;
        if (assignedId && toast.replacesId) {
            std::lock_guard<std::mutex> lock(idsMutex_);
            auto it = byId_.find(assignedId);
            if (it != byId_.end()) {
                options.replaces = it->second;
                options.flags |= UltraMsgFlag_Replace;
            }
        }
        const std::string ulid = host_->Publish(kAdapterName, UltraMsgTopics::SystemNotification, body, options);
        if (assignedId && !ulid.empty()) {
            std::lock_guard<std::mutex> lock(idsMutex_);
            if (auto it = byId_.find(assignedId); it != byId_.end()) byUlid_.erase(it->second);
            byId_[assignedId] = ulid;
            byUlid_[ulid] = assignedId;
        }

        // Mirrors (§9.1): a chat or mail toast also becomes a first-class feed
        // row, so the message centre groups it with UltraMail's and the
        // messenger adapters' messages. What a toast carries is heuristic:
        // the summary is the sender (chat) or the subject line (mail).
        const std::string service = !n.appId.empty() ? Lowercase(n.appId) : Lowercase(toast.appName);
        if (StartsWith(toast.category, "im.received")) {
            MessagingMessage m;
            m.service = service.empty() ? "notification" : service;
            m.account = toast.appName;
            m.conversationId = toast.summary;
            m.conversationTitle = toast.summary;
            m.sender.name = toast.summary;
            m.text = toast.body;
            m.incoming = true;
            m.externalId = ulid;
            UltraMsgSendOptions mirror;
            mirror.conversation = ConversationKey(m);
            JSONValue mirrorBody = MakeMessagingMessage(m);
            mirrorBody.Set("mirrorOf", ulid);
            host_->Publish(kAdapterName, UltraMsgTopics::MessagingMessage, mirrorBody, mirror);
        } else if (StartsWith(toast.category, "email")) {
            MailMessage m;
            m.account = toast.appName;
            m.from.name = toast.summary;
            m.subject = FirstLine(toast.body).empty() ? toast.summary : FirstLine(toast.body);
            m.snippet = toast.body;
            m.externalId = ulid;
            JSONValue mirrorBody = MakeMailMessage(m);
            mirrorBody.Set("mirrorOf", ulid);
            host_->Publish(kAdapterName, UltraMsgTopics::MailMessage, mirrorBody);
        }
    }

    void EmitSignal(const char* name, GVariant* parameters) {
        if (!connection_) {
            g_variant_unref(g_variant_ref_sink(parameters));
            return;
        }
        GError* error = nullptr;
        g_dbus_connection_emit_signal(connection_, nullptr, kObjectPath, kInterface, name, parameters, &error);
        if (error) g_error_free(error);
    }

    void Forget(guint32 id) {
        std::lock_guard<std::mutex> lock(idsMutex_);
        auto it = byId_.find(id);
        if (it == byId_.end()) return;
        byUlid_.erase(it->second);
        byId_.erase(it);
    }

    IAdapterHost* host_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<Mode> mode_{Mode::None};

    mutable std::mutex stateMutex_;
    UltraMsgAdapterState state_;
    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;

    GMainContext* context_ = nullptr;
    GMainLoop* loop_ = nullptr;
    GDBusConnection* connection_ = nullptr;
    GDBusConnection* monitorConnection_ = nullptr;
    guint registrationId_ = 0;
    guint ownerId_ = 0;
    guint filterId_ = 0;

    std::atomic<guint32> nextId_{1};
    std::mutex idsMutex_;
    std::map<guint32, std::string> byId_;     // native id -> journal / bus id
    std::map<std::string, guint32> byUlid_;
};

} // namespace

std::unique_ptr<IAdapter> CreateFreedesktopNotificationsAdapter() {
    return std::make_unique<FreedesktopNotificationsAdapter>();
}

} // namespace Internal
} // namespace UltraMessage
