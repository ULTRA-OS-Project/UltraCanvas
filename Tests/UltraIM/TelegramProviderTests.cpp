// Tests/UltraIM/TelegramProviderTests.cpp
// Telegram provider against FakeTdJson, a libtdjson stand-in holding a small
// Telegram in memory: sign-in in steps (phone, a wrong and the right code, the
// two-step password, registering a new number), finding people by username
// and phone number, a private chat (delivery, the echo, read receipts,
// typing, edit, deletion, a reply), a group, history paged back, presence,
// the stored session resumed with nothing asked, and a step Telegram never
// answers failing with Timeout instead of hanging.
// Version: 0.1.2 - the code asked for another way
// Version: 0.1.1 - the step timeout
// Version: 0.1.0
// Last Modified: 2026-10-25
// Author: UltraCanvas Framework

#include "TdJson.h"
#include "TelegramProvider.h"

#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace UltraIMTelegram;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace fs = std::filesystem;

struct Events {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<UltraIMEvent> list;

    UltraIMEventSink Sink() {
        return [this](const UltraIMEvent& e) {
            std::lock_guard<std::mutex> lock(mutex);
            list.push_back(e);
            changed.notify_all();
        };
    }
    bool WaitFor(const std::function<bool(const UltraIMEvent&)>& match, UltraIMEvent* found = nullptr,
                 int seconds = 10) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(seconds), [&]() {
            for (const auto& e : list) {
                if (match(e)) {
                    if (found) *found = e;
                    return true;
                }
            }
            return false;
        });
    }
};

UltraIMResult SignIn(TelegramProvider& p, const UltraIMCredentials& c) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    UltraIMResult out;
    p.SignIn(c, [&](const UltraIMResult& r) {
        std::lock_guard<std::mutex> lock(mutex);
        out = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; }))
        return UltraIMResult::Error(UltraIMResultCode::Timeout, "no answer");
    return out;
}

bool WaitConversation(const std::function<void(UltraIMConversationCallback)>& start, UltraIMConversation& out,
                      UltraIMResult* result = nullptr) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMConversation& c) {
        std::lock_guard<std::mutex> lock(mutex);
        out = c;
        ok = r.success;
        if (result) *result = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; });
    return ok;
}

std::vector<UltraIMMessage> Fetch(TelegramProvider& p, const std::string& conversation, const std::string& before,
                                  int limit) {
    std::vector<UltraIMMessage> out;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    p.FetchMessages(conversation, before, limit, [&](const UltraIMResult&, const std::vector<UltraIMMessage>& m) {
        std::lock_guard<std::mutex> lock(mutex);
        out = m;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; });
    return out;
}

bool IsReceived(const UltraIMEvent& e, const std::string& text) {
    return e.type == UltraIMEventType::MessageReceive && e.message.text == text;
}

UltraIMCredentials Phone(const std::string& phone) {
    UltraIMCredentials c;
    c.loginType = UltraIMLoginType::PhoneOTP;
    c.phoneNumber = phone;
    c.apiId = "12345";
    c.apiHash = "0123456789abcdef0123456789abcdef";
    return c;
}

UltraIMCredentials Code(const std::string& code) {
    UltraIMCredentials c;
    c.otpCode = code;
    return c;
}

std::unique_ptr<TelegramProvider> Device(const fs::path& dir, Events& events) {
    auto p = std::make_unique<TelegramProvider>("telegram");
    p->SetLibraryPath(ULTRAIM_TEST_FAKE_TDJSON);
    UltraIMConfig config;
    config.dataDirectory = UltraCanvas::PathToUtf8(dir);
    p->Initialize(config, events.Sink());
    return p;
}

int64_t AddAccount(const std::string& phone, const std::string& first, const std::string& username,
                   const std::string& password) {
    TdObject r = TdObject::MakeObject();
    r.Set("@type", TdObject("fakeAddAccount"));
    r.Set("phone", TdObject(phone));
    r.Set("first_name", TdObject(first));
    r.Set("username", TdObject(username));
    r.Set("password", TdObject(password));
    return TdInt(TdJsonLibrary::Instance().Execute(r)["id"]);
}

void TestTelegram() {
    std::string error;
    CHECK(TdJsonLibrary::Instance().Load({ULTRAIM_TEST_FAKE_TDJSON}, error));
    CHECK(TdJsonLibrary::Instance().GetVersion() == "fake");
    const int64_t aliceId = AddAccount("15550001", "Alice", "alice", "");
    const int64_t bobId = AddAccount("15550002", "Bob", "bob", "hunter2");

    const fs::path base = fs::temp_directory_path() / ("ultraim-telegram-" + std::to_string(::rand()));
    std::error_code ec;
    fs::remove_all(base, ec);
    Events aliceEvents, bobEvents, carolEvents;
    auto alice = Device(base / "alice", aliceEvents);
    auto bob = Device(base / "bob", bobEvents);
    auto carol = Device(base / "carol", carolEvents);
    CHECK(!alice->HasStoredSession());
    CHECK(alice->GetLoginType() == UltraIMLoginType::PhoneOTP);

    // Sign-in in steps. Without API credentials, refused at once.
    UltraIMCredentials noKeys = Phone("+1 555 0001");
    noKeys.apiId.clear();
    UltraIMResult r = SignIn(*alice, noKeys);
    CHECK(!r && r.code == UltraIMResultCode::InvalidArgument);
    r = SignIn(*alice, Phone("+1 555 0001"));
    CHECK(!r && r.code == UltraIMResultCode::NeedsInput);
    CHECK(alice->GetAuthStep() == UltraIMAuthStep::Code);
    // To the app the number is signed in on first, said so; SMS offered next.
    CHECK(alice->GetAuthHint().find("Telegram apps") != std::string::npos);
    CHECK(alice->GetAuthResendHint() == "Send it by SMS instead");
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::AuthStepChange && e.authStep == UltraIMAuthStep::Code;
    }));
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        UltraIMResult resent;
        alice->ResendAuthCode([&](const UltraIMResult& rr) {
            std::lock_guard<std::mutex> lock(mutex);
            resent = rr;
            done = true;
            cv.notify_all();
        });
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; }) && resent);
    }
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::AuthStepChange && e.errorText.find("SMS") != std::string::npos;
    }));
    CHECK(alice->GetAuthHint().find("SMS") != std::string::npos);
    CHECK(alice->GetAuthResendHint().empty());        // no way after SMS
    {
        UltraIMResult again;
        alice->ResendAuthCode([&](const UltraIMResult& rr) { again = rr; });
        CHECK(!again && again.code == UltraIMResultCode::CapabilityMissing);
    }
    r = SignIn(*alice, Code("00000"));
    CHECK(!r && r.code == UltraIMResultCode::AuthFailed && r.message == "PHONE_CODE_INVALID");
    CHECK(alice->GetAuthStep() == UltraIMAuthStep::Code);            // still asking
    r = SignIn(*alice, Code("12345"));
    CHECK(r);
    CHECK(alice->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(alice->GetLocalUserId() == std::to_string(aliceId));
    CHECK(alice->GetAuthStep() == UltraIMAuthStep::Idle);
    CHECK(alice->HasStoredSession());

    // The two-step password: wrong, then right.
    CHECK(SignIn(*bob, Phone("+15550002")).code == UltraIMResultCode::NeedsInput);
    r = SignIn(*bob, Code("12345"));
    CHECK(!r && r.code == UltraIMResultCode::NeedsInput && bob->GetAuthStep() == UltraIMAuthStep::Password);
    CHECK(bob->GetAuthHint().find("the usual") != std::string::npos);
    UltraIMCredentials pass;
    pass.password = "hunter3";
    r = SignIn(*bob, pass);
    CHECK(!r && r.code == UltraIMResultCode::AuthFailed);
    pass.password = "hunter2";
    CHECK(SignIn(*bob, pass));
    CHECK(bob->GetLocalUserId() == std::to_string(bobId));

    // A number Telegram does not know: registering, with a name.
    CHECK(SignIn(*carol, Phone("+15550003")).code == UltraIMResultCode::NeedsInput);
    r = SignIn(*carol, Code("12345"));
    CHECK(!r && carol->GetAuthStep() == UltraIMAuthStep::Registration);
    UltraIMCredentials name;
    name.displayName = "Carol Smith";
    CHECK(SignIn(*carol, name));
    const std::string carolId = carol->GetLocalUserId();
    CHECK(!carolId.empty());

    // Finding people.
    UltraIMConversation dm, again, none;
    UltraIMResult found;
    CHECK(!WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@nobody", cb); }, none, &found));
    CHECK(found.code == UltraIMResultCode::NotFound);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("@bob", cb); }, dm));
    CHECK(!dm.isGroup && dm.directContactId == std::to_string(bobId) && dm.title == "Bob");
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("+1 (555) 000-2", cb); }, again));
    CHECK(again.conversationId == dm.conversationId);
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) { alice->AddContact("t.me/bob", cb); }, again));
    CHECK(again.conversationId == dm.conversationId);

    // A message: delivered under our client id, then its echo; Bob gets it.
    UltraIMSendOptions o1;
    o1.clientMessageId = "alice-1";
    CHECK(alice->SendText(dm.conversationId, "hi bob", o1));
    UltraIMEvent sent, echo, got;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &sent));
    CHECK(!sent.message.messageId.empty() && sent.message.messageId != "alice-1");
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return IsReceived(e, "hi bob") && e.message.clientMessageId == "alice-1";
    }, &echo));
    CHECK(echo.message.isOwn && echo.message.messageId == sent.message.messageId);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hi bob"); }, &got));
    CHECK(got.message.senderName == "Alice" && !got.message.isOwn);
    CHECK(got.conversationId == std::to_string(aliceId));   // Bob's view of the chat
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == got.conversationId &&
               e.conversation.unreadCount == 1 && e.conversation.lastMessageSnippet == "hi bob";
    }));

    // Bob reads it: Alice's message turns read.
    CHECK(bob->MarkRead(got.conversationId, ""));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-1" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Read;
    }));

    // Typing, an edit, a deletion, a reply.
    CHECK(bob->SetTyping(got.conversationId, true));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::TypingChange && e.isTyping && e.contactId == std::to_string(bobId);
    }));
    CHECK(alice->EditMessage(dm.conversationId, sent.message.messageId, "hi bob!"));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == got.message.messageId &&
               e.message.text == "hi bob!" && e.message.isEdited;
    }));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.clientMessageId == "alice-1";
    }));
    UltraIMSendOptions o2;
    o2.clientMessageId = "alice-2";
    CHECK(alice->SendText(dm.conversationId, "oops", o2));
    UltraIMEvent oops;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "oops"); }, &oops));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "oops"); }, &oops));
    CHECK(alice->DeleteMessage(dm.conversationId, oops.message.messageId));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelete && e.message.messageId == oops.message.messageId;
    }));
    UltraIMSendOptions reply;
    reply.replyToMessageId = got.message.messageId;
    CHECK(bob->SendText(got.conversationId, "hi alice", reply));
    UltraIMEvent answered;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "hi alice"); }, &answered));
    CHECK(answered.message.replyTo.messageId == sent.message.messageId);
    CHECK(answered.message.replyTo.senderId == std::to_string(aliceId));

    // A group of three, by username and by phone number.
    UltraIMConversation group;
    CHECK(WaitConversation([&](UltraIMConversationCallback cb) {
        alice->CreateGroup("Team", {"@bob", "+15550003"}, cb);
    }, group));
    CHECK(group.isGroup && group.title == "Team");
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == group.conversationId &&
               e.conversation.isGroup && e.conversation.title == "Team";
    }));
    CHECK(carolEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::SystemEvent &&
               e.message.text == "Alice created the group \"Team\"";
    }));
    CHECK(carol->SendText(group.conversationId, "hi team", UltraIMSendOptions()));
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return IsReceived(e, "hi team") && e.conversationId == group.conversationId && e.message.senderName == "Carol Smith";
    }));
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) { return IsReceived(e, "hi team"); }));

    // History: more than one page of it, paged back.
    for (int i = 0; i < 30; ++i) alice->SendText(dm.conversationId, "message " + std::to_string(i), UltraIMSendOptions());
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "message 29"); }));
    const auto page = Fetch(*bob, got.conversationId, "", 25);
    CHECK(page.size() == 25);
    if (!page.empty()) CHECK(page.back().text == "message 29");
    bool ordered = true;
    for (size_t i = 1; i < page.size(); ++i) ordered = ordered && page[i - 1].timestampMs <= page[i].timestampMs;
    CHECK(ordered);
    if (!page.empty()) {
        const auto older = Fetch(*bob, got.conversationId, page.front().messageId, 50);
        CHECK(older.size() == 7);       // hi bob!, hi alice, messages 0-4
        if (!older.empty()) CHECK(older.front().text == "hi bob!" && older.back().text == "message 4");
    }

    std::vector<UltraIMConversation> list;
    alice->FetchConversations([&](const UltraIMResult&, const std::vector<UltraIMConversation>& l) { list = l; });
    CHECK(list.size() == 2);
    if (list.size() == 2) CHECK(list[0].conversationId == dm.conversationId);   // the newest message first

    // Bob goes: Alice sees him offline.
    bob->Shutdown();
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::PresenceChange && e.contactId == std::to_string(bobId) &&
               e.presence == UltraIMPresence::Offline;
    }));

    // The stored session: nothing asked, the chats come back.
    alice->Shutdown();
    Events resumedEvents;
    auto resumed = Device(base / "alice", resumedEvents);
    CHECK(resumed->HasStoredSession());
    CHECK(SignIn(*resumed, UltraIMCredentials()));
    CHECK(resumed->GetLocalUserId() == std::to_string(aliceId));
    CHECK(resumedEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == group.conversationId;
    }));
    resumed->Shutdown();

    // A step Telegram never answers (the network is down): the sign-in
    // fails with Timeout and says what TDLib was doing, instead of leaving
    // the application waiting forever.
    Events daveEvents;
    auto dave = Device(base / "dave", daveEvents);
    dave->SetStepTimeout(1500);
    r = SignIn(*dave, Phone("+1 555 9999"));
    CHECK(!r && r.code == UltraIMResultCode::Timeout);
    CHECK(r.message.find("has not answered") != std::string::npos);
    CHECK(daveEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConnectionStateChange && e.connectionState == UltraIMConnectionState::Error;
    }));
    dave->Shutdown();

    carol->Shutdown();
    fs::remove_all(base, ec);
}

} // namespace

int main() {
    TestTelegram();
    std::printf("TelegramProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
