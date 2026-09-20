// Tests/UltraMessage/test_adapters.cpp
// UltraMessage Phase 2: the adapter registry as seen through the API, and
// the freedesktop notification adapter driven over a private D-Bus session
// (test_main.cpp starts the daemon): serving org.freedesktop.Notifications,
// the toast -> system.notification translation with its chat and mail
// mirrors, replace / close, actions signalled back, the switch, and monitor
// mode when another server owns the name.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_helpers.h"

#include <UltraMessage/UltraMessage.h>
#include <UltraMessage/UltraMessageEndpoint.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

using UltraCanvas::JSONValue;
using namespace std::chrono_literals;
using ultramsg_test::Connect;
using ultramsg_test::Scoped;
using ultramsg_test::WaitFor;

namespace {

constexpr const char* kAdapterName = "freedesktop-notifications";

std::vector<UltraMsgAdapterInfo> ListAdapters(UltraMsgHandle endpoint) {
    std::vector<UltraMsgAdapterInfo> adapters;
    UltraMsgResult r = UltraMsg_ListAdapters(endpoint, adapters);
    if (!r) throw ultramsg_test::Failure{"ListAdapters: " + r.message};
    return adapters;
}

const UltraMsgAdapterInfo* Find(const std::vector<UltraMsgAdapterInfo>& adapters, const std::string& name) {
    for (const auto& a : adapters)
        if (a.name == name) return &a;
    return nullptr;
}

} // namespace

TEST(adapters_unknown_name_is_not_found) {
    Scoped ep{Connect("org.test.adapters.unknown")};
    UltraMsgAdapterState state;
    UltraMsgResult r = UltraMsg_GetAdapterState(ep.handle, "no-such-adapter", state);
    REQUIRE(!r);
    REQUIRE(r.code == UltraMsgResultCode::InvalidArgument);
    r = UltraMsg_EnableAdapter(ep.handle, "no-such-adapter", true);
    REQUIRE(!r);
    REQUIRE(r.code == UltraMsgResultCode::InvalidArgument);
}

TEST(adapter_status_names_round_trip) {
    for (UltraMsgAdapterStatus s : {UltraMsgAdapterStatus::Disabled, UltraMsgAdapterStatus::Starting,
                                    UltraMsgAdapterStatus::Running, UltraMsgAdapterStatus::NeedsPermission,
                                    UltraMsgAdapterStatus::Unavailable, UltraMsgAdapterStatus::Error}) {
        UltraMsgAdapterStatus back = UltraMsgAdapterStatus::Error;
        REQUIRE(UltraMsg_AdapterStatusFromName(UltraMsg_AdapterStatusName(s), back));
        REQUIRE(back == s);
    }
    UltraMsgAdapterStatus unknown;
    REQUIRE(!UltraMsg_AdapterStatusFromName("bogus", unknown));
    REQUIRE_EQ(std::string(UltraMsg_AdapterStatusName(UltraMsgAdapterStatus::NeedsPermission)), std::string("needs-permission"));
}

#if defined(ULTRAMESSAGE_HAVE_GIO) && defined(__linux__)

#include <gio/gio.h>

namespace {

constexpr const char* kBusName = "org.freedesktop.Notifications";
constexpr const char* kObjectPath = "/org/freedesktop/Notifications";
constexpr const char* kInterface = "org.freedesktop.Notifications";

void RequireTestBus() {
    const char* flag = std::getenv("ULTRAMSG_TEST_DBUS");
    if (!flag || std::string(flag) != "1") throw ultramsg_test::Skip{"no private dbus-daemon"};
}

struct SignalRecord {
    std::string member;
    guint32 id = 0;
    guint32 reason = 0;
    std::string actionKey;
};

// A desktop application, as far as the bus is concerned: its own connection
// to the private session bus, with signal delivery on a context the test
// pumps.
class BusClient {
public:
    BusClient() {
        const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!address) throw ultramsg_test::Skip{"no DBUS_SESSION_BUS_ADDRESS"};
        context_ = g_main_context_new();
        g_main_context_push_thread_default(context_);
        GError* error = nullptr;
        connection_ = g_dbus_connection_new_for_address_sync(
            address,
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        if (!connection_) {
            std::string message = error ? error->message : "unknown";
            if (error) g_error_free(error);
            g_main_context_pop_thread_default(context_);
            g_main_context_unref(context_);
            throw ultramsg_test::Failure{"bus client connect failed: " + message};
        }
        subscription_ = g_dbus_connection_signal_subscribe(connection_, nullptr, kInterface, nullptr, kObjectPath,
                                                           nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &OnSignal, this,
                                                           nullptr);
        g_main_context_pop_thread_default(context_);
    }

    ~BusClient() {
        if (connection_) {
            g_dbus_connection_signal_unsubscribe(connection_, subscription_);
            g_dbus_connection_close_sync(connection_, nullptr, nullptr);
            g_object_unref(connection_);
        }
        if (context_) g_main_context_unref(context_);
    }

    void Pump() {
        g_main_context_push_thread_default(context_);
        while (g_main_context_iteration(context_, FALSE)) {}
        g_main_context_pop_thread_default(context_);
    }

    // Notify() as a desktop application sends it. Returns the assigned id,
    // 0 on error (`error` then holds the reason).
    guint32 Notify(const std::string& appName, guint32 replacesId, const std::string& summary,
                   const std::string& body, const std::vector<std::string>& actions,
                   const std::string& category, const std::string& desktopEntry, int urgency,
                   std::string* error = nullptr) {
        GVariantBuilder actionsBuilder;
        g_variant_builder_init(&actionsBuilder, G_VARIANT_TYPE("as"));
        for (const auto& a : actions) g_variant_builder_add(&actionsBuilder, "s", a.c_str());
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
        if (!category.empty()) g_variant_builder_add(&hints, "{sv}", "category", g_variant_new_string(category.c_str()));
        if (!desktopEntry.empty())
            g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(desktopEntry.c_str()));
        g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(static_cast<guchar>(urgency)));
        g_variant_builder_add(&hints, "{sv}", "sender-pid", g_variant_new_int64(4242));
        GVariant* parameters = g_variant_new("(susssasa{sv}i)", appName.c_str(), replacesId, "dialog-information",
                                             summary.c_str(), body.c_str(), &actionsBuilder, &hints, -1);
        GError* gerror = nullptr;
        GVariant* reply = g_dbus_connection_call_sync(connection_, kBusName, kObjectPath, kInterface, "Notify",
                                                      parameters, G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
                                                      3000, nullptr, &gerror);
        if (!reply) {
            if (error) *error = gerror ? gerror->message : "unknown";
            if (gerror) g_error_free(gerror);
            return 0;
        }
        guint32 id = 0;
        g_variant_get(reply, "(u)", &id);
        g_variant_unref(reply);
        return id;
    }

    // Notify() without waiting for anybody to answer: what a monitor sees.
    void NotifyNoReply(const std::string& appName, const std::string& summary, const std::string& body) {
        GVariantBuilder actionsBuilder;
        g_variant_builder_init(&actionsBuilder, G_VARIANT_TYPE("as"));
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
        GVariant* parameters = g_variant_new("(susssasa{sv}i)", appName.c_str(), 0u, "", summary.c_str(),
                                             body.c_str(), &actionsBuilder, &hints, -1);
        GDBusMessage* message = g_dbus_message_new_method_call(kBusName, kObjectPath, kInterface, "Notify");
        g_dbus_message_set_body(message, parameters);
        g_dbus_message_set_flags(message, G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
        g_dbus_connection_send_message(connection_, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, nullptr, nullptr);
        g_object_unref(message);
        g_dbus_connection_flush_sync(connection_, nullptr, nullptr);
    }

    bool CloseNotification(guint32 id) {
        GVariant* reply = g_dbus_connection_call_sync(connection_, kBusName, kObjectPath, kInterface,
                                                      "CloseNotification", g_variant_new("(u)", id), nullptr,
                                                      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (!reply) return false;
        g_variant_unref(reply);
        return true;
    }

    std::string ServerName() {
        GVariant* reply = g_dbus_connection_call_sync(connection_, kBusName, kObjectPath, kInterface,
                                                      "GetServerInformation", nullptr, G_VARIANT_TYPE("(ssss)"),
                                                      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (!reply) return "";
        const gchar* name = nullptr;
        g_variant_get(reply, "(&s&s&s&s)", &name, nullptr, nullptr, nullptr);
        std::string result = name ? name : "";
        g_variant_unref(reply);
        return result;
    }

    // RequestName / ReleaseName straight at the daemon, synchronously.
    bool OwnName() {
        GVariant* reply = g_dbus_connection_call_sync(connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                      "org.freedesktop.DBus", "RequestName",
                                                      g_variant_new("(su)", kBusName, 0u), G_VARIANT_TYPE("(u)"),
                                                      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (!reply) return false;
        guint32 code = 0;
        g_variant_get(reply, "(u)", &code);
        g_variant_unref(reply);
        return code == 1;   // DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
    }

    void ReleaseName() {
        GVariant* reply = g_dbus_connection_call_sync(connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                      "org.freedesktop.DBus", "ReleaseName",
                                                      g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(u)"),
                                                      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (reply) g_variant_unref(reply);
    }

    std::vector<SignalRecord> Signals() {
        Pump();
        std::lock_guard<std::mutex> lock(mutex_);
        return signals_;
    }

    bool WaitForSignal(const std::string& member, guint32 id, SignalRecord* out = nullptr) {
        return WaitFor([&] {
            for (const auto& s : Signals())
                if (s.member == member && s.id == id) {
                    if (out) *out = s;
                    return true;
                }
            return false;
        });
    }

private:
    static void OnSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signalName,
                         GVariant* parameters, gpointer userData) {
        auto* self = static_cast<BusClient*>(userData);
        SignalRecord record;
        record.member = signalName ? signalName : "";
        if (record.member == "ActionInvoked") {
            const gchar* key = nullptr;
            g_variant_get(parameters, "(u&s)", &record.id, &key);
            record.actionKey = key ? key : "";
        } else if (record.member == "NotificationClosed") {
            g_variant_get(parameters, "(uu)", &record.id, &record.reason);
        }
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->signals_.push_back(record);
    }

    GMainContext* context_ = nullptr;
    GDBusConnection* connection_ = nullptr;
    guint subscription_ = 0;
    std::mutex mutex_;
    std::vector<SignalRecord> signals_;
};

// A feed-side observer of one topic.
struct Collector {
    std::mutex mutex;
    std::vector<UltraMsgMessage> messages;
    UltraMsgHandle subscription = UltraMsgInvalidHandle;

    void Subscribe(UltraMsgHandle endpoint, const std::string& pattern) {
        UltraMsgResult error;
        subscription = UltraMsg_Subscribe(
            endpoint, pattern,
            [this](const UltraMsgMessage& m) {
                std::lock_guard<std::mutex> lock(mutex);
                messages.push_back(m);
            },
            {}, &error);
        if (subscription == UltraMsgInvalidHandle)
            throw ultramsg_test::Failure{"subscribe " + pattern + ": " + error.message};
    }
    ~Collector() {
        if (subscription != UltraMsgInvalidHandle) UltraMsg_Unsubscribe(subscription);
    }

    // The first message whose body field `key` equals `value`.
    bool Find(const std::string& key, const std::string& value, UltraMsgMessage& out) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& m : messages) {
            const JSONValue* v = m.body.Find(key);
            if (v && v->IsString() && v->GetString() == value) {
                out = m;
                return true;
            }
        }
        return false;
    }
    bool WaitForField(const std::string& key, const std::string& value, UltraMsgMessage& out) {
        return WaitFor([&] { return Find(key, value, out); }, 5000ms);
    }
};

UltraMsgAdapterState WaitForStatus(UltraMsgHandle endpoint, UltraMsgAdapterStatus status,
                                   const std::string& mode = "") {
    UltraMsgAdapterState state;
    WaitFor([&] {
        UltraMsgResult r = UltraMsg_GetAdapterState(endpoint, kAdapterName, state);
        return r && state.status == status && (mode.empty() || state.mode == mode);
    }, 8000ms);
    return state;
}

std::string Str(const JSONValue& body, const char* key) {
    const JSONValue* v = body.Find(key);
    return v && v->IsString() ? v->GetString() : std::string();
}

int64_t Int(const JSONValue& body, const char* key) {
    const JSONValue* v = body.Find(key);
    return v && v->IsNumber() ? v->GetInteger() : 0;
}

} // namespace

TEST(freedesktop_adapter_serves_the_notification_name) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.list")};
    const auto adapters = ListAdapters(ep.handle);
    const UltraMsgAdapterInfo* info = Find(adapters, kAdapterName);
    REQUIRE(info != nullptr);
    REQUIRE(info->enabled);
    REQUIRE_EQ(info->platform, std::string("linux"));
    REQUIRE(!info->description.empty());

    const UltraMsgAdapterState state = WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    REQUIRE_EQ(UltraMsg_AdapterStatusName(state.status), std::string("running"));
    REQUIRE_EQ(state.mode, std::string("server"));

    BusClient app;
    REQUIRE_EQ(app.ServerName(), std::string("UltraMessage"));
}

TEST(freedesktop_notify_becomes_system_notification_with_chat_mirror) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.notify")};
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    Collector toasts, chats;
    toasts.Subscribe(ep.handle, UltraMsgTopics::SystemNotification);
    chats.Subscribe(ep.handle, UltraMsgTopics::MessagingMessage);

    BusClient app;
    std::string error;
    const guint32 id = app.Notify("TestChat", 0, "Alice", "hello from the bus", {"reply", "Reply"}, "im.received",
                                  "org.example.testchat.desktop", 2, &error);
    REQUIRE(id > 0);

    UltraMsgMessage toast;
    REQUIRE(toasts.WaitForField("summary", "Alice", toast));
    REQUIRE_EQ(toast.envelope.topic, std::string(UltraMsgTopics::SystemNotification));
    REQUIRE_EQ(toast.envelope.from.appId, std::string("org.ultraos.ultramessage.adapter.") + kAdapterName);
    REQUIRE(toast.envelope.from.verified);
    REQUIRE(toast.envelope.flags & UltraMsgFlag_Urgent);
    UltraMessage::SystemNotification n;
    REQUIRE(UltraMessage::ParseSystemNotification(toast.body, n));
    REQUIRE_EQ(n.appName, std::string("TestChat"));
    REQUIRE_EQ(n.appId, std::string("org.example.testchat"));
    REQUIRE_EQ(n.category, std::string("im.received"));
    REQUIRE_EQ(n.body, std::string("hello from the bus"));
    REQUIRE_EQ(n.urgency, std::string("critical"));
    REQUIRE_EQ(n.origin, std::string("freedesktop"));
    REQUIRE_EQ(n.actions.size(), size_t(1));
    REQUIRE_EQ(n.actions[0].id, std::string("reply"));
    REQUIRE_EQ(n.actions[0].label, std::string("Reply"));
    REQUIRE_EQ(Str(toast.body, "adapter"), std::string(kAdapterName));
    REQUIRE_EQ(Int(toast.body, "nativeId"), int64_t(id));
    REQUIRE_EQ(Int(toast.body, "senderPid"), int64_t(4242));

    UltraMsgMessage chat;
    REQUIRE(chats.WaitForField("mirrorOf", toast.envelope.id, chat));
    UltraMessage::MessagingMessage m;
    REQUIRE(UltraMessage::ParseMessagingMessage(chat.body, m));
    REQUIRE_EQ(m.service, std::string("org.example.testchat"));
    REQUIRE_EQ(m.sender.name, std::string("Alice"));
    REQUIRE_EQ(m.text, std::string("hello from the bus"));
    REQUIRE(m.incoming);
    REQUIRE_EQ(chat.envelope.conversation, UltraMessage::ConversationKey(m));

    // Both are journaled under the adapter's identity.
    UltraMsgQuery query;
    query.appId = toast.envelope.from.appId;
    query.topics = {"*.message"};
    std::vector<UltraMsgMessage> rows;
    REQUIRE(UltraMsg_Query(ep.handle, query, rows));
    bool journaled = false;
    for (const auto& r : rows) journaled = journaled || r.envelope.id == chat.envelope.id;
    REQUIRE(journaled);
}

TEST(freedesktop_mail_toast_mirrors_to_mail_message) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.mail")};
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    Collector mails;
    mails.Subscribe(ep.handle, UltraMsgTopics::MailMessage);

    BusClient app;
    const guint32 id = app.Notify("Thunderbird", 0, "Ada Lovelace", "Analytical engine\nThe notes are attached.",
                                  {}, "email.arrived", "", 1);
    REQUIRE(id > 0);

    UltraMsgMessage mail;
    REQUIRE(WaitFor([&] {
        std::lock_guard<std::mutex> lock(mails.mutex);
        for (const auto& m : mails.messages)
            if (Str(m.body, "subject") == "Analytical engine") { mail = m; return true; }
        return false;
    }, 5000ms));
    UltraMessage::MailMessage m;
    REQUIRE(UltraMessage::ParseMailMessage(mail.body, m));
    REQUIRE_EQ(m.from.name, std::string("Ada Lovelace"));
    REQUIRE_EQ(m.account, std::string("Thunderbird"));
    REQUIRE(!Str(mail.body, "mirrorOf").empty());
}

TEST(freedesktop_replace_and_close_notification) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.replace")};
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    Collector toasts, dismissed;
    toasts.Subscribe(ep.handle, UltraMsgTopics::SystemNotification);
    dismissed.Subscribe(ep.handle, UltraMsgTopics::SystemNotificationDismissed);

    BusClient app;
    const guint32 id = app.Notify("Downloader", 0, "Download 10%", "", {}, "transfer", "", 1);
    REQUIRE(id > 0);
    UltraMsgMessage first;
    REQUIRE(toasts.WaitForField("summary", "Download 10%", first));

    const guint32 again = app.Notify("Downloader", id, "Download 90%", "", {}, "transfer", "", 1);
    REQUIRE_EQ(again, id);
    UltraMsgMessage second;
    REQUIRE(toasts.WaitForField("summary", "Download 90%", second));
    REQUIRE(second.envelope.flags & UltraMsgFlag_Replace);
    REQUIRE_EQ(second.envelope.replaces, first.envelope.id);

    REQUIRE(app.CloseNotification(id));
    SignalRecord closed;
    REQUIRE(app.WaitForSignal("NotificationClosed", id, &closed));
    REQUIRE_EQ(closed.reason, guint32(3));
    UltraMsgMessage gone;
    REQUIRE(dismissed.WaitForField("notificationId", second.envelope.id, gone));
    // Exactly one close for the id: the adapter's own dismissed notice must
    // not come back as a second NotificationClosed.
    WaitFor([] { return false; }, 200ms);
    int closes = 0;
    for (const auto& s : app.Signals()) closes += (s.member == "NotificationClosed" && s.id == id) ? 1 : 0;
    REQUIRE_EQ(closes, 1);
}

TEST(freedesktop_feed_action_is_signalled_to_the_application) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.action")};
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    Collector toasts;
    toasts.Subscribe(ep.handle, UltraMsgTopics::SystemNotification);

    BusClient app;
    const guint32 id = app.Notify("TestChat", 0, "Bob", "call me", {"default", "Open", "reply", "Reply"},
                                  "im.received", "org.example.testchat", 1);
    REQUIRE(id > 0);
    UltraMsgMessage toast;
    REQUIRE(toasts.WaitForField("summary", "Bob", toast));

    // The feed invokes an action: the adapter must raise ActionInvoked and
    // then close the toast on the application's side.
    JSONValue action = JSONValue::MakeObject();
    action.Set("notificationId", toast.envelope.id);
    action.Set("actionId", "reply");
    REQUIRE(UltraMsg_Post(ep.handle, UltraMsgTopics::SystemNotificationAction, action));
    SignalRecord invoked;
    REQUIRE(app.WaitForSignal("ActionInvoked", id, &invoked));
    REQUIRE_EQ(invoked.actionKey, std::string("reply"));
    SignalRecord closed;
    REQUIRE(app.WaitForSignal("NotificationClosed", id, &closed));
    REQUIRE_EQ(closed.reason, guint32(2));

    // An action on a notification nobody produced is ignored, not an error.
    JSONValue stray = JSONValue::MakeObject();
    stray.Set("notificationId", "01ARZ3NDEKTSV4RRFFQ69G5FAV");
    stray.Set("actionId", "x");
    REQUIRE(UltraMsg_Post(ep.handle, UltraMsgTopics::SystemNotificationAction, stray));
}

TEST(freedesktop_switch_and_monitor_mode) {
    RequireTestBus();
    Scoped ep{Connect("org.test.adapters.monitor")};
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");

    // Off: the name is released and the switch reads back.
    REQUIRE(UltraMsg_EnableAdapter(ep.handle, kAdapterName, false));
    UltraMsgAdapterState state = WaitForStatus(ep.handle, UltraMsgAdapterStatus::Disabled);
    REQUIRE(state.status == UltraMsgAdapterStatus::Disabled);
    std::vector<UltraMsgAdapterInfo> adapters = ListAdapters(ep.handle);
    const UltraMsgAdapterInfo* info = Find(adapters, kAdapterName);
    REQUIRE(info != nullptr);
    REQUIRE(!info->enabled);

    // Another notification server takes the name; on again, the adapter can
    // only watch.
    BusClient rival;
    REQUIRE(WaitFor([&] { return rival.OwnName(); }));
    REQUIRE(UltraMsg_EnableAdapter(ep.handle, kAdapterName, true));
    state = WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "monitor");
    if (state.status == UltraMsgAdapterStatus::NeedsPermission) {
        rival.ReleaseName();
        throw ultramsg_test::Skip{"the bus refused monitor mode: " + state.message};
    }
    REQUIRE(state.status == UltraMsgAdapterStatus::Running);
    REQUIRE_EQ(state.mode, std::string("monitor"));
    adapters = ListAdapters(ep.handle);
    info = Find(adapters, kAdapterName);
    REQUIRE(info != nullptr);
    REQUIRE(info->enabled);

    Collector toasts;
    toasts.Subscribe(ep.handle, UltraMsgTopics::SystemNotification);
    BusClient app;
    app.NotifyNoReply("Watched", "Seen passively", "the rival server never answers");
    UltraMsgMessage toast;
    REQUIRE(toasts.WaitForField("summary", "Seen passively", toast));
    REQUIRE_EQ(Str(toast.body, "origin"), std::string("freedesktop-monitor"));
    REQUIRE_EQ(Int(toast.body, "nativeId"), int64_t(0));
    // A second one proves the monitor stayed connected (a monitor that
    // answered the first call would have been dropped by the daemon).
    app.NotifyNoReply("Watched", "Still watching", "");
    REQUIRE(toasts.WaitForField("summary", "Still watching", toast));

    // The rival goes away: a restart of the adapter serves the name again.
    rival.ReleaseName();
    REQUIRE(UltraMsg_EnableAdapter(ep.handle, kAdapterName, false));
    WaitForStatus(ep.handle, UltraMsgAdapterStatus::Disabled);
    REQUIRE(UltraMsg_EnableAdapter(ep.handle, kAdapterName, true));
    state = WaitForStatus(ep.handle, UltraMsgAdapterStatus::Running, "server");
    REQUIRE_EQ(state.mode, std::string("server"));
}

#endif // ULTRAMESSAGE_HAVE_GIO && __linux__
