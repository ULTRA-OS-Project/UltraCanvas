// Tests/UltraCloud/test_accounts.cpp
// AccountStore: ids, upsert/list/get, the default account, removal.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudAccounts.h>

using namespace UltraCloud;

namespace {
AccountStore Fresh(const std::string& tag) {
    AccountStore s;
    REQUIRE(s.Open("uctest-" + tag, ":memory:"));
    return s;
}
Account Make(const std::string& provider, const std::string& user, const std::string& url) {
    Account a;
    a.providerId = provider; a.username = user; a.serverUrl = url;
    a.accountId = MakeAccountId(provider, user, url);
    a.displayName = user + " on " + provider;
    return a;
}
} // namespace

TEST(account_id_is_a_slug_of_provider_user_host) {
    REQUIRE_EQ(MakeAccountId("nextcloud", "erika", "https://cloud.example.com/index.php"),
               std::string("nextcloud-erika-cloud-example-com"));
    REQUIRE_EQ(MakeAccountId("webdav", "Max Muster", ""), std::string("webdav-max-muster"));
}

TEST(first_account_becomes_default) {
    AccountStore s = Fresh("first");
    REQUIRE(s.Upsert(Make("nextcloud", "erika", "https://cloud.example.com")));
    Account d;
    REQUIRE(s.GetDefault(d));
    REQUIRE_EQ(d.username, std::string("erika"));
    REQUIRE(d.isDefault);

    REQUIRE(s.Upsert(Make("webdav", "max", "https://dav.example.org")));
    REQUIRE(s.GetDefault(d));
    REQUIRE_EQ(d.username, std::string("erika"));   // unchanged
    std::vector<Account> all;
    REQUIRE(s.List(all));
    REQUIRE_EQ(all.size(), (size_t)2);
    REQUIRE(all.front().isDefault);                   // default listed first
}

TEST(set_default_moves_the_flag) {
    AccountStore s = Fresh("default");
    REQUIRE(s.Upsert(Make("nextcloud", "erika", "https://cloud.example.com")));
    Account max = Make("webdav", "max", "https://dav.example.org");
    REQUIRE(s.Upsert(max));
    REQUIRE(s.SetDefault(max.accountId));
    Account d;
    REQUIRE(s.GetDefault(d));
    REQUIRE_EQ(d.accountId, max.accountId);
    Account erika;
    REQUIRE(s.Get("nextcloud-erika-cloud-example-com", erika));
    REQUIRE(!erika.isDefault);
}

TEST(upsert_updates_and_keeps_default) {
    AccountStore s = Fresh("upsert");
    Account a = Make("nextcloud", "erika", "https://cloud.example.com");
    REQUIRE(s.Upsert(a));
    a.displayName = "Work cloud";
    a.remoteFolder = "/Mail";
    a.isDefault = false;   // an update must not drop the stored default
    REQUIRE(s.Upsert(a));
    Account got;
    REQUIRE(s.Get(a.accountId, got));
    REQUIRE_EQ(got.displayName, std::string("Work cloud"));
    REQUIRE_EQ(got.remoteFolder, std::string("/Mail"));
    REQUIRE(got.isDefault);
}

TEST(remove_hands_default_to_the_next_account) {
    AccountStore s = Fresh("remove");
    Account erika = Make("nextcloud", "erika", "https://cloud.example.com");
    Account max   = Make("webdav", "max", "https://dav.example.org");
    REQUIRE(s.Upsert(erika));
    REQUIRE(s.Upsert(max));
    REQUIRE(s.Remove(erika.accountId));
    Account d;
    REQUIRE(s.GetDefault(d));
    REQUIRE_EQ(d.accountId, max.accountId);
    REQUIRE(s.Remove(max.accountId));
    REQUIRE(!s.GetDefault(d));
    REQUIRE(s.Remove("nope").code == ResultCode::NotFound);
}
