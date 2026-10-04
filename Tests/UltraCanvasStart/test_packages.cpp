// Tests/UltraCanvasStart/test_packages.cpp
// The dependency table and the install command.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartPackages.h"

#include <set>

using namespace UltraCanvasStart;

TEST(Table_ids_are_unique_and_core_covers_every_manager) {
    std::set<std::string> ids;
    for (const auto& d : AllDependencies()) {
        REQUIRE(ids.insert(d.id).second);
        REQUIRE(!d.title.empty());
        if (d.checkKind != CheckKind::None) REQUIRE(!d.checkName.empty());
    }
    // Cairo is the framework's renderer: every manager must name a package.
    for (const auto& d : AllDependencies()) {
        if (d.id != "cairo") continue;
        for (auto m : { PackageManager::Apt, PackageManager::Dnf, PackageManager::Pacman,
                        PackageManager::Zypper, PackageManager::Homebrew, PackageManager::Msys2Pacman }) {
            REQUIRE(!d.PackageFor(m).empty());
        }
    }
}

TEST(DependenciesFor_follows_the_groups_and_the_manager) {
    Choices choices;    // toolchain + core
    const auto apt = DependenciesFor(choices, PackageManager::Apt);
    REQUIRE(!apt.empty());
    for (const auto* d : apt) {
        REQUIRE(d->group == DependencyGroup::Toolchain || d->group == DependencyGroup::Core);
        REQUIRE(!d->apt.empty());
    }
    // X11 is Linux only: it must not appear for Homebrew.
    for (const auto* d : DependenciesFor(choices, PackageManager::Homebrew)) {
        REQUIRE(d->id != "x11");
    }
    choices.Set(DependencyGroup::Cdr, true);
    bool sawCdr = false;
    for (const auto* d : DependenciesFor(choices, PackageManager::Apt)) sawCdr |= d->id == "libcdr";
    REQUIRE(sawCdr);
}

TEST(PackageNames_match_the_guide) {
    Choices choices;
    const auto names = PackageNames(DependenciesFor(choices, PackageManager::Apt), PackageManager::Apt);
    std::set<std::string> set(names.begin(), names.end());
    for (const char* expected : { "build-essential", "cmake", "pkg-config", "clang",
                                  "libcairo2-dev", "libpango1.0-dev", "libharfbuzz-dev",
                                  "libfreetype6-dev", "libvips-dev", "libglib2.0-dev",
                                  "libtinyxml2-dev", "libfmt-dev", "libx11-dev",
                                  "libxcursor-dev", "libgl1-mesa-dev", "libgtk-3-dev" }) {
        REQUIRE(set.count(expected) == 1);
    }
    const auto brew = PackageNames(DependenciesFor(choices, PackageManager::Homebrew), PackageManager::Homebrew);
    std::set<std::string> brewSet(brew.begin(), brew.end());
    for (const char* expected : { "cmake", "pkg-config", "cairo", "pango", "harfbuzz", "vips",
                                  "glib", "freetype", "tinyxml2" }) {
        REQUIRE(brewSet.count(expected) == 1);
    }
}

TEST(InstallStep_builds_an_argument_list_not_a_command_line) {
    const PlanStep apt = InstallStep(PackageManager::Apt, { "cmake", "libcairo2-dev" });
    REQUIRE(apt.kind == StepKind::Install);
    REQUIRE(apt.needsElevation);
    REQUIRE_EQ(apt.argv.size(), size_t(5));
    REQUIRE_EQ(apt.argv[0], std::string("apt-get"));
    REQUIRE_EQ(apt.argv[1], std::string("install"));
    REQUIRE_EQ(apt.argv[4], std::string("libcairo2-dev"));

    const PlanStep brew = InstallStep(PackageManager::Homebrew, { "cairo" }, "/opt/homebrew/bin/brew");
    REQUIRE(!brew.needsElevation);
    REQUIRE_EQ(brew.argv[0], std::string("/opt/homebrew/bin/brew"));

    const PlanStep msys = InstallStep(PackageManager::Msys2Pacman, { "mingw-w64-clang-x86_64-cairo" });
    REQUIRE(!msys.needsElevation);
    REQUIRE_EQ(msys.argv[1], std::string("-S"));

    const PlanStep none = InstallStep(PackageManager::None, { "x" });
    REQUIRE(none.kind == StepKind::Manual);
}

TEST(Msys2_names_follow_the_architecture) {
    REQUIRE_EQ(Msys2PackageForArchitecture("mingw-w64-clang-x86_64-cairo", "arm64"),
               std::string("mingw-w64-clang-aarch64-cairo"));
    REQUIRE_EQ(Msys2PackageForArchitecture("mingw-w64-clang-x86_64-cairo", "x86_64"),
               std::string("mingw-w64-clang-x86_64-cairo"));
    REQUIRE_EQ(Msys2PackageForArchitecture("git", "arm64"), std::string("git"));
}
