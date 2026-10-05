// Tests/UltraMessage/test_toasts.cpp
// UltraCanvasNotificationToastHost without windows: which notifications a
// toast host draws (live, not Silent, nothing shows it yet), the same id and
// a replacement updating a toast, the visible limit, expiry, the pointer
// holding a toast, critical ones staying, and what the controls do; then, on
// the private bus, the host drawing what an application posts and answering
// with system.notification.action / .dismissed, and a dismissal from the
// application taking the toast away.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_helpers.h"

#include "Plugins/UltraMessage/UltraCanvasNotificationToast.h"

#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using UltraCanvas::JSONValue;
using UltraCanvas::NotificationToastContent;
using UltraCanvas::UltraCanvasNotificationToastHost;
using ultramsg_test::Connect;
using ultramsg_test::Scoped;
using ultramsg_test::TestBusPath;
using ultramsg_test::WaitFor;

namespace {

JSONValue Body(const std::string& summary, const std::string& body = "", const std::string& urgency = "normal",
               std::vector<UltraMessage::NotificationAction> actions = {}) {
    UltraMessage::SystemNotification n;
    n.appName = "Telegram";
    n.appId = "org.telegram.desktop";
    n.category = "im.received";
    n.summary = summary;
    n.body = body;
    n.urgency = urgency;
    n.actions = std::move(actions);
    return UltraMessage::MakeSystemNotification(n);
}

UltraMsgMessage Notification(const std::string& id, const JSONValue& body, uint32_t flags = UltraMsgFlag_None,
                             const std::string& replaces = "") {
    UltraMsgMessage m;
    m.envelope.id = id;
    m.envelope.topic = UltraMsgTopics::SystemNotification;
    m.envelope.flags = flags;
    m.envelope.replaces = replaces;
    m.envelope.from.appId = "org.ultraos.ultramessage.adapter.freedesktop-notifications";
    m.body = body;
    return m;
}

UltraCanvasNotificationToastHost& Headless(UltraCanvasNotificationToastHost& host) {
    host.SetWindowsEnabled(false);
    return host;
}

} // namespace

TEST(toasts_draw_only_what_nothing_shows) {
    REQUIRE(UltraCanvasNotificationToastHost::ShouldShow(Notification("n1", Body("Alice", "hi"))));
    // Silent: the feed only.
    REQUIRE(!UltraCanvasNotificationToastHost::ShouldShow(Notification("n2", Body("Alice"), UltraMsgFlag_Silent)));
    // Forwarded to the desktop's own notification service, or read from one
    // that drew it.
    JSONValue shown = Body("Alice");
    shown.Set("displayed", "freedesktop-presenter");
    REQUIRE(!UltraCanvasNotificationToastHost::ShouldShow(Notification("n3", shown)));
    // Not a notification.
    UltraMsgMessage mail = Notification("m1", Body("Alice"));
    mail.envelope.topic = UltraMsgTopics::MailMessage;
    REQUIRE(!UltraCanvasNotificationToastHost::ShouldShow(mail));
    // A passing alert the journal never keeps is still drawn.
    REQUIRE(UltraCanvasNotificationToastHost::ShouldShow(Notification("n4", Body("New mail"), UltraMsgFlag_NoJournal)));
}

TEST(toasts_content_is_read_from_the_notification) {
    JSONValue body = Body("Alice\nsecond line", "see you at 9", "critical", {{"default", "Open"}, {"reply", "Reply"}});
    body.Set("icon", "mail-unread");   // an icon-theme name: not drawn
    NotificationToastContent c;
    REQUIRE(NotificationToastContent::FromMessage(Notification("n1", body), c));
    REQUIRE_EQ(c.notificationId, std::string("n1"));
    REQUIRE_EQ(c.appName, std::string("Telegram"));
    REQUIRE_EQ(c.summary, std::string("Alice second line"));   // one line
    REQUIRE_EQ(c.body, std::string("see you at 9"));
    REQUIRE_EQ(c.urgency, std::string("critical"));
    REQUIRE(c.iconPath.empty());
    REQUIRE(c.HasDefaultAction());
    REQUIRE_EQ(c.actions.size(), size_t(2));

    // A very long body is cut, at a character boundary.
    std::string longText;
    for (int i = 0; i < 400; ++i) longText += "\xC3\xA4";   // "ä" x 400 = 800 bytes
    REQUIRE(NotificationToastContent::FromMessage(Notification("n2", Body("x", longText)), c));
    REQUIRE(c.body.size() < longText.size());
    REQUIRE_EQ(c.body.substr(c.body.size() - 3), std::string("\xE2\x80\xA6"));
    REQUIRE((static_cast<unsigned char>(c.body[c.body.size() - 4]) & 0xC0) != 0xC0);   // no half character

    UltraMsgMessage notJson = Notification("n3", JSONValue::MakeObject());
    REQUIRE(!NotificationToastContent::FromMessage(notJson, c));
}

TEST(toasts_update_replace_and_limit) {
    UltraCanvasNotificationToastHost host;
    Headless(host);
    int shown = 0;
    host.onShown = [&](const NotificationToastContent&) { ++shown; };

    REQUIRE(host.Ingest(Notification("n1", Body("Download 10%"))));
    REQUIRE(host.Ingest(Notification("n1", Body("Download 20%"))));   // the same id again
    REQUIRE_EQ(host.GetToastCount(), size_t(1));
    REQUIRE_EQ(host.GetToasts()[0].summary, std::string("Download 20%"));
    // A replacement takes the toast it names, with its own id.
    REQUIRE(host.Ingest(Notification("n2", Body("Download 90%"), UltraMsgFlag_Replace, "n1")));
    REQUIRE_EQ(host.GetToastCount(), size_t(1));
    REQUIRE_EQ(host.GetToasts()[0].notificationId, std::string("n2"));
    REQUIRE_EQ(shown, 3);

    // Not drawn: Silent, or on screen already.
    REQUIRE(!host.Ingest(Notification("q", Body("quiet"), UltraMsgFlag_Silent)));
    REQUIRE_EQ(host.GetToastCount(), size_t(1));

    // Newest first; the oldest beyond the limit goes.
    host.SetMaxVisible(3);
    for (int i = 3; i <= 6; ++i) host.Ingest(Notification("n" + std::to_string(i), Body("Message " + std::to_string(i))));
    REQUIRE_EQ(host.GetToastCount(), size_t(3));
    REQUIRE_EQ(host.GetToasts()[0].notificationId, std::string("n6"));
    REQUIRE_EQ(host.GetToasts()[2].notificationId, std::string("n4"));

    host.Withdraw("n5");
    REQUIRE_EQ(host.GetToastCount(), size_t(2));
    host.Withdraw("unknown");   // no-op
    REQUIRE_EQ(host.GetToastCount(), size_t(2));
}

TEST(toasts_expire_hold_and_critical_stays) {
    UltraCanvasNotificationToastHost host;
    Headless(host);
    host.SetTimeouts(1000, 3000);
    const int64_t t0 = UltraCanvasNotificationToastHost::NowMs();
    host.Ingest(Notification("low", Body("low", "", "low")));
    host.Ingest(Notification("normal", Body("normal")));
    host.Ingest(Notification("critical", Body("battery", "5 %", "critical")));
    REQUIRE_EQ(host.GetToastCount(), size_t(3));

    host.Tick(t0 + 500);
    REQUIRE_EQ(host.GetToastCount(), size_t(3));
    host.Tick(t0 + 1500);                        // low's time is up
    REQUIRE_EQ(host.GetToastCount(), size_t(2));

    // The pointer resting on it holds it past its time.
    host.Hold("normal", t0 + 2900);
    host.Tick(t0 + 3500);
    REQUIRE_EQ(host.GetToastCount(), size_t(2));
    host.Tick(t0 + 4500);
    REQUIRE_EQ(host.GetToastCount(), size_t(1));
    // Critical stays until closed.
    host.Tick(t0 + 3600 * 1000);
    REQUIRE_EQ(host.GetToastCount(), size_t(1));
    REQUIRE_EQ(host.GetToasts()[0].notificationId, std::string("critical"));
}

TEST(toasts_controls_without_a_bus) {
    UltraCanvasNotificationToastHost host;
    Headless(host);
    std::vector<std::string> actions;
    int dismissed = 0;
    host.onAction = [&](const NotificationToastContent& c, const std::string& action) {
        actions.push_back(c.notificationId + ":" + action);
    };
    host.onDismissed = [&](const NotificationToastContent&) { ++dismissed; };

    host.Ingest(Notification("a", Body("with default", "", "normal", {{"default", "Open"}})));
    host.Ingest(Notification("b", Body("no default")));
    host.Ingest(Notification("c", Body("buttons", "", "normal", {{"reply", "Reply"}})));
    host.Ingest(Notification("d", Body("closed")));

    host.ActivateBody("a");                       // the default action
    host.ActivateBody("b");                       // nothing to open: it just goes
    host.InvokeAction("c", "reply");
    host.Dismiss("d");
    REQUIRE_EQ(host.GetToastCount(), size_t(0));
    REQUIRE_EQ(actions.size(), size_t(2));
    REQUIRE_EQ(actions[0], std::string("a:default"));
    REQUIRE_EQ(actions[1], std::string("c:reply"));
    REQUIRE_EQ(dismissed, 1);
}

TEST(toasts_on_the_bus) {
    UltraCanvasNotificationToastHost host;
    Headless(host);
    UltraMsgConnectOptions options = UltraCanvasNotificationToastHost::DefaultConnectOptions();
    options.busPath = TestBusPath();
    options.journalPath = ":memory:";
    REQUIRE(host.Connect(options));
    REQUIRE(host.IsConnected());

    // An application, watching what the toasts say back.
    Scoped app{Connect("org.test.toasts.app", "Test App")};
    std::mutex mutex;
    std::vector<UltraMsgMessage> feedback;
    UltraMsgSubscribeOptions workerSide;
    workerSide.onWorkerThread = true;
    for (const char* topic : {UltraMsgTopics::SystemNotification, UltraMsgTopics::SystemNotificationAction,
                              UltraMsgTopics::SystemNotificationDismissed}) {
        UltraMsgSubscribeOptions own = workerSide;
        own.includeOwn = true;
        REQUIRE(UltraMsg_Subscribe(app.handle, topic, [&](const UltraMsgMessage& m) {
            std::lock_guard<std::mutex> lock(mutex);
            feedback.push_back(m);
        }, own) != UltraMsgInvalidHandle);
    }
    auto find = [&](const char* topic, const std::string& id, UltraMsgMessage& out) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& m : feedback) {
            const bool match = m.envelope.topic == topic &&
                               (m.envelope.id == id ||
                                (m.body.Find("notificationId") && m.body.Find("notificationId")->GetString() == id));
            if (match) { out = m; return true; }
        }
        return false;
    };

    std::string first;
    REQUIRE(UltraMsg_Post(app.handle, UltraMsgTopics::SystemNotification,
                          Body("Build finished", "all green", "normal", {{"default", "Open"}, {"log", "Show log"}}), {},
                          &first));
    UltraMsgMessage copy;
    REQUIRE(WaitFor([&] { return find(UltraMsgTopics::SystemNotification, first, copy); }));
    // Where a presenter hands notifications to a desktop's own service the
    // broker says so, and no toast host draws them.
    if (copy.body.Find("displayed")) throw ultramsg_test::Skip{"a presenter shows notifications on this machine"};
    REQUIRE(WaitFor([&] { return host.GetToastCount() == 1; }));
    REQUIRE_EQ(host.GetToasts()[0].notificationId, first);

    // The action button: an action back to the application, and the toast goes.
    host.InvokeAction(first, "log");
    UltraMsgMessage action;
    REQUIRE(WaitFor([&] { return find(UltraMsgTopics::SystemNotificationAction, first, action); }));
    REQUIRE_EQ(action.body.Find("actionId")->GetString(), std::string("log"));
    REQUIRE_EQ(action.envelope.from.appId, std::string("org.ultraos.notifications"));
    REQUIRE_EQ(host.GetToastCount(), size_t(0));

    // The close button: dismissed.
    std::string second;
    REQUIRE(UltraMsg_Post(app.handle, UltraMsgTopics::SystemNotification, Body("Disk almost full"), {}, &second));
    REQUIRE(WaitFor([&] { return host.GetToastCount() == 1; }));
    host.Dismiss(second);
    UltraMsgMessage dismissed;
    REQUIRE(WaitFor([&] { return find(UltraMsgTopics::SystemNotificationDismissed, second, dismissed); }));
    REQUIRE_EQ(dismissed.body.Find("reason")->GetString(), std::string("dismissed"));

    // The application withdrawing it (CloseNotification, read elsewhere).
    std::string third;
    REQUIRE(UltraMsg_Post(app.handle, UltraMsgTopics::SystemNotification, Body("Call from Bob"), {}, &third));
    REQUIRE(WaitFor([&] { return host.GetToastCount() == 1; }));
    JSONValue withdraw = JSONValue::MakeObject();
    withdraw.Set("notificationId", third);
    withdraw.Set("reason", "closed");
    REQUIRE(UltraMsg_Post(app.handle, UltraMsgTopics::SystemNotificationDismissed, withdraw));
    REQUIRE(WaitFor([&] { return host.GetToastCount() == 0; }));

    // Silent: listed, never drawn.
    UltraMsgSendOptions silent;
    silent.flags = UltraMsgFlag_Silent;
    std::string quiet;
    REQUIRE(UltraMsg_Post(app.handle, UltraMsgTopics::SystemNotification, Body("Quietly"), silent, &quiet));
    REQUIRE(WaitFor([&] { return find(UltraMsgTopics::SystemNotification, quiet, copy); }));
    WaitFor([] { return false; }, 200ms);
    REQUIRE_EQ(host.GetToastCount(), size_t(0));

    host.Disconnect();
    REQUIRE(!host.IsConnected());
}
