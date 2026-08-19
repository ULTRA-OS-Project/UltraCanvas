// Tests/UltraWin/test_validation.cpp
// Pure-helper coverage: environment-name validation, drive letters, and
// the default environments-root derivation. No Wine required.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraWinInternal.h"

using namespace ultrawin_internal;

TEST(env_name_accepts_simple_names) {
    CHECK(IsValidEnvironmentName("Default"));
    CHECK(IsValidEnvironmentName("my-app_1.2"));
    CHECK(IsValidEnvironmentName("A"));
}

TEST(env_name_rejects_path_tricks) {
    CHECK(!IsValidEnvironmentName(""));
    CHECK(!IsValidEnvironmentName("."));
    CHECK(!IsValidEnvironmentName(".."));
    CHECK(!IsValidEnvironmentName(".hidden"));
    CHECK(!IsValidEnvironmentName("a/b"));
    CHECK(!IsValidEnvironmentName("a\\b"));
    CHECK(!IsValidEnvironmentName("a b"));
    CHECK(!IsValidEnvironmentName(std::string(65, 'x')));
    CHECK(IsValidEnvironmentName(std::string(64, 'x')));
}

TEST(drive_letters) {
    CHECK(IsValidDriveLetter('a'));
    CHECK(IsValidDriveLetter('Z'));
    CHECK(!IsValidDriveLetter('1'));
    CHECK(!IsValidDriveLetter(':'));
    CHECK(!IsValidDriveLetter(0));
    REQUIRE_EQ(CanonicalDriveLetter('u'), 'U');
    REQUIRE_EQ(CanonicalDriveLetter('U'), 'U');
}

TEST(default_environments_root) {
    REQUIRE_EQ(DefaultEnvironmentsRoot("/data", "/home/u"),
               std::string("/data/ultrawin/environments"));
    REQUIRE_EQ(DefaultEnvironmentsRoot("", "/home/u"),
               std::string("/home/u/.local/share/ultrawin/environments"));
    // Degenerate: no HOME at all still yields a usable absolute path.
    REQUIRE_EQ(DefaultEnvironmentsRoot("", ""),
               std::string("/tmp/ultrawin/environments"));
}
