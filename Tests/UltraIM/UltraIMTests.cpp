// Tests/UltraIM/UltraIMTests.cpp
// UltraIM manager: registry, dispatcher, merge, sending, calls, modules
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "FakeProvider.h"
#include "UltraIM/UltraIMCore.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

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

// A dispatcher that queues tasks until the test drains them, standing in for
// PostToUIThread.
struct QueueDispatcher {
    std::mutex mutex;
    std::deque<std::function<void()>> tasks;

    UltraIMDispatcher Make() {
        return [this](std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        };
    }
    size_t Drain() {
        size_t count = 0;
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (tasks.empty()) return count;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task();
            ++count;
        }
    }
};

UltraIMConversation Conversation(const std::string& id, const std::string& provider, int64_t time) {
    UltraIMConversation c;
    c.conversationId = id;
    c.providerId = provider;
    c.lastMessageTimeMs = time;
    return c;
}

void TestNotInitialized() {
    CHECK(!UltraIM_IsInitialized());
    UltraIMResult r = UltraIM_SendText("x", "c", "hi");
    CHECK(!r && r.code == UltraIMResultCode::NotInitialized);
    CHECK(!UltraIM_RegisterProvider(std::make_shared<FakeProvider>("a")));
    CHECK(UltraIM_Subscribe([](const UltraIMEvent&) {}) == 0);
}

void TestRegistry() {
    CHECK(UltraIM_Initialize());
    auto a = std::make_shared<FakeProvider>("a");
    CHECK(UltraIM_RegisterProvider(a));
    CHECK(!UltraIM_RegisterProvider(std::make_shared<FakeProvider>("a")));   // duplicate id
    CHECK(UltraIM_ListProviders().size() == 1);
    UltraIMProviderInfo info;
    CHECK(UltraIM_GetProviderInfo("a", info) && info.displayName == "Fake a");
    CHECK(UltraIM_SendText("nobody", "c", "x").code == UltraIMResultCode::ProviderNotFound);
    CHECK(UltraIM_UnregisterProvider("a"));
    CHECK(a->shutDown);
    CHECK(UltraIM_ListProviders().empty());
    UltraIM_Shutdown();
}

void TestEventsGoThroughDispatcher() {
    QueueDispatcher queue;
    UltraIM_Initialize();
    UltraIM_SetEventDispatcher(queue.Make());
    auto a = std::make_shared<FakeProvider>("a");
    UltraIM_RegisterProvider(a);

    int received = 0;
    std::thread::id deliveredOn;
    const UltraIMSubscriptionId sub = UltraIM_Subscribe([&](const UltraIMEvent& e) {
        if (e.type == UltraIMEventType::MessageReceive && e.message.text == "hello") ++received;
        deliveredOn = std::this_thread::get_id();
    });
    CHECK(sub != 0);

    std::thread worker([a]() {
        UltraIMEvent e;
        e.type = UltraIMEventType::MessageReceive;
        e.providerId = "a";
        e.message.text = "hello";
        a->Raise(e);
    });
    worker.join();
    CHECK(received == 0);                       // not before the queue is drained
    CHECK(queue.Drain() == 1);
    CHECK(received == 1);
    CHECK(deliveredOn == std::this_thread::get_id());

    UltraIM_Unsubscribe(sub);
    UltraIMEvent e;
    e.type = UltraIMEventType::MessageReceive;
    e.message.text = "hello";
    a->Raise(e);
    queue.Drain();
    CHECK(received == 1);                       // unsubscribed

    // A task queued before Shutdown must not reach anyone after it.
    UltraIM_Subscribe([&](const UltraIMEvent&) { ++received; });
    a->Raise(e);
    UltraIM_Shutdown();
    queue.Drain();
    CHECK(received == 1);
    CHECK(a->shutDown);
}

void TestFetchConversationsMerges() {
    QueueDispatcher queue;
    UltraIM_Initialize();
    UltraIM_SetEventDispatcher(queue.Make());
    auto a = std::make_shared<FakeProvider>("a");
    auto b = std::make_shared<FakeProvider>("b");
    auto offline = std::make_shared<FakeProvider>("c", false);
    a->conversations = {Conversation("a1", "a", 10), Conversation("a2", "a", 30)};
    b->conversations = {Conversation("b1", "b", 20)};
    offline->conversations = {Conversation("c1", "c", 99)};
    UltraIM_RegisterProvider(a);
    UltraIM_RegisterProvider(b);
    UltraIM_RegisterProvider(offline);

    std::vector<UltraIMConversation> merged;
    int calls = 0;
    UltraIM_FetchConversations([&](const UltraIMResult& r, const std::vector<UltraIMConversation>& list) {
        CHECK(r);
        merged = list;
        ++calls;
    });
    CHECK(calls == 0);
    queue.Drain();
    CHECK(calls == 1);
    CHECK(merged.size() == 3);                  // the disconnected provider is left out
    if (merged.size() == 3) {
        CHECK(merged[0].conversationId == "a2");    // newest first
        CHECK(merged[1].conversationId == "b1");
        CHECK(merged[2].conversationId == "a1");
    }

    std::vector<UltraIMMessage> history;
    UltraIM_FetchMessages("a", "a1", "", 20, [&](const UltraIMResult& r, const std::vector<UltraIMMessage>& m) {
        CHECK(r);
        history = m;
    });
    queue.Drain();
    CHECK(history.size() == 1 && history[0].conversationId == "a1");
    UltraIM_Shutdown();
}

void TestSendingAndCalls() {
    UltraIM_Initialize();
    auto a = std::make_shared<FakeProvider>("a");
    UltraIM_RegisterProvider(a);

    CHECK(UltraIM_SendText("a", "c1", "hi"));
    CHECK(a->lastText == "hi" && a->lastConversation == "c1");
    CHECK(a->lastClientId.size() == 36);        // generated UUID
    UltraIMSendOptions options;
    options.clientMessageId = "mine";
    CHECK(UltraIM_SendText("a", "c1", "again", options));
    CHECK(a->lastClientId == "mine");
    CHECK(UltraIM_SendText("a", "c1", "").code == UltraIMResultCode::InvalidArgument);
    CHECK(UltraIM_SendFile("a", "c1", "/tmp/x").code == UltraIMResultCode::CapabilityMissing);

    std::string callId;
    CHECK(UltraIM_CanCall("a", "direct", false));
    CHECK(!UltraIM_CanCall("a", "group", false));
    CHECK(UltraIM_StartCall("a", "direct", false, callId) && callId == "call-direct");
    CHECK(UltraIM_StartCall("a", "group", true, callId).code == UltraIMResultCode::CapabilityMissing);

    // What one provider's service keeps for another: through the dispatcher
    // like every callback, and the default answer for a provider without one.
    UltraIMCredentials shared;
    UltraIMResult sharedResult;
    UltraIM_FetchSharedCredentials("a", "telegram", [&](const UltraIMResult& r, const UltraIMCredentials& c) {
        sharedResult = r;
        shared = c;
    });
    CHECK(sharedResult && shared.apiId == "1017053" && shared.apiHash == "0123456789abcdef0123456789abcdef");
    UltraIM_FetchSharedCredentials("a", "xmpp", [&](const UltraIMResult& r, const UltraIMCredentials&) { sharedResult = r; });
    CHECK(!sharedResult && sharedResult.code == UltraIMResultCode::NotFound);
    UltraIM_FetchSharedCredentials("nobody", "telegram", [&](const UltraIMResult& r, const UltraIMCredentials&) { sharedResult = r; });
    CHECK(sharedResult.code == UltraIMResultCode::ProviderNotFound);

    UltraIMResult resend;
    UltraIM_ResendAuthCode("a", [&](const UltraIMResult& r) { resend = r; });
    CHECK(!resend && resend.code == UltraIMResultCode::CapabilityMissing);

    const std::string id1 = UltraIM_NewClientMessageId();
    const std::string id2 = UltraIM_NewClientMessageId();
    CHECK(id1.size() == 36 && id1 != id2);
    UltraIM_Shutdown();
}

void TestModuleLoading() {
    UltraIMConfig config;
    config.pluginDirectory = ULTRAIM_TEST_MODULE_DIR;
    UltraIM_Initialize(config);
    CHECK(UltraIM_RefreshProviders() == 1);
    UltraIMProviderInfo info;
    CHECK(UltraIM_GetProviderInfo("moduletest", info));
    CHECK(UltraIM_RefreshProviders() == 0);     // already loaded
    bool signedIn = false;
    UltraIM_SignIn("moduletest", UltraIMCredentials(), [&](const UltraIMResult& r) { signedIn = r.success; });
    CHECK(signedIn);                            // no dispatcher: runs right away
    UltraIM_Shutdown();
}

} // namespace

int main() {
    TestNotInitialized();
    TestRegistry();
    TestEventsGoThroughDispatcher();
    TestFetchConversationsMerges();
    TestSendingAndCalls();
    TestModuleLoading();
    std::printf("UltraIMTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
