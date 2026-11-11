// Tests/UltraStore/test_selfupdate.cpp
// The store as one of its own apps: the store's own catalog cut out of a
// catalog and fetched under its own name beside a catalog too new to read;
// the installed version a program file belongs to; the first install and
// the update of the store through a local server, with the running
// version left in place until the next start; the menu entry.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"
#include "fixtures.h"

#include "Launcher.h"
#include "SelfUpdate.h"
#include "UltraCanvasPathUtf8.h"
#include "Verifier.h"

#include <filesystem>
#include <map>

using namespace UltraStore;
using namespace ultrastore_test;
namespace fs = std::filesystem;

namespace {

constexpr int kPort = 18437;

Component Simple(ComponentKind kind, const std::string& id, const std::string& version,
                 std::vector<Dependency> depends = {}) {
    Component c;
    c.kind = kind;
    c.id = id;
    c.name = id;
    c.publisher = "ultra-os-project";
    Release r;
    r.version = version;
    r.channel = "stable";
    r.depends = std::move(depends);
    Package p;
    p.target.os = OS::Linux;
    p.target.arch = Arch::X86_64;
    p.format = "zip";
    p.url = "https://pkg.example/" + id + "-" + version + ".zip";
    p.size = 10;
    p.sha256 = std::string(64, 'a');
    if (kind == ComponentKind::App) p.entry = "bin/app";
    r.packages = {p};
    c.releases = {r};
    return c;
}

// The store on a runtime that needs a base library; another app on a
// library the store has nothing to do with.
Catalog WholeCatalog() {
    Catalog c;
    c.schema = 2;
    c.sequence = 7;
    c.generated = "2026-11-11T10:00:00Z";
    c.trustedKeys = {std::string(64, '1')};
    c.paymentUrl = "https://pay.example";
    c.publishers = {{"ultra-os-project", "ULTRA OS Project", "https://ultraos.eu"}, {"other", "Other", ""}};
    c.components = {
        Simple(ComponentKind::Library, "org.x.unrelated", "1.0"),
        Simple(ComponentKind::Library, "org.x.base", "2.0"),
        Simple(ComponentKind::Runtime, "org.x.runtime", "0.9.410", {{"org.x.base", "2.0"}}),
    };
    c.apps = {
        Simple(ComponentKind::App, "org.x.other", "3.0", {{"org.x.unrelated", "1.0"}}),
        Simple(ComponentKind::App, kStoreAppId, "1.0", {{"org.x.runtime", "0.9.410"}}),
    };
    PromoCode code;
    code.code = "HELLO";
    code.discountPercent = 10;
    c.codes = {code};
    return c;
}

// url -> body, the way test_client.cpp serves.
struct FakeServer {
    std::map<std::string, std::string> files;
    std::vector<std::string> urls;
    Fetcher AsFetcher() {
        return [this](const std::string& url, const std::string&) {
            urls.push_back(url);
            FetchResponse r;
            const auto it = files.find(url);
            if (it == files.end()) { r.status = 404; return r; }
            r.status = 200;
            r.body = it->second;
            return r;
        };
    }
    void Put(const std::string& url, const std::string& json, const std::vector<uint8_t>& seed) {
        files[url] = json;
        files[url + ".sig"] = ToHex(SignCatalog(json, seed).Value()) + "\n";
    }
};

// A store on a runtime, served as zips; 1.1 published on demand.
struct World {
    std::string site = TempRoot("self-site");
    std::string root = TempRoot("self-store");
    std::string menu = TempRoot("self-menu");
    StaticServer server;
    Catalog catalog;
    PlatformInfo platform = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    bool ok = false;

    World() {
        if (!MakeZip(site + "/runtime.zip", {{"lib/libultracanvas.so", "runtime"}})) return;
        if (!MakeZip(site + "/store-1.0.zip", {{"bin/ultrastore", "store 1.0"}, {"bin/media/appicon/UltraStore.png", "png"}})) return;
        if (!MakeZip(site + "/store-1.1.zip", {{"bin/ultrastore", "store 1.1"}})) return;
        if (!server.Start(site, kPort)) return;
        catalog.schema = kSelfCatalogSchema;
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
        Component store;
        store.kind = ComponentKind::App;
        store.id = kStoreAppId;
        store.name = "UltraStore";
        store.releases = {StoreRelease("1.0")};
        catalog.apps = {store};
        ok = true;
    }
    ~World() {
        server.Stop();
        std::error_code ec;
        for (const std::string& dir : {site, root, menu}) fs::remove_all(dir, ec);
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
    Release StoreRelease(const std::string& version) {
        Release r;
        r.version = version;
        r.channel = "stable";
        r.depends = {{"org.x.runtime", "0.9.410"}};
        r.packages = {Served("store-" + version + ".zip", "bin/ultrastore")};
        return r;
    }
    void PublishUpdate() { catalog.apps[0].releases.insert(catalog.apps[0].releases.begin(), StoreRelease("1.1")); }
    fs::path StoreFolder(const std::string& version) const { return fs::path(root) / "apps" / kStoreAppId / version; }
    std::string Program(const std::string& version) const {
        return UltraCanvas::PathToUtf8(StoreFolder(version) / "bin" / "ultrastore");
    }
};

} // namespace

TEST(the_stores_own_catalog_holds_the_store_and_what_it_runs_on) {
    const Catalog whole = WholeCatalog();
    const Result<Catalog> cut = ExtractSelfCatalog(whole);
    REQUIRE(cut);
    const Catalog& self = cut.Value();
    REQUIRE_EQ(self.schema, kSelfCatalogSchema);
    REQUIRE_EQ(self.sequence, uint64_t(7));
    REQUIRE_EQ(self.apps.size(), size_t(1));
    REQUIRE_EQ(self.apps[0].id, std::string(kStoreAppId));
    // The runtime and, through it, the base library; not the other app's.
    REQUIRE_EQ(self.components.size(), size_t(2));
    REQUIRE(self.FindComponent("org.x.runtime") && self.FindComponent("org.x.base"));
    REQUIRE(!self.FindComponent("org.x.unrelated"));
    REQUIRE_EQ(self.publishers.size(), size_t(1));
    REQUIRE(self.codes.empty() && self.paymentUrl.empty());
    REQUIRE_EQ(self.trustedKeys.size(), size_t(1));

    // What is written reads back as a catalog of that schema.
    const Result<Catalog> back = ParseCatalog(SerializeCatalog(self));
    REQUIRE(back);
    REQUIRE_EQ(back.Value().schema, kSelfCatalogSchema);
    REQUIRE(back.Value().FindApp(kStoreAppId));
    REQUIRE_EQ(back.Value().components.size(), size_t(2));

    Catalog without = whole;
    without.apps.pop_back();
    const Result<Catalog> none = ExtractSelfCatalog(without);
    REQUIRE(!none && none.error.code == ErrorCode::NotFound);
}

TEST(the_stores_own_catalog_is_read_when_the_catalog_is_too_new) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string base = "https://store.example/v1";
    FakeServer server;
    // The catalog of a later store: this one cannot read it.
    server.Put(base + "/catalog.json", R"({"schema": 99, "sequence": 9, "apps": []})", seed);
    Catalog self = ExtractSelfCatalog(WholeCatalog()).Value();
    self.trustedKeys.clear();
    server.Put(base + "/ultrastore.json", SerializeCatalog(self), seed);

    CatalogClientConfig config;
    config.baseUrls = {base};
    config.trustedKeys = {pk};
    config.cacheDir = TempRoot("self-cache");
    CatalogClient main(config, server.AsFetcher());
    const Result<Catalog> tooNew = main.Refresh();
    REQUIRE(!tooNew && tooNew.error.code == ErrorCode::SchemaTooNew);

    const CatalogClientConfig selfConfig = SelfCatalogConfig(main);
    REQUIRE_EQ(selfConfig.fileName, std::string(kSelfCatalogFile));
    REQUIRE(selfConfig.cacheDir != config.cacheDir);
    server.urls.clear();
    CatalogClient own(selfConfig, server.AsFetcher());
    const Result<Catalog> fetched = own.Refresh();
    REQUIRE(fetched);
    REQUIRE(fetched.Value().FindApp(kStoreAppId));
    REQUIRE_EQ(server.urls.size(), size_t(2));
    REQUIRE_EQ(server.urls[0], base + "/ultrastore.json");
    REQUIRE_EQ(server.urls[1], base + "/ultrastore.json.sig");
    // Kept apart from the catalog's cache, and read back offline.
    REQUIRE(fs::is_regular_file(fs::path(config.cacheDir) / "self" / "state.json"));
    REQUIRE(own.LoadCached());
    REQUIRE_EQ(own.State().sequence, uint64_t(7));
    REQUIRE_EQ(main.State().sequence, uint64_t(0));

    // A tampered one is refused like any catalog.
    server.files[base + "/ultrastore.json"] += " ";
    CatalogClient again(selfConfig, server.AsFetcher());
    const Result<Catalog> tampered = again.Refresh();
    REQUIRE(!tampered && tampered.error.code == ErrorCode::SignatureInvalid);
    std::error_code ec;
    fs::remove_all(config.cacheDir, ec);
}

TEST(a_program_file_belongs_to_the_version_whose_folder_holds_it) {
    const std::string root = TempRoot("self-running");
    const fs::path folder = fs::path(root) / "apps" / kStoreAppId / "1.2.3";
    fs::create_directories(folder / "bin");
    const RunningInstall inside = FindRunningInstall(root, UltraCanvas::PathToUtf8(folder / "bin" / "ultrastore"));
    REQUIRE(inside.Managed());
    REQUIRE_EQ(inside.id, std::string(kStoreAppId));
    REQUIRE_EQ(inside.version, std::string("1.2.3"));
    REQUIRE(fs::equivalent(UltraCanvas::PathFromUtf8(inside.folder), folder));

    // Through `current`, as the menu entry of a symlinking system might name it.
    std::error_code ec;
    fs::create_directory_symlink("1.2.3", fs::path(root) / "apps" / kStoreAppId / "current", ec);
    if (!ec) {
        const RunningInstall linked = FindRunningInstall(root, UltraCanvas::PathToUtf8(fs::path(root) / "apps" / kStoreAppId / "current" / "bin" / "ultrastore"));
        REQUIRE_EQ(linked.version, std::string("1.2.3"));
    }

    REQUIRE(!FindRunningInstall(root, "/usr/bin/ultrastore").Managed());
    REQUIRE(!FindRunningInstall(root, UltraCanvas::PathToUtf8(fs::path(root) / "apps" / "stray")).Managed());
    REQUIRE(!FindRunningInstall(root, UltraCanvas::PathToUtf8(fs::path(root) / "components" / "x" / "1" / "bin")).Managed());
    REQUIRE(!FindRunningInstall("", "/usr/bin/ultrastore").Managed());
    REQUIRE(!RunningExecutable().empty());   // this test program
    fs::remove_all(root, ec);
}

TEST(a_desktop_entry_quotes_the_program_and_keeps_one_line_per_key) {
    LauncherEntry entry;
    entry.id = "org.x.app";
    entry.name = "An App\nInjected=1";
    entry.comment = "Does things";
    entry.executable = "/home/a b/apps/x/1.0/bin/my \"app\" $HOME 100%";
    entry.icon = "/home/a b/icon.png";
    entry.categories = "Utility;";
    const std::string text = DesktopEntryText(entry);
    REQUIRE(text.find("[Desktop Entry]\n") == 0);
    REQUIRE(text.find("Name=An App Injected=1\n") != std::string::npos);
    REQUIRE(text.find("\nInjected=1") == std::string::npos);
    REQUIRE(text.find("Exec=\"/home/a b/apps/x/1.0/bin/my \\\"app\\\" \\$HOME 100%%\" %u\n") != std::string::npos);
    REQUIRE(text.find("Icon=/home/a b/icon.png\n") != std::string::npos);
    REQUIRE(text.find("Categories=Utility;\n") != std::string::npos);
    REQUIRE(text.find("MimeType=") == std::string::npos);

    LauncherEntry none;
    const Result<std::string> refused = WriteLauncher(none);
    REQUIRE(!refused && refused.error.code == ErrorCode::InvalidArgument);
}

TEST(the_store_is_installed_updated_and_left_running_through_a_server) {
    RequireUnpacking();
    World w;
    if (!w.ok) SKIP("the local server could not be started");

    // ----- the first install: what ultrastore-setup does -----
    {
        StoreEngine engine(w.root);
        REQUIRE(engine.Open());
        const Result<SelfUpdateCheck> before = CheckSelfUpdate(w.catalog, engine, w.platform);
        REQUIRE(before);
        REQUIRE(before.Value().installedVersion.empty());
        REQUIRE(before.Value().available);
        REQUIRE_EQ(before.Value().newestVersion, std::string("1.0"));

        const Result<SelfUpdateOutcome> first = InstallOrUpdateStore(engine, w.catalog, w.platform);
        REQUIRE(first);
        REQUIRE(first.Value().changed);
        REQUIRE_EQ(first.Value().version, std::string("1.0"));
        REQUIRE_EQ(first.Value().install.installed.size(), size_t(2));   // the runtime, then the store
        REQUIRE(fs::equivalent(UltraCanvas::PathFromUtf8(first.Value().launchPath), UltraCanvas::PathFromUtf8(w.Program("1.0"))));
        REQUIRE(fs::is_regular_file(fs::path(w.root) / "components" / "org.x.runtime" / "0.9.410" / "lib" / "libultracanvas.so"));

        // Nothing newer: nothing happens, and nothing is said to be new.
        const Result<SelfUpdateOutcome> again = InstallOrUpdateStore(engine, w.catalog, w.platform);
        REQUIRE(again && !again.Value().changed);
        REQUIRE(!CheckSelfUpdate(w.catalog, engine, w.platform).Value().available);

#if defined(__linux__)
        // No entry yet: a refresh writes none; the setup's write does.
        REQUIRE(RefreshStoreLauncher(engine, w.menu).empty());
        const Result<std::string> entry = WriteStoreLauncher(engine, w.menu);
        REQUIRE(entry);
        REQUIRE_EQ(fs::path(entry.Value()).filename().string(), std::string(kStoreAppId) + ".desktop");
        const std::string text = ReadAll(entry.Value());
        REQUIRE(text.find("Name=UltraStore\n") != std::string::npos);
        REQUIRE(text.find("apps/" + std::string(kStoreAppId) + "/1.0/bin/ultrastore\" %u\n") != std::string::npos);
        REQUIRE(text.find("/1.0/bin/media/appicon/UltraStore.png\n") != std::string::npos);
        REQUIRE(text.find("MimeType=x-scheme-handler/ultrastore;\n") != std::string::npos);
#endif
    }

    // ----- the store runs from 1.0 and updates itself -----
    w.PublishUpdate();
    {
        StoreEngine engine(w.root);
        REQUIRE(engine.Open());
        const RunningInstall running = ProtectRunningInstall(engine, w.Program("1.0"));
        REQUIRE(running.Managed());
        REQUIRE_EQ(running.version, std::string("1.0"));
        REQUIRE(RemoveOldStoreVersions(engine).Value().empty());

        const Result<SelfUpdateCheck> check = CheckSelfUpdate(w.catalog, engine, w.platform);
        REQUIRE(check && check.Value().available);
        REQUIRE_EQ(check.Value().installedVersion, std::string("1.0"));
        REQUIRE_EQ(check.Value().newestVersion, std::string("1.1"));

        const Result<SelfUpdateOutcome> updated = InstallOrUpdateStore(engine, w.catalog, w.platform);
        REQUIRE(updated);
        REQUIRE(updated.Value().changed);
        REQUIRE_EQ(updated.Value().version, std::string("1.1"));
        REQUIRE_EQ(engine.Installer().CurrentVersionOf(kStoreAppId), std::string("1.1"));
        // The copy that is running keeps its files, whatever collects.
        REQUIRE(fs::is_regular_file(w.Program("1.0")));
        REQUIRE(fs::is_regular_file(w.Program("1.1")));
        REQUIRE(engine.CollectGarbage().Value().empty());
        REQUIRE(RemoveOldStoreVersions(engine).Value().empty());
        REQUIRE(fs::is_regular_file(w.Program("1.0")));
        const auto all = engine.UpdateAll(w.catalog, w.platform);
        REQUIRE(all && all.Value().size() == 1 && all.Value()[0].second);
        REQUIRE(fs::is_regular_file(w.Program("1.0")));
        // Nor can it be removed from under itself.
        const auto removed = engine.Remove(kStoreAppId);
        REQUIRE(!removed && removed.error.code == ErrorCode::InUse);
        REQUIRE(engine.Installer().IsInUse(kStoreAppId, "1.0"));

#if defined(__linux__)
        // The entry is there, so it follows the store to the new version.
        const std::string entry = RefreshStoreLauncher(engine, w.menu);
        REQUIRE(!entry.empty());
        REQUIRE(ReadAll(entry).find("/1.1/bin/ultrastore\" %u\n") != std::string::npos);
#endif
    }

    // ----- the next start, from 1.1: the old version goes -----
    {
        StoreEngine engine(w.root);
        REQUIRE(engine.Open());
        REQUIRE_EQ(ProtectRunningInstall(engine, w.Program("1.1")).version, std::string("1.1"));
        const auto gone = RemoveOldStoreVersions(engine);
        REQUIRE(gone);
        REQUIRE_EQ(gone.Value().size(), size_t(1));
        REQUIRE_EQ(gone.Value()[0].version, std::string("1.0"));
        REQUIRE(!fs::exists(w.StoreFolder("1.0")));
        REQUIRE(fs::is_regular_file(w.Program("1.1")));
        REQUIRE(fs::exists(fs::path(w.root) / "components" / "org.x.runtime" / "0.9.410"));   // 1.1 runs on it
        REQUIRE_EQ(engine.Installed().Value().size(), size_t(2));
    }
}

TEST(an_update_by_another_program_leaves_the_version_that_was_current) {
    RequireUnpacking();
    World w;
    if (!w.ok) SKIP("the local server could not be started");
    // The setup program run again, or `ultrastore self-update` from a build
    // folder, while a window runs from 1.0: neither names a running version.
    StoreEngine engine(w.root);
    REQUIRE(engine.Open());
    REQUIRE(InstallOrUpdateStore(engine, w.catalog, w.platform));
    w.PublishUpdate();
    const Result<SelfUpdateOutcome> updated = InstallOrUpdateStore(engine, w.catalog, w.platform);
    REQUIRE(updated && updated.Value().changed);
    REQUIRE_EQ(updated.Value().version, std::string("1.1"));
    REQUIRE(fs::is_regular_file(w.Program("1.0")));
    REQUIRE(engine.Installer().InUseId().empty());   // the guard was for the call only
    // The store's next start takes it.
    REQUIRE_EQ(RemoveOldStoreVersions(engine).Value().size(), size_t(1));
    REQUIRE(!fs::exists(w.StoreFolder("1.0")));
}
