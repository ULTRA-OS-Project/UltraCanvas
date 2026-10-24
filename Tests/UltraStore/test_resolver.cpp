// Tests/UltraStore/test_resolver.cpp
// Every row of the platform model, the Rosetta fallback, "not available",
// and the install plan with its dependencies: order, what is skipped as
// installed, what is missing, what cycles.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"

#include "PackageResolver.h"

using namespace UltraStore;
using namespace ultrastore_test;

TEST(every_platform_row_gets_its_package) {
    const Catalog c = FixtureCatalog();
    const Component* video = c.FindApp("org.ultraos.ultravideo");
    struct Row { PlatformInfo machine; MatchKind kind; Arch arch; };
    const Row rows[] = {
        {Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"}), MatchKind::Exact, Arch::X86_64},
        {Machine(OS::Windows, Arch::X86_64, "10.0.22631"), MatchKind::Exact, Arch::X86_64},
        {Machine(OS::MacOS, Arch::Arm64, "14.5"), MatchKind::Universal, Arch::Universal},
        {Machine(OS::MacOS, Arch::X86_64, "14.5"), MatchKind::Universal, Arch::Universal},
    };
    for (const Row& row : rows) {
        const Resolution r = PackageResolver::Resolve(*video, row.machine);
        REQUIRE(r.Available());
        REQUIRE(r.kind == row.kind);
        REQUIRE(r.package->target.arch == row.arch);
        REQUIRE_EQ(r.release->version, std::string("0.2.1"));
    }
    // Rows the catalog has nothing for yet.
    for (const PlatformInfo& m : {Machine(OS::Linux, Arch::Arm64, "6.8", {"glibc-2.39"}),
                                  Machine(OS::Linux, Arch::Armv7), Machine(OS::Linux, Arch::Riscv64),
                                  Machine(OS::Windows, Arch::Arm64), Machine(OS::UltraOS, Arch::X86_64)}) {
        const Resolution r = PackageResolver::Resolve(*video, m);
        REQUIRE(!r.Available());
        REQUIRE(r.kind == MatchKind::NoMatch);
        REQUIRE(r.release != nullptr);   // the newest release, for the systems it supports
    }
    REQUIRE_EQ(PackageResolver::SupportedSystems(video->releases[1]),
               std::string("linux/x86_64, macos/universal, windows/x86_64"));
}

TEST(an_x86_64_package_runs_under_rosetta_only_with_rosetta) {
    const Catalog c = FixtureCatalog();
    const Component* intel = c.FindApp("org.ultraos.intelonly");
    const Resolution withRosetta = PackageResolver::Resolve(*intel, Machine(OS::MacOS, Arch::Arm64, "14.5", {}, true));
    REQUIRE(withRosetta.Available());
    REQUIRE(withRosetta.kind == MatchKind::Emulated);
    const Resolution without = PackageResolver::Resolve(*intel, Machine(OS::MacOS, Arch::Arm64, "14.5", {}, false));
    REQUIRE(!without.Available());
    const Resolution native = PackageResolver::Resolve(*intel, Machine(OS::MacOS, Arch::X86_64, "14.5"));
    REQUIRE(native.kind == MatchKind::Exact);
    // Linux never emulates.
    const Component* linuxTool = c.FindApp("org.ultraos.linuxtool");
    REQUIRE(!PackageResolver::Resolve(*linuxTool, Machine(OS::Linux, Arch::Arm64, "6.8", {}, true)).Available());
}

TEST(exact_beats_universal_beats_any_beats_emulated) {
    Release r;
    r.version = "1";
    r.channel = "stable";
    r.packages = {
        Pkg(OS::MacOS, Arch::Any, "zip", "any.zip", 1, 'a'),
        Pkg(OS::MacOS, Arch::X86_64, "zip", "intel.zip", 1, 'b'),
        Pkg(OS::MacOS, Arch::Universal, "zip", "universal.zip", 1, 'c'),
        Pkg(OS::MacOS, Arch::Arm64, "zip", "arm.zip", 1, 'd'),
    };
    const PlatformInfo arm = Machine(OS::MacOS, Arch::Arm64, "14.5", {}, true);
    REQUIRE(PackageResolver::ResolveRelease(r, arm).package->target.arch == Arch::Arm64);
    r.packages.erase(r.packages.begin() + 3);
    REQUIRE(PackageResolver::ResolveRelease(r, arm).package->target.arch == Arch::Universal);
    r.packages.erase(r.packages.begin() + 2);
    REQUIRE(PackageResolver::ResolveRelease(r, arm).package->target.arch == Arch::Any);
    r.packages.erase(r.packages.begin());
    const Resolution emulated = PackageResolver::ResolveRelease(r, arm);
    REQUIRE(emulated.kind == MatchKind::Emulated);
}

TEST(variants_narrow_a_package) {
    const Catalog c = FixtureCatalog();
    const Component* runtime = c.FindComponent("org.ultraos.runtime");
    // Linux without glibc 2.31: the runtime's package asks for it.
    REQUIRE(!PackageResolver::ResolveRelease(runtime->releases[0], Machine(OS::Linux, Arch::X86_64, "5.4", {"glibc-2.28"})).Available());
    REQUIRE(PackageResolver::ResolveRelease(runtime->releases[0], Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.35"})).Available());
    // macOS below the minimum.
    REQUIRE(!PackageResolver::ResolveRelease(runtime->releases[0], Machine(OS::MacOS, Arch::Arm64, "13.6")).Available());
    REQUIRE(PackageResolver::ResolveRelease(runtime->releases[0], Machine(OS::MacOS, Arch::Arm64, "14.0")).Available());
}

TEST(an_older_release_serves_a_machine_the_newest_build_misses) {
    Component app;
    app.id = "org.x.a";
    app.name = "A";
    Release older;
    older.version = "1.0";
    older.channel = "stable";
    older.packages = {Pkg(OS::Linux, Arch::X86_64, "7z", "a-1.0.7z", 1, 'a'), Pkg(OS::Windows, Arch::X86_64, "zip", "a-1.0.zip", 1, 'b')};
    Release newer;
    newer.version = "1.1";
    newer.channel = "stable";
    newer.packages = {Pkg(OS::Linux, Arch::X86_64, "7z", "a-1.1.7z", 1, 'c')};
    app.releases = {newer, older};
    REQUIRE_EQ(PackageResolver::Resolve(app, Machine(OS::Windows, Arch::X86_64)).release->version, std::string("1.0"));
    REQUIRE_EQ(PackageResolver::Resolve(app, Machine(OS::Linux, Arch::X86_64)).release->version, std::string("1.1"));
    // The beta channel sees beta releases; stable does not.
    Release beta;
    beta.version = "2.0-beta.1";
    beta.channel = "beta";
    beta.packages = {Pkg(OS::Linux, Arch::X86_64, "7z", "a-2.0b1.7z", 1, 'd')};
    app.releases.insert(app.releases.begin(), beta);
    REQUIRE_EQ(PackageResolver::Resolve(app, Machine(OS::Linux, Arch::X86_64), "stable").release->version, std::string("1.1"));
    REQUIRE_EQ(PackageResolver::Resolve(app, Machine(OS::Linux, Arch::X86_64), "beta").release->version, std::string("2.0-beta.1"));
}

TEST(the_plan_installs_dependencies_first_and_skips_what_is_there) {
    const Catalog c = FixtureCatalog();
    const Component* video = c.FindApp("org.ultraos.ultravideo");
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    const Release* release = video->Find("0.2.1");

    const auto fresh = PackageResolver::Plan(c, *video, *release, linuxPc, {});
    REQUIRE(fresh);
    const auto& steps = fresh.Value().steps;
    REQUIRE_EQ(steps.size(), size_t(3));
    REQUIRE_EQ(steps[0].component->id, std::string("org.ultraos.runtime"));
    REQUIRE_EQ(steps[0].release->version, std::string("0.9.410"));
    REQUIRE_EQ(steps[1].component->id, std::string("org.ultraos.lib.videofx"));
    REQUIRE_EQ(steps[2].component->id, std::string("org.ultraos.ultravideo"));
    REQUIRE(!steps[0].alreadyInstalled && !steps[1].alreadyInstalled && !steps[2].alreadyInstalled);
    REQUIRE_EQ(fresh.Value().bytesToDownload, uint64_t(110000000 + 8000000 + 3000000));

    // The runtime at the right version is installed: only the rest is fetched.
    InstalledVersions installed;
    installed.entries = {{"org.ultraos.runtime", "0.9.410"}, {"org.ultraos.runtime", "0.9.400"}};
    const auto partial = PackageResolver::Plan(c, *video, *release, linuxPc, installed);
    REQUIRE(partial);
    REQUIRE(partial.Value().steps[0].alreadyInstalled);
    REQUIRE(!partial.Value().steps[1].alreadyInstalled);
    REQUIRE_EQ(partial.Value().bytesToDownload, uint64_t(8000000 + 3000000));

    // A runtime at another version does not count: the app was built against 0.9.410.
    InstalledVersions old;
    old.entries = {{"org.ultraos.runtime", "0.9.400"}};
    REQUIRE(!PackageResolver::Plan(c, *video, *release, linuxPc, old).Value().steps[0].alreadyInstalled);

    // Linux Tool needs the older runtime; both runtimes may be installed side by side.
    const Component* tool = c.FindApp("org.ultraos.linuxtool");
    const auto toolPlan = PackageResolver::Plan(c, *tool, tool->releases[0], linuxPc, installed);
    REQUIRE(toolPlan);
    REQUIRE_EQ(toolPlan.Value().steps[0].release->version, std::string("0.9.400"));
    REQUIRE(toolPlan.Value().steps[0].alreadyInstalled);
}

TEST(the_plan_fails_for_what_the_catalog_or_the_machine_lacks) {
    Catalog c = FixtureCatalog();
    Component* video = &c.apps[0];
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});

    // A dependency the catalog does not have.
    Release missingDep = *video->Find("0.2.1");
    missingDep.depends.push_back({"org.ultraos.lib.nothing", "1.0"});
    auto r1 = PackageResolver::Plan(c, *video, missingDep, linuxPc, {});
    REQUIRE(!r1 && r1.error.code == ErrorCode::DependencyError);
    REQUIRE(r1.error.message.find("org.ultraos.lib.nothing") != std::string::npos);

    // A dependency version the catalog does not have.
    Release missingVersion = *video->Find("0.2.1");
    missingVersion.depends[0].version = "0.9.999";
    auto r2 = PackageResolver::Plan(c, *video, missingVersion, linuxPc, {});
    REQUIRE(!r2 && r2.error.code == ErrorCode::DependencyError);

    // A dependency with no package for the machine: VideoFX has none for Linux arm64.
    const PlatformInfo arm = Machine(OS::Linux, Arch::Arm64, "6.8", {"glibc-2.39"});
    Release armApp = *video->Find("0.2.1");
    armApp.packages.push_back(Pkg(OS::Linux, Arch::Arm64, "7z", "ultravideo-arm.7z", 1, 'z', {}, "UltraVideo"));
    auto r3 = PackageResolver::Plan(c, *video, armApp, arm, {});
    REQUIRE(!r3 && r3.error.code == ErrorCode::DependencyError);
    REQUIRE(r3.error.message.find("VideoFX") != std::string::npos);

    // The same dependency, but installed from elsewhere: fine, nothing to fetch for it.
    InstalledVersions installed;
    installed.entries = {{"org.ultraos.lib.videofx", "0.12.0"}};
    auto r3b = PackageResolver::Plan(c, *video, armApp, arm, installed);
    REQUIRE(r3b);

    // The app itself has no package for the machine.
    auto r4 = PackageResolver::Plan(c, *video, *video->Find("0.2.1"), Machine(OS::Windows, Arch::Arm64), {});
    REQUIRE(!r4 && r4.error.code == ErrorCode::NotAvailable);

    // A cycle: the runtime depends on VideoFX which depends on the runtime.
    c.components[0].releases[0].depends.push_back({"org.ultraos.lib.videofx", "0.12.0"});
    auto r5 = PackageResolver::Plan(c, c.apps[0], *c.apps[0].Find("0.2.1"), linuxPc, {});
    REQUIRE(!r5 && r5.error.code == ErrorCode::DependencyError);
    REQUIRE(r5.error.message.find("cycle") != std::string::npos);
}
