// Tests/UltraIM/UltraIMTests.cpp
// UltraIM manager: registry, dispatcher, merge, sending, calls, modules
// Version: 0.1.1 - searching: the shared matcher, links, case folding, a search by reading history
// Version: 0.1.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework

#include "FakeProvider.h"
#include "UltraIM/UltraIMCore.h"

#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    CHECK(UltraIM_SendVideo("a", "c1", "/tmp/x.mp4", "a caption").code == UltraIMResultCode::CapabilityMissing);
    {
        // A provider without attachments answers through the dispatcher like
        // every callback.
        UltraIMResult downloaded;
        UltraIMAttachment attachment;
        UltraIM_DownloadAttachment("a", "c1", "m1", [&](const UltraIMResult& r, const UltraIMAttachment& a) {
            downloaded = r;
            attachment = a;
        });
        CHECK(downloaded.code == UltraIMResultCode::CapabilityMissing && attachment.localPath.empty());
        UltraIM_DownloadAttachment("nobody", "c1", "m1", [&](const UltraIMResult& r, const UltraIMAttachment&) {
            downloaded = r;
        });
        CHECK(downloaded.code == UltraIMResultCode::ProviderNotFound);
    }

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
    // The module built for this version, and not the two beside it that
    // were not (an ABI 1 module, one of another version): their vtables
    // would not match the interface.
    CHECK(UltraIM_RefreshProviders() == 1);
    UltraIMProviderInfo info;
    CHECK(UltraIM_GetProviderInfo("moduletest", info));
    CHECK(!UltraIM_GetProviderInfo("stale", info));
    CHECK(UltraIM_RefreshProviders() == 0);     // already loaded
    bool signedIn = false;
    UltraIM_SignIn("moduletest", UltraIMCredentials(), [&](const UltraIMResult& r) { signedIn = r.success; });
    CHECK(signedIn);                            // no dispatcher: runs right away
    UltraIM_Shutdown();
}

UltraIMMessage TextMessage(const std::string& id, const std::string& text,
                           UltraIMMessageType type = UltraIMMessageType::Text, const std::string& fileName = "") {
    UltraIMMessage m;
    m.messageId = id;
    m.conversationId = "room";
    m.text = text;
    m.type = type;
    m.attachment.fileName = fileName;
    return m;
}

UltraIMMessageSearch SearchFor(const std::string& text, UltraIMSearchKind kind = UltraIMSearchKind::AnyKind) {
    UltraIMMessageSearch s;
    s.conversationId = "room";
    s.text = text;
    s.kind = kind;
    return s;
}

void TestSearchMatching() {
    // Case, beyond ASCII: Latin-1, Latin Extended-A, Greek, Cyrillic.
    CHECK(UltraIM_FoldCase("Hello WORLD 123") == "hello world 123");
    CHECK(UltraIM_FoldCase("ÄPFEL Über Straße") == "äpfel über straße");
    CHECK(UltraIM_FoldCase("ŁÓDŹ Ľubica Žilina ŸÇ") == "łódź ľubica žilina ÿç");
    CHECK(UltraIM_FoldCase("ΑΘΗΝΑ ΟΔΟΣ οδός") == "αθηνα οδοσ οδόσ");
    CHECK(UltraIM_FoldCase("ПРИВЕТ Ёлка") == "привет ёлка");
    CHECK(UltraIM_FoldCase("สวัสดี 你好 😀") == "สวัสดี 你好 😀");          // no case: as it is
    CHECK(UltraIM_FoldCase("×÷") == "×÷");                                  // signs, not letters
    const std::string broken = std::string("A\xC3") + "B\xFF";             // not UTF-8: passed through
    CHECK(UltraIM_FoldCase(broken) == std::string("a\xC3") + "b\xFF");

    // Links.
    using Links = std::vector<std::string>;
    CHECK(UltraIM_ExtractLinks("no link here, www or http alone").empty());
    CHECK(UltraIM_ExtractLinks("see https://example.com/a?b=1.") == Links{"https://example.com/a?b=1"});
    CHECK(UltraIM_ExtractLinks("HTTP://Example.com, then www.ultra-os.org!") ==
          (Links{"HTTP://Example.com", "www.ultra-os.org"}));
    CHECK(UltraIM_ExtractLinks("(https://en.wikipedia.org/wiki/Cube_(algebra))") ==
          Links{"https://en.wikipedia.org/wiki/Cube_(algebra)"});
    CHECK(UltraIM_ExtractLinks("(see www.example.com)") == Links{"www.example.com"});
    CHECK(UltraIM_ExtractLinks("<https://a.example/x>\nhttp://b.example") ==
          (Links{"https://a.example/x", "http://b.example"}));
    CHECK(UltraIM_ExtractLinks("mail me@www.example.com or awww.example.com").empty());
    CHECK(UltraIM_ExtractLinks("https:// www. www.nodot http://").empty());

    // Words: all of them, anywhere, any case; the file's name counts.
    CHECK(UltraIM_MessageMatchesSearch(TextMessage("1", "Lunch at the Übersee café"), SearchFor("übersee LUNCH")));
    CHECK(UltraIM_MessageMatchesSearch(TextMessage("1", "Lunch at the Übersee café"), SearchFor("  sEE  ")));
    CHECK(!UltraIM_MessageMatchesSearch(TextMessage("1", "Lunch at the Übersee café"), SearchFor("lunch dinner")));
    CHECK(UltraIM_MessageMatchesSearch(TextMessage("1", "", UltraIMMessageType::File, "Invoice-2026.PDF"),
                                       SearchFor("invoice pdf")));
    CHECK(UltraIM_MessageMatchesSearch(TextMessage("1", "anything"), SearchFor("")));
    CHECK(!UltraIM_MessageMatchesSearch(TextMessage("1", "Bob joined", UltraIMMessageType::SystemEvent), SearchFor("bob")));

    // Kinds.
    const UltraIMMessage photo = TextMessage("1", "the harbour", UltraIMMessageType::Image, "p.jpg");
    const UltraIMMessage video = TextMessage("2", "", UltraIMMessageType::Video, "clip.mp4");
    const UltraIMMessage file = TextMessage("3", "", UltraIMMessageType::File, "notes.pdf");
    const UltraIMMessage link = TextMessage("4", "read www.example.com/harbour");
    CHECK(UltraIM_MessageMatchesSearch(photo, SearchFor("", UltraIMSearchKind::Images)));
    CHECK(!UltraIM_MessageMatchesSearch(video, SearchFor("", UltraIMSearchKind::Images)));
    CHECK(UltraIM_MessageMatchesSearch(video, SearchFor("", UltraIMSearchKind::Videos)));
    CHECK(UltraIM_MessageMatchesSearch(photo, SearchFor("", UltraIMSearchKind::Media)));
    CHECK(UltraIM_MessageMatchesSearch(video, SearchFor("", UltraIMSearchKind::Media)));
    CHECK(!UltraIM_MessageMatchesSearch(file, SearchFor("", UltraIMSearchKind::Media)));
    CHECK(UltraIM_MessageMatchesSearch(file, SearchFor("", UltraIMSearchKind::Files)));
    CHECK(!UltraIM_MessageMatchesSearch(photo, SearchFor("", UltraIMSearchKind::Files)));
    CHECK(UltraIM_MessageMatchesSearch(link, SearchFor("", UltraIMSearchKind::Links)));
    CHECK(UltraIM_MessageMatchesSearch(link, SearchFor("harbour", UltraIMSearchKind::Links)));
    CHECK(!UltraIM_MessageMatchesSearch(photo, SearchFor("harbour", UltraIMSearchKind::Links)));
    CHECK(UltraIM_MessageMatchesSearch(photo, SearchFor("harbour", UltraIMSearchKind::Images)));
}

// A history of `count` messages, "m1" the oldest, served a page at a time
// as FetchMessages does: oldest first, the ones before beforeMessageId.
struct PagedHistory {
    std::vector<UltraIMMessage> messages;
    int fetches = 0;
    int failFrom = 0;       // the fetch that fails, and every one after (0: none)

    UltraIMFetchPage Make() {
        return [this](const std::string& conversationId, const std::string& before, int limit,
                      UltraIMMessagesCallback done) {
            ++fetches;
            if (failFrom && fetches >= failFrom)
                return done(UltraIMResult::Error(UltraIMResultCode::NetworkError, "offline"), {});
            if (conversationId != "room") return done(UltraIMResult::Error(UltraIMResultCode::NotFound, "no room"), {});
            size_t end = messages.size();
            for (size_t i = 0; i < messages.size() && !before.empty(); ++i) {
                if (messages[i].messageId == before) {
                    end = i;
                    break;
                }
            }
            const size_t begin = end > static_cast<size_t>(limit) ? end - static_cast<size_t>(limit) : 0;
            done(UltraIMResult::Ok(), std::vector<UltraIMMessage>(messages.begin() + static_cast<std::ptrdiff_t>(begin),
                                                                  messages.begin() + static_cast<std::ptrdiff_t>(end)));
        };
    }
};

void TestSearchByFetching() {
    PagedHistory history;
    for (int i = 1; i <= 450; ++i)
        history.messages.push_back(TextMessage("m" + std::to_string(i), i % 50 == 0 ? "Milestone " + std::to_string(i)
                                                                                    : "chatter " + std::to_string(i)));
    UltraIMResult result;
    UltraIMSearchPage page;
    int answers = 0;
    const auto take = [&](const UltraIMResult& r, const UltraIMSearchPage& p) {
        result = r;
        page = p;
        ++answers;
    };

    // Newest first, a page at a time, the cursor going on where it stopped.
    UltraIMMessageSearch search = SearchFor("milestone");
    search.limit = 4;
    UltraIM_SearchByFetching(history.Make(), search, take);
    CHECK(answers == 1 && result && page.messages.size() == 4);
    CHECK(page.messages[0].messageId == "m450" && page.messages[3].messageId == "m300");
    CHECK(page.nextCursor == "m300");
    search.cursor = page.nextCursor;
    UltraIM_SearchByFetching(history.Make(), search, take);
    CHECK(result && page.messages.size() == 4 && page.messages[0].messageId == "m250" &&
          page.messages[3].messageId == "m100");
    search.cursor = page.nextCursor;
    UltraIM_SearchByFetching(history.Make(), search, take);
    CHECK(result && page.messages.size() == 1 && page.messages[0].messageId == "m50");
    CHECK(page.nextCursor.empty());             // the beginning of the history

    // Nothing there: the whole history read, an empty page, no cursor.
    history.fetches = 0;
    UltraIM_SearchByFetching(history.Make(), SearchFor("absent"), take);
    CHECK(result && page.messages.empty() && page.nextCursor.empty());
    CHECK(history.fetches == 6);                // five pages of a hundred, and the empty one

    // A long history is read a stretch at a time: the cursor says where to go on.
    PagedHistory lengthy;
    for (int i = 1; i <= 2500; ++i) lengthy.messages.push_back(TextMessage("m" + std::to_string(i), i == 3 ? "needle" : "hay"));
    UltraIM_SearchByFetching(lengthy.Make(), SearchFor("needle"), take);
    CHECK(result && page.messages.empty() && page.nextCursor == "m501" && lengthy.fetches == 20);
    UltraIMMessageSearch further = SearchFor("needle");
    further.cursor = page.nextCursor;
    UltraIM_SearchByFetching(lengthy.Make(), further, take);
    CHECK(result && page.messages.size() == 1 && page.messages[0].messageId == "m3" && page.nextCursor.empty());

    // A failure: the error when nothing was found yet, else what was found
    // and a cursor to try again from.
    PagedHistory offline = history;
    offline.fetches = 0;
    offline.failFrom = 1;
    UltraIM_SearchByFetching(offline.Make(), SearchFor("milestone"), take);
    CHECK(!result && result.code == UltraIMResultCode::NetworkError && page.messages.empty());
    offline.fetches = 0;
    offline.failFrom = 3;
    UltraIM_SearchByFetching(offline.Make(), SearchFor("milestone"), take);
    CHECK(result && page.messages.size() == 4 && page.messages[3].messageId == "m300" && page.nextCursor == "m251");

    // One conversation at a time; an unknown one is the provider's error.
    answers = 0;
    UltraIMMessageSearch everywhere = SearchFor("milestone");
    everywhere.conversationId.clear();
    UltraIM_SearchByFetching(history.Make(), everywhere, take);
    CHECK(answers == 1 && !result && result.code == UltraIMResultCode::CapabilityMissing);
    UltraIMMessageSearch elsewhere = SearchFor("milestone");
    elsewhere.conversationId = "other";
    UltraIM_SearchByFetching(history.Make(), elsewhere, take);
    CHECK(!result && result.code == UltraIMResultCode::NotFound);
}

void TestSearchThroughManager() {
    CHECK(UltraIM_Initialize());
    QueueDispatcher dispatcher;
    UltraIM_SetEventDispatcher(dispatcher.Make());
    auto plain = std::make_shared<FakeProvider>("plain");
    auto searching = std::make_shared<FakeProvider>("searching");
    searching->searchable = true;
    CHECK(UltraIM_RegisterProvider(plain));
    CHECK(UltraIM_RegisterProvider(searching));
    UltraIMProviderInfo info;
    CHECK(UltraIM_GetProviderInfo("plain", info) &&
          !UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::SearchMessages));
    CHECK(UltraIM_GetProviderInfo("searching", info) &&
          UltraIM_HasCapability(info.capabilities, UltraIMCapabilityFlags::SearchMessages));

    UltraIMResult result;
    UltraIMSearchPage page;
    int answers = 0;
    const auto take = [&](const UltraIMResult& r, const UltraIMSearchPage& p) {
        result = r;
        page = p;
        ++answers;
    };
    // A provider without it says so; an unknown provider is not found.
    UltraIM_SearchMessages("plain", SearchFor("history"), take);
    CHECK(answers == 0);                        // through the dispatcher, like every callback
    dispatcher.Drain();
    CHECK(answers == 1 && !result && result.code == UltraIMResultCode::CapabilityMissing);
    UltraIM_SearchMessages("nobody", SearchFor("history"), take);
    dispatcher.Drain();
    CHECK(answers == 2 && result.code == UltraIMResultCode::ProviderNotFound);
    // One with it: the search reaches the provider as asked, the page comes back.
    UltraIMMessageSearch search = SearchFor("History", UltraIMSearchKind::AnyKind);
    search.limit = 7;
    UltraIM_SearchMessages("searching", search, take);
    dispatcher.Drain();
    CHECK(answers == 3 && result && page.messages.size() == 1 && page.messages[0].text == "history");
    CHECK(page.messages[0].conversationId == "room" && page.nextCursor.empty());
    UltraIM_SearchMessages("searching", SearchFor("", UltraIMSearchKind::Images), take);
    dispatcher.Drain();
    CHECK(answers == 4 && result && page.messages.empty());
    // Deleting: a provider without it offers nothing and says so when asked.
    UltraIMDeleteChoices choices = UltraIM_GetDeleteChoices("plain", "room", "h1");
    CHECK(!choices.forMe && !choices.forEveryone);
    CHECK(!UltraIM_GetDeleteChoices("nobody", "room", "h1").forMe);
    CHECK(UltraIM_DeleteMessage("plain", "room", "h1").code == UltraIMResultCode::CapabilityMissing);
    CHECK(UltraIM_DeleteMessage("plain", "room", "h1", UltraIMDeleteScope::ForMe).code ==
          UltraIMResultCode::CapabilityMissing);
    CHECK(UltraIM_DeleteMessage("nobody", "room", "h1").code == UltraIMResultCode::ProviderNotFound);
    UltraIM_SearchMessages("searching", search, nullptr);       // no callback: nothing to crash on
    dispatcher.Drain();
    UltraIM_SetEventDispatcher(nullptr);
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
    TestSearchMatching();
    TestSearchByFetching();
    TestSearchThroughManager();
    std::printf("UltraIMTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
