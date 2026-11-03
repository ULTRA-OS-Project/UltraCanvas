// Tests/UltraStore/test_registry.cpp
// The registry on UltraDatabase: rows in and out, the current version,
// removal, persistence across a reopen, a newer schema refused.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "helpers.h"

#include "Registry.h"

#include <UltraDatabase/UltraDatabase.h>

#include <filesystem>

using namespace UltraStore;
using namespace ultrastore_test;

namespace {

InstalledRecord Rec(ComponentKind kind, const char* id, const char* version, bool current = false) {
    InstalledRecord r;
    r.kind = kind;
    r.id = id;
    r.version = version;
    r.folder = std::string("/store/") + id + "/" + version;
    r.entry = kind == ComponentKind::App ? "App" : "";
    r.sha256 = std::string(64, 'a');
    r.installedAt = "2026-10-24T15:00:00Z";
    r.channel = "stable";
    r.current = current;
    return r;
}

} // namespace

TEST(registry_remembers_rows_and_the_current_version) {
    const std::string root = TempRoot("registry");
    const std::string db = root + "/registry.sqlite";
    {
        SqliteRegistry reg;
        REQUIRE(!reg.IsOpen());
        REQUIRE(!reg.All());   // not open yet
        REQUIRE(reg.Open(db));
        REQUIRE(reg.IsOpen());
        REQUIRE(reg.All().Value().empty());

        InstalledRecord runtime = Rec(ComponentKind::Runtime, "org.x.runtime", "0.9.410");
        REQUIRE(reg.Add(runtime));
        REQUIRE(reg.Add(Rec(ComponentKind::Runtime, "org.x.runtime", "0.9.400")));
        InstalledRecord app = Rec(ComponentKind::App, "org.x.app", "1.0", true);
        app.depends = {{"org.x.runtime", "0.9.410"}};
        REQUIRE(reg.Add(app));

        const auto all = reg.All();
        REQUIRE(all);
        REQUIRE_EQ(all.Value().size(), size_t(3));
        const auto rt = reg.Of("org.x.runtime");
        REQUIRE_EQ(rt.Value().size(), size_t(2));
        const auto cur = reg.Current("org.x.app");
        REQUIRE(cur && cur.Value().version == "1.0");
        REQUIRE_EQ(cur.Value().depends.size(), size_t(1));
        REQUIRE_EQ(cur.Value().depends[0].version, std::string("0.9.410"));
        REQUIRE_EQ(cur.Value().entry, std::string("App"));
        REQUIRE(cur.Value().kind == ComponentKind::App);
        REQUIRE(!reg.Current("org.x.runtime"));
        REQUIRE(reg.Get("org.x.runtime", "0.9.400"));
        REQUIRE(!reg.Get("org.x.runtime", "0.9.999"));

        const InstalledVersions v = reg.Versions();
        REQUIRE(v.Has("org.x.runtime", "0.9.410"));
        REQUIRE(v.Has("org.x.app", "1.0"));
        REQUIRE(!v.Has("org.x.app", "2.0"));

        // A second app version becomes current; the first stays installed.
        REQUIRE(reg.Add(Rec(ComponentKind::App, "org.x.app", "1.1", true)));
        REQUIRE_EQ(reg.Current("org.x.app").Value().version, std::string("1.1"));
        REQUIRE(!reg.Get("org.x.app", "1.0").Value().current);
        REQUIRE(reg.SetCurrent("org.x.app", "1.0"));
        REQUIRE_EQ(reg.Current("org.x.app").Value().version, std::string("1.0"));
        REQUIRE(!reg.SetCurrent("org.x.app", "9.9"));
        REQUIRE(reg.SetCurrent("org.x.app", "9.9").error.code == ErrorCode::NotFound);
        REQUIRE_EQ(reg.Current("org.x.app").Value().version, std::string("1.0"));   // a miss changes nothing

        REQUIRE(reg.Remove("org.x.runtime", "0.9.400"));
        REQUIRE_EQ(reg.Of("org.x.runtime").Value().size(), size_t(1));
        REQUIRE(reg.Remove("org.x.nothing", "1"));   // nothing to forget is fine
    }
    {
        // Persisted.
        SqliteRegistry reg;
        REQUIRE(reg.Open(db));
        REQUIRE_EQ(reg.All().Value().size(), size_t(3));
        REQUIRE_EQ(reg.Current("org.x.app").Value().version, std::string("1.0"));
    }
    {
        // Written by a newer store: refused.
        UltraDbConnectionConfig cfg;
        cfg.name = "ultrastore-test-bump";
        cfg.driver = "sqlite";
        cfg.database = db;
        REQUIRE(UltraDb_RegisterConnection(cfg));
        REQUIRE(UltraDb_Exec(cfg.name, "UPDATE meta SET value = '99' WHERE key = 'schema_version'"));
        UltraDb_CloseConnection(cfg.name);
        SqliteRegistry reg;
        const auto r = reg.Open(db);
        REQUIRE(!r && r.error.code == ErrorCode::SchemaTooNew);
        REQUIRE(!reg.IsOpen());
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(depends_round_trip_through_the_column) {
    const std::vector<Dependency> deps = {{"org.x.runtime", "0.9.410"}, {"org.x.lib", "1.2.3-beta.1"}};
    const std::string text = SqliteRegistry::JoinDepends(deps);
    REQUIRE_EQ(text, std::string("org.x.runtime@0.9.410 org.x.lib@1.2.3-beta.1"));
    const auto back = SqliteRegistry::SplitDepends(text);
    REQUIRE_EQ(back.size(), size_t(2));
    REQUIRE_EQ(back[1].id, std::string("org.x.lib"));
    REQUIRE_EQ(back[1].version, std::string("1.2.3-beta.1"));
    REQUIRE(SqliteRegistry::SplitDepends("junk @x y@").empty());
    REQUIRE(SqliteRegistry::JoinDepends({}).empty());
}

TEST(a_registry_without_a_path_or_folder_fails_cleanly) {
    SqliteRegistry reg;
    REQUIRE(!reg.Open(""));
    const auto r = reg.Open("/nonexistent-folder-ultrastore/sub/registry.sqlite");
    REQUIRE(!r);
    REQUIRE(!reg.IsOpen());
}
