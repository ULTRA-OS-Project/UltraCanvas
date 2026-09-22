// Tests/UltraSocial/test_store_vault.cpp
// Store (accounts + history on an in-memory UltraDatabase) and the
// credential vault: UltraVault's device-key vault with UltraSocial's profile,
// including the migration of the 0.1 file format UltraSocial used to write.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraSocialCredentialVault.h"
#include "UltraSocialStore.h"

#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode (legacy format)

#include <filesystem>
#include <fstream>
#include <iterator>

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

// The vault is UltraVault's encrypted file behind a device key: nothing works
// before TryAutoUnlock(), a fresh directory gets a key and a vault, and a
// second instance on the same directory reads what the first one wrote.
TEST(vault_roundtrip) {
    std::string dir =
        (std::filesystem::temp_directory_path() / "ultrasocial-vault-test").string();
    std::filesystem::remove_all(dir);

    {
        CredentialVault vault(dir);
        REQUIRE(!vault.IsUnlocked());
        REQUIRE(!vault.Store("acct-1", "x"));          // locked: refused, not dropped
        REQUIRE(vault.TryAutoUnlock());
        REQUIRE(vault.IsUnlocked());
        REQUIRE(std::filesystem::exists(std::filesystem::path(dir) / "device.key"));
        REQUIRE_EQ(vault.KeyFor("acct-1"), std::string{"social.ultrasocial.acct-1"});

        REQUIRE(vault.Store("acct-1", "{\"token\":\"secret\"}"));
        REQUIRE(vault.Has("acct-1"));
        REQUIRE(!vault.Store("", "x"));

        // The vault file never contains the plaintext secret, and the 0.1
        // key file is not written — the key is derived, not stored.
        std::ifstream is(vault.VaultPath(), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(is)),
                            std::istreambuf_iterator<char>());
        REQUIRE(!content.empty());
        REQUIRE(content.find("secret") == std::string::npos);
        REQUIRE(!std::filesystem::exists(std::filesystem::path(dir) / "vault.key"));
        vault.Lock();
    }
    {
        // Fresh instance unlocks with the stored device key and reads what
        // the first one wrote.
        CredentialVault vault(dir);
        REQUIRE(vault.Exists());
        REQUIRE(vault.TryAutoUnlock());
        std::string secret;
        REQUIRE(vault.Retrieve("acct-1", secret));
        REQUIRE_EQ(secret, std::string{"{\"token\":\"secret\"}"});
        REQUIRE(vault.Remove("acct-1"));
        REQUIRE(!vault.Has("acct-1"));
        REQUIRE(!vault.Remove("acct-1"));
        vault.Lock();
    }
    std::filesystem::remove_all(dir);
}

// A vault written by UltraSocial 0.1 (secrets XOR-ed against vault.key, one
// base64 line per account in creds.dat) is carried into the encrypted vault
// on the first unlock, and the weak files are removed.
TEST(vault_migrates_legacy_format) {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ultrasocial-vault-migrate";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const std::vector<uint8_t> key(32, 0x5A);
    {
        std::ofstream ks(dir / "vault.key", std::ios::binary);
        ks.write(reinterpret_cast<const char*>(key.data()),
                 static_cast<std::streamsize>(key.size()));
    }
    auto xorWith = [&key](const std::string& in) {
        std::string out = in;
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
        return out;
    };
    auto b64 = [](const std::string& in) {
        return UltraNet_Base64Encode(std::vector<uint8_t>(in.begin(), in.end()), false);
    };
    const std::string blob = R"({"bot_token":"123:T","chat_id":"@c"})";
    {
        std::ofstream os(dir / "creds.dat");
        os << b64("telegram-old") << '\t' << b64(xorWith(blob)) << '\n';
        os << b64("mastodon-old") << '\t' << b64(xorWith("tok-2")) << '\n';
    }

    CredentialVault vault(dir.string());
    REQUIRE(vault.TryAutoUnlock());

    std::string got;
    REQUIRE(vault.Retrieve("telegram-old", got));
    REQUIRE_EQ(got, blob);
    REQUIRE(vault.Retrieve("mastodon-old", got));
    REQUIRE_EQ(got, std::string{"tok-2"});

    REQUIRE(!std::filesystem::exists(dir / "creds.dat"));
    REQUIRE(!std::filesystem::exists(dir / "vault.key"));
    REQUIRE(std::filesystem::exists(dir / "device.key"));
    REQUIRE(vault.Exists());

    vault.Lock();
    std::filesystem::remove_all(dir);
}
