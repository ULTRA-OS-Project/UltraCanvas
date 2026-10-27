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

#include "UltraCrypt/UltraCryptCore.h"

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
    REQUIRE(db.PutComponent(hello).Ok());
    PackageFile f;
    f.sha256 = imported.Value().sha256;
    f.file = imported.Value().file;
    f.size = imported.Value().size;
    f.format = imported.Value().format;
    REQUIRE(db.PutPackageFile(f).Ok());
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(Join(dir, "media/org.ultraos.hello")));
    REQUIRE(WriteFile(Join(dir, "media/org.ultraos.hello/icon.png"), "PNG"));
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
    REQUIRE_EQ(r.mediaCopied, size_t(1));     // the icon; the screenshot is missing (warned), the far one is a URL
    bool warnedShot = false;
    for (const std::string& w : r.warnings) if (w.find("shot.png") != std::string::npos) warnedShot = true;
    REQUIRE(warnedShot);
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
    // Another key does not verify it.
    const auto other = NewSigningKey("x", KeyCost::Fast());
    std::vector<uint8_t> otherPk;
    REQUIRE(UltraStore::FromHex(other.Value().publicKeyHex, otherPk));
    REQUIRE(!UltraStore::VerifyCatalogSignature(bytes, signature, {otherPk}).Ok());
    // The next publish is sequence 2; a retired key refuses.
    REQUIRE_EQ(Publish(db, added.Value().id, "pass", options).Value().sequence, uint64_t(2));
    REQUIRE(db.RetireKey(added.Value().id).Ok());
    REQUIRE(!Publish(db, added.Value().id, "pass", options).Ok());
    RemoveDir(dir);
}
