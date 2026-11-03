// Tests/UltraStore/test_client.cpp
// The catalog client against an in-memory server: fetch, verify, cache,
// 304, a second mirror, a rollback, a tampered body, offline reads.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"

#include "CatalogClient.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <random>

using namespace UltraStore;
using namespace ultrastore_test;

namespace {

struct FakeServer {
    std::map<std::string, std::string> files;   // url -> body
    std::string etag = "\"v1\"";
    int calls = 0;
    std::vector<std::string> urls;

    Fetcher AsFetcher() {
        return [this](const std::string& url, const std::string& ifNoneMatch) {
            ++calls;
            urls.push_back(url);
            FetchResponse r;
            auto it = files.find(url);
            if (it == files.end()) { r.status = 404; return r; }
            if (url.size() > 4 && url.compare(url.size() - 5, 5, ".json") == 0 && !ifNoneMatch.empty() && ifNoneMatch == etag) {
                r.status = 304;
                r.etag = etag;
                return r;
            }
            r.status = 200;
            r.body = it->second;
            if (url.compare(url.size() - 5, 5, ".json") == 0) r.etag = etag;
            return r;
        };
    }

    void Publish(const std::string& base, const Catalog& c, const std::vector<uint8_t>& seed) {
        const std::string json = SerializeCatalog(c);
        files[base + "/catalog.json"] = json;
        files[base + "/catalog.json.sig"] = ToHex(SignCatalog(json, seed).Value()) + "\n";
    }
};

std::string TempDir(const char* tag) {
    std::mt19937_64 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() / ("ultrastore-test-" + std::string(tag) + "-" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir.string();
}

} // namespace

TEST(refresh_fetches_verifies_caches_and_honours_304) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    FakeServer server;
    server.Publish("https://store.example", FixtureCatalog(), seed);

    CatalogClientConfig config;
    config.baseUrls = {"https://store.example"};
    config.trustedKeys = {pk};
    config.cacheDir = TempDir("refresh");
    CatalogClient client(config, server.AsFetcher());

    REQUIRE(!client.LoadCached());
    REQUIRE(client.LoadCached().error.code == ErrorCode::NotFound);

    const auto first = client.Refresh();
    REQUIRE(first);
    REQUIRE_EQ(first.Value().sequence, uint64_t(42));
    REQUIRE_EQ(server.calls, 2);
    CatalogState state = client.State();
    REQUIRE_EQ(state.sequence, uint64_t(42));
    REQUIRE_EQ(state.etag, std::string("\"v1\""));
    REQUIRE_EQ(state.fetchedFrom, std::string("https://store.example"));
    REQUIRE(!state.fetchedAt.empty());
    REQUIRE(std::filesystem::exists(std::filesystem::path(config.cacheDir) / "catalog.json"));
    REQUIRE(std::filesystem::exists(std::filesystem::path(config.cacheDir) / "catalog.json.sig"));
    REQUIRE(std::filesystem::exists(std::filesystem::path(config.cacheDir) / "state.json"));

    // Unchanged on the server: one request, 304, the cache is the answer.
    const auto again = client.Refresh();
    REQUIRE(again);
    REQUIRE_EQ(server.calls, 3);
    REQUIRE_EQ(again.Value().apps.size(), size_t(3));

    // A new client over the same cache works offline.
    FakeServer dead;
    CatalogClient offline(config, dead.AsFetcher());
    const auto cached = offline.LoadCached();
    REQUIRE(cached);
    REQUIRE_EQ(cached.Value().sequence, uint64_t(42));
    REQUIRE_EQ(offline.State().sequence, uint64_t(42));
    const auto failed = offline.Refresh();
    REQUIRE(!failed);
    REQUIRE(failed.error.code == ErrorCode::NetworkError);
    REQUIRE(offline.LoadCached());   // the failure left the cache alone

    std::filesystem::remove_all(config.cacheDir);
}

TEST(a_newer_catalog_replaces_and_an_older_one_is_refused) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    FakeServer server;
    Catalog c = FixtureCatalog();
    server.Publish("https://store.example", c, seed);
    CatalogClientConfig config;
    config.baseUrls = {"https://store.example"};
    config.trustedKeys = {pk};
    config.cacheDir = TempDir("sequence");
    CatalogClient client(config, server.AsFetcher());
    REQUIRE(client.Refresh());

    c.sequence = 43;
    server.etag = "\"v2\"";
    server.Publish("https://store.example", c, seed);
    const auto newer = client.Refresh();
    REQUIRE(newer && newer.Value().sequence == 43);
    REQUIRE_EQ(client.State().etag, std::string("\"v2\""));

    c.sequence = 41;
    server.etag = "\"v0\"";
    server.Publish("https://store.example", c, seed);
    const auto older = client.Refresh();
    REQUIRE(!older);
    REQUIRE(older.error.code == ErrorCode::SequenceRollback);
    REQUIRE_EQ(client.State().sequence, uint64_t(43));
    REQUIRE_EQ(client.LoadCached().Value().sequence, uint64_t(43));

    // The same sequence again (a mirror that lags by nothing) is fine.
    c.sequence = 43;
    server.Publish("https://store.example", c, seed);
    REQUIRE(client.Refresh());
    std::filesystem::remove_all(config.cacheDir);
}

TEST(a_bad_signature_or_a_tampered_cache_is_refused) {
    std::vector<uint8_t> seed, pk, otherSeed, otherPk;
    REQUIRE(GenerateSigningKey(seed, pk));
    REQUIRE(GenerateSigningKey(otherSeed, otherPk));
    FakeServer server;
    server.Publish("https://store.example", FixtureCatalog(), otherSeed);   // signed by a stranger
    CatalogClientConfig config;
    config.baseUrls = {"https://store.example"};
    config.trustedKeys = {pk};
    config.cacheDir = TempDir("signature");
    CatalogClient client(config, server.AsFetcher());
    const auto r = client.Refresh();
    REQUIRE(!r && r.error.code == ErrorCode::SignatureInvalid);
    REQUIRE(!std::filesystem::exists(std::filesystem::path(config.cacheDir) / "catalog.json"));

    // No signature file at all.
    server.files.erase("https://store.example/catalog.json.sig");
    const auto noSig = client.Refresh();
    REQUIRE(!noSig && noSig.error.code == ErrorCode::NetworkError);

    // A good fetch, then the cache edited on disk: LoadCached verifies again.
    server.Publish("https://store.example", FixtureCatalog(), seed);
    REQUIRE(client.Refresh());
    {
        const auto path = std::filesystem::path(config.cacheDir) / "catalog.json";
        std::string text;
        { std::ifstream in(path, std::ios::binary); std::ostringstream b; b << in.rdbuf(); text = b.str(); }
        text.replace(text.find("UltraVideo"), 10, "UltraVidxo");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    const auto tampered = client.LoadCached();
    REQUIRE(!tampered && tampered.error.code == ErrorCode::SignatureInvalid);
    std::filesystem::remove_all(config.cacheDir);
}

TEST(the_second_mirror_answers_when_the_first_is_down) {
    std::vector<uint8_t> seed, pk;
    REQUIRE(GenerateSigningKey(seed, pk));
    FakeServer server;
    server.Publish("https://mirror.example", FixtureCatalog(), seed);
    CatalogClientConfig config;
    config.baseUrls = {"https://down.example", "https://mirror.example"};
    config.trustedKeys = {pk};
    config.cacheDir = TempDir("mirror");
    CatalogClient client(config, server.AsFetcher());
    const auto r = client.Refresh();
    REQUIRE(r);
    REQUIRE_EQ(client.State().fetchedFrom, std::string("https://mirror.example"));
    REQUIRE_EQ(server.urls[0], std::string("https://down.example/catalog.json"));

    // The ETag belongs to the mirror; a refresh that finds the first server
    // up again must not send it there.
    server.Publish("https://down.example", FixtureCatalog(), seed);
    server.urls.clear();
    REQUIRE(client.Refresh());
    REQUIRE_EQ(client.State().fetchedFrom, std::string("https://down.example"));
    std::filesystem::remove_all(config.cacheDir);
}

TEST(the_client_needs_a_key_and_a_base_url) {
    FakeServer server;
    CatalogClientConfig config;
    config.cacheDir = TempDir("config");
    CatalogClient client(config, server.AsFetcher());
    const auto r = client.Refresh();
    REQUIRE(!r && r.error.code == ErrorCode::InvalidArgument);
    std::filesystem::remove_all(config.cacheDir);
}

TEST(a_key_the_catalog_announced_signs_the_next_one_and_an_unannounced_key_never_does) {
    std::vector<uint8_t> seedA, pkA, seedB, pkB, seedC, pkC;
    REQUIRE(GenerateSigningKey(seedA, pkA));
    REQUIRE(GenerateSigningKey(seedB, pkB));
    REQUIRE(GenerateSigningKey(seedC, pkC));
    FakeServer server;
    CatalogClientConfig config;
    config.baseUrls = {"https://store.example"};
    config.trustedKeys = {pkA};   // what the binary carries
    config.cacheDir = TempDir("rotation");
    CatalogClient client(config, server.AsFetcher());

    // A catalog signed by B that announces B itself: B is not trusted yet.
    Catalog c = FixtureCatalog();
    c.trustedKeys = {ToHex(pkB)};
    server.Publish("https://store.example", c, seedB);
    REQUIRE(!client.Refresh());
    REQUIRE(client.Refresh().error.code == ErrorCode::SignatureInvalid);
    REQUIRE(client.KeysTrustedNow().size() == size_t(1));

    // Signed by A, announcing B: accepted, and B is trusted from now on.
    server.Publish("https://store.example", c, seedA);
    server.etag = "\"v2\"";
    REQUIRE(client.Refresh());
    REQUIRE_EQ(client.State().announcedKeys.size(), size_t(1));
    REQUIRE_EQ(client.State().announcedKeys[0], ToHex(pkB));
    REQUIRE_EQ(client.KeysTrustedNow().size(), size_t(2));

    // The next catalog, signed by B, announcing only B: accepted, cached, and
    // the cache reads back with the announced key.
    c.sequence = 43;
    server.Publish("https://store.example", c, seedB);
    server.etag = "\"v3\"";
    REQUIRE(client.Refresh());
    REQUIRE_EQ(client.LoadCached().Value().sequence, uint64_t(43));
    CatalogClient again(config, server.AsFetcher());   // a fresh client over the same cache remembers it too
    REQUIRE(again.LoadCached());
    REQUIRE_EQ(again.KeysTrustedNow().size(), size_t(2));

    // A compiled-in key is never dropped by a catalog: A still signs.
    c.sequence = 44;
    server.Publish("https://store.example", c, seedA);
    server.etag = "\"v4\"";
    REQUIRE(client.Refresh());

    // A key no accepted catalog announced is refused, whatever it claims.
    c.sequence = 45;
    c.trustedKeys = {ToHex(pkC)};
    server.Publish("https://store.example", c, seedC);
    server.etag = "\"v5\"";
    const auto refused = client.Refresh();
    REQUIRE(!refused);
    REQUIRE(refused.error.code == ErrorCode::SignatureInvalid);
    REQUIRE_EQ(client.LoadCached().Value().sequence, uint64_t(44));   // the cache is as it was

    // A schema 1 catalog announces nothing: from then on only A signs.
    c.sequence = 46;
    c.trustedKeys.clear();
    server.Publish("https://store.example", c, seedA);
    server.etag = "\"v6\"";
    REQUIRE(client.Refresh());
    REQUIRE(client.State().announcedKeys.empty());
    c.sequence = 47;
    server.Publish("https://store.example", c, seedB);
    server.etag = "\"v7\"";
    REQUIRE(client.Refresh().error.code == ErrorCode::SignatureInvalid);
}
