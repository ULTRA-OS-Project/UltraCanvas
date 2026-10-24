// Tests/UltraStore/test_platform.cpp
// os-release parsing, architecture names, variants, and that Detect() says
// something sensible about the machine the tests run on.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "PlatformInfo.h"

using namespace UltraStore;

TEST(os_release_is_parsed_without_quotes) {
    const auto values = ParseOsRelease(
        "# comment\nNAME=\"Ubuntu\"\nID=ubuntu\nID_LIKE=debian\r\nPRETTY_NAME='Ubuntu 24.04 LTS'\nBROKEN LINE\n");
    REQUIRE_EQ(values.at("NAME"), std::string("Ubuntu"));
    REQUIRE_EQ(values.at("ID"), std::string("ubuntu"));
    REQUIRE_EQ(values.at("PRETTY_NAME"), std::string("Ubuntu 24.04 LTS"));
    REQUIRE(values.count("BROKEN LINE") == 0);
}

TEST(architectures_are_normalised) {
    REQUIRE(NormalizeArch("x86_64") == Arch::X86_64);
    REQUIRE(NormalizeArch("AMD64") == Arch::X86_64);
    REQUIRE(NormalizeArch("aarch64") == Arch::Arm64);
    REQUIRE(NormalizeArch("arm64") == Arch::Arm64);
    REQUIRE(NormalizeArch("armv7l") == Arch::Armv7);
    REQUIRE(NormalizeArch("riscv64") == Arch::Riscv64);
    REQUIRE(NormalizeArch("mips") == Arch::Unknown);
}

TEST(variants_are_satisfied_by_what_the_machine_has) {
    PlatformInfo linuxPc;
    linuxPc.target.os = OS::Linux;
    linuxPc.target.arch = Arch::X86_64;
    linuxPc.target.variants = {"glibc-2.39"};
    linuxPc.distroId = "ubuntu";
    linuxPc.osVersion = "6.8.0";
    REQUIRE(linuxPc.Satisfies("glibc-2.31"));
    REQUIRE(linuxPc.Satisfies("glibc-2.39"));
    REQUIRE(!linuxPc.Satisfies("glibc-2.40"));
    REQUIRE(linuxPc.Satisfies("ubuntu"));
    REQUIRE(!linuxPc.Satisfies("fedora"));
    REQUIRE(!linuxPc.Satisfies("rpi"));
    REQUIRE(!linuxPc.Satisfies("musl"));

    PlatformInfo pi = linuxPc;
    pi.target.variants.push_back("rpi");
    REQUIRE(pi.Satisfies("rpi"));

    PlatformInfo mac;
    mac.target.os = OS::MacOS;
    mac.target.arch = Arch::Arm64;
    mac.osVersion = "14.5";
    REQUIRE(mac.Satisfies("min-os=11.0"));
    REQUIRE(mac.Satisfies("min-os=14.5"));
    REQUIRE(!mac.Satisfies("min-os=15.0"));
    REQUIRE(!PlatformInfo().Satisfies("min-os=11.0"));

    // min_os on the package.
    REQUIRE(mac.MeetsMinOs(""));
    REQUIRE(mac.MeetsMinOs("14.0"));
    REQUIRE(mac.MeetsMinOs("13"));
    REQUIRE(!mac.MeetsMinOs("14.6"));
    REQUIRE(!PlatformInfo().MeetsMinOs("1.0"));
    PlatformInfo win;
    win.target.os = OS::Windows;
    win.osVersion = "10.0.22631";
    REQUIRE(win.MeetsMinOs("10.0.17763"));
    REQUIRE(!win.MeetsMinOs("10.0.26100"));
}

TEST(detect_knows_this_machine) {
    const PlatformInfo info = PlatformInfo::Detect();
    REQUIRE(info.target.os != OS::Unknown);
    REQUIRE(info.target.arch != Arch::Unknown);
    REQUIRE(!info.Describe().empty());
#if defined(__linux__) && defined(__GLIBC__)
    bool glibc = false;
    for (const std::string& v : info.target.variants) if (v.rfind("glibc-", 0) == 0) glibc = true;
    REQUIRE(glibc);
    REQUIRE(info.Satisfies("glibc-2.17"));
#endif
    if (info.target.arch == Arch::X86_64) REQUIRE(info.canRunX86_64);
}
