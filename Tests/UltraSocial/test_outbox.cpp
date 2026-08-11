// Tests/UltraSocial/test_outbox.cpp
// The scheduled outbox: queue round-trip (incl. media JSON), due
// selection, and FlushDueOutbox end-to-end through a real connector
// against a loopback fake — success, retry with backoff, giving up, and
// the account-removed path.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "fake_http_server.h"
#include "test_framework.h"

#include "UltraSocialCredentialVault.h"
#include "UltraSocialPublisher.h"
#include "UltraSocialStore.h"

#include <filesystem>

using namespace UltraSocial;
using ultrasocial_test::FakeHttpServer;

namespace {

Store FreshStore(const std::string& tag) {
    Store store;
    REQUIRE(store.Open("usoutbox-" + tag, ":memory:").success);
    return store;
}

// A vault in a fresh temp dir per test.
struct TempVault {
    std::string dir;
    CredentialVault vault;
    explicit TempVault(const std::string& tag)
        : dir((std::filesystem::temp_directory_path() /
               ("ultrasocial-outbox-" + tag)).string()),
          vault(dir) {
        std::filesystem::remove_all(dir);
    }
    ~TempVault() { std::filesystem::remove_all(dir); }
};

// A telegram account whose credentials point at the fake server — the
// cheapest way to drive the real publish path without external network.
Account SeedTelegramAccount(Store& store, CredentialVault& vault,
                            const std::string& serverBaseUrl) {
    Account account;
    account.accountId   = "telegram-testchannel";
    account.network     = SocialNetwork::Telegram;
    account.handle      = "@testchannel";
    account.displayName = "Test Channel";
    account.server      = serverBaseUrl;
    REQUIRE(store.UpsertAccount(account).success);
    REQUIRE(vault.Store(account.accountId,
                        R"({"server":")" + serverBaseUrl +
                        R"(","bot_token":"123:T","chat_id":"@testchannel",)"
                        R"("chat_username":"testchannel"})"));
    return account;
}

OutboxEntry Enqueue(Store& store, const std::string& accountId,
                    const std::string& text, int64_t when) {
    OutboxEntry entry;
    entry.accountId   = accountId;
    entry.network     = SocialNetwork::Telegram;
    entry.draft.text  = text;
    entry.scheduledAt = when;
    REQUIRE(store.EnqueuePost(entry).success);
    REQUIRE(entry.id > 0);
    return entry;
}

} // namespace

TEST(outbox_roundtrip_and_due_selection) {
    Store store = FreshStore("roundtrip");

    OutboxEntry entry;
    entry.accountId        = "mastodon-a";
    entry.network          = SocialNetwork::Mastodon;
    entry.draft.text       = "queued text";
    entry.draft.visibility = "unlisted";
    entry.draft.media      = {{"/tmp/a.png", "image/png", "alt a"},
                              {"/tmp/b.jpg", "", ""}};
    entry.scheduledAt      = 2000;
    REQUIRE(store.EnqueuePost(entry).success);

    std::vector<OutboxEntry> all;
    REQUIRE(store.ListOutbox(all).success);
    REQUIRE_EQ(all.size(), std::size_t{1});
    REQUIRE_EQ(all[0].draft.text, std::string{"queued text"});
    REQUIRE_EQ(all[0].draft.visibility, std::string{"unlisted"});
    REQUIRE_EQ(all[0].draft.media.size(), std::size_t{2});   // JSON round-trip
    REQUIRE_EQ(all[0].draft.media[0].altText, std::string{"alt a"});
    REQUIRE_EQ(all[0].draft.media[1].filePath, std::string{"/tmp/b.jpg"});
    REQUIRE_EQ(all[0].attempts, 0);

    std::vector<OutboxEntry> due;
    REQUIRE(store.DueOutbox(1999, due).success);
    REQUIRE(due.empty());                          // not due yet
    REQUIRE(store.DueOutbox(2000, due).success);
    REQUIRE_EQ(due.size(), std::size_t{1});

    REQUIRE(store.RescheduleOutbox(entry.id, 5000, 2, "flaky").success);
    REQUIRE(store.ListOutbox(all).success);
    REQUIRE_EQ(all[0].scheduledAt, int64_t{5000});
    REQUIRE_EQ(all[0].attempts, 2);
    REQUIRE_EQ(all[0].lastError, std::string{"flaky"});

    REQUIRE(store.RemoveOutbox(entry.id).success);
    REQUIRE(store.ListOutbox(all).success);
    REQUIRE(all.empty());
}

TEST(outbox_flush_publishes_due_entry) {
    UltraNet_Initialize();
    FakeHttpServer server({
        {200, R"({"ok":true,"result":{"message_id":91}})"}});
    Store store = FreshStore("flush-ok");
    TempVault tv("flush-ok");
    SeedTelegramAccount(store, tv.vault, server.BaseUrl());

    Enqueue(store, "telegram-testchannel", "scheduled hello", 1000);
    Enqueue(store, "telegram-testchannel", "far future", 9999999999);

    FlushStats stats = FlushDueOutbox(store, tv.vault, 1000);
    server.Join();

    REQUIRE_EQ(stats.published, 1);
    REQUIRE_EQ(stats.rescheduled, 0);
    REQUIRE_EQ(stats.abandoned, 0);
    REQUIRE(server.Requests()[0].BodyHas(R"("text":"scheduled hello")"));

    // Delivered entry left the queue; the future one stayed.
    std::vector<OutboxEntry> remaining;
    REQUIRE(store.ListOutbox(remaining).success);
    REQUIRE_EQ(remaining.size(), std::size_t{1});
    REQUIRE_EQ(remaining[0].draft.text, std::string{"far future"});

    // And produced a success history row with the permalink.
    std::vector<HistoryEntry> history;
    REQUIRE(store.ListHistory("", 0, history).success);
    REQUIRE_EQ(history.size(), std::size_t{1});
    REQUIRE(history[0].succeeded);
    REQUIRE_EQ(history[0].url, std::string{"https://t.me/testchannel/91"});
}

TEST(outbox_flush_retries_then_gives_up) {
    UltraNet_Initialize();
    Store store = FreshStore("flush-retry");
    TempVault tv("flush-retry");

    {
        // First flush: the Bot API refuses -> rescheduled with backoff.
        FakeHttpServer server({
            {200, R"({"ok":false,"description":"chat not found"})"}});
        SeedTelegramAccount(store, tv.vault, server.BaseUrl());
        OutboxEntry entry =
            Enqueue(store, "telegram-testchannel", "will fail", 1000);

        FlushStats stats = FlushDueOutbox(store, tv.vault, 1000);
        server.Join();
        REQUIRE_EQ(stats.rescheduled, 1);
        REQUIRE_EQ(stats.published, 0);

        std::vector<OutboxEntry> queued;
        REQUIRE(store.ListOutbox(queued).success);
        REQUIRE_EQ(queued.size(), std::size_t{1});
        REQUIRE_EQ(queued[0].attempts, 1);
        REQUIRE_EQ(queued[0].scheduledAt,
                   int64_t{1000 + kOutboxRetryBackoffSeconds});
        REQUIRE(queued[0].lastError.find("chat not found") != std::string::npos);

        // No history row for an intermediate failure.
        std::vector<HistoryEntry> history;
        REQUIRE(store.ListHistory("", 0, history).success);
        REQUIRE(history.empty());

        // Fast-forward to the last allowed attempt.
        REQUIRE(store.RescheduleOutbox(queued[0].id, 2000,
                                       kOutboxMaxAttempts - 1,
                                       "chat not found").success);
    }
    {
        // Final attempt fails -> abandoned with a failed history row.
        FakeHttpServer server({
            {200, R"({"ok":false,"description":"chat not found"})"}});
        Account account;   // refresh the account's server to the new fake
        account = SeedTelegramAccount(store, tv.vault, server.BaseUrl());

        FlushStats stats = FlushDueOutbox(store, tv.vault, 999999);
        server.Join();
        REQUIRE_EQ(stats.abandoned, 1);

        std::vector<OutboxEntry> queued;
        REQUIRE(store.ListOutbox(queued).success);
        REQUIRE(queued.empty());

        std::vector<HistoryEntry> history;
        REQUIRE(store.ListHistory("", 0, history).success);
        REQUIRE_EQ(history.size(), std::size_t{1});
        REQUIRE(!history[0].succeeded);
        REQUIRE(history[0].error.find("gave up after") != std::string::npos);
    }
}

TEST(outbox_flush_abandons_removed_account) {
    Store store = FreshStore("flush-gone");
    TempVault tv("flush-gone");

    Enqueue(store, "telegram-testchannel", "orphaned", 1000);
    // No matching account in the store.

    FlushStats stats = FlushDueOutbox(store, tv.vault, 1000);
    REQUIRE_EQ(stats.abandoned, 1);

    std::vector<OutboxEntry> queued;
    REQUIRE(store.ListOutbox(queued).success);
    REQUIRE(queued.empty());

    std::vector<HistoryEntry> history;
    REQUIRE(store.ListHistory("", 0, history).success);
    REQUIRE_EQ(history.size(), std::size_t{1});
    REQUIRE(history[0].error.find("account was removed") != std::string::npos);
}
