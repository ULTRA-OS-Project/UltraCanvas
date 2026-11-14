// Tests/UltraSocial/test_settings.cpp
// The sign-in settings: standard values, the vault round trip, falling back
// to the standard value for a field saved empty, and filling the fields the
// add-account form left empty.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraSocialSettings.h"

#include <filesystem>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraSocial;

TEST(settings_standard_values) {
    const SignInSettings s = DefaultSignInSettings();
    REQUIRE_EQ(s.mastodonInstance, std::string{"mastodon.social"});
    REQUIRE_EQ(s.blueskyServer, std::string{"bsky.social"});
}

TEST(settings_json_roundtrip) {
    SignInSettings s;
    s.mastodonInstance     = "fosstodon.org";
    s.blueskyServer        = "pds.example";
    s.redditClientId       = "rid";
    s.xClientId            = "xid";
    s.linkedInClientId     = "lid";
    s.linkedInClientSecret = "lsecret";
    REQUIRE(SignInSettingsFromJson(SignInSettingsToJson(s)) == s);
    REQUIRE(SignInSettingsFromJson("not json") == SignInSettings{});
}

TEST(settings_vault_roundtrip_and_fallback) {
    const auto dir = std::filesystem::temp_directory_path() / "ultrasocial-settings-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    {
        CredentialVault vault(UltraCanvas::PathToUtf8(dir));
        REQUIRE(vault.TryAutoUnlock());
        // Nothing saved yet: the standard values.
        REQUIRE(LoadSignInSettings(vault) == DefaultSignInSettings());

        SignInSettings edited = DefaultSignInSettings();
        edited.mastodonInstance = "";          // cleared -> standard again
        edited.redditClientId   = "my-reddit-app";
        REQUIRE(SaveSignInSettings(vault, edited));
    }
    {
        CredentialVault vault(UltraCanvas::PathToUtf8(dir));
        REQUIRE(vault.TryAutoUnlock());
        const SignInSettings loaded = LoadSignInSettings(vault);
        REQUIRE_EQ(loaded.mastodonInstance, std::string{"mastodon.social"});
        REQUIRE_EQ(loaded.redditClientId, std::string{"my-reddit-app"});
    }
    std::filesystem::remove_all(dir, ec);
}

TEST(settings_fill_what_the_form_left_empty) {
    SignInSettings s = DefaultSignInSettings();
    s.redditClientId       = "rid";
    s.linkedInClientId     = "lid";
    s.linkedInClientSecret = "lsecret";

    AuthInput mastodon;
    ApplySignInSettings(SocialNetwork::Mastodon, s, mastodon);
    REQUIRE_EQ(mastodon.server, std::string{"mastodon.social"});

    AuthInput typed;
    typed.server = "fosstodon.org";
    ApplySignInSettings(SocialNetwork::Mastodon, s, typed);
    REQUIRE_EQ(typed.server, std::string{"fosstodon.org"});   // the user's wins

    AuthInput reddit;
    ApplySignInSettings(SocialNetwork::Reddit, s, reddit);
    REQUIRE_EQ(reddit.clientId, std::string{"rid"});

    AuthInput linkedIn;
    ApplySignInSettings(SocialNetwork::LinkedIn, s, linkedIn);
    REQUIRE_EQ(linkedIn.clientId, std::string{"lid"});
    REQUIRE_EQ(linkedIn.secret, std::string{"lsecret"});

    // Telegram's secret is its bot token: never filled from Settings.
    AuthInput telegram;
    ApplySignInSettings(SocialNetwork::Telegram, s, telegram);
    REQUIRE(telegram.secret.empty());

    REQUIRE(!NeedsClientId(SocialNetwork::Reddit, s));
    REQUIRE(NeedsClientId(SocialNetwork::X, s));
    REQUIRE(!NeedsClientId(SocialNetwork::Mastodon, s));
}
