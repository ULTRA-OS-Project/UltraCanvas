// Tests/UltraStore/test_settings.cpp
// StoreSettings: the round trip through the settings file, the trusted keys,
// the lists the settings page takes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"

#include "StoreSettings.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>

using namespace UltraStore;
using namespace ultrastore_test;

TEST(settings_round_trip_through_the_file) {
    const std::string dir = TempRoot("settings");
    const std::string file = UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "settings.json");
    StoreSettings s;
    s.channel = "beta";
    s.language = "de";
    s.servers = {"https://a.example/store", "https://b.example/store"};
    s.extraKeys = {std::string(64, 'a')};
    s.refreshOnStart = false;
    s.windowWidth = 900;
    s.windowHeight = 600;
    s.downloadsExpanded = true;
    s.downloadsHeight = 240;
    {
        auto f = UltraCanvas::UltraCanvasAppSettings::AtFile(file);
        s.WriteTo(f);
        REQUIRE(f.Save());
    }
    auto f = UltraCanvas::UltraCanvasAppSettings::AtFile(file);
    REQUIRE(f.Load());
    const StoreSettings back = StoreSettings::From(f);
    REQUIRE_EQ(back.channel, std::string("beta"));
    REQUIRE_EQ(back.language, std::string("de"));
    REQUIRE_EQ(back.servers.size(), size_t(2));
    REQUIRE_EQ(back.servers[1], std::string("https://b.example/store"));
    REQUIRE_EQ(back.extraKeys.size(), size_t(1));
    REQUIRE(!back.refreshOnStart);
    REQUIRE_EQ(back.windowWidth, 900);
    REQUIRE_EQ(back.windowHeight, 600);
    REQUIRE(back.downloadsExpanded);
    REQUIRE_EQ(back.downloadsHeight, 240);
    std::error_code ec;
    std::filesystem::remove_all(UltraCanvas::PathFromUtf8(dir), ec);
}

TEST(defaults_and_fallbacks) {
    const StoreSettings s;
    REQUIRE_EQ(s.channel, std::string("stable"));
    REQUIRE(!s.Language().empty());        // the system's
    REQUIRE(s.Servers() == BuiltinServers());
    StoreSettings bad;
    bad.channel = "nightly";
    auto f = UltraCanvas::UltraCanvasAppSettings::AtFile("");
    bad.WriteTo(f);
    REQUIRE_EQ(StoreSettings::From(f).channel, std::string("stable"));   // an unknown channel is stable
}

TEST(trusted_keys_take_the_valid_hex_once) {
    StoreSettings s;
    const std::string key = std::string(64, 'b');
    s.extraKeys = {key, " " + key + " ", "not-a-key", std::string(62, 'c'), std::string(64, 'd')};
    const PublicKeys keys = s.TrustedKeys();
    REQUIRE_EQ(keys.size(), BuiltinKeysHex().size() + 2);
    REQUIRE(IsPublicKeyHex(key));
    REQUIRE(!IsPublicKeyHex("zz"));
    REQUIRE(!IsPublicKeyHex(std::string(62, 'c')));
}

TEST(lines_trim_and_skip_comments) {
    const auto lines = Lines("  https://a.example \n\n# a comment\r\nhttps://b.example\n");
    REQUIRE_EQ(lines.size(), size_t(2));
    REQUIRE_EQ(lines[0], std::string("https://a.example"));
    REQUIRE_EQ(lines[1], std::string("https://b.example"));
    REQUIRE(Lines("").empty());
}

TEST(the_code_stays_in_force_for_thirty_days) {
    StoreSettings s;
    REQUIRE(s.CodeInForce("2026-10-27").empty());
    s.code = "ANNA-20";
    s.codeEnteredAt = "2026-10-01";
    REQUIRE_EQ(s.CodeInForce("2026-10-01"), std::string("ANNA-20"));
    REQUIRE_EQ(s.CodeInForce("2026-10-31"), std::string("ANNA-20"));   // day 30
    REQUIRE(s.CodeInForce("2026-11-01").empty());                       // day 31
    REQUIRE(s.CodeInForce("2026-09-30").empty());                       // before it was entered: a clock set back
    // Through the file.
    const std::string dir = TempRoot("settings-code");
    auto file = UltraCanvas::UltraCanvasAppSettings::AtFile(UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "settings.json"));
    s.WriteTo(file);
    const StoreSettings back = StoreSettings::From(file);
    REQUIRE_EQ(back.code, std::string("ANNA-20"));
    REQUIRE_EQ(back.codeEnteredAt, std::string("2026-10-01"));
}
