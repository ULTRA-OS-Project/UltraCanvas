// Tests/UltraMessage/test_ultramessage.cpp
// UltraMessage Phase 1: codec and topics, schemas, an in-process broker on a
// private bus driven over the real transport by several endpoints —
// subscriptions, recorded delivery with bounce, request/reply, the journal
// and its queries, replay, lifecycle notices, the typed topic helpers.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraMessage/UltraMessage.h>
#include <UltraMessage/UltraMessageEndpoint.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <process.h>
#  define ULTRAMSG_TEST_GETPID _getpid
#else
#  include <unistd.h>
#  define ULTRAMSG_TEST_GETPID getpid
#endif

using UltraCanvas::JSONValue;
using namespace std::chrono_literals;

namespace {

// A private bus per test process, so a broker of the user's own never
// answers and parallel test runs never meet.
std::string TestBusPath() {
    static const std::string path = [] {
        const std::string pid = std::to_string(ULTRAMSG_TEST_GETPID());
#ifdef _WIN32
        return "\\\\.\\pipe\\UltraMessageTest-" + pid;
#else
        const char* tmp = std::getenv("TMPDIR");
        std::string base = tmp && *tmp ? tmp : "/tmp";
        return base + "/ultramsg-test-" + pid + "/bus.sock";
#endif
    }();
    return path;
}

UltraMsgHandle Connect(const std::string& appId, const std::string& name = "") {
    UltraMsgConnectOptions options;
    options.appId = appId;
    options.displayName = name.empty() ? appId : name;
    options.busPath = TestBusPath();
    options.journalPath = ":memory:";
    UltraMsgResult error;
    UltraMsgHandle handle = UltraMsg_Connect(options, &error);
    if (handle == UltraMsgInvalidHandle)
        throw ultramsg_test::Failure{"connect " + appId + " failed: " + error.message};
    return handle;
}

// Pumps queued callbacks until `done` holds or `timeout` elapses.
bool WaitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        UltraMsg_ProcessPending();
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    UltraMsg_ProcessPending();
    return done();
}

JSONValue Mail(const std::string& subject, const std::string& snippet = "") {
    UltraMessage::MailMessage m;
    m.account = "test@example.org";
    m.folder = "INBOX";
    m.from = {"Ada", "ada@example.org"};
    m.subject = subject;
    m.snippet = snippet;
    return UltraMessage::MakeMailMessage(m);
}

JSONValue Chat(const std::string& conversation, const std::string& text, const std::string& sender = "Bob") {
    UltraMessage::MessagingMessage m;
    m.service = "telegram";
    m.account = "me";
    m.conversationId = conversation;
    m.conversationTitle = "Chat " + conversation;
    m.sender = {"u1", sender, ""};
    m.text = text;
    return UltraMessage::MakeMessagingMessage(m);
}

struct Scoped {
    UltraMsgHandle handle;
    ~Scoped() { UltraMsg_Disconnect(handle); }
};

} // namespace

// ===========================================================================
// Codec and topics
// ===========================================================================

TEST(topic_validation) {
    REQUIRE(UltraMsg_IsValidTopic("mail.message"));
    REQUIRE(UltraMsg_IsValidTopic("com.example.my-app.thing_1"));
    REQUIRE(!UltraMsg_IsValidTopic(""));
    REQUIRE(!UltraMsg_IsValidTopic("Mail.Message"));
    REQUIRE(!UltraMsg_IsValidTopic("mail..message"));
    REQUIRE(!UltraMsg_IsValidTopic("mail.*"));
    REQUIRE(UltraMsg_IsValidAppId("org.ultraos.ultramail"));
    REQUIRE(UltraMsg_IsValidAppId("UltraMail"));
    REQUIRE(!UltraMsg_IsValidAppId("org ultraos"));
    REQUIRE(!UltraMsg_IsValidAppId(""));
}

TEST(topic_pattern_matching) {
    REQUIRE(UltraMsg_TopicMatches("mail.message", "mail.message"));
    REQUIRE(!UltraMsg_TopicMatches("mail.message", "mail.messages"));
    REQUIRE(UltraMsg_TopicMatches("mail.*", "mail.message"));
    REQUIRE(!UltraMsg_TopicMatches("mail.*", "mail.message.extra"));
    REQUIRE(!UltraMsg_TopicMatches("mail.*", "mail"));
    REQUIRE(UltraMsg_TopicMatches("*.message", "mail.message"));
    REQUIRE(UltraMsg_TopicMatches("*.message", "messaging.message"));
    REQUIRE(!UltraMsg_TopicMatches("*.message", "system.notification"));
    REQUIRE(UltraMsg_TopicMatches("#", "anything.at.all"));
    REQUIRE(UltraMsg_TopicMatches("app.#", "app.command.invoke"));
    REQUIRE(UltraMsg_TopicMatches("app.#", "app.open"));
    REQUIRE(!UltraMsg_TopicMatches("app.#", "app"));
    REQUIRE(!UltraMsg_TopicMatches("app.#", "mail.message"));
}

// ===========================================================================
// Schemas
// ===========================================================================

TEST(well_known_schemas_validate) {
    REQUIRE(UltraMsg_IsPersistentTopic("mail.message"));
    REQUIRE(UltraMsg_IsPersistentTopic("messaging.message"));
    REQUIRE(UltraMsg_IsPersistentTopic("system.notification"));
    REQUIRE(!UltraMsg_IsPersistentTopic("app.open.request"));
    REQUIRE(!UltraMsg_IsPersistentTopic("com.example.unknown"));

    REQUIRE(UltraMsg_Validate("mail.message", Mail("hi")).ok);
    JSONValue missing = JSONValue::MakeObject();
    missing.Set("account", "a");
    UltraMsgResult r = UltraMsg_Validate("mail.message", missing);
    REQUIRE(!r.ok);
    REQUIRE(r.code == UltraMsgResultCode::SchemaViolation);

    JSONValue wrongType = Mail("hi");
    wrongType.Set("read", "yes");
    REQUIRE(!UltraMsg_Validate("mail.message", wrongType).ok);

    // Unknown topics are unconstrained.
    REQUIRE(UltraMsg_Validate("com.example.free", JSONValue(42)).ok);
}

TEST(vendor_schema_registration) {
    UltraMsgTopicSchema schema;
    schema.topic = "com.example.test.*";
    schema.persistent = true;
    schema.fields = {{"value", "integer", true}};
    REQUIRE(UltraMsg_RegisterSchema(schema).ok);
    REQUIRE(UltraMsg_IsPersistentTopic("com.example.test.thing"));
    JSONValue good = JSONValue::MakeObject();
    good.Set("value", 3);
    REQUIRE(UltraMsg_Validate("com.example.test.thing", good).ok);
    JSONValue bad = JSONValue::MakeObject();
    bad.Set("value", "three");
    REQUIRE(!UltraMsg_Validate("com.example.test.thing", bad).ok);

    UltraMsgTopicSchema invalid;
    invalid.topic = "Not Valid";
    REQUIRE(!UltraMsg_RegisterSchema(invalid).ok);
}

// ===========================================================================
// Connecting
// ===========================================================================

TEST(connect_hosts_a_broker_and_lists_endpoints) {
    Scoped a{Connect("org.test.alpha", "Alpha")};
    Scoped b{Connect("org.test.beta", "Beta")};
    REQUIRE(UltraMsg_IsConnected(a.handle));
    REQUIRE(UltraMsg_IsConnected(b.handle));
    REQUIRE(UltraMsg_IsAvailable(TestBusPath()));

    UltraMsgBrokerInfo broker;
    REQUIRE(UltraMsg_GetBrokerInfo(a.handle, broker).ok);
    REQUIRE(broker.inProcess);
    REQUIRE_EQ(broker.busPath, TestBusPath());
    REQUIRE(broker.endpointCount >= 2);

    UltraMsgEndpointInfo self;
    REQUIRE(UltraMsg_GetEndpointInfo(a.handle, self).ok);
    REQUIRE_EQ(self.appId, std::string("org.test.alpha"));
    REQUIRE(!self.instanceId.empty());
#ifndef _WIN32
    REQUIRE(self.verified);
    REQUIRE_EQ(self.processId, static_cast<int>(ULTRAMSG_TEST_GETPID()));
#endif

    std::vector<UltraMsgEndpointInfo> endpoints;
    REQUIRE(UltraMsg_ListEndpoints(a.handle, endpoints).ok);
    bool sawAlpha = false, sawBeta = false;
    for (const auto& e : endpoints) {
        if (e.appId == "org.test.alpha") sawAlpha = true;
        if (e.appId == "org.test.beta" && e.displayName == "Beta") sawBeta = true;
    }
    REQUIRE(sawAlpha);
    REQUIRE(sawBeta);

    std::vector<std::string> instances;
    REQUIRE(UltraMsg_ResolveApp(a.handle, "org.test.beta", instances).ok);
    REQUIRE_EQ(instances.size(), static_cast<size_t>(1));
    REQUIRE(UltraMsg_ResolveApp(a.handle, "org.test.nobody", instances).ok);
    REQUIRE(instances.empty());
}

TEST(connect_rejects_bad_app_id_and_missing_broker) {
    UltraMsgConnectOptions options;
    options.appId = "not valid";
    options.busPath = TestBusPath();
    UltraMsgResult error;
    REQUIRE(UltraMsg_Connect(options, &error) == UltraMsgInvalidHandle);
    REQUIRE(error.code == UltraMsgResultCode::InvalidArgument);

    options.appId = "org.test.lonely";
#ifdef _WIN32
    options.busPath = "\\\\.\\pipe\\UltraMessageTest-nobody-" + std::to_string(ULTRAMSG_TEST_GETPID());
#else
    options.busPath = TestBusPath() + ".nobody";
#endif
    options.startBrokerIfAbsent = false;
    REQUIRE(UltraMsg_Connect(options, &error) == UltraMsgInvalidHandle);
    REQUIRE(error.code == UltraMsgResultCode::BrokerUnavailable);
    REQUIRE(!UltraMsg_IsAvailable(options.busPath));
}

// ===========================================================================
// Notices and subscriptions
// ===========================================================================

TEST(post_reaches_matching_subscribers_only) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    std::vector<UltraMsgMessage> gotMail;
    std::vector<UltraMsgMessage> gotAll;
    int ownCount = 0;
    UltraMsgResult error;
    UltraMsgHandle subMail = UltraMsg_Subscribe(a.handle, "mail.*", [&](const UltraMsgMessage& m) { gotMail.push_back(m); }, {}, &error);
    REQUIRE(subMail != UltraMsgInvalidHandle);
    UltraMsgHandle subAll = UltraMsg_Subscribe(a.handle, "#", [&](const UltraMsgMessage& m) { gotAll.push_back(m); });
    REQUIRE(subAll != UltraMsgInvalidHandle);
    // B's own subscription must not see B's own post (includeOwn is off).
    UltraMsgHandle subOwn = UltraMsg_Subscribe(b.handle, "mail.message", [&](const UltraMsgMessage&) { ++ownCount; });
    REQUIRE(subOwn != UltraMsgInvalidHandle);

    std::string id;
    REQUIRE(UltraMsg_Post(b.handle, "mail.message", Mail("Invoice"), {}, &id).ok);
    REQUIRE_EQ(id.size(), static_cast<size_t>(26));
    REQUIRE(UltraMsg_Post(b.handle, "system.notification", JSONValue::MakeObject(), {}).ok == false); // schema: appName, summary required

    JSONValue notification = JSONValue::MakeObject();
    notification.Set("appName", "Test");
    notification.Set("summary", "Hello");
    REQUIRE(UltraMsg_Post(b.handle, "system.notification", notification).ok);

    REQUIRE(WaitFor([&] { return gotMail.size() == 1 && gotAll.size() >= 2; }));
    REQUIRE_EQ(gotMail[0].envelope.id, id);
    REQUIRE_EQ(gotMail[0].envelope.topic, std::string("mail.message"));
    REQUIRE_EQ(gotMail[0].envelope.from.appId, std::string("org.test.beta"));
    REQUIRE(gotMail[0].envelope.kind == UltraMsgKind::Notice);
    REQUIRE(gotMail[0].envelope.timestampMs > 0);
    REQUIRE_EQ(gotMail[0].body.Get("subject").GetString(), std::string("Invoice"));
    std::this_thread::sleep_for(50ms);
    UltraMsg_ProcessPending();
    REQUIRE_EQ(ownCount, 0);

    REQUIRE(UltraMsg_Unsubscribe(subMail).ok);
    REQUIRE(UltraMsg_Post(b.handle, "mail.message", Mail("After unsubscribe")).ok);
    REQUIRE(WaitFor([&] { return gotAll.size() >= 3; }));
    REQUIRE_EQ(gotMail.size(), static_cast<size_t>(1));
    REQUIRE(!UltraMsg_Unsubscribe(subMail).ok);
    UltraMsg_Unsubscribe(subAll);
    UltraMsg_Unsubscribe(subOwn);
}

TEST(include_own_and_targeted_posts) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    int aCount = 0, bCount = 0;
    UltraMsgSubscribeOptions own;
    own.includeOwn = true;
    UltraMsgHandle subA = UltraMsg_Subscribe(a.handle, "com.test.ping", [&](const UltraMsgMessage&) { ++aCount; }, own);
    UltraMsgHandle subB = UltraMsg_Subscribe(b.handle, "com.test.ping", [&](const UltraMsgMessage&) { ++bCount; });
    REQUIRE(subA != UltraMsgInvalidHandle && subB != UltraMsgInvalidHandle);

    REQUIRE(UltraMsg_Post(a.handle, "com.test.ping", JSONValue::MakeObject()).ok);
    REQUIRE(WaitFor([&] { return aCount == 1 && bCount == 1; }));

    UltraMsgSendOptions toB;
    toB.to = "org.test.beta";
    REQUIRE(UltraMsg_Post(a.handle, "com.test.ping", JSONValue::MakeObject(), toB).ok);
    REQUIRE(WaitFor([&] { return bCount == 2; }));
    std::this_thread::sleep_for(50ms);
    UltraMsg_ProcessPending();
    REQUIRE_EQ(aCount, 1);
    UltraMsg_Unsubscribe(subA);
    UltraMsg_Unsubscribe(subB);
}

TEST(worker_thread_delivery_needs_no_pump) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    std::atomic<int> count{0};
    UltraMsgSubscribeOptions options;
    options.onWorkerThread = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(a.handle, "com.test.worker", [&](const UltraMsgMessage&) { ++count; }, options);
    REQUIRE(sub != UltraMsgInvalidHandle);
    REQUIRE(UltraMsg_Post(b.handle, "com.test.worker", JSONValue::MakeObject()).ok);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (count.load() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    REQUIRE_EQ(count.load(), 1);
    UltraMsg_Unsubscribe(sub);
}

TEST(post_validates_topic_and_schema) {
    Scoped a{Connect("org.test.alpha")};
    REQUIRE(UltraMsg_Post(a.handle, "Bad Topic", JSONValue::MakeObject()).code == UltraMsgResultCode::InvalidArgument);
    REQUIRE(UltraMsg_Post(a.handle, "ultramessage.control.hello", JSONValue::MakeObject()).code == UltraMsgResultCode::InvalidArgument);
    REQUIRE(UltraMsg_Post(a.handle, "mail.message", JSONValue::MakeObject()).code == UltraMsgResultCode::SchemaViolation);
    UltraMsgSendOptions big;
    UltraMsgAttachment attachment;
    attachment.name = "big.bin";
    attachment.bytes.assign(UltraMsgMaxInlineAttachment + 1, 0);
    big.attachments.push_back(attachment);
    REQUIRE(UltraMsg_Post(a.handle, "com.test.big", JSONValue::MakeObject(), big).code == UltraMsgResultCode::TooLarge);
    REQUIRE(UltraMsg_Post(UltraMsgInvalidHandle, "com.test.x", JSONValue::MakeObject()).code == UltraMsgResultCode::NotConnected);
}

TEST(attachments_travel_inline) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    UltraMsgMessage received;
    bool got = false;
    UltraMsgHandle sub = UltraMsg_Subscribe(a.handle, "com.test.attach", [&](const UltraMsgMessage& m) { received = m; got = true; });
    UltraMsgSendOptions options;
    UltraMsgAttachment attachment;
    attachment.name = "note.txt";
    attachment.mimeType = "text/plain";
    attachment.bytes = {'h', 'e', 'l', 'l', 'o', 0, 255};
    options.attachments.push_back(attachment);
    REQUIRE(UltraMsg_Post(b.handle, "com.test.attach", JSONValue::MakeObject(), options).ok);
    REQUIRE(WaitFor([&] { return got; }));
    REQUIRE_EQ(received.attachments.size(), static_cast<size_t>(1));
    REQUIRE_EQ(received.attachments[0].name, std::string("note.txt"));
    REQUIRE(received.attachments[0].bytes == attachment.bytes);
    UltraMsg_Unsubscribe(sub);
}

// ===========================================================================
// Recorded delivery
// ===========================================================================

TEST(recorded_notice_bounces_without_subscriber) {
    Scoped a{Connect("org.test.alpha")};
    bool bounced = false;
    std::string bouncedId;
    std::string id;
    UltraMsgResult r = UltraMsg_PostRecorded(a.handle, "com.test.nobody-listens", JSONValue::MakeObject(), {},
                                             [&](const UltraMsgMessage& original) {
                                                 bounced = true;
                                                 bouncedId = original.envelope.id;
                                                 REQUIRE(original.envelope.kind == UltraMsgKind::Bounce);
                                             },
                                             &id);
    REQUIRE(r.ok);
    REQUIRE(WaitFor([&] { return bounced; }));
    REQUIRE_EQ(bouncedId, id);
}

TEST(recorded_notice_acknowledged_automatically) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    int received = 0;
    bool bounced = false;
    UltraMsgHandle sub = UltraMsg_Subscribe(b.handle, "com.test.recorded", [&](const UltraMsgMessage& m) {
        REQUIRE(m.envelope.kind == UltraMsgKind::RecordedNotice);
        ++received;
    });
    UltraMsgSendOptions options;
    options.ttlSeconds = 1;
    REQUIRE(UltraMsg_PostRecorded(a.handle, "com.test.recorded", JSONValue::MakeObject(), options,
                                  [&](const UltraMsgMessage&) { bounced = true; }).ok);
    REQUIRE(WaitFor([&] { return received == 1; }));
    // Past the ttl: no bounce, because the callback's return acknowledged it.
    REQUIRE(!WaitFor([&] { return bounced; }, 1500ms));
    UltraMsg_Unsubscribe(sub);
}

TEST(recorded_notice_bounces_when_not_acknowledged) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    int received = 0;
    bool bounced = false;
    UltraMsgSubscribeOptions manual;
    manual.manualAck = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(b.handle, "com.test.recorded2", [&](const UltraMsgMessage&) { ++received; }, manual);
    UltraMsgSendOptions options;
    options.ttlSeconds = 1;
    REQUIRE(UltraMsg_PostRecorded(a.handle, "com.test.recorded2", JSONValue::MakeObject(), options,
                                  [&](const UltraMsgMessage&) { bounced = true; }).ok);
    REQUIRE(WaitFor([&] { return received == 1; }));
    REQUIRE(WaitFor([&] { return bounced; }, 3000ms));
    UltraMsg_Unsubscribe(sub);
}

TEST(recorded_notice_manual_ack_prevents_bounce) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    bool bounced = false;
    UltraMsgMessage seen;
    bool got = false;
    UltraMsgSubscribeOptions manual;
    manual.manualAck = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(b.handle, "com.test.recorded3", [&](const UltraMsgMessage& m) { seen = m; got = true; }, manual);
    UltraMsgSendOptions options;
    options.ttlSeconds = 1;
    REQUIRE(UltraMsg_PostRecorded(a.handle, "com.test.recorded3", JSONValue::MakeObject(), options,
                                  [&](const UltraMsgMessage&) { bounced = true; }).ok);
    REQUIRE(WaitFor([&] { return got; }));
    REQUIRE(UltraMsg_Acknowledge(b.handle, seen).ok);
    REQUIRE(!WaitFor([&] { return bounced; }, 1500ms));
    UltraMsg_Unsubscribe(sub);
}

// ===========================================================================
// Request / reply
// ===========================================================================

TEST(request_reply_blocking_and_async) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    UltraMsgSubscribeOptions worker;
    worker.onWorkerThread = true;   // the handler replies without a pump
    UltraMsgHandle sub = UltraMsg_Subscribe(b.handle, "app.command.invoke", [&](const UltraMsgMessage& request) {
        REQUIRE(request.envelope.kind == UltraMsgKind::Request);
        JSONValue result = JSONValue::MakeObject();
        result.Set("echo", request.body.Get("verb").GetString());
        result.Set("n", request.body.Get("args").Get("n").GetInteger() * 2);
        UltraMsg_Reply(b.handle, request, result);
    }, worker);
    REQUIRE(sub != UltraMsgInvalidHandle);

    JSONValue body = JSONValue::MakeObject();
    body.Set("verb", "double");
    JSONValue args = JSONValue::MakeObject();
    args.Set("n", 21);
    body.Set("args", args);

    UltraMsgMessage reply;
    UltraMsgResult r = UltraMsg_Request(a.handle, "org.test.beta", "app.command.invoke", body, 3000, reply);
    REQUIRE(r.ok);
    REQUIRE(reply.envelope.kind == UltraMsgKind::Reply);
    REQUIRE_EQ(reply.body.Get("echo").GetString(), std::string("double"));
    REQUIRE_EQ(reply.body.Get("n").GetInteger(), static_cast<int64_t>(42));
    REQUIRE_EQ(reply.envelope.from.appId, std::string("org.test.beta"));

    bool done = false;
    UltraMsgResult asyncResult;
    UltraMsgMessage asyncReply;
    REQUIRE(UltraMsg_RequestAsync(a.handle, "org.test.beta", "app.command.invoke", body, 3000,
                                  [&](const UltraMsgResult& res, const UltraMsgMessage& m) {
                                      asyncResult = res;
                                      asyncReply = m;
                                      done = true;
                                  }).ok);
    REQUIRE(WaitFor([&] { return done; }));
    REQUIRE(asyncResult.ok);
    REQUIRE_EQ(asyncReply.body.Get("n").GetInteger(), static_cast<int64_t>(42));
    UltraMsg_Unsubscribe(sub);
}

TEST(request_errors_no_target_not_handled_timeout) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    UltraMsgMessage reply;

    UltraMsgResult r = UltraMsg_Request(a.handle, "org.test.nobody", "com.test.anything", JSONValue::MakeObject(), 2000, reply);
    REQUIRE(!r.ok);
    REQUIRE(r.code == UltraMsgResultCode::NoSuchTarget);

    r = UltraMsg_Request(a.handle, "org.test.beta", "com.test.unhandled", JSONValue::MakeObject(), 2000, reply);
    REQUIRE(!r.ok);
    REQUIRE(r.code == UltraMsgResultCode::NotHandled);

    r = UltraMsg_Request(a.handle, "*", "com.test.x", JSONValue::MakeObject(), 2000, reply);
    REQUIRE(r.code == UltraMsgResultCode::InvalidArgument);

    // A handler that never answers: the requester times out.
    UltraMsgSubscribeOptions worker;
    worker.onWorkerThread = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(b.handle, "com.test.silent", [](const UltraMsgMessage&) {}, worker);
    r = UltraMsg_Request(a.handle, "org.test.beta", "com.test.silent", JSONValue::MakeObject(), 300, reply);
    REQUIRE(r.code == UltraMsgResultCode::Timeout);

    bool done = false;
    UltraMsgResult asyncResult;
    REQUIRE(UltraMsg_RequestAsync(a.handle, "org.test.beta", "com.test.silent", JSONValue::MakeObject(), 300,
                                  [&](const UltraMsgResult& res, const UltraMsgMessage&) { asyncResult = res; done = true; }).ok);
    REQUIRE(WaitFor([&] { return done; }, 3000ms));
    REQUIRE(asyncResult.code == UltraMsgResultCode::Timeout);

    // An error reply carries its code.
    UltraMsgHandle sub2 = UltraMsg_Subscribe(b.handle, "com.test.refuse", [&](const UltraMsgMessage& request) {
        UltraMsg_ReplyError(b.handle, request, "Refused", "not today");
    }, worker);
    r = UltraMsg_Request(a.handle, "org.test.beta", "com.test.refuse", JSONValue::MakeObject(), 2000, reply);
    REQUIRE(!r.ok);
    REQUIRE_EQ(r.message, std::string("not today"));
    REQUIRE_EQ(reply.body.Get("error").Get("code").GetString(), std::string("Refused"));
    UltraMsg_Unsubscribe(sub);
    UltraMsg_Unsubscribe(sub2);
}

TEST(request_to_instance_id_and_target_disconnect) {
    Scoped a{Connect("org.test.alpha")};
    UltraMsgHandle b = Connect("org.test.beta");
    UltraMsgEndpointInfo bInfo;
    REQUIRE(UltraMsg_GetEndpointInfo(b, bInfo).ok);
    UltraMsgSubscribeOptions worker;
    worker.onWorkerThread = true;
    UltraMsgHandle sub = UltraMsg_Subscribe(b, "com.test.byinstance", [&](const UltraMsgMessage& request) {
        UltraMsg_Reply(b, request, JSONValue("ok"));
    }, worker);
    UltraMsgMessage reply;
    REQUIRE(UltraMsg_Request(a.handle, bInfo.instanceId, "com.test.byinstance", JSONValue::MakeObject(), 2000, reply).ok);
    REQUIRE_EQ(reply.body.GetString(), std::string("ok"));

    // A pending request whose target leaves fails instead of hanging.
    UltraMsgHandle silent = UltraMsg_Subscribe(b, "com.test.leave", [&](const UltraMsgMessage&) {
        std::thread([b] { std::this_thread::sleep_for(100ms); UltraMsg_Disconnect(b); }).detach();
    }, worker);
    (void)silent;
    UltraMsgResult r = UltraMsg_Request(a.handle, "org.test.beta", "com.test.leave", JSONValue::MakeObject(), 3000, reply);
    REQUIRE(!r.ok);
    REQUIRE(r.code == UltraMsgResultCode::NoSuchTarget);
    (void)sub;
}

// ===========================================================================
// Journal
// ===========================================================================

TEST(journal_stores_persistent_topics_and_queries) {
    // The in-process broker and its journal live for the whole test run, so
    // this test filters everything on its own sender.
    Scoped a{Connect("org.test.journal-reader")};
    Scoped b{Connect("org.test.journal-writer")};
    std::string mailId, chatId;
    REQUIRE(UltraMsg_Post(b.handle, "mail.message", Mail("Quarterly report", "Numbers attached"), {}, &mailId).ok);
    UltraMsgSendOptions chatOptions;
    chatOptions.conversation = "telegram:journal-42";
    REQUIRE(UltraMsg_Post(b.handle, "messaging.message", Chat("journal-42", "Lunch at noon?"), chatOptions, &chatId).ok);
    REQUIRE(UltraMsg_Post(b.handle, "messaging.message", Chat("journal-42", "Or later"), chatOptions).ok);
    // Not persistent: no schema says so and no flag.
    REQUIRE(UltraMsg_Post(b.handle, "com.test.ephemeral", JSONValue::MakeObject()).ok);
    // Persistent by flag.
    UltraMsgSendOptions flagged;
    flagged.flags = UltraMsgFlag_Persistent;
    std::string flaggedId;
    REQUIRE(UltraMsg_Post(b.handle, "com.test.kept", JSONValue::MakeObject(), flagged, &flaggedId).ok);

    // The journal writes before fan-out, but the endpoint's Post returns
    // before the broker has processed it: wait until the count settles.
    UltraMsgQuery all;
    all.appId = "org.test.journal-writer";
    int64_t count = 0;
    REQUIRE(WaitFor([&] { return UltraMsg_Count(a.handle, all, count).ok && count >= 4; }));
    REQUIRE_EQ(count, static_cast<int64_t>(4));

    std::vector<UltraMsgMessage> messages;
    UltraMsgQuery mail = all;
    mail.topics = {"mail.*"};
    REQUIRE(UltraMsg_Query(a.handle, mail, messages).ok);
    REQUIRE_EQ(messages.size(), static_cast<size_t>(1));
    REQUIRE_EQ(messages[0].envelope.id, mailId);
    REQUIRE_EQ(messages[0].body.Get("subject").GetString(), std::string("Quarterly report"));
    REQUIRE(!messages[0].read);

    UltraMsgQuery text = all;
    text.textContains = "noon";
    REQUIRE(UltraMsg_Query(a.handle, text, messages).ok);
    REQUIRE_EQ(messages.size(), static_cast<size_t>(1));
    REQUIRE_EQ(messages[0].envelope.id, chatId);

    UltraMsgQuery byService = all;
    byService.service = "telegram";
    REQUIRE(UltraMsg_Count(a.handle, byService, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(2));

    UltraMsgQuery byApp;
    byApp.appId = "org.test.journal-reader";
    REQUIRE(UltraMsg_Count(a.handle, byApp, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(0));

    UltraMsgMessage one;
    REQUIRE(UltraMsg_GetMessage(a.handle, flaggedId, one).ok);
    REQUIRE_EQ(one.envelope.topic, std::string("com.test.kept"));
    REQUIRE(!UltraMsg_GetMessage(a.handle, "nope", one).ok);

    // Read state, dismissal, deletion.
    REQUIRE(UltraMsg_MarkRead(a.handle, {mailId}).ok);
    UltraMsgQuery unread = all;
    unread.unreadOnly = true;
    REQUIRE(UltraMsg_Count(a.handle, unread, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(3));
    REQUIRE(UltraMsg_MarkUnread(a.handle, {mailId}).ok);
    REQUIRE(UltraMsg_Count(a.handle, unread, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(4));

    REQUIRE(UltraMsg_Dismiss(a.handle, {chatId}).ok);
    REQUIRE(UltraMsg_Count(a.handle, all, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(3));
    UltraMsgQuery withDismissed = all;
    withDismissed.includeDismissed = true;
    REQUIRE(UltraMsg_Count(a.handle, withDismissed, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(4));

    REQUIRE(UltraMsg_Delete(a.handle, {flaggedId}).ok);
    REQUIRE(!UltraMsg_GetMessage(a.handle, flaggedId, one).ok);

    // Conversations.
    std::vector<UltraMsgConversation> conversations;
    UltraMsgQuery byConversation;
    byConversation.conversation = "telegram:journal-42";
    REQUIRE(UltraMsg_ListConversations(a.handle, byConversation, conversations).ok);
    REQUIRE_EQ(conversations.size(), static_cast<size_t>(1));
    REQUIRE_EQ(conversations[0].id, std::string("telegram:journal-42"));
    REQUIRE_EQ(conversations[0].service, std::string("telegram"));
    REQUIRE_EQ(conversations[0].title, std::string("Chat journal-42"));
    REQUIRE_EQ(conversations[0].messageCount, 1);   // one dismissed
    REQUIRE_EQ(conversations[0].unreadCount, 1);

    // Export.
    const std::string path = TestBusPath() + ".export.jsonl";
    int64_t exported = 0;
    REQUIRE(UltraMsg_Export(a.handle, all, path, &exported).ok);
    REQUIRE_EQ(exported, static_cast<int64_t>(2));   // one dismissed, one deleted
    std::ifstream file(path);
    int lines = 0;
    std::string line;
    while (std::getline(file, line)) if (!line.empty()) ++lines;
    REQUIRE_EQ(lines, 2);
    std::remove(path.c_str());

    REQUIRE(UltraMsg_SetRetention(a.handle, "mail.*", 30, 1000).ok);
    REQUIRE(!UltraMsg_SetRetention(a.handle, "bad pattern", 30, 1000).ok);
}

TEST(journal_replace_and_no_journal_flags) {
    Scoped a{Connect("org.test.journal-replacer")};
    std::string first;
    REQUIRE(UltraMsg_Post(a.handle, "mail.message", Mail("Draft v1"), {}, &first).ok);
    UltraMsgSendOptions replace;
    replace.flags = UltraMsgFlag_Replace;
    replace.replaces = first;
    std::string second;
    UltraMsgQuery mail;
    mail.topics = {"mail.message"};
    mail.appId = "org.test.journal-replacer";
    int64_t count = 0;
    REQUIRE(WaitFor([&] { return UltraMsg_Count(a.handle, mail, count).ok && count == 1; }));
    REQUIRE(UltraMsg_Post(a.handle, "mail.message", Mail("Draft v2"), replace, &second).ok);
    std::vector<UltraMsgMessage> messages;
    REQUIRE(WaitFor([&] {
        return UltraMsg_Query(a.handle, mail, messages).ok && messages.size() == 1 && messages[0].envelope.id == second;
    }));

    UltraMsgSendOptions skip;
    skip.flags = UltraMsgFlag_NoJournal;
    REQUIRE(UltraMsg_Post(a.handle, "mail.message", Mail("Never stored"), skip).ok);
    std::this_thread::sleep_for(100ms);
    REQUIRE(UltraMsg_Count(a.handle, mail, count).ok);
    REQUIRE_EQ(count, static_cast<int64_t>(1));
}

TEST(subscribe_with_replay_delivers_history_first) {
    Scoped a{Connect("org.test.alpha")};
    Scoped b{Connect("org.test.beta")};
    const int64_t since = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch()).count() - 60000;
    // A topic of this test's own, journaled by flag, so earlier tests' mail
    // is not replayed into it.
    UltraMsgSendOptions keep;
    keep.flags = UltraMsgFlag_Persistent;
    REQUIRE(UltraMsg_Post(b.handle, "com.test.replay", Mail("Old one"), keep).ok);
    UltraMsgQuery mine;
    mine.topics = {"com.test.replay"};
    int64_t count = 0;
    REQUIRE(WaitFor([&] { return UltraMsg_Count(a.handle, mine, count).ok && count >= 1; }));

    std::vector<std::string> subjects;
    UltraMsgSubscribeOptions options;
    options.replaySinceMs = since;
    UltraMsgHandle sub = UltraMsg_Subscribe(a.handle, "com.test.replay", [&](const UltraMsgMessage& m) {
        subjects.push_back(m.body.Get("subject").GetString());
    }, options);
    REQUIRE(sub != UltraMsgInvalidHandle);
    REQUIRE(UltraMsg_Post(b.handle, "com.test.replay", Mail("New one"), keep).ok);
    REQUIRE(WaitFor([&] { return subjects.size() >= 2; }));
    REQUIRE_EQ(subjects.front(), std::string("Old one"));
    REQUIRE_EQ(subjects.back(), std::string("New one"));
    UltraMsg_Unsubscribe(sub);
}

// ===========================================================================
// Lifecycle and disconnect
// ===========================================================================

TEST(lifecycle_notices_and_endpoint_removal) {
    Scoped a{Connect("org.test.alpha")};
    std::vector<std::string> events;
    UltraMsgHandle sub = UltraMsg_Subscribe(a.handle, "app.lifecycle.*", [&](const UltraMsgMessage& m) {
        if (m.body.Get("appId").GetString() == "org.test.gamma") events.push_back(m.envelope.topic);
    });
    UltraMsgHandle c = Connect("org.test.gamma");
    REQUIRE(WaitFor([&] { return events.size() == 1; }));
    REQUIRE_EQ(events[0], std::string("app.lifecycle.started"));
    REQUIRE(UltraMsg_Disconnect(c).ok);
    REQUIRE(WaitFor([&] { return events.size() == 2; }));
    REQUIRE_EQ(events[1], std::string("app.lifecycle.stopping"));
    REQUIRE(!UltraMsg_IsConnected(c));
    REQUIRE(!UltraMsg_Disconnect(c).ok);

    std::vector<UltraMsgEndpointInfo> endpoints;
    REQUIRE(UltraMsg_ListEndpoints(a.handle, endpoints).ok);
    for (const auto& e : endpoints) REQUIRE(e.appId != "org.test.gamma");
    UltraMsg_Unsubscribe(sub);
}

// ===========================================================================
// C++ layer and typed helpers
// ===========================================================================

TEST(cpp_endpoint_layer) {
    UltraMsgConnectOptions options;
    options.appId = "org.test.cpp";
    options.busPath = TestBusPath();
    options.journalPath = ":memory:";
    auto a = UltraMessage::Endpoint::Connect(options);
    REQUIRE(a != nullptr);
    options.appId = "org.test.cpp2";
    auto b = UltraMessage::Endpoint::Connect(options);
    REQUIRE(b != nullptr);

    int got = 0;
    UltraMsgSubscribeOptions worker;
    worker.onWorkerThread = true;
    UltraMessage::Subscription sub = b->Subscribe("com.test.cpp", [&](const UltraMsgMessage& m) {
        ++got;
        if (m.envelope.kind == UltraMsgKind::Request) b->Reply(m, JSONValue("pong"));
    }, worker);
    REQUIRE(sub.IsActive());

    REQUIRE(a->Post("com.test.cpp", JSONValue::MakeObject()).ok);
    std::future<UltraMsgMessage> future = a->Request("org.test.cpp2", "com.test.cpp", JSONValue::MakeObject(), 2s);
    REQUIRE(future.wait_for(3s) == std::future_status::ready);
    UltraMsgMessage reply = future.get();
    REQUIRE_EQ(reply.body.GetString(), std::string("pong"));

    std::future<UltraMsgMessage> failed = a->Request("org.test.nobody", "com.test.cpp", JSONValue::MakeObject(), 2s);
    REQUIRE(failed.wait_for(3s) == std::future_status::ready);
    REQUIRE_EQ(failed.get().body.Get("error").Get("code").GetString(), std::string("NoSuchTarget"));

    sub.Cancel();
    REQUIRE(!sub.IsActive());
    b.reset();
    REQUIRE(a->IsConnected());
    a->Disconnect();
    REQUIRE(!a->IsConnected());
}

TEST(typed_helpers_round_trip) {
    UltraMessage::MessagingMessage chat;
    chat.service = "signal";
    chat.account = "+4915";
    chat.conversationId = "grp1";
    chat.conversationTitle = "Family";
    chat.isGroup = true;
    chat.sender = {"u9", "Eve", "avatar.png"};
    chat.text = "Dinner?";
    chat.incoming = false;
    chat.externalId = "x1";
    chat.attachments.push_back({"pic.jpg", "image/jpeg", 1234, "/tmp/pic.jpg"});
    JSONValue body = UltraMessage::MakeMessagingMessage(chat);
    REQUIRE(UltraMsg_Validate("messaging.message", body).ok);
    UltraMessage::MessagingMessage parsed;
    REQUIRE(UltraMessage::ParseMessagingMessage(body, parsed));
    REQUIRE_EQ(parsed.service, std::string("signal"));
    REQUIRE_EQ(parsed.conversationTitle, std::string("Family"));
    REQUIRE(parsed.isGroup);
    REQUIRE_EQ(parsed.sender.name, std::string("Eve"));
    REQUIRE(!parsed.incoming);
    REQUIRE_EQ(parsed.attachments.size(), static_cast<size_t>(1));
    REQUIRE_EQ(parsed.attachments[0].size, static_cast<int64_t>(1234));
    REQUIRE_EQ(UltraMessage::ConversationKey(chat), std::string("signal:grp1"));

    UltraMessage::MailMessage mail;
    mail.account = "a@b.c";
    mail.from = {"A", "a@b.c"};
    mail.to = {{"B", "b@b.c"}, {"", "c@b.c"}};
    mail.subject = "S";
    mail.flagged = true;
    JSONValue mailBody = UltraMessage::MakeMailMessage(mail);
    REQUIRE(UltraMsg_Validate("mail.message", mailBody).ok);
    UltraMessage::MailMessage mailParsed;
    REQUIRE(UltraMessage::ParseMailMessage(mailBody, mailParsed));
    REQUIRE_EQ(mailParsed.to.size(), static_cast<size_t>(2));
    REQUIRE(mailParsed.flagged);

    UltraMessage::SystemNotification n;
    n.appName = "Signal";
    n.category = "im.received";
    n.summary = "Eve";
    n.body = "Dinner?";
    n.actions.push_back({"reply", "Reply"});
    JSONValue nBody = UltraMessage::MakeSystemNotification(n);
    REQUIRE(UltraMsg_Validate("system.notification", nBody).ok);
    UltraMessage::SystemNotification nParsed;
    REQUIRE(UltraMessage::ParseSystemNotification(nBody, nParsed));
    REQUIRE_EQ(nParsed.category, std::string("im.received"));
    REQUIRE_EQ(nParsed.actions.size(), static_cast<size_t>(1));
    REQUIRE_EQ(nParsed.urgency, std::string("normal"));
}
