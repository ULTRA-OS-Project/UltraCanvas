// Apps/UltraStore/core/CatalogClient.h
// Getting the catalog: fetched from the first server that answers, checked
// against the trusted keys, refused when it is older than the one already
// seen, kept on disk so the store works offline. The fetch is a function the
// client is given, so the tests hand it a local one and nothing here touches
// the network in a test; the default fetches with UltraNet. The keys a
// catalog announces (trusted_keys, schema 2) are remembered with the cache
// and trusted beside the configured ones for the next catalog, which is how
// a signing key is rotated without a new store binary.
// Version: 0.2.0 - the announced keys
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "Verifier.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraStore {

// What a fetch gives back: the status (200, 304, 404, 0 when nothing came),
// the body and the ETag the server sent, or an error message.
struct FetchResponse {
    int status = 0;
    std::string body;
    std::string etag;
    std::string error;    // empty when the server answered, whatever it said
};

// GET url, with If-None-Match when etag is set.
using Fetcher = std::function<FetchResponse(const std::string& url, const std::string& etag)>;

// The default: UltraNet, HTTPS, redirects to https:// only.
Fetcher UltraNetFetcher();

struct CatalogClientConfig {
    // Where catalog.json lives, tried in order; "<base>/catalog.json" and
    // "<base>/catalog.json.sig". A list, so a mirror is a configuration
    // change and not a release.
    std::vector<std::string> baseUrls;
    PublicKeys trustedKeys;     // compiled in, and the ones the person added: never dropped by a catalog
    std::string cacheDir;       // UTF-8 folder for catalog.json, .sig and state.json
};

// What the client remembers between runs.
struct CatalogState {
    uint64_t sequence = 0;      // the highest sequence accepted so far
    std::string etag;           // of the cached catalog, for If-None-Match
    std::string fetchedFrom;    // the base URL it came from
    std::string fetchedAt;      // ISO 8601, UTC
    // The keys the last accepted catalog announced (its trusted_keys, hex):
    // trusted beside the configured ones until the next accepted catalog
    // replaces the list. Empty after a schema 1 catalog.
    std::vector<std::string> announcedKeys;
};

class CatalogClient {
public:
    CatalogClient(CatalogClientConfig config, Fetcher fetcher = UltraNetFetcher());

    // Fetch, verify the signature, check the sequence, replace the cache
    // atomically, and return the catalog. A 304 returns the cached one.
    // Every failure names what went wrong; the cache is left as it was, so
    // LoadCached() still works after a failed refresh.
    Result<Catalog> Refresh();

    // The cached catalog, verified again before it is trusted; NotFound when
    // there is none.
    Result<Catalog> LoadCached() const;

    // What is remembered; sequence 0 and empty strings when nothing is.
    CatalogState State() const;

    // The keys a catalog may be signed with now: the configured ones and
    // the ones the last accepted catalog announced.
    PublicKeys KeysTrustedNow() const;

    const CatalogClientConfig& Config() const { return config_; }

private:
    Result<Catalog> Accept(const std::string& catalogBytes, const std::vector<uint8_t>& signature,
                           const std::string& etag, const std::string& baseUrl);
    bool WriteCache(const std::string& catalogBytes, const std::vector<uint8_t>& signature,
                    const CatalogState& state, std::string& error) const;

    CatalogClientConfig config_;
    Fetcher fetcher_;
};

// "2026-10-24T14:00:00Z".
std::string NowIso8601Utc();

} // namespace UltraStore
