// Tests/UltraStore/test_engine.cpp
// The whole path against a local server: a catalog naming two zips (a
// runtime and an app on it), install, the second install finding everything
// there, an update to a newer release that removes the old one, a wrong hash
// refused, removal that collects the runtime.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"
#include "fixtures.h"

#include "StoreEngine.h"
#include "Verifier.h"

#include <filesystem>

using namespace UltraStore;
using namespace ultrastore_test;
namespace fs = std::filesystem;

namespace {

constexpr int kPort = 18433;

struct World {
    std::string site = TempRoot("engine-site");
    std::string root = TempRoot("engine-store");
    StaticServer server;
    Catalog catalog;
    PlatformInfo platform = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    bool ok = false;

    World() {
        if (!MakeZip(site + "/runtime.zip", {{"lib/libultracanvas.so", "runtime 0.9.410"}})) return;
        if (!MakeZip(site + "/app-1.0.zip", {{"bin/app", "app 1.0"}, {"data/a.txt", "a"}})) return;
        if (!MakeZip(site + "/app-1.1.zip", {{"bin/app", "app 1.1"}})) return;
        if (!server.Start(site, kPort)) return;
        catalog.schema = 1;
        catalog.sequence = 1;
        Component runtime;
        runtime.kind = ComponentKind::Runtime;
        runtime.id = "org.x.runtime";
        runtime.name = "Runtime";
        Release rr;
        rr.version = "0.9.410";
        rr.channel = "stable";
        rr.packages = {Served("runtime.zip")};
        runtime.releases = {rr};
        catalog.components = {runtime};
        Component app;
        app.kind = ComponentKind::App;
        app.id = "org.x.app";
        app.name = "App";
        Release a10;
        a10.version = "1.0";
        a10.channel = "stable";
        a10.depends = {{"org.x.runtime", "0.9.410"}};
        a10.packages = {Served("app-1.0.zip", "bin/app")};
        app.releases = {a10};
        catalog.apps = {app};
        ok = true;
    }
    ~World() {
        server.Stop();
        std::error_code ec;
        fs::remove_all(site, ec);
        fs::remove_all(root, ec);
    }
    Package Served(const std::string& name, const std::string& entry = {}) {
        Package p;
        p.target.os = OS::Linux;
        p.target.arch = Arch::X86_64;
        p.format = "zip";
        p.url = server.baseUrl + "/" + name;
        p.size = fs::file_size(fs::path(site) / name);
        p.sha256 = Sha256HexOfFile(site + "/" + name);
        p.entry = entry;
        return p;
    }
    // Publishes app 1.1 (newest first, as after parsing).
    void PublishUpdate() {
        Release a11;
        a11.version = "1.1";
        a11.channel = "stable";
        a11.depends = {{"org.x.runtime", "0.9.410"}};
        a11.packages = {Served("app-1.1.zip", "bin/app")};
        catalog.apps[0].releases.insert(catalog.apps[0].releases.begin(), a11);
    }
};

} // namespace

TEST(the_engine_installs_updates_and_removes_through_a_server) {
    RequireUnpacking();
    World w;
    if (!w.ok) SKIP("the local server could not be started");

    StoreEngine engine(w.root);
    REQUIRE(engine.Open());
    REQUIRE(fs::is_directory(fs::path(w.root) / "downloads"));
    REQUIRE(engine.Installed().Value().empty());

    int steps = 0;
    int64_t lastTotal = 0;
    EngineCallbacks cb;
    cb.onStep = [&](const InstallStep&) { ++steps; };
    cb.onDownload = [&](const InstallStep&, int64_t, int64_t total) { lastTotal = total; return true; };
    const auto first = engine.Install(w.catalog, "org.x.app", w.platform, "stable", cb);
    REQUIRE(first);
    REQUIRE_EQ(first.Value().installed.size(), size_t(2));
    REQUIRE_EQ(first.Value().installed[0], std::string("org.x.runtime 0.9.410"));
    REQUIRE_EQ(first.Value().installed[1], std::string("org.x.app 1.0"));
    REQUIRE_EQ(steps, 2);
    REQUIRE(lastTotal > 0);
    REQUIRE(fs::is_regular_file(fs::path(w.root) / "components" / "org.x.runtime" / "0.9.410" / "lib" / "libultracanvas.so"));
    REQUIRE(fs::is_regular_file(fs::path(w.root) / "apps" / "org.x.app" / "1.0" / "bin" / "app"));
    REQUIRE_EQ(engine.Installer().CurrentVersionOf("org.x.app"), std::string("1.0"));
    REQUIRE(fs::is_empty(fs::path(w.root) / "downloads"));   // unpacked, so gone

    // Everything is there: nothing is fetched or unpacked.
    steps = 0;
    const auto again = engine.Install(w.catalog, "org.x.app", w.platform, "stable", cb);
    REQUIRE(again);
    REQUIRE(again.Value().installed.empty());
    REQUIRE_EQ(again.Value().bytesDownloaded, uint64_t(0));
    REQUIRE_EQ(steps, 0);
    for (const InstallStep& s : again.Value().plan.steps) REQUIRE(s.alreadyInstalled);

    // Nothing newer: update does nothing.
    const auto same = engine.Update(w.catalog, "org.x.app", w.platform);
    REQUIRE(same && same.Value().installed.empty());

    // A newer release: the app moves, the old version goes, the runtime stays.
    w.PublishUpdate();
    const auto updated = engine.Update(w.catalog, "org.x.app", w.platform);
    REQUIRE(updated);
    REQUIRE_EQ(updated.Value().installed.size(), size_t(1));
    REQUIRE_EQ(updated.Value().installed[0], std::string("org.x.app 1.1"));
    REQUIRE_EQ(engine.Installer().CurrentVersionOf("org.x.app"), std::string("1.1"));
    REQUIRE(!fs::exists(fs::path(w.root) / "apps" / "org.x.app" / "1.0"));
    REQUIRE(fs::exists(fs::path(w.root) / "components" / "org.x.runtime" / "0.9.410"));
    REQUIRE_EQ(engine.Installed().Value().size(), size_t(2));

    // UpdateAll runs over the installed apps.
    const auto all = engine.UpdateAll(w.catalog, w.platform);
    REQUIRE(all && all.Value().size() == 1 && all.Value()[0].second);

    // Removing the app takes the runtime nothing else needs.
    const auto removed = engine.Remove("org.x.app");
    REQUIRE(removed);
    REQUIRE_EQ(removed.Value().size(), size_t(2));
    REQUIRE(engine.Installed().Value().empty());
    REQUIRE(!fs::exists(fs::path(w.root) / "apps" / "org.x.app"));
    REQUIRE(!fs::exists(fs::path(w.root) / "components" / "org.x.runtime"));
    const auto gone = engine.Remove("org.x.app");
    REQUIRE(!gone && gone.error.code == ErrorCode::NotFound);
    const auto unknownApp = engine.Update(w.catalog, "org.x.app", w.platform);
    REQUIRE(!unknownApp && unknownApp.error.code == ErrorCode::NotFound);
}

TEST(a_package_with_the_wrong_hash_or_a_missing_file_is_refused) {
    RequireUnpacking();
    World w;
    if (!w.ok) SKIP("the local server could not be started");
    StoreEngine engine(w.root);
    REQUIRE(engine.Open());

    Catalog bad = w.catalog;
    bad.components[0].releases[0].packages[0].sha256 = std::string(64, '0');
    const auto wrongHash = engine.Install(bad, "org.x.app", w.platform);
    REQUIRE(!wrongHash && wrongHash.error.code == ErrorCode::SignatureInvalid);
    REQUIRE(engine.Installed().Value().empty());
    REQUIRE(fs::is_empty(fs::path(w.root) / "downloads"));   // the wrong file is deleted

    Catalog missing = w.catalog;
    missing.components[0].releases[0].packages[0].url = w.server.baseUrl + "/nothing.zip";
    const auto notFound = engine.Install(missing, "org.x.app", w.platform);
    REQUIRE(!notFound && notFound.error.code == ErrorCode::NetworkError);
    REQUIRE(notFound.error.message.find("404") != std::string::npos);

    const auto noApp = engine.Install(w.catalog, "org.x.other", w.platform);
    REQUIRE(!noApp && noApp.error.code == ErrorCode::NotFound);
    const auto wrongMachine = engine.Install(w.catalog, "org.x.app", Machine(OS::Windows, Arch::Arm64));
    REQUIRE(!wrongMachine && wrongMachine.error.code == ErrorCode::NotAvailable);
}

TEST(a_download_resumes_a_partial_file_and_reuses_a_complete_one) {
    RequireUnpacking();
    World w;
    if (!w.ok) SKIP("the local server could not be started");
    Downloader downloader(w.root + "/dl");
    const Package pkg = w.catalog.apps[0].releases[0].packages[0];

    // A complete file from an earlier run is used without a request.
    fs::create_directories(w.root + "/dl");
    fs::copy_file(fs::path(w.site) / "app-1.0.zip", fs::path(w.root) / "dl" / Downloader::FileNameFor(pkg));
    int calls = 0;
    const auto reused = downloader.Fetch(pkg, [&](int64_t, int64_t) { ++calls; return true; });
    REQUIRE(reused);
    REQUIRE_EQ(calls, 1);   // one final progress call, no transfer
    downloader.Clear();
    REQUIRE(fs::is_empty(fs::path(w.root) / "dl"));

    // A partial file: python's http.server ignores Range and sends the whole
    // body with 200, which UltraNet writes from the start - the result is
    // still the right file.
    {
        std::ofstream part(fs::path(w.root) / "dl" / (Downloader::FileNameFor(pkg) + ".part"), std::ios::binary);
        part << "garbage-prefix";
    }
    const auto resumed = downloader.Fetch(pkg);
    REQUIRE(resumed);
    REQUIRE(PackageHashMatches(Sha256HexOfFile(resumed.Value()), pkg.sha256));

    // A cancelled transfer says so.
    downloader.Clear();
    const auto cancelled = downloader.Fetch(pkg, [&](int64_t, int64_t) { return false; });
    REQUIRE(!cancelled && cancelled.error.code == ErrorCode::NetworkError);
}
