// Tests/UltraStore/test_catalog.cpp
// The catalog round-trips through JSON, the field rules hold, and what the
// store relies on is refused when it is wrong.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"

#include "Catalog.h"

using namespace UltraStore;
using namespace ultrastore_test;

TEST(catalog_round_trips_through_json) {
    const Catalog original = FixtureCatalog();
    const std::string json = SerializeCatalog(original);
    const Result<Catalog> parsed = ParseCatalog(json);
    REQUIRE(parsed);
    const Catalog& c = parsed.Value();
    REQUIRE_EQ(c.schema, 1);
    REQUIRE_EQ(c.sequence, uint64_t(42));
    REQUIRE_EQ(c.apps.size(), size_t(3));
    REQUIRE_EQ(c.components.size(), size_t(2));
    REQUIRE_EQ(c.publishers.size(), size_t(1));
    const Component* video = c.FindApp("org.ultraos.ultravideo");
    REQUIRE(video != nullptr);
    REQUIRE(video->kind == ComponentKind::App);
    REQUIRE_EQ(video->summary.Text("de"), std::string("Der Videoeditor"));
    REQUIRE_EQ(video->releases.size(), size_t(2));
    // Newest first, whatever the order in the file.
    REQUIRE_EQ(video->releases[0].version, std::string("0.3.0-beta.1"));
    REQUIRE_EQ(video->releases[1].version, std::string("0.2.1"));
    REQUIRE_EQ(video->releases[1].depends.size(), size_t(2));
    REQUIRE_EQ(video->releases[1].depends[1].id, std::string("org.ultraos.lib.videofx"));
    REQUIRE_EQ(video->releases[1].packages[2].entry, std::string("UltraVideo.app"));
    REQUIRE(video->releases[1].packages[2].target.arch == Arch::Universal);
    const Component* runtime = c.FindComponent("org.ultraos.runtime");
    REQUIRE(runtime != nullptr && runtime->kind == ComponentKind::Runtime);
    REQUIRE_EQ(runtime->releases[0].version, std::string("0.9.410"));
    REQUIRE_EQ(runtime->releases[0].packages[0].target.variants.size(), size_t(1));
    REQUIRE_EQ(runtime->releases[0].packages[3].minOs, std::string("14.0"));
    // The admin backend's settings.
    REQUIRE(video->pricing.model == PricingModel::OneTime);
    REQUIRE_EQ(video->pricing.price, std::string("29.90"));
    REQUIRE_EQ(video->pricing.currency, std::string("EUR"));
    REQUIRE_EQ(video->pricing.trialDays, 30);
    REQUIRE(video->updates.period == UpdatePeriod::Until);
    REQUIRE_EQ(video->updates.until, std::string("2027-12-31"));
    REQUIRE(video->visibility == Visibility::Listed && video->lifecycle == Lifecycle::Active);
    REQUIRE_EQ(video->ageRating, std::string("3"));
    REQUIRE(video->openSource && !video->requiresAccount);
    REQUIRE_EQ(video->tags.size(), size_t(2));
    REQUIRE_EQ(video->sourceLanguage, std::string("de"));
    REQUIRE(video->machineTranslated == std::vector<std::string>{"en"});
    REQUIRE(c.FindApp("org.ultraos.linuxtool")->sourceLanguage.empty());
    const Component* intel = c.FindApp("org.ultraos.intelonly");
    REQUIRE(intel->visibility == Visibility::Unlisted);
    REQUIRE(intel->lifecycle == Lifecycle::Deprecated);
    REQUIRE_EQ(intel->successor, std::string("org.ultraos.ultravideo"));
    REQUIRE(intel->pricing.model == PricingModel::Free && intel->updates.period == UpdatePeriod::Unlimited);
    REQUIRE(c.FindAny("org.ultraos.lib.videofx") != nullptr);
    REQUIRE(c.FindApp("org.ultraos.lib.videofx") == nullptr);
    REQUIRE(c.FindAny("nope") == nullptr);
}

TEST(newest_release_respects_the_channel) {
    const Catalog c = FixtureCatalog();
    const Component* video = c.FindApp("org.ultraos.ultravideo");
    REQUIRE_EQ(video->Newest("stable")->version, std::string("0.2.1"));
    REQUIRE_EQ(video->Newest("beta")->version, std::string("0.3.0-beta.1"));
    REQUIRE(video->Find("0.2.1") != nullptr);
    REQUIRE(video->Find("9.9.9") == nullptr);
}

TEST(a_newer_schema_asks_for_a_newer_store) {
    const Result<Catalog> r = ParseCatalog(R"({"schema": 7, "sequence": 1, "apps": []})");
    REQUIRE(!r);
    REQUIRE(r.error.code == ErrorCode::SchemaTooNew);
    REQUIRE(r.error.detail.find("schema 7") != std::string::npos);
}

TEST(the_store_refuses_what_it_cannot_rely_on) {
    auto refuse = [](const std::string& json, const std::string& what) {
        const Result<Catalog> r = ParseCatalog(json);
        if (r) throw ::ultrastore_test::Failure{"accepted: " + what};
        if (r.error.code != ErrorCode::ParseError) throw ::ultrastore_test::Failure{"wrong code for: " + what};
    };
    refuse("not json at all", "junk");
    refuse(R"({"sequence": 1, "apps": []})", "no schema");
    refuse(R"({"schema": 1, "apps": []})", "no sequence");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "noreversedns", "releases": []}]})", "an id without dots");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"channel": "stable", "packages": []}]}]})",
           "a release without version");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "channel": "nightly", "packages": []}]}]})",
           "an unknown channel");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
              {"os": "linux", "arch": "x86_64", "format": "zip", "url": "ftp://x/y", "sha256": ")" + Hash('a') + R"("}]}]}]})",
           "a package url that is not http(s)");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
              {"os": "linux", "arch": "x86_64", "format": "zip", "url": "https://x/y", "sha256": "abc"}]}]}]})",
           "a short sha256");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
              {"os": "linux", "arch": "x86_64", "format": "zip", "url": "https://x/y", "sha256": ")" + std::string(64, 'z') + R"("}]}]}]})",
           "a sha256 that is not hex");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
              {"os": "amiga", "arch": "x86_64", "format": "zip", "url": "https://x/y", "sha256": ")" + Hash('a') + R"("}]}]}]})",
           "an unknown os");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "depends": [{"id": "org.x.r"}], "packages": []}]}]})",
           "a dependency without version");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": []}, {"id": "org.x.a", "releases": []}]})",
           "two entries with one id");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [], "components": [{"id": "org.x.c", "kind": "app", "releases": []}]})",
           "an app among the components");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "pricing": {"model": "donation"}, "releases": []}]})",
           "an unknown pricing model");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "pricing": {"model": "monthly", "price": "4.99"}, "releases": []}]})",
           "a paid app without currency");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "updates": {"period": "until", "until": "soon"}, "releases": []}]})",
           "an update period until no date");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "visibility": "secret", "releases": []}]})",
           "an unknown visibility");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "lifecycle": "dead", "releases": []}]})",
           "an unknown lifecycle");
    refuse(R"({"schema": 1, "sequence": 1, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
              {"os": "linux", "arch": "x86_64", "min_os": "latest", "format": "zip", "url": "https://x/y", "sha256": ")" + Hash('a') + R"("}]}]}]})",
           "a min_os that is not a version");
}

TEST(the_wider_targets_are_accepted_for_the_admin_backend) {
    const Result<Catalog> r = ParseCatalog(
        R"({"schema": 1, "sequence": 3, "apps": [{"id": "org.x.a", "releases": [{"version": "1", "packages": [
            {"os": "ios", "arch": "arm64", "min_os": "17.0", "format": "zip", "url": "https://x/ios", "sha256": ")" + Hash('a') + R"("},
            {"os": "android", "arch": "arm64", "min_os": "10", "format": "zip", "url": "https://x/apk", "sha256": ")" + Hash('b') + R"("},
            {"os": "wasm", "arch": "wasm32", "format": "zip", "url": "https://x/web", "sha256": ")" + Hash('c') + R"("},
            {"os": "ultraos", "arch": "riscv64", "format": "7z", "url": "https://x/rv", "sha256": ")" + Hash('d') + R"("}]}]}]})");
    REQUIRE(r);
    const auto& pk = r.Value().apps[0].releases[0].packages;
    REQUIRE(pk[0].target.os == OS::IOS && pk[1].target.os == OS::Android && pk[2].target.os == OS::Wasm && pk[3].target.os == OS::UltraOS);
    REQUIRE(pk[2].target.arch == Arch::Wasm32 && pk[3].target.arch == Arch::Riscv64);
    REQUIRE_EQ(pk[1].minOs, std::string("10"));
    const std::string again = SerializeCatalog(r.Value());
    REQUIRE(again.find("\"ios\"") != std::string::npos && again.find("\"wasm32\"") != std::string::npos);
}

TEST(lenient_about_what_the_store_does_not_rely_on) {
    const Result<Catalog> r = ParseCatalog(
        R"({"schema": 1, "sequence": 3, "apps": [{"id": "org.x.a", "name": "A", "summary": "plain string",
            "releases": [{"version": "1.2", "packages": []}]}]})");
    REQUIRE(r);
    const Component& a = r.Value().apps[0];
    REQUIRE_EQ(a.summary.Text("en"), std::string("plain string"));
    REQUIRE_EQ(a.releases[0].channel, std::string("stable"));
    REQUIRE(a.releases[0].packages.empty());
    REQUIRE(a.publisher.empty());
}

TEST(trusted_keys_round_trip_as_schema_2_and_a_bad_one_is_refused) {
    Catalog c = FixtureCatalog();
    REQUIRE(c.trustedKeys.empty());   // the fixture is schema 1
    c.trustedKeys = {std::string(64, 'B'), std::string(64, 'a'), std::string(64, 'b')};   // upper case, a duplicate
    const std::string json = SerializeCatalog(c);
    REQUIRE(json.find("\"schema\": 2") != std::string::npos);   // carrying trusted_keys makes it schema 2
    const Result<Catalog> parsed = ParseCatalog(json);
    REQUIRE(parsed);
    REQUIRE_EQ(parsed.Value().schema, 2);
    REQUIRE_EQ(parsed.Value().trustedKeys.size(), size_t(2));   // lower-cased, deduplicated, in order
    REQUIRE_EQ(parsed.Value().trustedKeys[0], std::string(64, 'b'));
    REQUIRE_EQ(parsed.Value().trustedKeys[1], std::string(64, 'a'));
    // A schema 1 catalog has none, and the writer keeps it schema 1.
    REQUIRE(SerializeCatalog(FixtureCatalog()).find("\"schema\": 1") != std::string::npos);
    // Not a key: refused, the catalog with it.
    const Result<Catalog> bad = ParseCatalog(R"({"schema": 2, "sequence": 1, "trusted_keys": ["abc"], "apps": []})");
    REQUIRE(!bad);
    REQUIRE(bad.error.code == ErrorCode::ParseError);
    REQUIRE(bad.error.detail == "abc");
    const Result<Catalog> none = ParseCatalog(R"({"schema": 2, "sequence": 1, "apps": []})");
    REQUIRE(none);
    REQUIRE(none.Value().trustedKeys.empty());
}
