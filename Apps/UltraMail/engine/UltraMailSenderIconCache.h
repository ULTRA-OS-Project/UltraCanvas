// Apps/UltraMail/engine/UltraMailSenderIconCache.h
// The sender-icon cache: one folder holding the icon of every known service an
// inbox actually hears from, so a message from Facebook, LinkedIn, Claude or
// the parcel carrier is recognisable before a word of it is read.
//
// Three properties are deliberate:
//
//  * Only the curated registry is ever fetched. UltraMail does not go and ask
//    the internet about a stranger's domain — that would tell a third party
//    who writes to the user. The set of possible requests is the brand table
//    in UltraMailSenderBrands, and each is made at most once.
//  * The fetch itself is injected (SetFetcher). The engine has no network
//    dependency, the test suite drives the cache with a fake fetcher, and a
//    build with no fetcher set simply never downloads anything.
//  * A miss is remembered. A brand whose icon could not be fetched is not
//    retried until the retry interval has passed, so an offline machine does
//    not spend every sync on the same failures.
//
// No icon is a normal state, not an error: the badge falls back to the brand's
// monogram in the brand's own colour.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSenderBrands.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace UltraMail {

class SenderIconCache {
public:
    // Fetch `url` into `out`. Returns false on any failure (no network, 404,
    // a body that is not an image). Called on the caller's thread — UltraMail
    // calls the cache from the sync worker, never from the UI thread.
    using Fetcher = std::function<bool(const std::string& url, std::vector<uint8_t>& out)>;

    // The cache directory (created on first write). Typically
    // <cacheDir>/sender-icons.
    void SetRoot(std::string directory);
    std::string Root() const;

    void SetFetcher(Fetcher fetcher);
    // Turn downloading off entirely (the user's preference); lookups of
    // already-cached icons keep working.
    void SetNetworkEnabled(bool enabled);
    bool NetworkEnabled() const;

    // How long a failed fetch is remembered before it may be tried again.
    void SetRetryInterval(int64_t seconds);

    // ---- Lookup (no network) ----------------------------------------------
    // The cached icon file for an address's brand, or "" when nothing is
    // cached (or the address belongs to no known brand).
    std::string IconForAddress(const std::string& address) const;
    std::string IconForBrand(const std::string& brandId) const;

    // ---- Lookup that may fill the cache -----------------------------------
    // As above, but fetches the icon once when it is missing. Returns the
    // cached path, or "" when there is nothing to show.
    std::string EnsureIconForAddress(const std::string& address);
    std::string EnsureIconForBrand(const SenderBrand& brand);

    // Fetch every brand's icon that is not cached yet; returns how many were
    // newly stored. Used by the "refresh sender icons" action.
    int WarmAll();

    // Write raw image bytes into the cache under `brandId`, choosing the file
    // extension from the bytes themselves. Returns the path ("" when the bytes
    // are not a recognisable image, which is what a captive-portal or error
    // page looks like). Public so tests and a future manual "use this icon"
    // action can fill the cache without a fetch.
    std::string Store(const std::string& brandId, const std::vector<uint8_t>& bytes);

    // Number of brand icons currently cached.
    int CachedCount() const;

private:
    std::string PathFor(const std::string& brandId) const;   // no lock
    bool RetryAllowed(const std::string& brandId) const;     // no lock
    void NoteFailure(const std::string& brandId);            // no lock

    mutable std::mutex mutex_;
    std::string        root_;
    // brand id -> resolved path ("" = nothing cached). A message list asks for
    // a badge per row, so the lookup must not stat the folder once per row;
    // Store() and SetRoot() are the only things that can invalidate it, and
    // both do.
    mutable std::map<std::string, std::string> resolved_;
    Fetcher            fetcher_;
    bool               network_ = true;
    int64_t            retrySeconds_ = 7 * 24 * 60 * 60;   // a week
};

// The image format of a byte buffer, as a file extension ("png", "ico", …);
// empty when the bytes are not an image the framework can load.
std::string SniffImageExtension(const std::vector<uint8_t>& bytes);

} // namespace UltraMail
