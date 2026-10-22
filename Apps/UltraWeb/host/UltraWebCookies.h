// Apps/UltraWeb/host/UltraWebCookies.h
// The browser's cookies (RFC 6265bis): what a server's Set-Cookie headers
// store, and the Cookie header a request carries back - expiry (Expires,
// Max-Age, at most 400 days), Domain and Path, Secure, HttpOnly, SameSite and
// the __Secure- / __Host- prefixes. Cookies go only to the site whose page is
// on show: a picture or a style sheet from another site neither sends nor
// sets one (no third-party cookies). Cookies with an expiry are kept across
// runs in one JSON file; session cookies end when UltraWeb quits.
//
// A site is told apart without the Public Suffix List: the last two labels of
// the host, the last three under a two-letter country code whose second level
// is generic (example.co.uk, example.com.au), and one more under the
// well-known shared hosting suffixes (github.io, netlify.app, ...). A Domain
// attribute naming a whole suffix (Domain=co.uk) is refused.
//
// Thread-safe: the page services store answers on the UI thread, downloads
// read the header from it too, and the lock makes any thread safe.
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraWeb {

// Unrestricted is SameSite=None (a name X11 keeps as a macro).
enum class CookieSameSite { Default, Strict, Lax, Unrestricted };

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;      // lower case, no leading dot
    std::string path;        // starts with '/'
    int64_t expires = 0;     // Unix seconds; 0 = a session cookie
    int64_t created = 0;     // Unix seconds; orders cookies of one path length
    bool hostOnly = true;    // no Domain attribute: this host only, not its subdomains
    bool secure = false;
    bool httpOnly = false;
    CookieSameSite sameSite = CookieSameSite::Default;
};

// What a request is, for the cookies it may carry and set.
enum class CookieRequest {
    Navigation,   // the page itself: a typed address, a link, a form
    SameSite,     // a page's style sheet or picture from the page's own site
    CrossSite     // one from another site: no cookies either way
};

class CookieJar {
public:
    static constexpr size_t kMaxCookieBytes = 4096;            // name and value
    static constexpr size_t kMaxPerDomain = 50;
    static constexpr size_t kMaxCookies = 3000;
    static constexpr int64_t kMaxLifetimeSeconds = 400ll * 24 * 3600;

    // Kept in the UTF-8 file `path`: read now (a missing or unreadable file
    // is an empty jar), written by Flush. Empty: in memory only.
    explicit CookieJar(std::string path = std::string());
    ~CookieJar();
    CookieJar(const CookieJar&) = delete;
    CookieJar& operator=(const CookieJar&) = delete;

    // One Set-Cookie header of an answer from `url`. True when it was
    // stored, replaced a cookie, or deleted one (an expiry in the past).
    bool Store(const std::string& url, const std::string& setCookie, CookieRequest request, int64_t now);
    // The Cookie header for a request to `url`; "" when none goes.
    std::string HeaderFor(const std::string& url, CookieRequest request, int64_t now) const;

    // The cookies that have not expired, by domain then name.
    std::vector<Cookie> List(int64_t now) const;
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
    static CookieRequest Classify(const std::string& url, const std::string& pageUrl);
    static int64_t Now();

private:
    void Load();
    void EvictLocked(int64_t now);

    mutable std::mutex mutex_;
    std::vector<Cookie> cookies_;
    std::string path_;
    bool dirty_ = false;            // a persistent cookie changed since the last Flush
    bool flushRequested_ = false;
};

// UltraWeb's jar, <settings folder>/UltraWeb/cookies.json, shared by every
// tab and window of the process (in memory only without a settings folder).
std::shared_ptr<CookieJar> SharedCookieJar();

} // namespace UltraWeb
