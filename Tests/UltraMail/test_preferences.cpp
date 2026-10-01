// Tests/UltraMail/test_preferences.cpp
// The app-wide preferences behind the Settings window: the remote-image
// policy, trusted websites (domain matching) and the reading options survive
// a save and a load, and an old file keeps the defaults.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailPreferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraMail;

TEST(preferences_normalize_domain) {
    REQUIRE_EQ(Preferences::NormalizeDomain("https://www.Anthropic.com/x?y"), std::string("anthropic.com"));
    REQUIRE_EQ(Preferences::NormalizeDomain("@mail.example.org"), std::string("mail.example.org"));
    REQUIRE_EQ(Preferences::NormalizeDomain("*.example.org"), std::string("example.org"));
    REQUIRE_EQ(Preferences::NormalizeDomain("news@Example.COM"), std::string("example.com"));
    REQUIRE_EQ(Preferences::NormalizeDomain("//cdn.example.net:8080/a.png"), std::string("cdn.example.net"));
    REQUIRE(Preferences::NormalizeDomain("localhost").empty());
    REQUIRE(Preferences::NormalizeDomain("not a domain.com").empty());
}

TEST(preferences_domain_matches_subdomains_only) {
    REQUIRE(Preferences::DomainMatches("example.com", "example.com"));
    REQUIRE(Preferences::DomainMatches("news.example.com", "example.com"));
    REQUIRE(!Preferences::DomainMatches("badexample.com", "example.com"));
    REQUIRE(!Preferences::DomainMatches("example.com.evil.net", "example.com"));
    Preferences p;
    p.trustedImageDomains = { "anthropic.com" };
    REQUIRE(p.IsTrustedDomain("https://claude.ai.anthropic.com/i.png"));
    REQUIRE(p.IsTrustedDomain("mail.anthropic.com"));
    REQUIRE(!p.IsTrustedDomain("https://anthropic.com.example/i.png"));
}

TEST(preferences_round_trip) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_test.ini").string();
    Preferences out;
    out.showReadingPane = false;
    out.fetchSenderIcons = false;
    out.remoteImages = RemoteImagePolicy::LoadNever;
    out.trustedImageDomains = { "anthropic.com", "example.org" };
    out.remoteImageSenders = { "news@example.com" };
    out.showHtml = false;
    out.messageTextSize = 16;
    REQUIRE(out.Save(path));

    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(!in.showReadingPane);
    REQUIRE(!in.fetchSenderIcons);
    REQUIRE(in.remoteImages == RemoteImagePolicy::LoadNever);
    REQUIRE(in.trustedImageDomains == out.trustedImageDomains);
    REQUIRE(in.remoteImageSenders == out.remoteImageSenders);
    REQUIRE(!in.showHtml);
    REQUIRE_EQ(in.messageTextSize, 16);
    std::remove(path.c_str());
}

TEST(preferences_old_file_keeps_defaults) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "ultramail_prefs_old.ini").string();
    {
        std::ofstream f(UltraCanvas::PathFromUtf8(path));
        f << "reading_pane = true\nfetch_sender_icons = true\nremote_images_from = a@b.c\n"
             "message_text_size = 400\n";
    }
    Preferences in;
    REQUIRE(in.Load(path));
    REQUIRE(in.remoteImages == RemoteImagePolicy::LoadTrusted);   // the default
    REQUIRE(in.showHtml);
    REQUIRE_EQ(in.messageTextSize, 24);                           // clamped
    REQUIRE(in.remoteImageSenders.count("a@b.c") == 1);
    std::remove(path.c_str());
}
