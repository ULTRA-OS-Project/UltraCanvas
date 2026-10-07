// Apps/UltraMail/engine/UltraMailSenderIconCache.h
// The sender-icon cache: one folder holding the icon of every service an inbox
// actually hears from, so a message from Facebook, LinkedIn, Claude, the
// parcel carrier or the shop down the road is recognisable before a word of it
// is read.
//
// Four properties are deliberate:
//
//  * Icons are fetched lazily, in the background. The message list asks
//    (Request) when it paints a row whose sender has no icon yet; a loader
//    thread of the cache's own downloads it and says so (SetReadyHandler).
//    Neither the sync nor the window ever waits for a download, and only the
//    senders the user actually scrolls past are asked for.
//  * Two sources, two keys. A service in the curated registry
//    (UltraMailSenderBrands) is fetched from the registry's own icon URL and
//    filed under its brand id ("facebook.png"). Any other sender's icon is
//    its website's (SiteIconKey: "site:example.com", filed as
//    sites/example.com.png): the site's home page is read for its <link
//    rel="icon">, /favicon.ico being the fallback. Website icons tell the
//    sender's web server that someone looked, so they are off unless the
//    app turns them on (SetSiteIconsEnabled - the user's preference), and
//    the app asks only for senders whose mail passed the content scan.
//  * The fetch itself is injected (SetFetcher, SetPageFetcher). The engine
//    has no network dependency, the test suite drives the cache with a fake
//    fetcher, and a build with no fetcher set simply never downloads anything.
//  * A miss is remembered. An icon that could not be fetched is not retried
//    until the retry interval has passed (a marker file), nor a second time
//    in one session, so an offline machine does not spend its time on the
//    same failures.
//
// No icon is a normal state, not an error: the badge falls back to the brand's
// monogram in the brand's own colour, or to the sender's initial.
// Version: 0.2.0 - lazy, background fetching (Request, SetReadyHandler, a
//                  loader thread), website icons (SiteIconKey,
//                  SetSiteIconsEnabled, SetPageFetcher, FindSiteIconUrls),
//                  IconForKey / Store by key
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSenderBrands.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraMail {

class SenderIconCache {
public:
    // Fetch `url` into `out`. Returns false on any failure (no network, 404,
    // a body that is not an image). Called on the loader thread (or the
    // caller's, for EnsureIconFor*) - never on the UI thread.
    using Fetcher = std::function<bool(const std::string& url, std::vector<uint8_t>& out)>;
    // Fetch a web page's HTML (its start is enough: the <head>) into `html`,
    // and the address it was finally served from, after redirects, into
    // `finalUrl` ("" when unknown). False on any failure.
    using PageFetcher = std::function<bool(const std::string& url, std::string& html,
                                           std::string& finalUrl)>;
    // Told the key (a brand id or a site key) whose icon has just been
    // stored. Runs on the loader thread: post to the UI thread from it.
    using ReadyHandler = std::function<void(const std::string& key)>;

    SenderIconCache();
    ~SenderIconCache();   // stops the loader; a download in flight is dropped
    SenderIconCache(const SenderIconCache&) = delete;
    SenderIconCache& operator=(const SenderIconCache&) = delete;

    // The cache directory (created on first write). Typically
    // <cacheDir>/sender-icons.
    void SetRoot(std::string directory);
    std::string Root() const;

    void SetFetcher(Fetcher fetcher);
    void SetPageFetcher(PageFetcher fetcher);
    void SetReadyHandler(ReadyHandler handler);
    // Turn downloading off entirely (the user's preference); lookups of
    // already-cached icons keep working. Turning it on again lets every
    // icon be asked for once more.
    void SetNetworkEnabled(bool enabled);
    bool NetworkEnabled() const;
    // Website icons for senders that are no known service (the user's
    // preference; off by default). Cached ones are shown either way.
    void SetSiteIconsEnabled(bool enabled);
    bool SiteIconsEnabled() const;

    // How long a failed fetch is remembered before it may be tried again.
    void SetRetryInterval(int64_t seconds);

    // ---- Lookup (no network) ----------------------------------------------
    // The cached icon file for an address's brand, or "" when nothing is
    // cached (or the address belongs to no known brand).
    std::string IconForAddress(const std::string& address) const;
    std::string IconForBrand(const std::string& brandId) const;
    // By key: a brand id, or a SiteIconKey.
    std::string IconForKey(const std::string& key) const;

    // ---- Lazy filling, in the background ----------------------------------
    // Ask for the icon of `key` (a brand id or a SiteIconKey) to be fetched
    // when it is not cached. Returns at once - no disk, no network - so a
    // list may call it while painting a row. Each key is asked for once a
    // session; up to three loader threads fetch, and the ready handler hears
    // of each icon stored.
    void Request(const std::string& key);
    // Keys asked for and not yet fetched (or given up on).
    std::size_t PendingCount() const;

    // ---- Lookup that may fill the cache -----------------------------------
    // As above, but fetches the icon once when it is missing. Returns the
    // cached path, or "" when there is nothing to show.
    std::string EnsureIconForAddress(const std::string& address);
    std::string EnsureIconForBrand(const SenderBrand& brand);

    // Fetch every brand's icon that is not cached yet; returns how many were
    // newly stored. Used by the "refresh sender icons" action.
    int WarmAll();

    // Write raw image bytes into the cache under `key` (a brand id or a
    // SiteIconKey), choosing the file extension from the bytes themselves.
    // Returns the path ("" when the bytes are not a recognisable image, which
    // is what a captive-portal or error page looks like, or the key is not
    // one). Public so tests and a future manual "use this icon" action can
    // fill the cache without a fetch.
    std::string Store(const std::string& key, const std::vector<uint8_t>& bytes);

    // Number of brand icons currently cached.
    int CachedCount() const;

private:
    // What the loader does for one key; built under the lock, carried out
    // outside it.
    struct FetchPlan {
        std::string iconUrl;      // a registry brand's icon
        std::string siteDomain;   // a website's: its home page first
        Fetcher     fetcher;
        PageFetcher pageFetcher;
        bool Empty() const { return !fetcher; }
    };
    struct Loader;   // the queue and the loader threads' shared state
    friend void RunIconLoader(std::shared_ptr<Loader> loader);

    std::string PathFor(const std::string& key) const;       // no lock
    void Prime() const;                                      // no lock
    bool RetryAllowed(const std::string& key) const;         // no lock
    void NoteFailure(const std::string& key);                // no lock
    FetchPlan PlanFetch(const std::string& key);             // takes the lock
    std::string Finish(const std::string& key, bool fetched,   // takes the lock
                       const std::vector<uint8_t>& bytes, ReadyHandler* ready);

    mutable std::mutex mutex_;
    std::string        root_;
    // key -> resolved path ("" = nothing cached). A message list asks for
    // a badge per row, so the lookup must not stat the folder once per row -
    // nor six times per sender domain, which on Windows meets the virus
    // scanner: the folder is listed once (Prime) and the map answers from
    // then on. Only this cache writes the folder; Store() keeps the map up
    // to date and SetRoot() starts it again.
    mutable std::map<std::string, std::string> resolved_;
    mutable bool       primed_ = false;
    std::set<std::string> requested_;   // asked for this session
    Fetcher            fetcher_;
    PageFetcher        pageFetcher_;
    ReadyHandler       ready_;
    bool               network_ = true;
    bool               siteIcons_ = false;
    int64_t            retrySeconds_ = 7 * 24 * 60 * 60;   // a week
    std::shared_ptr<Loader> loader_;
};

// The cache key of `domain`'s website icon: "site:" plus its registrable
// domain ("mail.shop.example.co.uk" -> "site:example.co.uk"). Empty for a
// domain that cannot be a website's or a file name (no dot, characters other
// than a-z, 0-9, '.' and '-').
std::string SiteIconKey(const std::string& domain);

// The icons a web page names, best first, as absolute http(s) URLs resolved
// against `pageUrl`: <link rel="icon"> (and "shortcut icon") at a useful size,
// rel="apple-touch-icon", icons of no stated size, SVG icons, tiny ones, and
// last the site's /favicon.ico. Only the page's <head> is read.
std::vector<std::string> FindSiteIconUrls(const std::string& html, const std::string& pageUrl);

// The image format of a byte buffer, as a file extension ("png", "ico", …);
// empty when the bytes are not an image the framework can load.
std::string SniffImageExtension(const std::vector<uint8_t>& bytes);

} // namespace UltraMail
