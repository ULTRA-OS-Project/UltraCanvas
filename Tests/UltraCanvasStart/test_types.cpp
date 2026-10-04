// Tests/UltraCanvasStart/test_types.cpp
// Version comparison, version extraction, architecture names, the choices set.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartTypes.h"

using namespace UltraCanvasStart;

TEST(CompareVersions_orders_dotted_numbers) {
    REQUIRE(CompareVersions("3.16", "3.22.1") < 0);
    REQUIRE(CompareVersions("3.22.1", "3.16") > 0);
    REQUIRE_EQ(CompareVersions("3.16", "3.16.0"), 0);
    REQUIRE(CompareVersions("10.0", "9.9") > 0);
    REQUIRE(CompareVersions("3.28.3-dirty", "3.28.3") == 0);
}

TEST(ExtractVersion_finds_the_number_in_tool_output) {
    REQUIRE_EQ(ExtractVersion("cmake version 3.28.3"), std::string("3.28.3"));
    REQUIRE_EQ(ExtractVersion("Ubuntu clang version 14.0.0-1ubuntu1.1"), std::string("14.0.0"));
    REQUIRE_EQ(ExtractVersion("git version 2.43.0"), std::string("2.43.0"));
    REQUIRE_EQ(ExtractVersion("x86_64 something 1.2"), std::string("1.2"));
    REQUIRE_EQ(ExtractVersion("no number here"), std::string());
    REQUIRE_EQ(ExtractVersion("GNU Make 4.3"), std::string("4.3"));
}

TEST(NormalizeArchitecture_maps_the_spellings) {
    REQUIRE_EQ(NormalizeArchitecture("x86_64"), std::string("x86_64"));
    REQUIRE_EQ(NormalizeArchitecture("AMD64"), std::string("x86_64"));
    REQUIRE_EQ(NormalizeArchitecture("aarch64"), std::string("arm64"));
    REQUIRE_EQ(NormalizeArchitecture("arm64"), std::string("arm64"));
    REQUIRE_EQ(NormalizeArchitecture(""), std::string("unknown"));
}

TEST(Choices_set_and_has) {
    Choices choices;
    REQUIRE(choices.Has(DependencyGroup::Toolchain));
    REQUIRE(choices.Has(DependencyGroup::Core));
    REQUIRE(!choices.Has(DependencyGroup::Cdr));
    choices.Set(DependencyGroup::Cdr, true);
    REQUIRE(choices.Has(DependencyGroup::Cdr));
    choices.Set(DependencyGroup::Cdr, true);
    REQUIRE_EQ(choices.groups.size(), size_t(3));
    choices.Set(DependencyGroup::Cdr, false);
    REQUIRE(!choices.Has(DependencyGroup::Cdr));
}

TEST(Names_are_spelled) {
    REQUIRE_EQ(PlatformName(Platform::MacOS), std::string("macOS"));
    REQUIRE_EQ(PackageManagerName(PackageManager::Msys2Pacman), std::string("MSYS2 pacman"));
    REQUIRE(PlatformOf(PackageManager::Homebrew) == Platform::MacOS);
    REQUIRE(PlatformOf(PackageManager::Zypper) == Platform::Linux);
}
