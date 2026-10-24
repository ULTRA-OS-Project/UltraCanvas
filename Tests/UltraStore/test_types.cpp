// Tests/UltraStore/test_types.cpp
// Targets, version comparison, localized text, hex.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraStoreTypes.h"

using namespace UltraStore;

TEST(targets_parse_and_print) {
    REQUIRE(ParseOS("linux") == OS::Linux);
    REQUIRE(ParseOS("MacOS") == OS::MacOS);
    REQUIRE(ParseOS("ultraos") == OS::UltraOS);
    REQUIRE(ParseOS("ios") == OS::IOS);
    REQUIRE(ParseOS("android") == OS::Android);
    REQUIRE(ParseOS("wasm") == OS::Wasm);
    REQUIRE_EQ(OSName(OS::Wasm), std::string("wasm"));
    REQUIRE(ParseOS("beos") == OS::Unknown);
    REQUIRE(ParseArch("x86_64") == Arch::X86_64);
    REQUIRE(ParseArch("aarch64") == Arch::Arm64);
    REQUIRE(ParseArch("armv7l") == Arch::Armv7);
    REQUIRE(ParseArch("wasm32") == Arch::Wasm32);
    REQUIRE(ParseArch("universal") == Arch::Universal);
    REQUIRE(ParseArch("any") == Arch::Any);
    REQUIRE(ParseArch("sparc") == Arch::Unknown);
    Target t;
    t.os = OS::MacOS; t.arch = Arch::Arm64; t.variants = {"min-os=11.0"};
    REQUIRE_EQ(t.Describe(), std::string("macos/arm64 (min-os=11.0)"));
}

TEST(versions_compare_numerically_and_by_prerelease) {
    REQUIRE(CompareVersions("0.9.400", "0.9.41") > 0);
    REQUIRE(CompareVersions("0.9.41", "0.9.400") < 0);
    REQUIRE(CompareVersions("1.0", "1.0.0") == 0);
    REQUIRE(CompareVersions("1.0.0-beta.1", "1.0.0") < 0);
    REQUIRE(CompareVersions("1.0.0", "1.0.0-beta.1") > 0);
    REQUIRE(CompareVersions("1.0.0-beta.1", "1.0.0-beta.2") < 0);
    REQUIRE(CompareVersions("1.10.0", "1.9.9") > 0);
    REQUIRE(CompareVersions("01.2", "1.2") == 0);
    REQUIRE(VersionLess("2.0", "10.0"));
    REQUIRE(VersionEqual("3.1.0", "3.1"));
}

TEST(localized_text_falls_back_to_english_then_anything) {
    LocalizedText t;
    t.byLanguage["en"] = "Hello";
    t.byLanguage["de"] = "Hallo";
    REQUIRE_EQ(t.Text("de"), std::string("Hallo"));
    REQUIRE_EQ(t.Text("de-AT"), std::string("Hallo"));
    REQUIRE_EQ(t.Text("fr"), std::string("Hello"));
    LocalizedText onlyDe;
    onlyDe.byLanguage["de"] = "Nur Deutsch";
    REQUIRE_EQ(onlyDe.Text("en"), std::string("Nur Deutsch"));
    REQUIRE_EQ(LocalizedText().Text("en"), std::string());
}

TEST(hex_round_trips_and_rejects_junk) {
    std::vector<uint8_t> bytes;
    REQUIRE(FromHex("00ff10Ab", bytes));
    REQUIRE(bytes.size() == 4 && bytes[1] == 0xFF && bytes[3] == 0xAB);
    REQUIRE_EQ(ToHex(bytes), std::string("00ff10ab"));
    REQUIRE(!FromHex("abc", bytes));
    REQUIRE(!FromHex("zz", bytes));
}

TEST(results_carry_values_or_errors) {
    auto ok = Result<int>::Succeed(7);
    REQUIRE(ok && ok.Value() == 7);
    auto bad = Result<int>::Failure(ErrorCode::NotFound, "nothing", "detail");
    REQUIRE(!bad);
    REQUIRE(bad.error.code == ErrorCode::NotFound);
    REQUIRE_EQ(bad.error.message, std::string("nothing"));
    auto done = Result<void>::Succeed();
    REQUIRE(done);
    REQUIRE(!Result<void>::Failure(ErrorCode::IoError, "disk"));
    REQUIRE_EQ(ErrorCodeName(ErrorCode::SequenceRollback), std::string("SequenceRollback"));
}
