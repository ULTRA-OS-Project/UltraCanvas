// Tests/UltraNet/test_oauth2_apps.cpp
// The shared OAuth2 app registry: the four tiers in order, whole-tier
// precedence, environment prefixes, INI parsing, aliases and Clear(). No
// network. Environment variables are set and unset within each test.
#include "test_framework.h"

#include <UltraNet/UltraNetOAuth2Apps.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    if (value && *value) setenv(name, value, 1);
    else                 unsetenv(name);
#endif
}

UltraNetOAuth2App App(const std::string& id, const std::string& secret = "",
                      const std::string& redirect = "") {
    UltraNetOAuth2App a;
    a.clientId = id; a.clientSecret = secret; a.redirectUri = redirect;
    return a;
}

} // namespace

TEST(oauth2_apps_env_name_upper_cases_and_sanitises_the_id) {
    REQUIRE_EQ(UltraNet_OAuth2AppEnvName("ULTRAMAIL_", "google", "_CLIENT_ID"),
               std::string("ULTRAMAIL_GOOGLE_CLIENT_ID"));
    REQUIRE_EQ(UltraNet_OAuth2AppEnvName("X_", "my-cloud.v2", "_REDIRECT_URI"),
               std::string("X_MY_CLOUD_V2_REDIRECT_URI"));
}

TEST(oauth2_apps_tiers_in_order_and_whole) {
    UltraNet_OAuth2ClearApps();
    SetEnv("ULTRANET_OAUTH_TESTPROV_CLIENT_ID", "");
    REQUIRE(!UltraNet_OAuth2HasApp("testprov"));
    REQUIRE(UltraNet_OAuth2GetApp("testprov").redirectUri.empty());

    // Tier 3: the INI file.
    REQUIRE_EQ(UltraNet_OAuth2ParseAppsIni(
        "; comment\n"
        "[TestProv]\n"
        "client_id = from-ini\n"
        "client_secret=ini-secret\n"
        "redirect_uri = http://127.0.0.1:4711/cb\n"
        "[empty]\n"
        "client_secret = no-id-so-ignored\n"), 1);
    REQUIRE(UltraNet_OAuth2HasApp("testprov"));
    REQUIRE(!UltraNet_OAuth2HasApp("empty"));
    UltraNetOAuth2App a = UltraNet_OAuth2GetApp("testprov");
    REQUIRE_EQ(a.clientId, std::string("from-ini"));
    REQUIRE_EQ(a.clientSecret, std::string("ini-secret"));
    REQUIRE_EQ(a.redirectUri, std::string("http://127.0.0.1:4711/cb"));

    // Tier 2 outranks the file, and is taken whole: no secret and no redirect
    // leak through from the file tier.
    SetEnv("ULTRANET_OAUTH_TESTPROV_CLIENT_ID", "from-env");
    a = UltraNet_OAuth2GetApp("testprov");
    REQUIRE_EQ(a.clientId, std::string("from-env"));
    REQUIRE(a.clientSecret.empty());
    REQUIRE(a.redirectUri.empty());

    // Tier 1 outranks both.
    UltraNet_OAuth2SetApp("testprov", App("from-set"));
    REQUIRE_EQ(UltraNet_OAuth2GetApp("testprov").clientId, std::string("from-set"));

    // An unconfigured Set() entry hides nothing.
    UltraNet_OAuth2SetApp("testprov", UltraNetOAuth2App{});
    REQUIRE_EQ(UltraNet_OAuth2GetApp("testprov").clientId, std::string("from-env"));

    SetEnv("ULTRANET_OAUTH_TESTPROV_CLIENT_ID", "");
    UltraNet_OAuth2ClearApps();
    REQUIRE(!UltraNet_OAuth2HasApp("testprov"));
}

TEST(oauth2_apps_built_in_is_the_floor_and_survives_clear) {
    UltraNet_OAuth2ClearApps();
    UltraNet_OAuth2SetBuiltInApp("bakedprov", UltraNetOAuth2App{});   // ignored
    REQUIRE(!UltraNet_OAuth2HasApp("bakedprov"));
    UltraNet_OAuth2SetBuiltInApp("bakedprov", App("baked-id", "baked-secret"));
    REQUIRE_EQ(UltraNet_OAuth2GetApp("bakedprov").clientId, std::string("baked-id"));

    // Everything above it wins.
    REQUIRE_EQ(UltraNet_OAuth2ParseAppsIni("[bakedprov]\nclient_id = from-ini\n"), 1);
    REQUIRE_EQ(UltraNet_OAuth2GetApp("bakedprov").clientId, std::string("from-ini"));

    // Clear() drops the session's registrations, not the build's.
    UltraNet_OAuth2ClearApps();
    REQUIRE_EQ(UltraNet_OAuth2GetApp("bakedprov").clientId, std::string("baked-id"));
    REQUIRE_EQ(UltraNet_OAuth2GetApp("bakedprov").clientSecret, std::string("baked-secret"));
}

TEST(oauth2_apps_env_prefixes_are_consulted_in_registration_order) {
    UltraNet_OAuth2ClearApps();
    UltraNet_OAuth2AddAppEnvPrefix("TESTAPP_");
    UltraNet_OAuth2AddAppEnvPrefix("TESTAPP_");   // kept once
    std::vector<std::string> prefixes = UltraNet_OAuth2AppEnvPrefixes();
    REQUIRE(!prefixes.empty());
    REQUIRE_EQ(prefixes.front(), std::string("ULTRANET_OAUTH_"));
    int found = 0;
    for (const auto& p : prefixes) if (p == "TESTAPP_") ++found;
    REQUIRE_EQ(found, 1);

    SetEnv("TESTAPP_PREFPROV_CLIENT_ID", "app-prefix");
    REQUIRE_EQ(UltraNet_OAuth2GetApp("prefprov").clientId, std::string("app-prefix"));
    // The shared prefix comes first.
    SetEnv("ULTRANET_OAUTH_PREFPROV_CLIENT_ID", "shared-prefix");
    REQUIRE_EQ(UltraNet_OAuth2GetApp("prefprov").clientId, std::string("shared-prefix"));
    SetEnv("ULTRANET_OAUTH_PREFPROV_CLIENT_ID", "");
    SetEnv("TESTAPP_PREFPROV_CLIENT_ID", "");
    REQUIRE(!UltraNet_OAuth2HasApp("prefprov"));
}

TEST(oauth2_apps_alias_falls_back_and_the_specific_id_wins) {
    UltraNet_OAuth2ClearApps();
    UltraNet_OAuth2SetAppAlias("drive-test", "google-test");
    REQUIRE(!UltraNet_OAuth2HasApp("drive-test"));

    // One generic registration serves the specific id ...
    UltraNet_OAuth2SetApp("google-test", App("google-client"));
    REQUIRE_EQ(UltraNet_OAuth2GetApp("drive-test").clientId, std::string("google-client"));

    // ... until the specific id has its own, at any tier: the file tier for
    // the specific id beats the Set() tier of the alias.
    REQUIRE_EQ(UltraNet_OAuth2ParseAppsIni("[drive-test]\nclient_id = drive-client\n"), 1);
    REQUIRE_EQ(UltraNet_OAuth2GetApp("drive-test").clientId, std::string("drive-client"));
    REQUIRE_EQ(UltraNet_OAuth2GetApp("google-test").clientId, std::string("google-client"));

    // Chains follow; a cycle terminates.
    UltraNet_OAuth2SetAppAlias("photos-test", "drive-test");
    REQUIRE_EQ(UltraNet_OAuth2GetApp("photos-test").clientId, std::string("drive-client"));
    UltraNet_OAuth2SetAppAlias("loop-a", "loop-b");
    UltraNet_OAuth2SetAppAlias("loop-b", "loop-a");
    REQUIRE(!UltraNet_OAuth2HasApp("loop-a"));

    // Removing the alias removes the fallback.
    UltraNet_OAuth2SetAppAlias("photos-test", "");
    REQUIRE(!UltraNet_OAuth2HasApp("photos-test"));
    UltraNet_OAuth2SetAppAlias("drive-test", "");
    UltraNet_OAuth2SetAppAlias("loop-a", "");
    UltraNet_OAuth2SetAppAlias("loop-b", "");
    UltraNet_OAuth2ClearApps();
}

TEST(oauth2_apps_load_file_tolerates_a_missing_file) {
    UltraNet_OAuth2ClearApps();
    fs::path dir = fs::temp_directory_path() / "ultranet_oauth2_apps_ini";
    fs::remove_all(dir);
    fs::create_directories(dir);
    REQUIRE_EQ(UltraNet_OAuth2LoadAppsFile((dir / "oauth.ini").string()), 0);
    {
        std::ofstream os(dir / "oauth.ini");
        os << "[fileprov]\nclient_id = file-id\n";
    }
    REQUIRE_EQ(UltraNet_OAuth2LoadAppsFile((dir / "oauth.ini").string()), 1);
    REQUIRE_EQ(UltraNet_OAuth2GetApp("fileprov").clientId, std::string("file-id"));
    UltraNet_OAuth2ClearApps();
    fs::remove_all(dir);
}
