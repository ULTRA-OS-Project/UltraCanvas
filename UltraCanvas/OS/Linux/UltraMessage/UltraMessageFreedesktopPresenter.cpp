// UltraCanvas/OS/Linux/UltraMessage/UltraMessageFreedesktopPresenter.cpp
// The freedesktop-presenter adapter: the way out to the screen. A
// `system.notification` an application posts on the bus (UltraMail's "new
// mail", a download finished, ...) is handed to the desktop's notification
// server - GNOME Shell, Plasma, XFCE, dunst, mako, whatever owns
// `org.freedesktop.Notifications` - as a Notify() call, so it pops up like
// every other application's. What the user then does with it comes back:
// ActionInvoked becomes a `system.notification.action`, a close by the user a
// `system.notification.dismissed`, both naming the bus message, so the
// application that posted it can act (UltraMail opens the mail). A
// dismissal or action posted on the bus closes the notification on screen.
//
// Each Notify carries the hint `x-ultramessage-id`, so the
// freedesktop-notifications adapter, which reads every Notify on the bus,
// does not publish it a second time. Where UltraMessage itself serves
// org.freedesktop.Notifications no notification server draws them; the
// adapter then presents nothing and says so in its state, and a toast host
// on the bus (UltraCanvasNotificationToastHost, in the ULTRA OS desktop)
// draws what carries no `displayed`.
//
// GDBus (GIO) on a private connection and a private GMainContext in the
// adapter's own thread, like the freedesktop-notifications adapter.
// Version: 0.1.1 - the state names the toast host for the case nothing else draws
// Version: 0.1.0
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

constexpr const char* kAdapterName = "freedesktop-presenter";
constexpr const char* kBusName = "org.freedesktop.Notifications";
constexpr const char* kObjectPath = "/org/freedesktop/Notifications";
constexpr const char* kInterface = "org.freedesktop.Notifications";
// The name the freedesktop-notifications adapter answers GetServerInformation
// with: a server that draws nothing.
constexpr const char* kUltraMessageServerName = "UltraMessage";

// NotificationClosed reasons, per the specification.
constexpr guint kReasonDismissed = 2;

// Long enough for a D-Bus activated server (dunst, mako, xfce4-notifyd) to
// start on the first notification.
constexpr int kNotifyTimeoutMs = 10000;
constexpr int kProbeTimeoutMs = 5000;

class FreedesktopPresenterAdapter;

// What one Present hands the adapter thread.
struct PendingNotify {
    FreedesktopPresenterAdapter* self = nullptr;
    std::string notificationId;      // the bus message id
    std::string replacesId;          // a presented bus message this one updates
    std::string appName;
    std::string appIcon;
    std::string summary;
    std::string body;
    std::vector<NotificationAction> actions;
    std::string category;
    std::string desktopEntry;
    int urgency = 1;
    int senderPid = 0;
};

// The body markup subset (`body-markup`) treats these as markup; a subject
// line such as "Q&A <draft>" must reach the screen as it was written.
std::string EscapeMarkup(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// app_icon takes an icon name or a URI; an absolute path becomes a file URI.
std::string IconArgument(const std::string& icon) {
    if (icon.empty() || icon[0] != '/') return icon;
    gchar* uri = g_filename_to_uri(icon.c_str(), nullptr, nullptr);
    if (!uri) return std::string();
    std::string out = uri;
    g_free(uri);
    return out;
}

int UrgencyByte(const std::string& urgency) {
    if (urgency == "low") return 0;
    if (urgency == "critical") return 2;
    return 1;
}

std::string StringField(const JSONValue& body, const char* key) {
    const JSONValue* v = body.Find(key);
    return v && v->IsString() ? v->GetString() : std::string();
}

class FreedesktopPresenterAdapter final : public IAdapter {
public:
    ~FreedesktopPresenterAdapter() override { Stop(); }

    std::string Name() const override { return kAdapterName; }
    std::string Description() const override {
        return "Applications' notifications on screen: handed to the desktop's notification server "
               "(org.freedesktop.Notifications), with its clicks and closes reported back";
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
        readyCv_.wait_for(lock, std::chrono::seconds(6), [&] { return ready_; });
        return State();
    }

    void Stop() override {
        stopping_.store(true);
        {
            // Through the context as well: a quit sent before the loop runs
            // would be lost, an idle source on its context is not.
            std::lock_guard<std::mutex> lock(lifeMutex_);
            if (loop_) {
                g_main_loop_quit(loop_);
                g_main_context_invoke(context_, &QuitLoop, loop_);
            }
        }
        if (thread_.joinable()) thread_.join();
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            byNative_.clear();
            byId_.clear();
        }
        host_ = nullptr;
        SetState(UltraMsgAdapterStatus::Disabled, "", "", "");
    }

    UltraMsgAdapterState State() const override {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return state_;
    }

    bool Present(const UltraMsgMessage& notification) override {
        if (noDisplay_.load()) return false;
        SystemNotification n;
        if (!ParseSystemNotification(notification.body, n)) return false;

        auto* pending = new PendingNotify;
        pending->self = this;
        pending->notificationId = notification.envelope.id;
        if (notification.envelope.flags & UltraMsgFlag_Replace) pending->replacesId = notification.envelope.replaces;
        pending->appName = !n.appName.empty() ? n.appName
                           : !notification.envelope.from.displayName.empty() ? notification.envelope.from.displayName
                                                                             : notification.envelope.from.appId;
        pending->appIcon = n.icon;
        pending->summary = n.summary;
        pending->body = n.body;
        pending->actions = n.actions;
        pending->category = n.category;
        pending->desktopEntry = StringField(notification.body, "desktopEntry");
        pending->urgency = UrgencyByte(n.urgency);
        pending->senderPid = notification.envelope.from.processId;

        std::lock_guard<std::mutex> lock(lifeMutex_);
        if (!context_ || stopping_.load()) {
            delete pending;
            return false;
        }
        g_main_context_invoke_full(context_, G_PRIORITY_DEFAULT, &DoNotify, pending, &FreePending);
        return true;
    }

    bool HandleAction(const UltraMsgMessage& action) override {
        if (!action.body.IsObject()) return false;
        const std::string notificationId = StringField(action.body, "notificationId");
        if (notificationId.empty()) return false;
        guint32 nativeId = 0;
        {
            std::lock_guard<std::mutex> lock(idsMutex_);
            auto it = byId_.find(notificationId);
            if (it == byId_.end()) return false;
            nativeId = it->second;
            byNative_.erase(nativeId);
            byId_.erase(it);
        }
        // The application, or the feed, acted on it: it has served its
        // purpose on screen too.
        std::lock_guard<std::mutex> lock(lifeMutex_);
        if (connection_)
            g_dbus_connection_call(connection_, kBusName, kObjectPath, kInterface, "CloseNotification",
                                   g_variant_new("(u)", nativeId), nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                                   nullptr, nullptr, nullptr);
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

    // ---- the adapter thread ------------------------------------------------

    void Run() {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GMainLoop* loop = g_main_loop_new(context, FALSE);

        // A connection of its own: its unique name is the sender of every
        // Notify, and nothing it does disturbs the freedesktop-notifications
        // adapter's connection (which may itself be the server).
        GError* error = nullptr;
        gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        GDBusConnection* connection = nullptr;
        if (address) {
            connection = g_dbus_connection_new_for_address_sync(
                address,
                static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                nullptr, nullptr, &error);
            g_free(address);
        }
        if (!connection) {
            SetState(UltraMsgAdapterStatus::Unavailable,
                     std::string("no session bus: ") + (error ? error->message : "unknown error"),
                     "start the desktop session bus (dbus-daemon --session) or set DBUS_SESSION_BUS_ADDRESS", "");
            if (error) g_error_free(error);
            SignalReady();
            g_main_loop_unref(loop);
            g_main_context_pop_thread_default(context);
            g_main_context_unref(context);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(lifeMutex_);
            context_ = context;
            loop_ = loop;
            connection_ = connection;
        }

        signalId_ = g_dbus_connection_signal_subscribe(connection_, nullptr, kInterface, nullptr, kObjectPath,
                                                       nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &OnServerSignal, this,
                                                       nullptr);
        ownerId_ = g_dbus_connection_signal_subscribe(connection_, "org.freedesktop.DBus", "org.freedesktop.DBus",
                                                      "NameOwnerChanged", "/org/freedesktop/DBus", kBusName,
                                                      G_DBUS_SIGNAL_FLAGS_NONE, &OnOwnerChanged, this, nullptr);
        // Whether anybody draws notifications here; a server only D-Bus
        // activation would start is left for the first notification to start.
        Probe(/*autoStart=*/false);
        SignalReady();
        if (!stopping_.load()) g_main_loop_run(loop_);

        g_dbus_connection_signal_unsubscribe(connection_, signalId_);
        g_dbus_connection_signal_unsubscribe(connection_, ownerId_);
        signalId_ = ownerId_ = 0;
        {
            std::lock_guard<std::mutex> lock(lifeMutex_);
            g_dbus_connection_close_sync(connection_, nullptr, nullptr);
            g_object_unref(connection_);
            connection_ = nullptr;
            loop_ = nullptr;
            context_ = nullptr;
        }
        g_main_loop_unref(loop);
        // Sources still queued (a Present that raced Stop) are destroyed with
        // the context, which frees what they carry.
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
    }

    // Asks the current owner of org.freedesktop.Notifications who it is.
    // Returns false when nobody answered (no server, or one that failed to
    // start). Adapter thread.
    bool Probe(bool autoStart) {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_sync(
            connection_, kBusName, kObjectPath, kInterface, "GetServerInformation", nullptr,
            G_VARIANT_TYPE("(ssss)"), autoStart ? G_DBUS_CALL_FLAGS_NONE : G_DBUS_CALL_FLAGS_NO_AUTO_START,
            kProbeTimeoutMs, nullptr, &error);
        if (!reply) {
            const std::string why = error ? error->message : "no answer";
            if (error) g_error_free(error);
            serverKnown_ = autoStart;
            noDisplay_.store(autoStart);
            serverOwner_.clear();
            if (autoStart)
                SetState(UltraMsgAdapterStatus::Running,
                         "no notification server answers on this desktop (" + why +
                             "): applications' notifications reach the feed only",
                         "install or start a notification server (the desktop's own, dunst, mako, ...)", "none");
            else
                SetState(UltraMsgAdapterStatus::Running,
                         "no notification server is running yet; the first notification starts one where the "
                         "desktop installs it",
                         "", "waiting");
            return false;
        }
        const gchar* name = nullptr;
        const gchar* vendor = nullptr;
        g_variant_get(reply, "(&s&s&s&s)", &name, &vendor, nullptr, nullptr);
        const std::string server = name ? name : "";
        g_variant_unref(reply);
        serverOwner_ = NameOwner();
        serverKnown_ = true;

        if (server == kUltraMessageServerName) {
            // The freedesktop-notifications adapter serves the name (this
            // process or another broker's): nothing draws what it receives.
            noDisplay_.store(true);
            SetState(UltraMsgAdapterStatus::Running,
                     "UltraMessage itself serves org.freedesktop.Notifications here, so no notification server "
                     "draws them: a toast host on the bus (the ULTRA OS desktop) does, and the feed lists them",
                     "where no toast host runs, start a notification server (the desktop's own, dunst, mako, ...) "
                     "and switch the freedesktop-notifications adapter off and on again",
                     "none");
            return false;
        }
        noDisplay_.store(false);
        bodyMarkup_ = ServerHasCapability("body-markup");
        SetState(UltraMsgAdapterStatus::Running,
                 "applications' notifications are shown by " + (server.empty() ? std::string("the desktop") : server),
                 "", "forward");
        return true;
    }

    std::string NameOwner() {
        GVariant* reply = g_dbus_connection_call_sync(connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                      "org.freedesktop.DBus", "GetNameOwner",
                                                      g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(s)"),
                                                      G_DBUS_CALL_FLAGS_NONE, kProbeTimeoutMs, nullptr, nullptr);
        if (!reply) return std::string();
        const gchar* owner = nullptr;
        g_variant_get(reply, "(&s)", &owner);
        std::string out = owner ? owner : "";
        g_variant_unref(reply);
        return out;
    }

    bool ServerHasCapability(const char* capability) {
        GVariant* reply = g_dbus_connection_call_sync(connection_, kBusName, kObjectPath, kInterface,
                                                      "GetCapabilities", nullptr, G_VARIANT_TYPE("(as)"),
                                                      G_DBUS_CALL_FLAGS_NO_AUTO_START, kProbeTimeoutMs, nullptr,
                                                      nullptr);
        if (!reply) return false;
        GVariantIter* iter = nullptr;
        g_variant_get(reply, "(as)", &iter);
        bool found = false;
        const gchar* item = nullptr;
        while (g_variant_iter_next(iter, "&s", &item))
            if (item && g_strcmp0(item, capability) == 0) found = true;
        g_variant_iter_free(iter);
        g_variant_unref(reply);
        return found;
    }

    static void FreePending(gpointer data) { delete static_cast<PendingNotify*>(data); }

    static gboolean QuitLoop(gpointer loop) {
        g_main_loop_quit(static_cast<GMainLoop*>(loop));
        return G_SOURCE_REMOVE;
    }

    static gboolean DoNotify(gpointer data) {
        auto* pending = static_cast<PendingNotify*>(data);
        FreedesktopPresenterAdapter* self = pending->self;
        if (self->stopping_.load() || !self->connection_) return G_SOURCE_REMOVE;
        // The first notification since the owner changed (or ever) finds out
        // who draws it, and starts an activatable server if none runs.
        if (!self->serverKnown_ && !self->Probe(/*autoStart=*/true)) return G_SOURCE_REMOVE;
        if (self->noDisplay_.load()) return G_SOURCE_REMOVE;

        guint32 replacesNative = 0;
        if (!pending->replacesId.empty()) {
            std::lock_guard<std::mutex> lock(self->idsMutex_);
            auto it = self->byId_.find(pending->replacesId);
            if (it != self->byId_.end()) replacesNative = it->second;
        }

        GVariantBuilder actions;
        g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
        for (const auto& action : pending->actions) {
            if (action.id.empty()) continue;
            g_variant_builder_add(&actions, "s", action.id.c_str());
            g_variant_builder_add(&actions, "s", action.label.empty() ? action.id.c_str() : action.label.c_str());
        }
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(static_cast<guchar>(pending->urgency)));
        if (!pending->category.empty())
            g_variant_builder_add(&hints, "{sv}", "category", g_variant_new_string(pending->category.c_str()));
        if (!pending->desktopEntry.empty())
            g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(pending->desktopEntry.c_str()));
        if (pending->senderPid > 0)
            g_variant_builder_add(&hints, "{sv}", "sender-pid", g_variant_new_int64(pending->senderPid));
        // The freedesktop-notifications adapter skips a Notify with this hint:
        // the notification is on the bus already.
        g_variant_builder_add(&hints, "{sv}", "x-ultramessage-id",
                              g_variant_new_string(pending->notificationId.c_str()));

        const std::string icon = IconArgument(pending->appIcon);
        const std::string body = self->bodyMarkup_ ? EscapeMarkup(pending->body) : pending->body;
        GVariant* parameters = g_variant_new("(susssasa{sv}i)", pending->appName.c_str(), replacesNative,
                                             icon.c_str(), pending->summary.c_str(), body.c_str(), &actions, &hints,
                                             -1);
        NotePresented(pending->summary, pending->body);
        // The reply carries the id the server gave it; the pending record
        // travels with the call and is freed by its callback.
        auto* call = new PendingNotify(*pending);
        g_dbus_connection_call(self->connection_, kBusName, kObjectPath, kInterface, "Notify", parameters,
                               G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, kNotifyTimeoutMs, nullptr,
                               &OnNotifyReply, call);
        return G_SOURCE_REMOVE;
    }

    static void OnNotifyReply(GObject* source, GAsyncResult* result, gpointer data) {
        auto* call = static_cast<PendingNotify*>(data);
        FreedesktopPresenterAdapter* self = call->self;
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if (!reply) {
            if (!self->stopping_.load())
                self->SetState(UltraMsgAdapterStatus::Running,
                               std::string("the notification server refused a notification: ") +
                                   (error ? error->message : "no answer"),
                               "", "forward");
            if (error) g_error_free(error);
            delete call;
            return;
        }
        guint32 nativeId = 0;
        g_variant_get(reply, "(u)", &nativeId);
        g_variant_unref(reply);
        if (nativeId != 0) {
            std::lock_guard<std::mutex> lock(self->idsMutex_);
            if (!call->replacesId.empty()) self->byId_.erase(call->replacesId);
            if (auto it = self->byNative_.find(nativeId); it != self->byNative_.end()) self->byId_.erase(it->second);
            self->byNative_[nativeId] = call->notificationId;
            self->byId_[call->notificationId] = nativeId;
        }
        delete call;
    }

    // ActionInvoked / NotificationClosed from the server, for every
    // application's notifications; only the ones this adapter showed count.
    static void OnServerSignal(GDBusConnection*, const gchar* sender, const gchar*, const gchar*,
                               const gchar* signalName, GVariant* parameters, gpointer userData) {
        auto* self = static_cast<FreedesktopPresenterAdapter*>(userData);
        if (!self->serverOwner_.empty() && g_strcmp0(sender, self->serverOwner_.c_str()) != 0) return;
        const std::string member = signalName ? signalName : "";
        guint32 nativeId = 0;
        std::string actionKey;
        guint32 reason = 0;
        if (member == "ActionInvoked" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)"))) {
            const gchar* key = nullptr;
            g_variant_get(parameters, "(u&s)", &nativeId, &key);
            actionKey = key ? key : "";
        } else if (member == "NotificationClosed" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uu)"))) {
            g_variant_get(parameters, "(uu)", &nativeId, &reason);
        } else {
            return;
        }
        std::string notificationId;
        {
            // Forgotten before anything is published: the notice comes
            // straight back through HandleAction, which must not close the
            // notification a second time.
            std::lock_guard<std::mutex> lock(self->idsMutex_);
            auto it = self->byNative_.find(nativeId);
            if (it == self->byNative_.end()) return;
            notificationId = it->second;
            self->byId_.erase(notificationId);
            self->byNative_.erase(it);
        }
        if (!self->host_) return;
        JSONValue body = JSONValue::MakeObject();
        body.Set("notificationId", notificationId);
        body.Set("adapter", kAdapterName);
        if (member == "ActionInvoked") {
            body.Set("actionId", actionKey);
            self->host_->Publish(kAdapterName, UltraMsgTopics::SystemNotificationAction, body);
        } else if (reason == kReasonDismissed) {
            // Closed by the user. Expired, or closed by a CloseNotification
            // call, is not a decision about the notification.
            body.Set("reason", "dismissed");
            self->host_->Publish(kAdapterName, UltraMsgTopics::SystemNotificationDismissed, body);
        }
    }

    // Somebody took or left org.freedesktop.Notifications (a server started,
    // stopped or replaced another): find out again who draws notifications.
    static void OnOwnerChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                               GVariant*, gpointer userData) {
        auto* self = static_cast<FreedesktopPresenterAdapter*>(userData);
        if (self->stopping_.load()) return;
        self->serverKnown_ = false;
        self->noDisplay_.store(false);
        {
            // The old server's ids mean nothing to the new one.
            std::lock_guard<std::mutex> lock(self->idsMutex_);
            self->byNative_.clear();
            self->byId_.clear();
        }
        self->Probe(/*autoStart=*/false);
    }

    IAdapterHost* host_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex stateMutex_;
    UltraMsgAdapterState state_;
    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    bool ready_ = false;

    // Guards the pointers below against Stop while a broker thread uses them.
    std::mutex lifeMutex_;
    GMainContext* context_ = nullptr;
    GMainLoop* loop_ = nullptr;
    GDBusConnection* connection_ = nullptr;
    guint signalId_ = 0;
    guint ownerId_ = 0;

    // Adapter thread only, except noDisplay_, which Present reads.
    bool serverKnown_ = false;
    bool bodyMarkup_ = false;
    std::string serverOwner_;
    std::atomic<bool> noDisplay_{false};

    std::mutex idsMutex_;
    std::map<guint32, std::string> byNative_;   // server id -> bus message id
    std::map<std::string, guint32> byId_;
};

} // namespace

std::unique_ptr<IAdapter> CreateFreedesktopPresenterAdapter() {
    return std::make_unique<FreedesktopPresenterAdapter>();
}

} // namespace Internal
} // namespace UltraMessage
