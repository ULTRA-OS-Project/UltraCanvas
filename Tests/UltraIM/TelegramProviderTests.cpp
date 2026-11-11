// Tests/UltraIM/TelegramProviderTests.cpp
// Telegram provider against FakeTdJson, a libtdjson stand-in holding a small
// Telegram in memory: sign-in in steps (phone, a wrong and the right code, the
// two-step password, registering a new number; by a QR code, scanned from a
// signed-in app, asked for at the start or in place of a code), finding people by username
// and phone number, a private chat (delivery, the echo, read receipts,
// typing, edit, deletion, a reply), a group, history paged back, presence,
// the stored session resumed with nothing asked, a step Telegram never
// answers failing with Timeout instead of hanging; a contact's profile
// picture fetched, a photo, a video and a file sent and received - the photo
// and the video's preview picture arriving by themselves, the video when
// asked for.
// Version: 0.1.4 - avatars, photos, videos and files
// Version: 0.1.3 - sign-in by a QR code
// Version: 0.1.2 - the code asked for another way
// Version: 0.1.1 - the step timeout
// Version: 0.1.0
// Last Modified: 2026-10-29
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

// A QR code instead of the number: the application's keys, nothing else.
UltraIMCredentials QrCode() {
    UltraIMCredentials c = Phone(std::string());
    c.loginType = UltraIMLoginType::QRCode;
    return c;
}

// The other device: the app signed in as `phone` scans the link, or Telegram
// replaces the link with a fresh one.
TdObject OtherDevice(const char* what, const std::string& link, const std::string& phone = std::string()) {
    TdObject r = TdObject::MakeObject();
    r.Set("@type", TdObject(what));
    r.Set("link", TdObject(link));
    if (!phone.empty()) r.Set("phone", TdObject(phone));
    return TdJsonLibrary::Instance().Execute(r);
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
                   const std::string& password, const std::string& photo = std::string()) {
    TdObject r = TdObject::MakeObject();
    r.Set("@type", TdObject("fakeAddAccount"));
    r.Set("phone", TdObject(phone));
    r.Set("first_name", TdObject(first));
    r.Set("username", TdObject(username));
    r.Set("password", TdObject(password));
    r.Set("photo", TdObject(photo));
    return TdInt(TdJsonLibrary::Instance().Execute(r)["id"]);
}

// A file to send: a few bytes under `base`.
std::string WriteFile(const fs::path& base, const std::string& name) {
    const std::string path = UltraCanvas::PathToUtf8(base / UltraCanvas::PathFromUtf8(name));
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "wb");
    if (f) {
        std::fputs("not really media", f);
        std::fclose(f);
    }
    return path;
}

bool WaitAttachment(const std::function<void(UltraIMAttachmentCallback)>& start, UltraIMAttachment& out,
                    UltraIMResult* result = nullptr) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false, ok = false;
    start([&](const UltraIMResult& r, const UltraIMAttachment& a) {
        std::lock_guard<std::mutex> lock(mutex);
        out = a;
        ok = r.success;
        if (result) *result = r;
        done = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; });
    return ok;
}

void TestTelegram() {
    std::string error;
    CHECK(TdJsonLibrary::Instance().Load({ULTRAIM_TEST_FAKE_TDJSON}, error));
    CHECK(TdJsonLibrary::Instance().GetVersion() == "fake");
    const int64_t aliceId = AddAccount("15550001", "Alice", "alice", "");
    const int64_t bobId = AddAccount("15550002", "Bob", "bob", "hunter2", "/pictures/bob.jpg");
    const int64_t erinId = AddAccount("15550004", "Erin", "erin", "hunter4");

    const fs::path base = fs::temp_directory_path() / ("ultraim-telegram-" + std::to_string(::rand()));
    std::error_code ec;
    fs::remove_all(base, ec);
    Events aliceEvents, bobEvents, carolEvents, erinEvents;
    auto alice = Device(base / "alice", aliceEvents);
    auto bob = Device(base / "bob", bobEvents);
    auto carol = Device(base / "carol", carolEvents);
    auto erin = Device(base / "erin", erinEvents);
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

    // A QR code instead of the number: the step OtherDevice, its hint the
    // link to show. No number and no QR code asked for is refused at once.
    UltraIMCredentials nothing = Phone(std::string());
    r = SignIn(*erin, nothing);
    CHECK(!r && r.code == UltraIMResultCode::NotAuthenticated);
    r = SignIn(*erin, QrCode());
    CHECK(!r && r.code == UltraIMResultCode::NeedsInput);
    CHECK(erin->GetAuthStep() == UltraIMAuthStep::OtherDevice);
    std::string link = erin->GetAuthHint();
    CHECK(link.rfind("tg://login?token=", 0) == 0);
    CHECK(r.message == link);
    CHECK(erin->GetAuthResendHint().empty());
    CHECK(erinEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::AuthStepChange && e.authStep == UltraIMAuthStep::OtherDevice &&
               e.errorText == link;
    }));
    // A number while the QR code waits starts over with it (TDLib takes
    // none in that state) - and the QR code can be asked for in its turn in
    // place of the code that never comes.
    r = SignIn(*erin, Phone("+15550004"));
    CHECK(!r && r.code == UltraIMResultCode::NeedsInput && erin->GetAuthStep() == UltraIMAuthStep::Code);
    r = SignIn(*erin, QrCode());
    CHECK(!r && r.code == UltraIMResultCode::NeedsInput && erin->GetAuthStep() == UltraIMAuthStep::OtherDevice);
    CHECK(erin->GetAuthHint() != link);           // a fresh one
    const std::string earlier = link;
    link = erin->GetAuthHint();
    // Telegram replaces the link before it expires: the new one as AuthStepChange.
    CHECK(TdType(OtherDevice("fakeRefreshLoginToken", "tg://login?token=nobody")) == "error");
    CHECK(TdType(OtherDevice("fakeRefreshLoginToken", link)) == "ok");
    UltraIMEvent refreshed;
    CHECK(erinEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::AuthStepChange && e.authStep == UltraIMAuthStep::OtherDevice &&
               e.errorText != link && e.errorText != earlier && e.errorText.rfind("tg://login?token=", 0) == 0;
    }, &refreshed));
    link = refreshed.errorText;
    CHECK(erin->GetAuthHint() == link);
    // Scanned by the app signed in as Dave: on to the two-step password, with
    // no SignIn pending - the step arrives as the event alone.
    CHECK(TdType(OtherDevice("fakeScanLoginToken", link, "+1 555 0004")) == "ok");
    CHECK(erinEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::AuthStepChange && e.authStep == UltraIMAuthStep::Password;
    }));
    CHECK(erin->GetAuthStep() == UltraIMAuthStep::Password);
    pass.password = "hunter4";
    CHECK(SignIn(*erin, pass));
    CHECK(erin->GetConnectionState() == UltraIMConnectionState::Connected);
    CHECK(erin->GetLocalUserId() == std::to_string(erinId));
    CHECK(erin->HasStoredSession());

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
    CHECK(UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::ImageMessages));
    CHECK(UltraIM_HasCapability(alice->GetCapabilities(), UltraIMCapabilityFlags::FileTransfer));

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
    // Bob's profile picture: fetched by itself, and the conversation - in
    // the list now that it has a message - says so.
    CHECK(aliceEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == dm.conversationId &&
               e.conversation.avatarPath == "/pictures/bob.jpg";
    }));
    {
        std::vector<UltraIMConversation> listed;
        alice->FetchConversations([&](const UltraIMResult&, const std::vector<UltraIMConversation>& l) { listed = l; });
        CHECK(listed.size() == 1 && listed.front().avatarPath == "/pictures/bob.jpg");
        std::vector<UltraIMContact> contacts;
        alice->FetchContacts([&](const UltraIMResult&, const std::vector<UltraIMContact>& l) { contacts = l; });
        CHECK(contacts.size() == 1 && contacts.front().avatarPath == "/pictures/bob.jpg");
    }

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

    // A photo: the sender's echo points at the file; the other side gets the
    // message first, then its picture - fetched by itself - as an update.
    const std::string picture = WriteFile(base, "pic.jpg");
    const std::string clip = WriteFile(base, "clip.mp4");
    const std::string notes = WriteFile(base, "notes.pdf");
    UltraIMSendOptions o3;
    o3.clientMessageId = "alice-photo";
    CHECK(alice->SendImage(dm.conversationId, UltraCanvas::PathToUtf8(base / "missing.jpg"), "", o3).code ==
          UltraIMResultCode::InvalidArgument);
    CHECK(alice->SendImage(dm.conversationId, picture, "look", o3));
    UltraIMEvent photoSent, photoEcho, photoGot, photoHere;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageDelivered && e.message.clientMessageId == "alice-photo" &&
               e.deliveryStatus == UltraIMDeliveryStatus::Sent;
    }, &photoSent));
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.clientMessageId == "alice-photo";
    }, &photoEcho));
    CHECK(photoEcho.message.type == UltraIMMessageType::Image && photoEcho.message.text == "look");
    CHECK(photoEcho.message.attachment.isDownloaded && photoEcho.message.attachment.localPath == picture);
    CHECK(photoEcho.message.attachment.width == 800 && photoEcho.message.attachment.height == 600);
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::Image;
    }, &photoGot));
    CHECK(photoGot.message.text == "look" && photoGot.message.senderAvatarPath.empty());   // Alice has no picture
    CHECK(photoGot.message.attachment.width == 800 && !photoGot.message.attachment.remoteUrl.empty());
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == photoGot.message.messageId &&
               e.message.attachment.isDownloaded;
    }, &photoHere));
    CHECK(photoHere.message.attachment.localPath == picture && photoHere.message.type == UltraIMMessageType::Image);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == got.conversationId &&
               e.conversation.lastMessageSnippet == "[photo] look";
    }));
    // Bob's messages to Alice carry his picture.
    CHECK(bob->SendText(got.conversationId, "nice", UltraIMSendOptions()));
    UltraIMEvent nice;
    CHECK(aliceEvents.WaitFor([](const UltraIMEvent& e) { return IsReceived(e, "nice"); }, &nice));
    CHECK(nice.message.senderAvatarPath == "/pictures/bob.jpg");

    // A video: its preview picture comes by itself, the video when asked for.
    CHECK(alice->SendVideo(dm.conversationId, clip, "", UltraIMSendOptions()));
    UltraIMEvent videoGot, videoThumb, videoHere;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::Video;
    }, &videoGot));
    CHECK(videoGot.message.attachment.fileName == "clip.mp4" && videoGot.message.attachment.durationSeconds == 12.0);
    CHECK(videoGot.message.attachment.width == 640 && !videoGot.message.attachment.isDownloaded);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == videoGot.message.messageId &&
               !e.message.attachment.thumbnailPath.empty();
    }, &videoThumb));
    CHECK(videoThumb.message.attachment.thumbnailPath == clip + ".thumb.jpg" && !videoThumb.message.attachment.isDownloaded);
    UltraIMAttachment fetched;
    UltraIMResult fetchResult;
    CHECK(!WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(got.conversationId, got.message.messageId, cb); },
                          fetched, &fetchResult));
    CHECK(fetchResult.code == UltraIMResultCode::NotFound);      // a text has none
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(got.conversationId, videoGot.message.messageId, cb); },
                         fetched));
    CHECK(fetched.isDownloaded && fetched.localPath == clip && fetched.fileName == "clip.mp4");
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageUpdate && e.message.messageId == videoGot.message.messageId &&
               e.message.attachment.isDownloaded;
    }, &videoHere));
    CHECK(videoHere.message.attachment.localPath == clip && videoHere.message.attachment.thumbnailPath == clip + ".thumb.jpg");
    // Asked again: answered from disk.
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(got.conversationId, videoGot.message.messageId, cb); },
                         fetched));
    CHECK(fetched.localPath == clip);

    // A file: its name, and the file when asked for.
    CHECK(alice->SendFile(dm.conversationId, notes, UltraIMSendOptions()));
    UltraIMEvent fileGot;
    CHECK(bobEvents.WaitFor([](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::MessageReceive && e.message.type == UltraIMMessageType::File;
    }, &fileGot));
    CHECK(fileGot.message.attachment.fileName == "notes.pdf" && !fileGot.message.attachment.isDownloaded);
    CHECK(bobEvents.WaitFor([&](const UltraIMEvent& e) {
        return e.type == UltraIMEventType::ConversationUpdate && e.conversationId == got.conversationId &&
               e.conversation.lastMessageSnippet == "[file] notes.pdf";
    }));
    CHECK(WaitAttachment([&](UltraIMAttachmentCallback cb) { bob->DownloadAttachment(got.conversationId, fileGot.message.messageId, cb); },
                         fetched));
    CHECK(fetched.localPath == notes && fetched.isDownloaded);
    // History brings the attachments back with their files.
    {
        const auto recent = Fetch(*bob, got.conversationId, "", 10);
        int pictures = 0;
        for (const auto& m : recent) {
            if (m.type == UltraIMMessageType::Image && m.attachment.localPath == picture) ++pictures;
        }
        CHECK(pictures == 1);
    }

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
        CHECK(older.size() == 11);      // hi bob!, hi alice, the photo, nice, the video, the file, messages 0-4
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
    erin->Shutdown();
    fs::remove_all(base, ec);
}

} // namespace

int main() {
    TestTelegram();
    std::printf("TelegramProviderTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
