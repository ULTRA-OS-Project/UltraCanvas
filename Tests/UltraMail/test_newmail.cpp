// Tests/UltraMail/test_newmail.cpp
// The new-mail notification: which stored messages are news (the inbox's
// UID watermark, the first download, read and deleted mail, overlapping
// syncs, a cache reset), its wording, and - on Linux, on a private bus with
// no D-Bus session, so nothing reaches a real screen - the notification
// FeedPublisher::Notify posts and the click that comes back.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailFeedPublisher.h"
#include "UltraMailNewMail.h"

#include <string>

using namespace UltraMail;

namespace {

MessageEnvelope Mail(int64_t uid, const std::string& from, const std::string& subject, int64_t date = 0,
                     uint32_t flags = Flag_None, const std::string& folder = "INBOX") {
    MessageEnvelope m;
    m.accountId = "erika";
    m.folder = folder;
    m.uid = uid;
    m.fromName = from;
    m.fromAddr = "someone@example.org";
    m.subject = subject;
    m.date = date ? date : 1'800'000'000 + uid;
    m.flags = flags;
    return m;
}

} // namespace

TEST(newmail_is_unread_inbox_mail_above_the_watermark) {
    REQUIRE(NewMailTracker::IsNew(Mail(11, "Ada", "Hi"), 10));
    REQUIRE(!NewMailTracker::IsNew(Mail(10, "Ada", "Hi"), 10));   // held before
    REQUIRE(!NewMailTracker::IsNew(Mail(3, "Ada", "Hi"), 10));    // a gap repaired
    REQUIRE(!NewMailTracker::IsNew(Mail(11, "Ada", "Hi", 0, Flag_Seen), 10));
    REQUIRE(!NewMailTracker::IsNew(Mail(11, "Ada", "Hi", 0, Flag_Deleted), 10));
    REQUIRE(!NewMailTracker::IsNew(Mail(11, "Ada", "Hi", 0, Flag_Draft), 10));
    REQUIRE(!NewMailTracker::IsNew(Mail(11, "Ada", "Hi", 0, Flag_None, "Archive"), 10));
    // An empty inbox before the sync: the account's first download.
    REQUIRE(!NewMailTracker::IsNew(Mail(11, "Ada", "Hi"), 0));
}

TEST(newmail_tracker_gathers_one_sync) {
    NewMailTracker tracker;
    tracker.Begin("erika", 100);
    tracker.Note(Mail(101, "Ada", "First"));
    tracker.Note(Mail(103, "Grace", "Third"));
    tracker.Note(Mail(102, "Konrad", "Second"));
    tracker.Note(Mail(50, "Old", "Repaired gap"));
    tracker.Note(Mail(104, "Read", "Elsewhere", 0, Flag_Seen));
    tracker.Note(Mail(101, "Ada", "First"));   // stored twice: counted once
    MessageEnvelope other = Mail(105, "Other", "Other account");
    other.accountId = "work";
    tracker.Note(other);                       // no sync of that account runs
    tracker.Note(Mail(105, "Charles", "Fourth"));

    NewMailSummary s = tracker.Finish("erika");
    REQUIRE_EQ(s.accountId, std::string("erika"));
    REQUIRE_EQ(s.count, 4);
    REQUIRE_EQ(s.latest.size(), kNewMailNamed);
    REQUIRE_EQ(s.latest[0].uid, int64_t(105));   // newest first
    REQUIRE_EQ(s.latest[1].uid, int64_t(103));
    REQUIRE_EQ(s.latest[2].uid, int64_t(102));

    // Finished: nothing more is gathered until the next Begin.
    tracker.Note(Mail(106, "Late", "After the sync"));
    REQUIRE_EQ(tracker.Finish("erika").count, 0);
}

TEST(newmail_first_download_and_cache_reset_announce_nothing) {
    NewMailTracker tracker;
    tracker.Begin("erika", 0);
    for (int uid = 1; uid <= 40; ++uid) tracker.Note(Mail(uid, "Sender", "History"));
    REQUIRE_EQ(tracker.Finish("erika").count, 0);

    tracker.Begin("erika", 10);
    tracker.Note(Mail(11, "Ada", "Hi"));
    REQUIRE_EQ(tracker.Finish("erika", /*announce=*/false).count, 0);
}

TEST(newmail_overlapping_syncs_announce_once_at_the_end) {
    NewMailTracker tracker;
    tracker.Begin("erika", 20);        // the timer's account sync
    tracker.Begin("erika", 21);        // the inbox opened meanwhile: the first mark holds
    tracker.Note(Mail(21, "Ada", "Hi"));
    tracker.Note(Mail(22, "Grace", "Hello"));
    tracker.Note(Mail(22, "Grace", "Hello"));   // both syncs stored it
    REQUIRE_EQ(tracker.Finish("erika").count, 0);   // one still runs
    NewMailSummary s = tracker.Finish("erika");
    REQUIRE_EQ(s.count, 2);
    REQUIRE_EQ(s.latest.front().uid, int64_t(22));

    // A cache reset in either one drops the lot.
    tracker.Begin("erika", 30);
    tracker.Begin("erika", 30);
    tracker.Note(Mail(31, "Ada", "Hi"));
    REQUIRE_EQ(tracker.Finish("erika", false).count, 0);
    REQUIRE_EQ(tracker.Finish("erika").count, 0);
}

TEST(newmail_text_for_one_message) {
    NewMailSummary s;
    s.accountId = "erika";
    s.count = 1;
    s.latest = {Mail(1, "Ada Lovelace", "The engine notes")};
    NewMailText text = FormatNewMail(s);
    REQUIRE_EQ(text.summary, std::string("New mail from Ada Lovelace"));
    REQUIRE_EQ(text.body, std::string("The engine notes"));

    text = FormatNewMail(s, "erika@example.org");
    REQUIRE_EQ(text.body, std::string("The engine notes\nto erika@example.org"));

    // No name: the address. No subject: said so. A folded subject: one line.
    s.latest = {Mail(1, "", "")};
    text = FormatNewMail(s);
    REQUIRE_EQ(text.summary, std::string("New mail from someone@example.org"));
    REQUIRE_EQ(text.body, std::string("(no subject)"));
    s.latest = {Mail(1, "Ada", "Line one\r\nline two")};
    REQUIRE_EQ(FormatNewMail(s).body, std::string("Line one  line two"));
}

TEST(newmail_text_for_several_messages) {
    NewMailSummary s;
    s.accountId = "erika";
    s.count = 2;
    s.latest = {Mail(2, "Grace", "Moth"), Mail(1, "Ada", "Notes")};
    NewMailText text = FormatNewMail(s);
    REQUIRE_EQ(text.summary, std::string("2 new messages"));
    REQUIRE_EQ(text.body, std::string("Grace: Moth\nAda: Notes"));

    s.count = 7;
    s.latest = {Mail(7, "G", "c"), Mail(6, "K", "b"), Mail(5, "A", "a")};
    text = FormatNewMail(s, "erika@example.org");
    REQUIRE_EQ(text.summary, std::string("7 new messages for erika@example.org"));
    REQUIRE_EQ(text.body, std::string("G: c\nK: b\nA: a\nand 4 more"));

    s.count = 0;
    REQUIRE(FormatNewMail(s).summary.empty());
}

#if defined(ULTRAMAIL_HAVE_ULTRAMESSAGE) && defined(__linux__)

#include <UltraMessage/UltraMessage.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unistd.h>

TEST(newmail_notification_is_posted_and_its_click_comes_back) {
    // A broker of the test's own. Without a session bus its adapters find no
    // desktop, so nothing is drawn on the screen of whoever runs the tests.
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    unsetenv("DISPLAY");
    const std::string bus = "/tmp/ultramail-newmail-test-" + std::to_string(getpid()) + "/bus.sock";

    FeedPublisher publisher;
    publisher.SetBusPath(bus, ":memory:");
    std::mutex mutex;
    std::string clickedId, clickedAction;
    publisher.SetOnNotificationAction([&](const std::string& id, const std::string& action) {
        std::lock_guard<std::mutex> lock(mutex);
        clickedId = id;
        clickedAction = action;
    });

    MailNotification n;
    n.accountId = "erika";
    n.summary = "New mail from Ada Lovelace";
    n.body = "The engine notes";
    const std::string id = publisher.Notify(n);
    REQUIRE(!id.empty());

    // The desktop's view of it: a feed-side endpoint on the same bus.
    UltraMsgConnectOptions options;
    options.appId = "org.test.ultramail.desktop";
    options.busPath = bus;
    options.journalPath = ":memory:";
    options.deliverOnUIThread = false;
    UltraMsgHandle desktop = UltraMsg_Connect(options);
    REQUIRE(desktop != UltraMsgInvalidHandle);

    std::atomic<bool> seen{false};
    UltraMsgMessage got;
    UltraMsgSubscribeOptions live;
    live.onWorkerThread = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(desktop, UltraMsgTopics::SystemNotification,
                                            [&](const UltraMsgMessage& m) {
                                                std::lock_guard<std::mutex> lock(mutex);
                                                got = m;
                                                seen = true;
                                            },
                                            live);
    REQUIRE(sub != UltraMsgInvalidHandle);
    const std::string second = publisher.Notify(n);   // subscribed now: this one is seen live
    REQUIRE(!second.empty());
    for (int i = 0; i < 300 && !seen; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(seen.load());
    {
        std::lock_guard<std::mutex> lock(mutex);
        REQUIRE_EQ(got.envelope.id, second);
        REQUIRE_EQ(got.envelope.from.appId, std::string("org.ultraos.ultramail"));
        REQUIRE(got.envelope.flags & UltraMsgFlag_NoJournal);      // the feed lists the mail itself
        REQUIRE(!(got.envelope.flags & UltraMsgFlag_Silent));      // on screen
        const UltraCanvas::JSONValue* category = got.body.Find("category");
        REQUIRE(category && category->GetString() == "email.arrived");
        const UltraCanvas::JSONValue* entry = got.body.Find("desktopEntry");
        REQUIRE(entry && entry->GetString() == "UltraMail");
        const UltraCanvas::JSONValue* account = got.body.Find("accountId");
        REQUIRE(account && account->GetString() == "erika");
        const UltraCanvas::JSONValue* actions = got.body.Find("actions");
        REQUIRE(actions && actions->IsArray() && actions->GetSize() == 1);
    }

    // Not journaled: the feed's query never returns it.
    UltraMsgQuery query;
    query.topics = {UltraMsgTopics::SystemNotification};
    std::vector<UltraMsgMessage> rows;
    REQUIRE(UltraMsg_Query(desktop, query, rows));
    REQUIRE(rows.empty());

    // A click on another application's notification is not UltraMail's.
    UltraCanvas::JSONValue stray = UltraCanvas::JSONValue::MakeObject();
    stray.Set("notificationId", "01ARZ3NDEKTSV4RRFFQ69G5FAV");
    stray.Set("actionId", "default");
    REQUIRE(UltraMsg_Post(desktop, UltraMsgTopics::SystemNotificationAction, stray));
    // The click on UltraMail's comes back to it.
    UltraCanvas::JSONValue click = UltraCanvas::JSONValue::MakeObject();
    click.Set("notificationId", id);
    click.Set("actionId", "default");
    REQUIRE(UltraMsg_Post(desktop, UltraMsgTopics::SystemNotificationAction, click));
    bool clicked = false;
    for (int i = 0; i < 300 && !clicked; ++i) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            clicked = !clickedId.empty();
        }
        if (!clicked) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(clicked);
    {
        std::lock_guard<std::mutex> lock(mutex);
        REQUIRE_EQ(clickedId, id);
        REQUIRE_EQ(clickedAction, std::string("default"));
    }

    UltraMsg_Unsubscribe(sub);
    UltraMsg_Disconnect(desktop);
    publisher.Disconnect();
}

#endif
