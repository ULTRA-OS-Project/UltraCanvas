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

#include "Purchase.h"
#include "UltraCanvasPathUtf8.h"
#include "StoreEngine.h"
#include "Verifier.h"

#include <filesystem>

using namespace UltraStore;
using namespace ultrastore_test;
namespace fs = std::filesystem;

namespace {

constexpr int kPort = 18433;

// base64url without padding, as the service encodes (test_purchase.cpp has the same).
std::string B64Url(const std::string& bytes) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : bytes) {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6) { bits -= 6; out += alphabet[(buffer >> bits) & 63]; }
    }
    if (bits > 0) out += alphabet[(buffer << (6 - bits)) & 63];
    return out;
}

std::string MakeLicence(const std::vector<uint8_t>& seed, const std::string& app, const std::string& order, const std::string& updates) {
    const std::string payload = R"({"app":")" + app + R"(","issued":"2026-10-27","licence":"lic-1","order":")" + order + R"(","updates":")" + updates + R"(","v":1})";
    const auto sig = SignCatalog(payload, seed);
    return "ULIC1." + B64Url(payload) + "." + B64Url(std::string(sig.Value().begin(), sig.Value().end()));
}

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

TEST(a_restricted_package_needs_the_signer_and_is_fetched_through_it) {
    World w;
    if (!w.ok) SKIP("no local server");
    // The app's package is served from restricted/: the catalog's URL is
    // refused by the web server, the signed one is the real file.
    REQUIRE(MakeZip(w.site + "/paid.zip", {{"bin/paid", "paid 1.0"}}));
    Component paid;
    paid.kind = ComponentKind::App;
    paid.id = "org.x.paid";
    paid.name = "Paid";
    paid.pricing.model = PricingModel::OneTime;
    paid.pricing.price = "9.90";
    paid.pricing.currency = "EUR";
    Release r;
    r.version = "1.0";
    r.channel = "stable";
    Package p = w.Served("paid.zip", "bin/paid");
    p.url = w.server.baseUrl + "/restricted/" + p.sha256 + ".zip";   // nothing is served there
    r.packages = {p};
    paid.releases = {r};
    w.catalog.apps.push_back(paid);
    StoreEngine engine(w.root);
    REQUIRE(engine.Open());
    const auto refused = engine.Install(w.catalog, "org.x.paid", w.platform);
    REQUIRE(!refused);
    REQUIRE(refused.error.code == ErrorCode::NotAvailable);
    REQUIRE(refused.error.message.find("buy it first") != std::string::npos);
    // With a signer that knows the buyer: the real file, under its hash.
    std::string askedFor;
    engine.SetUrlSigner([&](const Package& package, const Component& app) {
        askedFor = app.id + " " + RestrictedFileOf(package.url);
        return w.server.baseUrl + "/paid.zip?md5=signed&expires=1";
    });
    const auto installed = engine.Install(w.catalog, "org.x.paid", w.platform);
    REQUIRE(installed);
    REQUIRE_EQ(askedFor, "org.x.paid restricted/" + p.sha256 + ".zip");
    REQUIRE(engine.Registry().Current("org.x.paid"));
}

TEST(an_update_outside_the_purchase_is_refused_and_the_licence_file_follows_the_app) {
    World w;
    if (!w.ok) SKIP("no local server");
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    const std::string keyHex = ToHex(pk);
    StoreEngine engine(w.root);
    REQUIRE(engine.Open());
    // Bought here: the terms say "the bought version only", issued the day
    // of the 1.0 release; 1.1 comes later.
    w.catalog.apps[0].releases[0].date = "2026-10-27";
    LicenceRecord bought;
    bought.app = "org.x.app";
    bought.licenceId = "lic-1";
    bought.orderId = "o-1";
    bought.orderSecret = "s";
    bought.token = MakeLicence(seed, "org.x.app", "o-1", "never");
    bought.issuedAt = "2026-10-27";
    REQUIRE(engine.Registry().PutLicence(bought));
    REQUIRE(engine.Install(w.catalog, "org.x.app", w.platform));
    const std::string file = LicenceFilePath(w.root, "org.x.app");
    REQUIRE(std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(file)));
    REQUIRE(ReadLicenceFile(file, keyHex));
    w.PublishUpdate();
    w.catalog.apps[0].releases[0].date = "2026-11-15";
    REQUIRE(!engine.PurchaseCovers("org.x.app", w.catalog.apps[0].releases[0]));
    const auto refused = engine.Update(w.catalog, "org.x.app", w.platform);
    REQUIRE(!refused);
    REQUIRE(refused.error.code == ErrorCode::NotAvailable);
    REQUIRE(refused.error.message.find("not in your purchase") != std::string::npos);
    REQUIRE_EQ(refused.error.detail, std::string("the bought version only (2026-10-27)"));
    REQUIRE_EQ(engine.Registry().Current("org.x.app").Value().version, std::string("1.0"));
    // A new purchase with updates included: the update goes through.
    bought.token = MakeLicence(seed, "org.x.app", "o-2", "always");
    REQUIRE(engine.Registry().PutLicence(bought));
    REQUIRE(engine.PurchaseCovers("org.x.app", w.catalog.apps[0].releases[0]));
    REQUIRE(engine.Update(w.catalog, "org.x.app", w.platform));
    REQUIRE_EQ(engine.Registry().Current("org.x.app").Value().version, std::string("1.1"));
    // Removed with the app; the licence stays in the registry.
    REQUIRE(engine.Remove("org.x.app"));
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(file)));
    REQUIRE(engine.Registry().GetLicence("org.x.app"));
    // An app not bought here is never held back.
    REQUIRE(engine.PurchaseCovers("org.x.other", w.catalog.apps[0].releases[0]));
}
