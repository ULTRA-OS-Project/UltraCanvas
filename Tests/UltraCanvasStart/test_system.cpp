// Tests/UltraCanvasStart/test_system.cpp
// os-release parsing, the distribution -> package manager mapping, detection.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartSystem.h"

using namespace UltraCanvasStart;

TEST(ParseOsRelease_strips_quotes_and_skips_comments) {
    const auto values = ParseOsRelease(
        "# comment\n"
        "NAME=\"Ubuntu\"\n"
        "VERSION_ID=\"24.04\"\n"
        "ID=ubuntu\n"
        "ID_LIKE=debian\r\n"
        "PRETTY_NAME='Ubuntu 24.04 LTS'\n"
        "BROKEN LINE\n");
    REQUIRE_EQ(values.at("NAME"), std::string("Ubuntu"));
    REQUIRE_EQ(values.at("VERSION_ID"), std::string("24.04"));
    REQUIRE_EQ(values.at("ID_LIKE"), std::string("debian"));
    REQUIRE_EQ(values.at("PRETTY_NAME"), std::string("Ubuntu 24.04 LTS"));
    REQUIRE(values.count("BROKEN LINE") == 0);
}

TEST(PackageManagerForDistribution_knows_the_families) {
    REQUIRE(PackageManagerForDistribution("ubuntu", "debian") == PackageManager::Apt);
    REQUIRE(PackageManagerForDistribution("linuxmint", "ubuntu debian") == PackageManager::Apt);
    REQUIRE(PackageManagerForDistribution("fedora", "") == PackageManager::Dnf);
    REQUIRE(PackageManagerForDistribution("rocky", "rhel centos fedora") == PackageManager::Dnf);
    REQUIRE(PackageManagerForDistribution("manjaro", "arch") == PackageManager::Pacman);
    REQUIRE(PackageManagerForDistribution("opensuse-tumbleweed", "opensuse suse") == PackageManager::Zypper);
    REQUIRE(PackageManagerForDistribution("gentoo", "") == PackageManager::None);
}

TEST(DetectSystem_reports_this_platform) {
    const SystemProfile profile = DetectSystem();
    REQUIRE(profile.platform == CurrentPlatform());
    REQUIRE(!profile.architecture.empty());
#if defined(__linux__)
    REQUIRE(profile.platform == Platform::Linux);
#endif
}

TEST(FindProgram_finds_something_on_path) {
#if defined(_WIN32)
    REQUIRE(!FindProgram("cmd").empty());
#else
    REQUIRE(!FindProgram("sh").empty());
#endif
    REQUIRE(FindProgram("no-such-program-ultracanvasstart").empty());
}
