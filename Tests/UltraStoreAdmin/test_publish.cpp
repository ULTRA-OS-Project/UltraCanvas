// Tests/UltraStoreAdmin/test_publish.cpp
// The whole way: the database, the package store, the key, the publish
// folder, and the store's own reader and verifier over the result.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "CatalogPublisher.h"
#include "KeyVault.h"
#include "PackageStore.h"
#include "Verifier.h"
#include "WebSite.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <filesystem>

using namespace UltraStoreAdmin;
using namespace admin_test;

TEST(the_catalog_is_built_with_the_published_urls) {
    const std::string dir = TempDir("build");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.SetSetting("catalog.base_url", "https://store.example/v1/").Ok());
    REQUIRE(db.PutPublisher({"ultra-os-project", "ULTRA OS Project", ""}).Ok());
    UltraStore::Component hello = HelloApp();
    UltraStore::Component runtime = Runtime();
    UltraStore::Component empty = HelloApp();
    empty.id = "org.ultraos.empty";
    empty.releases.clear();
    REQUIRE(db.PutComponent(hello).Ok());
    REQUIRE(db.PutComponent(runtime).Ok());
    REQUIRE(db.PutComponent(empty).Ok());
    PackageFile f;
    f.sha256 = std::string(64, 'a');
    f.file = Join(dir, "hello.zip");
    f.size = 999;
    f.format = "zip";
    REQUIRE(db.PutPackageFile(f).Ok());
    PublishOptions options;
    options.generatedAt = "2026-10-27T10:00:00Z";
    std::vector<std::string> warnings;
    const auto built = BuildCatalog(db, options, &warnings);
    REQUIRE(built.Ok());
    const UltraStore::Catalog& c = built.Value();
    REQUIRE_EQ(c.sequence, uint64_t(1));
    REQUIRE_EQ(c.generated, std::string("2026-10-27T10:00:00Z"));
    REQUIRE_EQ(c.apps.size(), size_t(1));          // the one without a release is left out
    REQUIRE_EQ(c.components.size(), size_t(0));    // the runtime has no file behind its package: left out, warned
    REQUIRE_EQ(c.apps[0].releases[0].packages[0].url, std::string("https://store.example/v1/packages/") + std::string(64, 'a') + ".zip");
    REQUIRE_EQ(c.apps[0].releases[0].packages[0].size, uint64_t(999));   // measured, not typed
    bool warnedEmpty = false, warnedRuntimeFile = false;
    for (const std::string& w : warnings) {
        if (w.find("org.ultraos.empty") != std::string::npos) warnedEmpty = true;
        if (w.find("org.ultraos.runtime") != std::string::npos && w.find("no file") != std::string::npos) warnedRuntimeFile = true;
    }
    REQUIRE(warnedEmpty);
    REQUIRE(warnedRuntimeFile);
    // A restricted package goes to restricted/.
    f.access = "restricted";
    REQUIRE(db.PutPackageFile(f).Ok());
    REQUIRE(BuildCatalog(db, options).Value().apps[0].releases[0].packages[0].url.find("/restricted/") != std::string::npos);
    REQUIRE_EQ(PublishedPackagePath(f), "restricted/" + f.sha256 + ".zip");
    // A restricted package on an app with a trial: Try would fail, so publish warns.
    hello.pricing.model = UltraStore::PricingModel::OneTime;
    hello.pricing.price = "9.90";
    hello.pricing.currency = "EUR";
    hello.pricing.trialDays = 14;
    REQUIRE(db.PutComponent(hello).Ok());
    warnings.clear();
    REQUIRE(BuildCatalog(db, options, &warnings).Ok());
    bool warnedTrial = false;
    for (const std::string& w : warnings) if (w.find("14-day trial") != std::string::npos) warnedTrial = true;
    REQUIRE(warnedTrial);
    RemoveDir(dir);
}

TEST(publish_writes_what_the_store_reads_and_verifies) {
    if (!UltraCrypt_IsAvailable()) SKIP("no crypto backend");
    const std::string dir = TempDir("publish");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.SetSetting("catalog.base_url", "https://store.example/v1").Ok());
    REQUIRE(db.PutPublisher({"ultra-os-project", "ULTRA OS Project", ""}).Ok());
    // A real package file through the package store, so the hash is measured.
    const std::string zip = Join(dir, "hello-1.0.0.zip");
    REQUIRE(WriteFile(zip, "hello bytes"));
    const auto imported = ImportPackage(zip, Join(dir, "packages"));
    REQUIRE(imported.Ok());
    UltraStore::Component hello = HelloApp();
    hello.releases[0].packages[0].sha256 = imported.Value().sha256;
    hello.releases[0].depends.clear();
    hello.icon = "media/org.ultraos.hello/icon.png";
    hello.screenshots = {"media/org.ultraos.hello/shot.png", "https://cdn.example/far.png"};
    // The web store's sizes: copied beside the original; the one over its
    // budget is listed in the warnings.
    hello.iconVariants = {{"webp", 64, "media/org.ultraos.hello/icon-64.webp"}, {"webp", 256, "media/org.ultraos.hello/icon-256.webp"}};
    REQUIRE(db.PutComponent(hello).Ok());
    PackageFile f;
    f.sha256 = imported.Value().sha256;
    f.file = imported.Value().file;
    f.size = imported.Value().size;
    f.format = imported.Value().format;
    REQUIRE(db.PutPackageFile(f).Ok());
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(Join(dir, "media/org.ultraos.hello")));
    REQUIRE(WriteFile(Join(dir, "media/org.ultraos.hello/icon.png"), "PNG"));
    REQUIRE(WriteFile(Join(dir, "media/org.ultraos.hello/icon-64.webp"), "WEBP"));
    REQUIRE(WriteFile(Join(dir, "media/org.ultraos.hello/icon-256.webp"), std::string(13 * 1024, 'x')));   // over the 12 KB budget
    const auto key = NewSigningKey("pass", KeyCost::Fast());
    REQUIRE(key.Ok());
    const auto added = db.AddKey("test", key.Value().publicKeyHex, key.Value().sealed.Bytes());
    REQUIRE(added.Ok());

    PublishOptions options;
    options.outDirUtf8 = Join(dir, "out");
    options.mediaRootUtf8 = dir;   // the catalog's media/<id>/<file> is relative to the store folder
    options.generatedAt = "2026-10-27T10:00:00Z";
    REQUIRE(!Publish(db, added.Value().id, "wrong", options).Ok());
    REQUIRE_EQ(db.Sequence(), uint64_t(0));   // nothing recorded on failure
    const auto report = Publish(db, added.Value().id, "pass", options);
    REQUIRE(report.Ok());
    const PublishReport& r = report.Value();
    REQUIRE_EQ(r.sequence, uint64_t(1));
    REQUIRE_EQ(r.apps, size_t(1));
    REQUIRE_EQ(r.packagesCopied, size_t(1));
    REQUIRE_EQ(r.mediaCopied, size_t(3));     // the icon and its two sizes; the screenshot is missing (warned), the far one is a URL
    bool warnedShot = false, warnedBudget = false;
    for (const std::string& w : r.warnings) {
        if (w.find("shot.png") != std::string::npos) warnedShot = true;
        if (w.find("icon-256.webp") != std::string::npos && w.find("over the 12 KB budget") != std::string::npos) warnedBudget = true;
    }
    REQUIRE(warnedShot);
    REQUIRE(warnedBudget);
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "media/org.ultraos.hello/icon-64.webp")), std::string("WEBP"));
    REQUIRE_EQ(db.Sequence(), uint64_t(1));
    REQUIRE_EQ(db.Setting("catalog.published_at"), std::string("2026-10-27T10:00:00Z"));
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "packages/" + f.sha256 + ".zip")), std::string("hello bytes"));
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "media/org.ultraos.hello/icon.png")), std::string("PNG"));

    // The store's side: parse, verify with the exported public key, resolve.
    const std::string bytes = ReadFile(r.catalogPath);
    const std::string sig = ReadFile(r.signaturePath);
    std::vector<uint8_t> signature, publicKey;
    REQUIRE(UltraStore::ParseSignatureFile(sig, signature));
    REQUIRE(UltraStore::FromHex(r.publicKeyHex, publicKey));
    REQUIRE(UltraStore::VerifyCatalogSignature(bytes, signature, {publicKey}).Ok());
    const auto parsed = UltraStore::ParseCatalog(bytes);
    REQUIRE(parsed.Ok());
    REQUIRE_EQ(parsed.Value().sequence, uint64_t(1));
    REQUIRE_EQ(parsed.Value().apps[0].releases[0].packages[0].url, "https://store.example/v1/packages/" + f.sha256 + ".zip");
    REQUIRE_EQ(parsed.Value().apps[0].icon, std::string("media/org.ultraos.hello/icon.png"));
    REQUIRE_EQ(parsed.Value().apps[0].iconVariants.size(), size_t(2));   // the catalog names the sizes
    // Schema 2: the catalog announces every active key, so a store that
    // accepted it trusts the next key before the binary carrying it ships.
    REQUIRE_EQ(parsed.Value().schema, 2);
    REQUIRE_EQ(parsed.Value().trustedKeys.size(), size_t(1));
    REQUIRE_EQ(parsed.Value().trustedKeys[0], r.publicKeyHex);
    // Another key does not verify it.
    const auto other = NewSigningKey("x", KeyCost::Fast());
    std::vector<uint8_t> otherPk;
    REQUIRE(UltraStore::FromHex(other.Value().publicKeyHex, otherPk));
    REQUIRE(!UltraStore::VerifyCatalogSignature(bytes, signature, {otherPk}).Ok());
    // A second key, made for the rotation: the next publish (sequence 2,
    // still signed by the first) announces both; once the first is retired
    // it refuses to sign and drops out of the list, and the second signs.
    const auto next = NewSigningKey("next", KeyCost::Fast());
    const auto nextAdded = db.AddKey("next", next.Value().publicKeyHex, next.Value().sealed.Bytes());
    REQUIRE(nextAdded.Ok());
    const auto second = Publish(db, added.Value().id, "pass", options);
    REQUIRE_EQ(second.Value().sequence, uint64_t(2));
    const auto announced = UltraStore::ParseCatalog(ReadFile(second.Value().catalogPath)).Value().trustedKeys;
    REQUIRE_EQ(announced.size(), size_t(2));
    REQUIRE(std::find(announced.begin(), announced.end(), next.Value().publicKeyHex) != announced.end());
    REQUIRE(db.RetireKey(added.Value().id).Ok());
    REQUIRE(!Publish(db, added.Value().id, "pass", options).Ok());
    const auto third = Publish(db, nextAdded.Value().id, "next", options);
    REQUIRE(third.Ok());
    const auto afterRetire = UltraStore::ParseCatalog(ReadFile(third.Value().catalogPath)).Value().trustedKeys;
    REQUIRE_EQ(afterRetire.size(), size_t(1));
    REQUIRE_EQ(afterRetire[0], next.Value().publicKeyHex);
    RemoveDir(dir);
}

TEST(publish_lays_the_web_store_beside_the_catalog) {
    if (!UltraCrypt_IsAvailable()) SKIP("no crypto backend");
    const std::string dir = TempDir("website");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE(db.SetSetting("catalog.base_url", "https://store.example").Ok());
    REQUIRE(db.SetSetting("store.contact", "https://store.example/contact").Ok());
    REQUIRE(db.SetSetting("web.store_download_url", "https://ultraos.eu/ultrastore").Ok());
    REQUIRE(db.PutPublisher({"ultra-os-project", "ULTRA OS Project", ""}).Ok());
    const std::string zip = Join(dir, "hello-1.0.0.zip");
    REQUIRE(WriteFile(zip, "hello bytes"));
    const auto imported = ImportPackage(zip, Join(dir, "packages"));
    REQUIRE(imported.Ok());
    UltraStore::Component hello = HelloApp();
    hello.releases[0].packages[0].sha256 = imported.Value().sha256;
    hello.releases[0].depends.clear();
    hello.categories = {"utilities"};
    hello.featured = true;
    hello.icon = "media/org.ultraos.hello/icon.png";
    hello.iconVariants = {{"webp", 64, "media/org.ultraos.hello/icon-64.webp"}, {"webp", 128, "media/org.ultraos.hello/icon-128.webp"}, {"avif", 256, "media/org.ultraos.hello/icon-256.avif"}};
    REQUIRE(db.PutComponent(hello).Ok());
    UltraStore::Component hidden = HelloApp();
    hidden.id = "org.ultraos.secret";
    hidden.name = "Secret";
    hidden.visibility = UltraStore::Visibility::Hidden;
    hidden.releases[0].packages[0].sha256 = imported.Value().sha256;
    hidden.releases[0].depends.clear();
    REQUIRE(db.PutComponent(hidden).Ok());
    PackageFile f;
    f.sha256 = imported.Value().sha256;
    f.file = imported.Value().file;
    f.size = imported.Value().size;
    f.format = imported.Value().format;
    REQUIRE(db.PutPackageFile(f).Ok());
    const auto key = NewSigningKey("pass", KeyCost::Fast());
    const auto added = db.AddKey("test", key.Value().publicKeyHex, key.Value().sealed.Bytes());
    REQUIRE(added.Ok());
    PublishOptions options;
    options.outDirUtf8 = Join(dir, "out");
    options.generatedAt = "2026-11-14T10:00:00Z";

    // Without web.site_dir: no page of the site, as before.
    REQUIRE(Publish(db, added.Value().id, "pass", options).Ok());
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(Join(options.outDirUtf8, "index.html"))));

    // A site folder as the web store's build leaves it (the bytes stand in).
    const std::string site = Join(dir, "site");
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(Join(site, "runtime")));
    REQUIRE(WriteFile(Join(site, "ultrastore-web.wasm"), "module bytes"));
    REQUIRE(WriteFile(Join(site, "ultracanvas-loader.js"), "loader"));
    REQUIRE(WriteFile(Join(site, "ultrastore.png"), "PNG"));
    REQUIRE(WriteFile(Join(site, "site.json"), "{\"runtime_version\": \"0.9.600\"}"));
    REQUIRE(WriteFile(Join(site, "runtime/ultracanvas-runtime.js"), "js"));
    REQUIRE(WriteFile(Join(site, "runtime/ultracanvas-runtime.wasm"), "runtime bytes"));
    REQUIRE(WriteFile(Join(site, "runtime/ultracanvas-runtime.data"), "data"));
    REQUIRE(db.SetSetting("web.site_dir", site).Ok());
    const auto report = Publish(db, added.Value().id, "pass", options);
    REQUIRE(report.Ok());
    const PublishReport& r = report.Value();
    REQUIRE_EQ(r.webPages, size_t(4));   // Discover, the categories, utilities, the hello app; the hidden app gets none
    const std::string hash = UltraStore::Sha256Hex("runtime bytes").substr(0, 12);
    REQUIRE_EQ(r.webRuntime, "runtime/" + hash + "/");
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "runtime/" + hash + "/ultracanvas-runtime.wasm")), std::string("runtime bytes"));
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "ultrastore-web.wasm")), std::string("module bytes"));
    REQUIRE_EQ(ReadFile(Join(options.outDirUtf8, "ultracanvas-loader.js")), std::string("loader"));
    REQUIRE(!std::filesystem::exists(UltraCanvas::PathFromUtf8(Join(options.outDirUtf8, "app/org.ultraos.secret/index.html"))));

    const std::string index = ReadFile(Join(options.outDirUtf8, "index.html"));
    REQUIRE(index.find("data-page=\"\"") != std::string::npos);
    REQUIRE(index.find("data-root=\"\"") != std::string::npos);
    REQUIRE(index.find("data-store-download=\"https://ultraos.eu/ultrastore\"") != std::string::npos);
    REQUIRE(index.find("<script src=\"ultracanvas-loader.js\" data-app=\"ultrastore-web.wasm\" data-runtime=\"runtime/" + hash + "/\" data-runtime-version=\"0.9.600\"></script>") != std::string::npos);
    REQUIRE(index.find("FEATURED APP") != std::string::npos);
    REQUIRE(index.find("Says hello") != std::string::npos);
    REQUIRE(index.find("Secret") == std::string::npos);

    const std::string app = ReadFile(Join(options.outDirUtf8, "app/org.ultraos.hello/index.html"));
    REQUIRE(app.find("data-page=\"app/org.ultraos.hello\"") != std::string::npos);
    REQUIRE(app.find("data-root=\"../../\"") != std::string::npos);
    REQUIRE(app.find("<h1>Hello</h1>") != std::string::npos);
    REQUIRE(app.find("href=\"ultrastore://app/org.ultraos.hello\"") != std::string::npos);
    REQUIRE(app.find("<link rel=\"canonical\" href=\"https://store.example/app/org.ultraos.hello/\">") != std::string::npos);
    // The icon as a <picture>: the 1x and 2x WebP sources, the AVIF one, the original as the img.
    REQUIRE(app.find("type=\"image/webp\" srcset=\"../../media/org.ultraos.hello/icon-128.webp 1x\"") != std::string::npos);
    REQUIRE(app.find("type=\"image/avif\" srcset=\"../../media/org.ultraos.hello/icon-256.avif 1x\"") != std::string::npos);
    REQUIRE(app.find("src=\"../../media/org.ultraos.hello/icon.png\"") != std::string::npos);
    REQUIRE(app.find("src=\"../../ultracanvas-loader.js\" data-app=\"../../ultrastore-web.wasm\" data-runtime=\"../../runtime/" + hash + "/\"") != std::string::npos);
    const std::string category = ReadFile(Join(options.outDirUtf8, "category/utilities/index.html"));
    REQUIRE(category.find("data-page=\"category/utilities\"") != std::string::npos);
    REQUIRE(category.find("href=\"../../app/org.ultraos.hello/\"") != std::string::npos);

    // A site folder without the module: the pages still, without the script, and a warning.
    std::filesystem::remove(UltraCanvas::PathFromUtf8(Join(site, "ultrastore-web.wasm")));
    const auto again = Publish(db, added.Value().id, "pass", options);
    REQUIRE(again.Ok());
    bool warned = false;
    for (const std::string& w : again.Value().warnings) if (w.find("web store") != std::string::npos && w.find("ultrastore-web.wasm") != std::string::npos) warned = true;
    REQUIRE(warned);
    REQUIRE(ReadFile(Join(options.outDirUtf8, "index.html")).find("ultracanvas-loader.js\" data-app") == std::string::npos);
    RemoveDir(dir);
}
