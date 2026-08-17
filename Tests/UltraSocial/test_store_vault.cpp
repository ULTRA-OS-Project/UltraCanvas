// Tests/UltraSocial/test_store_vault.cpp
// Store (accounts + history on an in-memory UltraDatabase) and the
// credential vault file backend.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraSocialCredentialVault.h"
#include "UltraSocialStore.h"

#include <filesystem>

using namespace UltraSocial;

namespace {

Store FreshStore(const std::string& tag) {
    Store store;
    auto r = store.Open("ustest-" + tag, ":memory:");
    REQUIRE(r.success);
    return store;
}

Account MakeAccount(SocialNetwork network, const std::string& handle) {
    Account a;
    a.network     = network;
    a.accountId   = MakeAccountId(network, handle, "");
    a.handle      = handle;
    a.displayName = handle;
    a.server      = "https://example.social";
    return a;
}

} // namespace

TEST(types_account_id_slug) {
    REQUIRE_EQ(MakeAccountId(SocialNetwork::Mastodon, "mastodon.social", "Erika"),
               std::string{"mastodon-mastodon.social-erika"});
    REQUIRE_EQ(MakeAccountId(SocialNetwork::Telegram, "@My Channel", ""),
               std::string{"telegram--my-channel"});
    REQUIRE_EQ(SocialNetworkFromString(ToString(SocialNetwork::Bluesky)) ==
               SocialNetwork::Bluesky, true);
}

TEST(store_account_roundtrip) {
    Store store = FreshStore("accounts");
    REQUIRE(store.UpsertAccount(MakeAccount(SocialNetwork::Mastodon,
                                            "@e@m.social")).success);
    REQUIRE(store.UpsertAccount(MakeAccount(SocialNetwork::Bluesky,
                                            "e.bsky.social")).success);

    std::vector<Account> accounts;
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE_EQ(accounts.size(), std::size_t{2});

    // Upsert updates, not duplicates.
    Account updated = MakeAccount(SocialNetwork::Bluesky, "e.bsky.social");
    updated.displayName = "Erika";
    REQUIRE(store.UpsertAccount(updated).success);
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE_EQ(accounts.size(), std::size_t{2});

    REQUIRE(store.RemoveAccount(updated.accountId).success);
    REQUIRE(store.ListAccounts(accounts).success);
    REQUIRE_EQ(accounts.size(), std::size_t{1});

    // Empty id is rejected.
    Account bad;
    REQUIRE(!store.UpsertAccount(bad).success);
}

TEST(store_history_roundtrip) {
    Store store = FreshStore("history");

    HistoryEntry sent;
    sent.accountId  = "mastodon-m.social-e";
    sent.network    = SocialNetwork::Mastodon;
    sent.text       = "hello world";
    sent.mediaCount = 1;
    sent.succeeded  = true;
    sent.postId     = "111";
    sent.url        = "https://m.social/@e/111";
    REQUIRE(store.AddHistory(sent).success);
    REQUIRE(sent.id > 0);
    REQUIRE(sent.createdAt > 0);

    HistoryEntry failed;
    failed.accountId = "bluesky-e.bsky.social";
    failed.network   = SocialNetwork::Bluesky;
    failed.text      = "hello";
    failed.error     = "ExpiredToken";
    REQUIRE(store.AddHistory(failed).success);
    REQUIRE(failed.id > sent.id);

    std::vector<HistoryEntry> all;
    REQUIRE(store.ListHistory("", 0, all).success);
    REQUIRE_EQ(all.size(), std::size_t{2});
    REQUIRE_EQ(all[0].id, failed.id);          // most recent first
    REQUIRE_EQ(all[0].succeeded, false);
    REQUIRE_EQ(all[1].url, sent.url);
    REQUIRE(all[1].network == SocialNetwork::Mastodon);

    std::vector<HistoryEntry> one;
    REQUIRE(store.ListHistory(sent.accountId, 0, one).success);
    REQUIRE_EQ(one.size(), std::size_t{1});
    REQUIRE(store.ListHistory("", 1, one).success);
    REQUIRE_EQ(one.size(), std::size_t{1});
}

TEST(vault_roundtrip) {
    std::string dir =
        (std::filesystem::temp_directory_path() / "ultrasocial-vault-test").string();
    std::filesystem::remove_all(dir);

    {
        CredentialVault vault(dir);
        REQUIRE(vault.Store("acct-1", "{\"token\":\"secret\"}"));
        REQUIRE(vault.Has("acct-1"));
        REQUIRE(!vault.Store("", "x"));
    }
    {
        // Fresh instance reads what the first one wrote.
        CredentialVault vault(dir);
        std::string secret;
        REQUIRE(vault.Retrieve("acct-1", secret));
        REQUIRE_EQ(secret, std::string{"{\"token\":\"secret\"}"});
        REQUIRE(vault.Remove("acct-1"));
        REQUIRE(!vault.Has("acct-1"));
        REQUIRE(!vault.Remove("acct-1"));
    }
    // The on-disk file never contains the plaintext secret.
    std::filesystem::remove_all(dir);
}
