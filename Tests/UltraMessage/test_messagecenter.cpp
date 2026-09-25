// Tests/UltraMessage/test_messagecenter.cpp
// UltraCanvasMessageCenter without a window: the translation of feed
// messages into rows, sections, sources, filters and search; the mirror and
// replace rules; and, on a private bus, the element receiving live messages,
// reading the journal, and posting feed.read / feed.dismissed /
// system.notification.action back for the sources.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_helpers.h"

#include "Plugins/UltraMessage/UltraCanvasMessageCenter.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using UltraCanvas::JSONValue;
using UltraCanvas::MessageCenterEntry;
using UltraCanvas::MessageCenterSection;
using UltraCanvas::UltraCanvasMessageCenter;
using ultramsg_test::Connect;
using ultramsg_test::Scoped;
using ultramsg_test::TestBusPath;
using ultramsg_test::WaitFor;

namespace {

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

UltraMsgMessage Message(const std::string& id, const std::string& topic, const JSONValue& body,
                        int64_t timeMs, bool read = false, const std::string& conversation = "",
                        uint32_t flags = UltraMsgFlag_None, const std::string& replaces = "") {
    UltraMsgMessage m;
    m.envelope.id = id;
    m.envelope.topic = topic;
    m.envelope.timestampMs = timeMs;
    m.envelope.conversation = conversation;
    m.envelope.flags = flags;
    m.envelope.replaces = replaces;
    m.envelope.from.appId = "org.test.source";
    m.envelope.from.displayName = "Source";
    m.body = body;
    m.read = read;
    return m;
}

JSONValue Chat(const std::string& service, const std::string& conversation, const std::string& sender,
               const std::string& text, bool group = false) {
    UltraMessage::MessagingMessage m;
    m.service = service;
    m.account = "me";
    m.conversationId = conversation;
    m.conversationTitle = conversation;
    m.isGroup = group;
    m.sender = {sender, sender, ""};
    m.text = text;
    return UltraMessage::MakeMessagingMessage(m);
}

JSONValue Mail(const std::string& account, const std::string& from, const std::string& subject,
               const std::string& snippet = "") {
    UltraMessage::MailMessage m;
    m.account = account;
    m.folder = "INBOX";
    m.from = {from, from + "@example.org"};
    m.subject = subject;
    m.snippet = snippet;
    return UltraMessage::MakeMailMessage(m);
}

JSONValue Toast(const std::string& appName, const std::string& appId, const std::string& summary,
                const std::string& body, std::vector<UltraMessage::NotificationAction> actions = {},
                const std::string& urgency = "normal") {
    UltraMessage::SystemNotification n;
    n.appName = appName;
    n.appId = appId;
    n.summary = summary;
    n.body = body;
    n.urgency = urgency;
    n.actions = std::move(actions);
    return UltraMessage::MakeSystemNotification(n);
}

std::shared_ptr<UltraCanvasMessageCenter> MakeCenter(const std::string& id) {
    return UltraCanvas::CreateMessageCenter(id, 0, 0, 900, 600);
}

} // namespace

TEST(message_center_translates_feed_messages_into_rows) {
    const int64_t now = Now();
    MessageCenterEntry e;
    REQUIRE(UltraCanvasMessageCenter::BuildEntry(
        Message("c1", UltraMsgTopics::MessagingMessage, Chat("telegram", "Ada", "Ada", "hello\nsecond line"), now), e));
    REQUIRE(e.section == MessageCenterSection::Chats);
    REQUIRE_EQ(e.service, std::string("telegram"));
    REQUIRE_EQ(e.sourceKey, std::string("telegram:Ada"));
    REQUIRE_EQ(e.title, std::string("Ada"));
    REQUIRE_EQ(e.snippet, std::string("hello"));
    REQUIRE_EQ(e.body, std::string("hello\nsecond line"));
    REQUIRE(e.unread);

    REQUIRE(UltraCanvasMessageCenter::BuildEntry(
        Message("m1", UltraMsgTopics::MailMessage, Mail("erika@example.org", "Konrad", "Relays", "Thursday"), now, true), e));
    REQUIRE(e.section == MessageCenterSection::Mail);
    REQUIRE_EQ(e.service, std::string("mail"));
    REQUIRE_EQ(e.sourceKey, std::string("mail:erika@example.org"));
    REQUIRE_EQ(e.title, std::string("Konrad"));
    REQUIRE_EQ(e.snippet, std::string("Relays"));
    REQUIRE(!e.unread);

    REQUIRE(UltraCanvasMessageCenter::BuildEntry(
        Message("n1", UltraMsgTopics::SystemNotification,
                Toast("Battery", "org.ultraos.power", "Battery low", "12 % left", {{"plug", "Plug in"}}, "critical"),
                now), e));
    REQUIRE(e.section == MessageCenterSection::System);
    REQUIRE_EQ(e.service, std::string("org.ultraos.power"));
    REQUIRE_EQ(e.sourceKey, std::string("app:org.ultraos.power"));
    REQUIRE_EQ(e.title, std::string("Battery"));
    REQUIRE_EQ(e.snippet, std::string("Battery low \xE2\x80\x94 12 % left"));
    REQUIRE(e.urgent);
    REQUIRE_EQ(e.actions.size(), size_t(1));
    REQUIRE_EQ(e.actions[0].id, std::string("plug"));

    REQUIRE(!UltraCanvasMessageCenter::BuildEntry(
        Message("x1", UltraMsgTopics::AppLifecycleStarted, JSONValue::MakeObject(), now), e));
    REQUIRE(UltraCanvasMessageCenter::SectionForTopic(UltraMsgTopics::MailMessage) == MessageCenterSection::Mail);
}

TEST(message_center_formats_times_relative_to_now) {
    const int64_t now = Now();
    const std::string today = UltraCanvasMessageCenter::FormatTime(now - 60 * 1000, now);
    REQUIRE_EQ(today.size(), size_t(5));
    REQUIRE_EQ(today[2], ':');
    const std::string thisWeek = UltraCanvasMessageCenter::FormatTime(now - 3 * 86400 * 1000LL, now);
    REQUIRE_EQ(thisWeek.size(), size_t(3));   // a weekday name
    const std::string older = UltraCanvasMessageCenter::FormatTime(now - 40 * 86400 * 1000LL, now);
    REQUIRE(older.size() == 6 || older.size() == 10);   // DD.MM. or DD.MM.YYYY across a year boundary
    REQUIRE_EQ(UltraCanvasMessageCenter::FormatTime(0, now), std::string(""));
}

TEST(message_center_sections_sources_filters_and_search) {
    auto center = MakeCenter("mc-filters");
    const int64_t now = Now();
    int unreadReported = -1;
    center->onUnreadCountChanged = [&](int unread) { unreadReported = unread; };

    center->Ingest(Message("c1", UltraMsgTopics::MessagingMessage, Chat("telegram", "Ada", "Ada", "engine review at 9"), now - 5000, false, "telegram:Ada"));
    center->Ingest(Message("c2", UltraMsgTopics::MessagingMessage, Chat("signal", "Grace", "Grace", "found the moth"), now - 4000, true, "signal:Grace"));
    center->Ingest(Message("c3", UltraMsgTopics::MessagingMessage, Chat("telegram", "Engine", "Charles", "the mill is finished", true), now - 3000, false, "telegram:Engine"));
    center->Ingest(Message("m1", UltraMsgTopics::MailMessage, Mail("erika@example.org", "Konrad", "Z3 relay order"), now - 2000));
    center->Ingest(Message("n1", UltraMsgTopics::SystemNotification, Toast("Downloads", "org.ultraos.filer", "Download finished", "archive.tar.gz"), now - 1000));

    REQUIRE_EQ(center->GetEntries().size(), size_t(5));
    REQUIRE_EQ(center->GetVisibleCount(), 5);
    REQUIRE_EQ(center->GetUnreadCount(), 4);
    REQUIRE_EQ(unreadReported, 4);

    center->SetSection(MessageCenterSection::Chats);
    REQUIRE_EQ(center->GetVisibleCount(), 3);
    center->SetServiceFilter("telegram");
    REQUIRE_EQ(center->GetVisibleCount(), 2);
    center->SetUnreadOnly(true);
    REQUIRE_EQ(center->GetVisibleCount(), 2);
    center->SetServiceFilter("signal");
    REQUIRE_EQ(center->GetVisibleCount(), 0);       // Grace's is read
    center->SetUnreadOnly(false);
    REQUIRE_EQ(center->GetVisibleCount(), 1);
    center->SetServiceFilter("");
    center->SetSource("telegram:Engine");
    REQUIRE_EQ(center->GetVisibleCount(), 1);
    center->SetSection(MessageCenterSection::All);   // changing the section drops the source
    REQUIRE_EQ(center->GetVisibleCount(), 5);
    center->SetSearchText("MOTH");
    REQUIRE_EQ(center->GetVisibleCount(), 1);
    center->SetSearchText("");
    center->SetSection(MessageCenterSection::Mail);
    REQUIRE_EQ(center->GetVisibleCount(), 1);
    center->SetSection(MessageCenterSection::System);
    REQUIRE_EQ(center->GetVisibleCount(), 1);
    center->SetSection(MessageCenterSection::All);

    // Reading and removing rows, without a bus.
    center->MarkRead("c1", true);
    REQUIRE_EQ(center->GetUnreadCount(), 3);
    REQUIRE_EQ(unreadReported, 3);
    center->MarkRead("c1", false);
    REQUIRE_EQ(center->GetUnreadCount(), 4);
    center->RemoveEntry("m1");
    REQUIRE_EQ(center->GetEntries().size(), size_t(4));
    center->Dismiss("n1");
    REQUIRE_EQ(center->GetEntries().size(), size_t(3));

    // The sources tree lists the sections and their sources.
    auto tree = center->GetSourcesTree();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->FindNode("all") != nullptr);
    REQUIRE(tree->FindNode("sec:1") != nullptr);
    REQUIRE(tree->FindNode("src:1:telegram:Ada") != nullptr);
    REQUIRE(tree->FindNode("src:1:signal:Grace") != nullptr);
    REQUIRE(tree->FindNode("src:2:mail:erika@example.org") == nullptr);   // removed above

    center->Clear();
    REQUIRE_EQ(center->GetEntries().size(), size_t(0));
    REQUIRE_EQ(center->GetUnreadCount(), 0);
}

TEST(message_center_mirrors_and_replacements_keep_one_row) {
    auto center = MakeCenter("mc-mirrors");
    const int64_t now = Now();
    // A notification, then the chat row an adapter mirrored from it: one row,
    // the chat, remembering the notification for dismissal.
    center->Ingest(Message("n1", UltraMsgTopics::SystemNotification,
                           Toast("Telegram", "org.telegram.desktop", "Ada", "hello"), now - 2000));
    JSONValue mirror = Chat("org.telegram.desktop", "Ada", "Ada", "hello");
    mirror.Set("mirrorOf", "n1");
    center->Ingest(Message("c1", UltraMsgTopics::MessagingMessage, mirror, now - 1900));
    REQUIRE_EQ(center->GetEntries().size(), size_t(1));
    REQUIRE(center->GetEntries()[0].section == MessageCenterSection::Chats);
    REQUIRE_EQ(center->GetEntries()[0].mirrorOf, std::string("n1"));
    // The notification arriving again (a replay) does not come back.
    center->Ingest(Message("n1", UltraMsgTopics::SystemNotification,
                           Toast("Telegram", "org.telegram.desktop", "Ada", "hello"), now - 2000));
    REQUIRE_EQ(center->GetEntries().size(), size_t(1));

    // A replacing message takes the row of the one it supersedes.
    center->Ingest(Message("p1", UltraMsgTopics::SystemNotification,
                           Toast("Downloads", "org.ultraos.filer", "Download 10%", ""), now - 1000));
    center->Ingest(Message("p2", UltraMsgTopics::SystemNotification,
                           Toast("Downloads", "org.ultraos.filer", "Download 90%", ""), now - 500, false, "",
                           UltraMsgFlag_Replace, "p1"));
    REQUIRE_EQ(center->GetEntries().size(), size_t(2));
    bool found90 = false, found10 = false;
    for (const auto& e : center->GetEntries()) {
        found90 = found90 || e.snippet == "Download 90%";
        found10 = found10 || e.snippet == "Download 10%";
    }
    REQUIRE(found90);
    REQUIRE(!found10);
    // The same id again replaces in place.
    center->Ingest(Message("p2", UltraMsgTopics::SystemNotification,
                           Toast("Downloads", "org.ultraos.filer", "Download done", ""), now - 100));
    REQUIRE_EQ(center->GetEntries().size(), size_t(2));
}

TEST(message_center_on_the_bus_receives_reads_and_answers) {
    auto center = MakeCenter("mc-bus");
    UltraMsgConnectOptions options = UltraCanvasMessageCenter::DefaultConnectOptions();
    options.busPath = TestBusPath();
    options.journalPath = ":memory:";
    REQUIRE(center->Connect(options));
    REQUIRE(center->IsConnected());

    // A source on the same bus, watching what the feed says back.
    Scoped source{Connect("org.test.mc.source", "Source")};
    std::mutex mutex;
    std::vector<UltraMsgMessage> feedback;
    UltraMsgSubscribeOptions workerSide;
    workerSide.onWorkerThread = true;
    for (const char* topic : {UltraMsgTopics::FeedRead, UltraMsgTopics::FeedDismissed,
                              UltraMsgTopics::SystemNotificationAction, UltraMsgTopics::SystemNotificationDismissed}) {
        REQUIRE(UltraMsg_Subscribe(source.handle, topic, [&](const UltraMsgMessage& m) {
            std::lock_guard<std::mutex> lock(mutex);
            feedback.push_back(m);
        }, workerSide) != UltraMsgInvalidHandle);
    }
    auto feedbackCount = [&](const char* topic) {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (const auto& m : feedback) n += m.envelope.topic == topic ? 1 : 0;
        return n;
    };
    auto feedbackBody = [&](const char* topic, const char* key) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& m : feedback)
            if (m.envelope.topic == topic) {
                const JSONValue* v = m.body.Find(key);
                return v && v->IsString() ? v->GetString() : std::string();
            }
        return std::string();
    };

    // Live delivery: three posts arrive through the element's subscriptions
    // (pumped here, since no UI dispatcher is installed in a test).
    std::string chatId, mailId, toastId;
    UltraMsgSendOptions chatOptions;
    chatOptions.conversation = "telegram:Ada";
    REQUIRE(UltraMsg_Post(source.handle, UltraMsgTopics::MessagingMessage, Chat("telegram", "Ada", "Ada", "live one"), chatOptions, &chatId));
    REQUIRE(UltraMsg_Post(source.handle, UltraMsgTopics::MailMessage, Mail("erika@example.org", "Konrad", "live two"), {}, &mailId));
    REQUIRE(UltraMsg_Post(source.handle, UltraMsgTopics::SystemNotification,
                          Toast("Downloads", "org.ultraos.filer", "live three", "", {{"open", "Open"}}), {}, &toastId));
    REQUIRE(WaitFor([&] { return center->GetEntries().size() == 3; }));
    REQUIRE_EQ(center->GetUnreadCount(), 3);

    // The journal agrees: a refresh re-reads the same three.
    center->Refresh();
    REQUIRE_EQ(center->GetEntries().size(), size_t(3));

    // Reading a row reaches the journal and the sources.
    center->MarkRead(chatId, true);
    REQUIRE(WaitFor([&] { return feedbackCount(UltraMsgTopics::FeedRead) == 1; }));
    REQUIRE_EQ(feedbackBody(UltraMsgTopics::FeedRead, "messageId"), chatId);
    UltraMsgQuery query;
    query.topics = {UltraMsgTopics::MessagingMessage};
    std::vector<UltraMsgMessage> rows;
    REQUIRE(UltraMsg_Query(source.handle, query, rows));
    bool journaledRead = false;
    for (const auto& r : rows) journaledRead = journaledRead || (r.envelope.id == chatId && r.read);
    REQUIRE(journaledRead);

    // A notification's action goes back to whoever produced it.
    center->InvokeAction(toastId, "open");
    REQUIRE(WaitFor([&] { return feedbackCount(UltraMsgTopics::SystemNotificationAction) == 1; }));
    REQUIRE_EQ(feedbackBody(UltraMsgTopics::SystemNotificationAction, "notificationId"), toastId);
    REQUIRE_EQ(feedbackBody(UltraMsgTopics::SystemNotificationAction, "actionId"), std::string("open"));

    // Dismissing a notification tells the feed and the adapter, and the row goes.
    center->Dismiss(toastId);
    REQUIRE(WaitFor([&] {
        return feedbackCount(UltraMsgTopics::FeedDismissed) == 1 &&
               feedbackCount(UltraMsgTopics::SystemNotificationDismissed) == 1;
    }));
    REQUIRE_EQ(feedbackBody(UltraMsgTopics::SystemNotificationDismissed, "notificationId"), toastId);
    REQUIRE_EQ(center->GetEntries().size(), size_t(2));
    rows.clear();
    query.topics = {UltraMsgTopics::SystemNotification};
    REQUIRE(UltraMsg_Query(source.handle, query, rows));
    for (const auto& r : rows) REQUIRE(r.envelope.id != toastId);   // dismissed rows leave the default query

    // Another feed reading the mail (feed.read from elsewhere) updates this one.
    JSONValue readElsewhere = JSONValue::MakeObject();
    readElsewhere.Set("messageId", mailId);
    REQUIRE(UltraMsg_Post(source.handle, UltraMsgTopics::FeedRead, readElsewhere));
    REQUIRE(WaitFor([&] { return center->GetUnreadCount() == 0; }));

    center->Disconnect();
    REQUIRE(!center->IsConnected());
}
