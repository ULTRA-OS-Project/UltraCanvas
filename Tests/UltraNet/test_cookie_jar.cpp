// Tests/UltraNet/test_cookie_jar.cpp
// UltraNetCookieJar: Set-Cookie parsing and the rules for what is stored and
// what is sent - domain, path, Secure, SameSite, the __Secure- / __Host-
// prefixes, expiry, Max-Age over Expires, no third-party cookies, a Domain
// naming a whole suffix refused - the cookie dates servers send, the sites
// hosts belong to, the limits, and the file the cookies with an expiry are
// kept in. (The page services that use it: Tests/UltraWebCookiesTest.cpp.)
#include "test_framework.h"

#include <UltraNet/UltraNetCookieJar.h>

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <string>
#include <system_error>

using namespace UltraCanvas;

namespace {

void Check(bool cond, const std::string& what) {
    if (!cond) ultranet_test::RecordFail(__FILE__, __LINE__, what);
}

} // namespace

// 2026-10-22 12:00:00 UTC.
constexpr int64_t kNow = 1792670400;

TEST(cookie_jar_dates) {
    // cookie dates
    int64_t t = 0;
    Check(UltraNetCookieJar::ParseCookieDate("Thu, 22 Oct 2026 12:00:00 GMT", t) && t == kNow, "RFC 1123 date");
    Check(UltraNetCookieJar::ParseCookieDate("Thursday, 22-Oct-26 12:00:00 GMT", t) && t == kNow, "RFC 850 date, two-digit year");
    Check(UltraNetCookieJar::ParseCookieDate("Thu Oct 22 12:00:00 2026", t) && t == kNow, "asctime date");
    Check(UltraNetCookieJar::ParseCookieDate("22 oct 2026 12:00:00", t) && t == kNow, "no weekday, lower-case month");
    Check(UltraNetCookieJar::ParseCookieDate("Thu, 01 Jan 1970 00:00:00 GMT", t) && t == 0, "the epoch");
    Check(!UltraNetCookieJar::ParseCookieDate("tomorrow", t), "not a date");
    Check(!UltraNetCookieJar::ParseCookieDate("Thu, 32 Oct 2026 12:00:00 GMT", t), "day 32 refused");
    Check(!UltraNetCookieJar::ParseCookieDate("Thu, 22 Oct 2026 GMT", t), "no time refused");
}

TEST(cookie_jar_sites) {
    // sites
    Check(UltraNetCookieJar::SiteOf("www.example.com") == "example.com", "www.example.com -> example.com");
    Check(UltraNetCookieJar::SiteOf("a.b.example.co.uk") == "example.co.uk", "a.b.example.co.uk -> example.co.uk");
    Check(UltraNetCookieJar::SiteOf("shop.example.com.au") == "example.com.au", "shop.example.com.au -> example.com.au");
    Check(UltraNetCookieJar::SiteOf("alice.github.io") == "alice.github.io", "alice.github.io is a site of its own");
    Check(UltraNetCookieJar::SiteOf("192.168.1.10") == "192.168.1.10", "an IP address is its own site");
    Check(UltraNetCookieJar::SiteOf("localhost") == "localhost", "localhost");
    Check(UltraNetCookieJar::Classify("https://cdn.example.com/a.png", "https://www.example.com/") == UltraNetCookieRequest::SameSite,
          "a picture from the page's own site is same-site");
    Check(UltraNetCookieJar::Classify("https://tracker.net/p.gif", "https://www.example.com/") == UltraNetCookieRequest::CrossSite,
          "a picture from another site is cross-site");
    Check(UltraNetCookieJar::Classify("https://www.example.com/", "") == UltraNetCookieRequest::Navigation, "the page itself");
}

TEST(cookie_jar_store_and_send) {
    // storing and sending
    UltraNetCookieJar jar;
    const auto nav = UltraNetCookieRequest::Navigation;
    Check(jar.Store("https://www.example.com/account/login", "sid=1; Path=/; Secure; HttpOnly", nav, kNow), "a session cookie");
    Check(jar.HeaderFor("https://www.example.com/", nav, kNow) == "sid=1", "sent back to its host");
    Check(jar.HeaderFor("http://www.example.com/", nav, kNow).empty(), "a Secure cookie never over http");
    Check(jar.HeaderFor("https://shop.example.com/", nav, kNow).empty(), "a host-only cookie not to a sibling");

    // Default path: the request's directory.
    jar.Store("https://www.example.com/docs/page.html", "lang=en", nav, kNow);
    Check(jar.HeaderFor("https://www.example.com/docs/other", nav, kNow).find("lang=en") != std::string::npos,
          "the default path covers its directory");
    Check(jar.HeaderFor("https://www.example.com/docsx", nav, kNow).find("lang=en") == std::string::npos,
          "/docs does not match /docsx");
    Check(jar.HeaderFor("https://www.example.com/docs/a", nav, kNow) == "lang=en; sid=1", "the longer path first");

    // Domain: to the subdomains too, but never a whole suffix or another site.
    Check(jar.Store("https://www.example.com/", "pref=dark; Domain=.example.com; Max-Age=3600", nav, kNow), "Domain=.example.com");
    Check(jar.HeaderFor("http://shop.example.com/", nav, kNow) == "pref=dark", "sent to a subdomain");
    Check(!jar.Store("https://www.example.com/", "bad=1; Domain=com", nav, kNow), "Domain=com refused");
    Check(!jar.Store("https://www.example.co.uk/", "bad=1; Domain=co.uk", nav, kNow), "Domain=co.uk refused");
    Check(!jar.Store("https://www.example.com/", "bad=1; Domain=other.com", nav, kNow), "another site's Domain refused");

    // Third-party: a cross-site picture neither sets nor sends.
    Check(!jar.Store("https://tracker.net/p.gif", "id=42; Max-Age=86400", UltraNetCookieRequest::CrossSite, kNow),
          "a cross-site answer sets nothing");
    jar.Store("https://tracker.net/", "id=42; Max-Age=86400; SameSite=None; Secure", nav, kNow);
    Check(jar.HeaderFor("https://tracker.net/p.gif", UltraNetCookieRequest::CrossSite, kNow).empty(),
          "a cross-site request sends nothing, SameSite=None or not");
    Check(jar.HeaderFor("https://tracker.net/p.gif", UltraNetCookieRequest::SameSite, kNow) == "id=42", "same-site sends it");

    // Secure rules and the prefixes.
    Check(!jar.Store("http://www.example.com/", "s=1; Secure", nav, kNow), "Secure set over http refused");
    Check(!jar.Store("https://www.example.com/", "n=1; SameSite=None", nav, kNow), "SameSite=None without Secure refused");
    Check(!jar.Store("http://www.example.com/", "sid=2; Path=/", nav, kNow), "http cannot overwrite a Secure cookie");
    Check(!jar.Store("https://www.example.com/", "__Secure-a=1", nav, kNow), "__Secure- without Secure refused");
    Check(!jar.Store("https://www.example.com/", "__Host-a=1; Secure; Path=/; Domain=example.com", nav, kNow),
          "__Host- with a Domain refused");
    Check(jar.Store("https://www.example.com/", "__Host-a=1; Secure; Path=/", nav, kNow), "__Host- as it should be");

    // Expiry: Max-Age wins over Expires; the past deletes; 400 days at most.
    Check(jar.Store("https://www.example.com/", "t=1; Expires=Thu, 01 Jan 2099 00:00:00 GMT; Max-Age=60", nav, kNow),
          "Max-Age with Expires");
    Check(jar.HeaderFor("https://www.example.com/", nav, kNow + 61).find("t=1") == std::string::npos,
          "gone after its Max-Age, whatever Expires said");
    jar.Store("https://www.example.com/", "long=1; Max-Age=999999999", nav, kNow);
    for (const UltraNetCookie& c : jar.List(kNow))
        if (c.name == "long") Check(c.expires == kNow + UltraNetCookieJar::kMaxLifetimeSeconds, "capped at 400 days");
    Check(jar.Store("https://www.example.com/", "lang=x; Path=/docs; Max-Age=0", nav, kNow), "Max-Age=0 deletes");
    Check(jar.HeaderFor("https://www.example.com/docs/a", nav, kNow).find("lang=") == std::string::npos, "deleted");
    jar.Store("https://www.example.com/", "pref=light; Domain=example.com; Max-Age=3600", nav, kNow);
    Check(jar.HeaderFor("https://shop.example.com/", nav, kNow) == "pref=light", "a cookie replaced by name, domain and path");

    // Malformed.
    Check(!jar.Store("https://www.example.com/", "novalue", nav, kNow), "no '=' refused");
    Check(!jar.Store("https://www.example.com/", "=x", nav, kNow), "no name refused");
    Check(!jar.Store("https://www.example.com/", std::string("a=") + std::string(5000, 'x'), nav, kNow), "over 4 KB refused");

    Check(jar.RemoveSite("www.example.com") > 0 && jar.HeaderFor("https://www.example.com/", nav, kNow).empty(),
          "RemoveSite drops a site's cookies");
    Check(jar.HeaderFor("https://tracker.net/", nav, kNow) == "id=42", "and only that site's");
    jar.Clear();
    Check(jar.Count() == 0, "Clear empties the jar");
}

TEST(cookie_jar_limits) {
    // limits
    UltraNetCookieJar jar;
    for (int i = 0; i < 60; ++i)
        jar.Store("https://a.example/", "c" + std::to_string(i) + "=1", UltraNetCookieRequest::Navigation, kNow + i);
    Check(jar.Count() == UltraNetCookieJar::kMaxPerDomain, "50 cookies per domain");
    const std::string header = jar.HeaderFor("https://a.example/", UltraNetCookieRequest::Navigation, kNow + 100);
    Check(header.find("c0=1") == std::string::npos && header.find("c59=1") != std::string::npos, "the oldest went first");
}

TEST(cookie_jar_kept_in_a_file) {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "UltraNetCookieJarTest";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    // kept across runs
    const std::string path = PathToUtf8(dir / "cookies.json");
    const int64_t now = UltraNetCookieJar::Now();
    {
        UltraNetCookieJar jar(path);
        jar.Store("https://www.example.com/", "keep=1; Max-Age=3600", UltraNetCookieRequest::Navigation, now);
        jar.Store("https://www.example.com/", "session=1", UltraNetCookieRequest::Navigation, now);
        Check(jar.TakeFlushRequest(), "a change asks for a flush");
        Check(!jar.TakeFlushRequest(), "once per batch");
        Check(jar.Flush(), "written");
    }
    UltraNetCookieJar again(path);
    const std::string header = again.HeaderFor("https://www.example.com/", UltraNetCookieRequest::Navigation, now);
    Check(header == "keep=1", "the persistent cookie came back, the session one did not");
    again.Clear();
    Check(again.Flush() && !std::filesystem::exists(PathFromUtf8(path)), "an empty jar removes its file");
    std::filesystem::remove_all(dir, ec);
}

