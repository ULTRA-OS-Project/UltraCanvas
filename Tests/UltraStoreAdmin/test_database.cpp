// Tests/UltraStoreAdmin/test_database.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

using namespace UltraStoreAdmin;
using namespace admin_test;

TEST(the_database_keeps_settings_publishers_and_components_whole) {
    const std::string dir = TempDir("db");
    {
        AdminDatabase db;
        REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
        REQUIRE_EQ(db.Setting("catalog.base_url", "none"), std::string("none"));
        REQUIRE(db.SetSetting("catalog.base_url", "https://store.example/v1").Ok());
        REQUIRE_EQ(db.Setting("catalog.base_url"), std::string("https://store.example/v1"));
        REQUIRE_EQ(db.Sequence(), uint64_t(0));
        REQUIRE(db.SetSequence(7).Ok());
        REQUIRE_EQ(db.Sequence(), uint64_t(7));

        REQUIRE(db.PutPublisher({"ultra-os-project", "ULTRA OS Project", "https://ultraos.eu"}).Ok());
        REQUIRE(db.PutPublisher({"acme", "", ""}).Ok());   // the name falls back to the id
        const auto publishers = db.Publishers();
        REQUIRE(publishers.Ok());
        REQUIRE_EQ(publishers.Value().size(), size_t(2));
        REQUIRE_EQ(publishers.Value()[0].name, std::string("acme"));
        REQUIRE(db.RemovePublisher("acme").Ok());
        REQUIRE(!db.RemovePublisher("acme").Ok());

        REQUIRE(db.PutComponent(HelloApp()).Ok());
        REQUIRE(db.PutComponent(Runtime()).Ok());
        const auto back = db.GetComponent("org.ultraos.hello");
        REQUIRE(back.Ok());
        REQUIRE_EQ(back.Value().name, std::string("Hello"));
        REQUIRE_EQ(back.Value().summary.Text("de"), std::string("Sagt Hallo"));
        REQUIRE_EQ(back.Value().releases.size(), size_t(1));
        REQUIRE_EQ(back.Value().releases[0].packages[0].entry, std::string("bin/hello"));
        REQUIRE_EQ(back.Value().releases[0].depends[0].version, std::string("0.9.410"));
        REQUIRE_EQ(back.Value().machineTranslated.size(), size_t(1));
        // Replaced whole.
        UltraStore::Component changed = back.Value();
        changed.name = "Hello again";
        changed.releases.clear();
        REQUIRE(db.PutComponent(changed).Ok());
        REQUIRE_EQ(db.GetComponent("org.ultraos.hello").Value().name, std::string("Hello again"));
        REQUIRE(db.GetComponent("org.ultraos.hello").Value().releases.empty());
        const auto all = db.Components();
        REQUIRE(all.Ok());
        REQUIRE_EQ(all.Value().size(), size_t(2));
        REQUIRE(all.Value()[1].kind == UltraStore::ComponentKind::Runtime);
        REQUIRE(!db.GetComponent("org.ultraos.none").Ok());
        REQUIRE(db.RemoveComponent("org.ultraos.runtime").Ok());
        REQUIRE(!db.RemoveComponent("org.ultraos.runtime").Ok());
        // An id the store would refuse is refused here.
        UltraStore::Component bad;
        bad.id = "nodots";
        REQUIRE(!db.PutComponent(bad).Ok());
    }
    // Reopened: everything is still there.
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    REQUIRE_EQ(db.Sequence(), uint64_t(7));
    REQUIRE_EQ(db.Components().Value().size(), size_t(1));
    RemoveDir(dir);
}

TEST(package_files_and_keys) {
    const std::string dir = TempDir("db2");
    AdminDatabase db;
    REQUIRE(db.Open(Join(dir, "admin.sqlite")).Ok());
    PackageFile f;
    f.sha256 = std::string(64, 'c');
    f.file = "/store/packages/" + f.sha256 + ".zip";
    f.size = 42;
    f.format = "zip";
    REQUIRE(db.PutPackageFile(f).Ok());
    f.access = "secret";
    REQUIRE(!db.PutPackageFile(f).Ok());
    f.access = "restricted";
    REQUIRE(db.PutPackageFile(f).Ok());
    const auto got = db.GetPackageFile(f.sha256);
    REQUIRE(got.Ok());
    REQUIRE_EQ(got.Value().access, std::string("restricted"));
    REQUIRE_EQ(got.Value().size, uint64_t(42));
    REQUIRE_EQ(db.PackageFiles().Value().size(), size_t(1));
    REQUIRE(!db.GetPackageFile(std::string(64, 'd')).Ok());
    REQUIRE(db.RemovePackageFile(f.sha256).Ok());

    const std::vector<uint8_t> sealed = {1, 2, 3, 4};
    const auto key = db.AddKey("production", std::string(64, 'e'), sealed);
    REQUIRE(key.Ok());
    REQUIRE(key.Value().id > 0);
    REQUIRE(key.Value().Active());
    REQUIRE(!db.AddKey("short", "abc", sealed).Ok());
    REQUIRE(db.SealedSeed(key.Value().id).Value() == sealed);
    REQUIRE(db.RetireKey(key.Value().id).Ok());
    REQUIRE(!db.RetireKey(key.Value().id).Ok());   // once
    REQUIRE(!db.GetKey(key.Value().id).Value().Active());
    REQUIRE(!db.GetKey(99).Ok());
    RemoveDir(dir);
}
