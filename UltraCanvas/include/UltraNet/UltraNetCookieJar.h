// include/UltraNet/UltraNetCookieJar.h
// A browser's cookie jar (RFC 6265bis) with the rules in the open: what a
// server's Set-Cookie headers store, and the Cookie header a request carries
// back - expiry (Expires, Max-Age, at most 400 days), Domain and Path,
// Secure, HttpOnly, SameSite and the __Secure- / __Host- prefixes. A request
// says whether it is for the page itself, for something of the page's own
// site, or for another site's (UltraNetCookieRequest); another site's request
// neither sends nor sets a cookie (no third-party cookies).
//
// Unlike UltraNetCookies.h - libcurl's own cookie engine inside a session,
// which keeps cookies opaque - the caller sees every cookie and decides when
// they go: a browser (UltraWeb) or any client that follows redirects itself
// and has to apply each hop's cookies. The jar does no network I/O.
//
// A site is told apart without the Public Suffix List: the last two labels of
// the host, the last three under a two-letter country code whose second level
// is generic (example.co.uk, example.com.au), and one more under well-known
// shared hosting suffixes (github.io, netlify.app, ...). A Domain attribute
// naming a whole suffix (Domain=co.uk) is refused.
//
// The jar can be kept in a file: the cookies with an expiry, one per line,
// tab-separated (session cookies are never written). Thread-safe.
// Version: 1.0.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Unrestricted is SameSite=None (a name X11 keeps as a macro).
enum class UltraNetCookieSameSite { Default, Strict, Lax, Unrestricted };

struct UltraNetCookie {
    std::string name;
    std::string value;
    std::string domain;      // lower case, no leading dot
    std::string path;        // starts with '/'
    int64_t expires = 0;     // Unix seconds; 0 = a session cookie
    int64_t created = 0;     // Unix seconds; orders cookies of one path length
    bool hostOnly = true;    // no Domain attribute: this host only, not its subdomains
    bool secure = false;
    bool httpOnly = false;
    UltraNetCookieSameSite sameSite = UltraNetCookieSameSite::Default;
};

// What a request is, for the cookies it may carry and set.
enum class UltraNetCookieRequest {
    Navigation,   // the page itself: a typed address, a link, a form
    SameSite,     // a page's style sheet or picture from the page's own site
    CrossSite     // one from another site: no cookies either way
};

class UltraNetCookieJar {
public:
    static constexpr size_t kMaxCookieBytes = 4096;            // name and value
    static constexpr size_t kMaxPerDomain = 50;
    static constexpr size_t kMaxCookies = 3000;
    static constexpr int64_t kMaxLifetimeSeconds = 400ll * 24 * 3600;

    // Kept in the UTF-8 file `path`: read now (a missing or unreadable file
    // is an empty jar), written by Flush. Empty: in memory only.
    explicit UltraNetCookieJar(std::string path = std::string());
    ~UltraNetCookieJar();
    UltraNetCookieJar(const UltraNetCookieJar&) = delete;
    UltraNetCookieJar& operator=(const UltraNetCookieJar&) = delete;

    // One Set-Cookie header of an answer from `url`. True when it was
    // stored, replaced a cookie, or deleted one (an expiry in the past).
    bool Store(const std::string& url, const std::string& setCookie, UltraNetCookieRequest request, int64_t now);
    // The Cookie header for a request to `url`; "" when none goes.
    std::string HeaderFor(const std::string& url, UltraNetCookieRequest request, int64_t now) const;

    // The cookies that have not expired, by domain then name.
    std::vector<UltraNetCookie> List(int64_t now) const;
    size_t Count() const;
    void Clear();
    // Every cookie of the site `host` belongs to (SiteOf); the count removed.
    size_t RemoveSite(const std::string& host);

    // Writes the cookies with an expiry through a temporary file and a
    // rename (the file is removed when there are none). True when nothing
    // needed writing or the write worked.
    bool Flush();
    // True once after a change that Flush has not written: the caller
    // schedules one Flush per batch of changes.
    bool TakeFlushRequest();

    // An RFC 6265 cookie date ("Wed, 21 Oct 2026 07:28:00 GMT" and the
    // forms servers really send) as Unix seconds.
    static bool ParseCookieDate(const std::string& text, int64_t& seconds);
    // The site a host belongs to (see the head of this file); the host
    // itself for an IP address or a single label.
    static std::string SiteOf(const std::string& host);
    // How a request to `url` from the page `pageUrl` counts ("" = the page
    // itself is being fetched).
    static UltraNetCookieRequest Classify(const std::string& url, const std::string& pageUrl);
    static int64_t Now();

private:
    void Load();
    void EvictLocked(int64_t now);

    mutable std::mutex mutex_;
    std::vector<UltraNetCookie> cookies_;
    std::string path_;
    bool dirty_ = false;            // a persistent cookie changed since the last Flush
    bool flushRequested_ = false;
};
