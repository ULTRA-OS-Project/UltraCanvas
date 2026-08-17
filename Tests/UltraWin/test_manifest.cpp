// Tests/UltraWin/test_manifest.cpp
// Drive-mapping manifest parse/serialize round-trip. No Wine required.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

using namespace ultrawin_internal;

TEST(manifest_parses_valid_lines) {
    auto m = ParseMappingManifest(
        "# comment\n"
        "\n"
        "D=/data\n"
        "e=/media/stick\r\n");
    REQUIRE_EQ(m.size(), static_cast<size_t>(2));
    REQUIRE_EQ(m[0].driveLetter, 'D');
    REQUIRE_EQ(m[0].hostPath, std::string("/data"));
    REQUIRE_EQ(m[1].driveLetter, 'E');  // canonicalised to upper case
    REQUIRE_EQ(m[1].hostPath, std::string("/media/stick"));
}

TEST(manifest_drops_invalid_lines) {
    auto m = ParseMappingManifest(
        "no-equals-line\n"
        "1=/digit-letter\n"
        "D=relative/path\n"
        "D=\n"
        "=/nothing\n");
    REQUIRE_EQ(m.size(), static_cast<size_t>(0));
}

TEST(manifest_later_line_wins_per_letter) {
    auto m = ParseMappingManifest("D=/first\nd=/second\n");
    REQUIRE_EQ(m.size(), static_cast<size_t>(1));
    REQUIRE_EQ(m[0].hostPath, std::string("/second"));
}

TEST(manifest_round_trip) {
    std::vector<UltraWinFolderMapping> in = {
        {'D', "/data"},
        {'m', "/media"},
        {'#', "/bad-letter-dropped"},
        {'R', "relative-dropped"},
    };
    auto out = ParseMappingManifest(SerializeMappingManifest(in));
    REQUIRE_EQ(out.size(), static_cast<size_t>(2));
    REQUIRE_EQ(out[0].driveLetter, 'D');
    REQUIRE_EQ(out[1].driveLetter, 'M');
    REQUIRE_EQ(out[1].hostPath, std::string("/media"));
}
