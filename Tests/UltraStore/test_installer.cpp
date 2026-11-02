// Tests/UltraStore/test_installer.cpp
// The archive installer: a zip unpacked into <root>/apps/<id>/<version>,
// the entry made executable, `current` pointing at it, the registry row;
// a second version beside it; removal moving `current`; components side by
// side; the garbage collector following the dependencies; an unsupported
// format and a missing file refused.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"
#include "fixtures.h"

#include "Installer.h"
#include "Registry.h"

#include <filesystem>

using namespace UltraStore;
using namespace ultrastore_test;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    std::string root = TempRoot("installer");
    SqliteRegistry registry;
    Component app, runtime;
    Release appRelease, appRelease2, runtimeRelease;
    Package appPackage, appPackage2, runtimePackage;

    Fixture() {
        registry.Open(root + "/registry.sqlite");
        app.kind = ComponentKind::App;
        app.id = "org.x.app";
        app.name = "App";
        appRelease.version = "1.0";
        appRelease.channel = "stable";
        appRelease.depends = {{"org.x.runtime", "0.9.410"}};
        appPackage = Pkg(OS::Linux, Arch::X86_64, "zip", "app-1.0.zip", 100, 'a', {}, "bin/app");
        appRelease2 = appRelease;
        appRelease2.version = "1.1";
        appPackage2 = Pkg(OS::Linux, Arch::X86_64, "zip", "app-1.1.zip", 100, 'b', {}, "bin/app");
        runtime.kind = ComponentKind::Runtime;
        runtime.id = "org.x.runtime";
        runtime.name = "Runtime";
        runtimeRelease.version = "0.9.410";
        runtimeRelease.channel = "stable";
        runtimePackage = Pkg(OS::Linux, Arch::X86_64, "zip", "runtime.zip", 100, 'c');
    }
    ~Fixture() {
        registry.Close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    InstallStep Step(const Component& c, const Release& r, const Package& p) {
        InstallStep s;
        s.component = &c;
        s.release = &r;
        s.package = &p;
        s.match = MatchKind::Exact;
        return s;
    }
};

bool IsExecutable(const fs::path& p) {
    std::error_code ec;
    const fs::perms perms = fs::status(p, ec).permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none;
}

} // namespace

TEST(installs_a_zip_into_the_tree_and_records_it) {
    RequireUnpacking();
    Fixture f;
    const std::string zip = f.root + "/app-1.0.zip";
    REQUIRE(MakeZip(zip, {{"bin/app", "#!/bin/sh\necho app 1.0\n"}, {"share/readme.txt", "hello"}, {"share/empty/", ""}}));
    ArchiveInstaller installer(f.root, f.registry);

    std::vector<std::string> files;
    const auto r = installer.Install(f.Step(f.app, f.appRelease, f.appPackage), zip,
                                     [&](uint64_t, uint64_t, const std::string& file) { files.push_back(file); return true; });
    REQUIRE(r);
    const fs::path dest = fs::path(f.root) / "apps" / "org.x.app" / "1.0";
    REQUIRE(fs::is_regular_file(dest / "bin" / "app"));
    REQUIRE(fs::is_regular_file(dest / "share" / "readme.txt"));
    REQUIRE_EQ(ReadAll(dest / "share" / "readme.txt"), std::string("hello"));
    REQUIRE(IsExecutable(dest / "bin" / "app"));
    REQUIRE(!fs::exists(fs::path(f.root) / "apps" / "org.x.app" / "1.0.installing"));
    REQUIRE_EQ(installer.CurrentVersionOf("org.x.app"), std::string("1.0"));
    REQUIRE_EQ(installer.FolderFor(ComponentKind::App, "org.x.app", "1.0"), dest.string());

    const auto row = f.registry.Current("org.x.app");
    REQUIRE(row);
    REQUIRE_EQ(row.Value().folder, dest.string());
    REQUIRE_EQ(row.Value().entry, std::string("bin/app"));
    REQUIRE_EQ(row.Value().sha256, f.appPackage.sha256);
    REQUIRE_EQ(row.Value().depends.size(), size_t(1));
    REQUIRE(!row.Value().installedAt.empty());

    // The same version again replaces the folder and keeps one row.
    REQUIRE(installer.Install(f.Step(f.app, f.appRelease, f.appPackage), zip, {}));
    REQUIRE_EQ(f.registry.Of("org.x.app").Value().size(), size_t(1));
}

TEST(a_second_version_becomes_current_and_removal_moves_it_back) {
    RequireUnpacking();
    Fixture f;
    const std::string zip1 = f.root + "/app-1.0.zip", zip2 = f.root + "/app-1.1.zip";
    REQUIRE(MakeZip(zip1, {{"bin/app", "1.0"}}));
    REQUIRE(MakeZip(zip2, {{"bin/app", "1.1"}}));
    ArchiveInstaller installer(f.root, f.registry);
    REQUIRE(installer.Install(f.Step(f.app, f.appRelease, f.appPackage), zip1, {}));
    REQUIRE(installer.Install(f.Step(f.app, f.appRelease2, f.appPackage2), zip2, {}));
    REQUIRE_EQ(f.registry.Of("org.x.app").Value().size(), size_t(2));
    REQUIRE_EQ(f.registry.Current("org.x.app").Value().version, std::string("1.1"));
    REQUIRE_EQ(installer.CurrentVersionOf("org.x.app"), std::string("1.1"));
    REQUIRE(fs::exists(fs::path(f.root) / "apps" / "org.x.app" / "1.0"));

    REQUIRE(installer.Remove("org.x.app", "1.1"));
    REQUIRE(!fs::exists(fs::path(f.root) / "apps" / "org.x.app" / "1.1"));
    REQUIRE_EQ(f.registry.Current("org.x.app").Value().version, std::string("1.0"));
    REQUIRE_EQ(installer.CurrentVersionOf("org.x.app"), std::string("1.0"));

    REQUIRE(installer.Remove("org.x.app", "1.0"));
    REQUIRE(!fs::exists(fs::path(f.root) / "apps" / "org.x.app"));
    REQUIRE(f.registry.Of("org.x.app").Value().empty());
    REQUIRE(installer.CurrentVersionOf("org.x.app").empty());
    const auto gone = installer.Remove("org.x.app", "1.0");
    REQUIRE(!gone && gone.error.code == ErrorCode::NotFound);
}

TEST(the_collector_keeps_what_current_apps_need) {
    RequireUnpacking();
    Fixture f;
    const std::string zip = f.root + "/x.zip";
    REQUIRE(MakeZip(zip, {{"lib/libx.so", "x"}}));
    ArchiveInstaller installer(f.root, f.registry);

    // Two runtimes; the app needs 0.9.410. An older app version needed 0.9.400.
    Release oldRuntime = f.runtimeRelease;
    oldRuntime.version = "0.9.400";
    Package oldRuntimePackage = Pkg(OS::Linux, Arch::X86_64, "zip", "runtime-old.zip", 100, 'd');
    REQUIRE(installer.Install(f.Step(f.runtime, oldRuntime, oldRuntimePackage), zip, {}));
    REQUIRE(installer.Install(f.Step(f.runtime, f.runtimeRelease, f.runtimePackage), zip, {}));
    Release oldApp = f.appRelease;
    oldApp.version = "0.9";
    oldApp.depends = {{"org.x.runtime", "0.9.400"}};
    Package oldAppPackage = Pkg(OS::Linux, Arch::X86_64, "zip", "app-0.9.zip", 100, 'e', {}, "lib/libx.so");
    REQUIRE(installer.Install(f.Step(f.app, oldApp, oldAppPackage), zip, {}));
    REQUIRE(installer.Install(f.Step(f.app, f.appRelease, f.appPackage), zip, {}));   // current: 1.0 -> runtime 0.9.410
    REQUIRE(fs::is_directory(fs::path(f.root) / "components" / "org.x.runtime" / "0.9.400"));
    REQUIRE(fs::is_directory(fs::path(f.root) / "components" / "org.x.runtime" / "0.9.410"));
    REQUIRE(!f.registry.Current("org.x.runtime"));   // components have no current

    const auto removed = installer.CollectGarbage();
    REQUIRE(removed);
    // The old app version and the runtime only it needed go; the current app and its runtime stay.
    REQUIRE_EQ(removed.Value().size(), size_t(2));
    REQUIRE(!fs::exists(fs::path(f.root) / "components" / "org.x.runtime" / "0.9.400"));
    REQUIRE(fs::exists(fs::path(f.root) / "components" / "org.x.runtime" / "0.9.410"));
    REQUIRE(!fs::exists(fs::path(f.root) / "apps" / "org.x.app" / "0.9"));
    REQUIRE(fs::exists(fs::path(f.root) / "apps" / "org.x.app" / "1.0"));
    REQUIRE_EQ(f.registry.All().Value().size(), size_t(2));
    REQUIRE(installer.CollectGarbage().Value().empty());

    // Without any app, every component goes, and the component folder with it.
    REQUIRE(installer.Remove("org.x.app", "1.0"));
    REQUIRE_EQ(installer.CollectGarbage().Value().size(), size_t(1));
    REQUIRE(!fs::exists(fs::path(f.root) / "components" / "org.x.runtime"));
    REQUIRE(f.registry.All().Value().empty());
}

TEST(an_appimage_is_copied_and_made_executable) {
    if (!HavePython()) SKIP("fork not available");
    Fixture f;
    const std::string image = f.root + "/tool.AppImage";
    { std::ofstream out(image, std::ios::binary); out << "ELF-ish"; }
    Package pkg = Pkg(OS::Linux, Arch::X86_64, "appimage", "tool.AppImage", 7, 'f', {}, "Tool.AppImage");
    ArchiveInstaller installer(f.root, f.registry);
    REQUIRE(installer.Install(f.Step(f.app, f.appRelease, pkg), image, {}));
    const fs::path dest = fs::path(f.root) / "apps" / "org.x.app" / "1.0" / "Tool.AppImage";
    REQUIRE(fs::is_regular_file(dest));
    REQUIRE(IsExecutable(dest));
    REQUIRE_EQ(ReadAll(dest), std::string("ELF-ish"));
}

TEST(what_cannot_be_installed_is_refused_and_leaves_nothing) {
    Fixture f;
    ArchiveInstaller installer(f.root, f.registry);
    const auto missing = installer.Install(f.Step(f.app, f.appRelease, f.appPackage), f.root + "/no-such.zip", {});
    REQUIRE(!missing && missing.error.code == ErrorCode::IoError);
    const std::string file = f.root + "/x.bin";
    { std::ofstream out(file, std::ios::binary); out << "x"; }
    Package odd = Pkg(OS::Linux, Arch::X86_64, "rar", "x.rar", 1, 'a');
    const auto unsupported = installer.Install(f.Step(f.app, f.appRelease, odd), file, {});
    REQUIRE(!unsupported && unsupported.error.code == ErrorCode::NotAvailable);
    REQUIRE(!ArchiveInstaller::SupportsFormat("rar"));
    REQUIRE(ArchiveInstaller::SupportsFormat("appimage"));
    REQUIRE(!fs::exists(fs::path(f.root) / "apps" / "org.x.app"));
    REQUIRE(f.registry.All().Value().empty());
    InstallStep empty;
    REQUIRE(!installer.Install(empty, file, {}));
}
