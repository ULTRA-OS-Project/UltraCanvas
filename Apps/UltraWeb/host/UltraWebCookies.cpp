// Apps/UltraWeb/host/UltraWebCookies.cpp
// The browser's cookies (UltraWebCookies.h).
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebCookies.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

// The file: {"ultrawebCookies": 1, "cookies": [{...}, ...]}.
constexpr int kFormatVersion = 1;
constexpr size_t kMaxFileBytes = 16u * 1024u * 1024u;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return std::string();
    const size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

bool IsIpAddress(const std::string& host) {
    if (host.empty()) return false;
    if (host.find(':') != std::string::npos || host.front() == '[') return true;   // IPv6
    return std::all_of(host.begin(), host.end(), [](unsigned char c) { return std::isdigit(c) || c == '.'; });
}

struct Target {
    bool ok = false;
    bool https = false;
    std::string host;
    std::string path;
};

Target TargetOf(const std::string& url) {
    Target target;
    UltraNetUrlComponents parts;
    if (!UltraNet_ParseUrl(url, parts)) return target;
    const std::string scheme = Lower(parts.scheme);
    if (scheme != "http" && scheme != "https") return target;
    target.https = scheme == "https";
    target.host = Lower(parts.host);
    if (!target.host.empty() && target.host.back() == '.') target.host.pop_back();
    target.path = parts.path.empty() ? "/" : parts.path;
    target.ok = !target.host.empty();
    return target;
}

// RFC 6265 5.1.4: the directory of the request's path.
std::string DefaultPath(const std::string& path) {
    if (path.empty() || path[0] != '/') return "/";
    const size_t slash = path.rfind('/');
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool DomainMatches(const std::string& host, const Cookie& cookie) {
    if (host == cookie.domain) return true;
    if (cookie.hostOnly || IsIpAddress(host)) return false;
    return host.size() > cookie.domain.size() &&
           host.compare(host.size() - cookie.domain.size(), cookie.domain.size(), cookie.domain) == 0 &&
           host[host.size() - cookie.domain.size() - 1] == '.';
}

// RFC 6265 5.1.4.
bool PathMatches(const std::string& requestPath, const std::string& cookiePath) {
    if (requestPath == cookiePath) return true;
    if (requestPath.compare(0, cookiePath.size(), cookiePath) != 0) return false;
    return cookiePath.back() == '/' || requestPath[cookiePath.size()] == '/';
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

const char* SameSiteName(CookieSameSite s) {
    switch (s) {
        case CookieSameSite::Strict: return "strict";
        case CookieSameSite::Lax: return "lax";
        case CookieSameSite::Unrestricted: return "none";
        default: return "";
    }
}

CookieSameSite SameSiteFrom(const std::string& text) {
    const std::string lower = Lower(text);
    if (lower == "strict") return CookieSameSite::Strict;
    if (lower == "lax") return CookieSameSite::Lax;
    if (lower == "none") return CookieSameSite::Unrestricted;
    return CookieSameSite::Default;
}

} // namespace

// ===== SITES =================================================================

std::string CookieJar::SiteOf(const std::string& rawHost) {
    std::string host = Lower(rawHost);
    if (!host.empty() && host.back() == '.') host.pop_back();
    if (IsIpAddress(host)) return host;
    std::vector<std::string> labels;
    size_t start = 0;
    while (start <= host.size()) {
        size_t dot = host.find('.', start);
        if (dot == std::string::npos) dot = host.size();
        labels.push_back(host.substr(start, dot - start));
        start = dot + 1;
    }
    if (labels.size() <= 2) return host;
    auto lastLabels = [&](size_t n) {
        std::string out;
        for (size_t i = labels.size() - n; i < labels.size(); ++i) out += (out.empty() ? "" : ".") + labels[i];
        return out;
    };
    // Shared hosting: every customer is a site of its own.
    static const char* const kSharedSuffixes[] = {
        "github.io", "gitlab.io", "blogspot.com", "herokuapp.com", "netlify.app", "vercel.app",
        "pages.dev", "workers.dev", "web.app", "firebaseapp.com", "appspot.com", "azurewebsites.net",
        "cloudfront.net", "s3.amazonaws.com", "glitch.me", "fly.dev", "onrender.com", "neocities.org",
    };
    for (const char* suffix : kSharedSuffixes) {
        const std::string s(suffix);
        const size_t parts = static_cast<size_t>(std::count(s.begin(), s.end(), '.')) + 1;
        if (labels.size() > parts && lastLabels(parts) == s) return lastLabels(parts + 1);
    }
    // example.co.uk, example.com.au: a generic second level under a country.
    static const char* const kGenericSecondLevels[] = {
        "co", "com", "net", "org", "gov", "edu", "ac", "or", "ne", "go", "gob", "mil", "nic", "ltd", "plc", "sch",
    };
    const std::string& tld = labels.back();
    const std::string& second = labels[labels.size() - 2];
    if (tld.size() == 2 && std::find(std::begin(kGenericSecondLevels), std::end(kGenericSecondLevels), second) !=
                               std::end(kGenericSecondLevels))
        return lastLabels(3);
    return lastLabels(2);
}

CookieRequest CookieJar::Classify(const std::string& url, const std::string& pageUrl) {
    if (pageUrl.empty()) return CookieRequest::Navigation;
    const Target target = TargetOf(url);
    const Target page = TargetOf(pageUrl);
    if (!target.ok || !page.ok) return CookieRequest::CrossSite;
    return SiteOf(target.host) == SiteOf(page.host) ? CookieRequest::SameSite : CookieRequest::CrossSite;
}

int64_t CookieJar::Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ===== DATES =================================================================

bool CookieJar::ParseCookieDate(const std::string& text, int64_t& seconds) {
    // RFC 6265 5.1.1: tokens between delimiters, each the first of time,
    // day of month, month and year it can be.
    auto isDelimiter = [](unsigned char c) {
        return c == 0x09 || (c >= 0x20 && c <= 0x2F) || (c >= 0x3B && c <= 0x40) || (c >= 0x5B && c <= 0x60) ||
               (c >= 0x7B && c <= 0x7E);
    };
    bool haveTime = false, haveDay = false, haveMonth = false, haveYear = false;
    int hour = 0, minute = 0, second = 0, day = 0, month = 0, year = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isDelimiter(static_cast<unsigned char>(text[i]))) ++i;
        size_t end = i;
        while (end < text.size() && !isDelimiter(static_cast<unsigned char>(text[end]))) ++end;
        if (end == i) break;
        const std::string token = text.substr(i, end - i);
        i = end;
        auto leadingDigits = [&](size_t from, size_t minCount, size_t maxCount, int& value, size_t& used) {
            size_t n = 0;
            value = 0;
            while (from + n < token.size() && std::isdigit(static_cast<unsigned char>(token[from + n])) && n < maxCount) {
                value = value * 10 + (token[from + n] - '0');
                ++n;
            }
            used = n;
            return n >= minCount;
        };
        if (!haveTime) {
            int h, m, s;
            size_t a, b, c;
            if (leadingDigits(0, 1, 2, h, a) && a < token.size() && token[a] == ':' &&
                leadingDigits(a + 1, 1, 2, m, b) && a + 1 + b < token.size() && token[a + 1 + b] == ':' &&
                leadingDigits(a + 2 + b, 1, 2, s, c)) {
                haveTime = true;
                hour = h; minute = m; second = s;
                continue;
            }
        }
        if (!haveDay) {
            int d; size_t used;
            if (leadingDigits(0, 1, 2, d, used) && (used == token.size() || !std::isdigit(static_cast<unsigned char>(token[used])))) {
                haveDay = true;
                day = d;
                continue;
            }
        }
        if (!haveMonth && token.size() >= 3) {
            static const char* const kMonths[] = { "jan", "feb", "mar", "apr", "may", "jun",
                                                   "jul", "aug", "sep", "oct", "nov", "dec" };
            const std::string three = Lower(token.substr(0, 3));
            const auto at = std::find_if(std::begin(kMonths), std::end(kMonths),
                                         [&](const char* m) { return three == m; });
            if (at != std::end(kMonths)) {
                haveMonth = true;
                month = static_cast<int>(at - std::begin(kMonths)) + 1;
                continue;
            }
        }
        if (!haveYear) {
            int y; size_t used;
            if (leadingDigits(0, 2, 4, y, used) && (used == token.size() || !std::isdigit(static_cast<unsigned char>(token[used])))) {
                haveYear = true;
                year = y;
                continue;
            }
        }
    }
    if (!haveTime || !haveDay || !haveMonth || !haveYear) return false;
    if (year >= 70 && year <= 99) year += 1900;
    else if (year >= 0 && year <= 69) year += 2000;
    if (day < 1 || day > 31 || year < 1601 || hour > 23 || minute > 59 || second > 59) return false;
    seconds = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
              hour * 3600 + minute * 60 + second;
    return true;
}

// ===== THE JAR ===============================================================

CookieJar::CookieJar(std::string path) : path_(std::move(path)) {
    if (!path_.empty()) Load();
}

CookieJar::~CookieJar() { Flush(); }

bool CookieJar::Store(const std::string& url, const std::string& setCookie, CookieRequest request, int64_t now) {
    // A picture or a style sheet from another site sets nothing.
    if (request == CookieRequest::CrossSite) return false;
    const Target target = TargetOf(url);
    if (!target.ok) return false;

    // name=value, then attributes, all separated by ';'.
    const size_t firstSemicolon = setCookie.find(';');
    const std::string pair = setCookie.substr(0, firstSemicolon);
    const size_t equals = pair.find('=');
    if (equals == std::string::npos) return false;
    Cookie cookie;
    cookie.name = Trim(pair.substr(0, equals));
    cookie.value = Trim(pair.substr(equals + 1));
    if (cookie.name.empty() || cookie.name.size() + cookie.value.size() > kMaxCookieBytes) return false;
    for (unsigned char c : cookie.name + cookie.value)
        if (c < 0x20 || c == 0x7F) return false;

    bool haveMaxAge = false, haveExpires = false, haveDomain = false;
    int64_t maxAge = 0, expires = 0;
    std::string domain, path;
    size_t at = firstSemicolon;
    while (at != std::string::npos) {
        const size_t next = setCookie.find(';', at + 1);
        const std::string attribute = setCookie.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1);
        at = next;
        const size_t eq = attribute.find('=');
        const std::string key = Lower(Trim(attribute.substr(0, eq)));
        const std::string value = eq == std::string::npos ? std::string() : Trim(attribute.substr(eq + 1));
        if (key == "expires") {
            int64_t when;
            if (ParseCookieDate(value, when)) { haveExpires = true; expires = when; }
        } else if (key == "max-age") {
            const bool negative = !value.empty() && value[0] == '-';
            const std::string digits = negative ? value.substr(1) : value;
            if (!digits.empty() && digits.size() <= 18 &&
                std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
                haveMaxAge = true;
                maxAge = std::stoll(digits) * (negative ? -1 : 1);
            }
        } else if (key == "domain") {
            std::string d = Lower(value);
            if (!d.empty() && d[0] == '.') d.erase(0, 1);
            if (!d.empty()) { haveDomain = true; domain = d; }
        } else if (key == "path") {
            path = (!value.empty() && value[0] == '/') ? value : std::string();
        } else if (key == "secure") {
            cookie.secure = true;
        } else if (key == "httponly") {
            cookie.httpOnly = true;
        } else if (key == "samesite") {
            cookie.sameSite = SameSiteFrom(value);
        }
    }

    // Domain: the host or a site above it, never a whole suffix.
    if (haveDomain) {
        if (IsIpAddress(target.host)) {
            if (domain != target.host) return false;
        } else {
            Cookie probe;
            probe.domain = domain;
            probe.hostOnly = false;
            if (!DomainMatches(target.host, probe)) return false;
            if (domain.size() < SiteOf(target.host).size()) return false;   // Domain=co.uk
        }
        cookie.domain = domain;
        cookie.hostOnly = domain == target.host && IsIpAddress(target.host);
    } else {
        cookie.domain = target.host;
        cookie.hostOnly = true;
    }
    cookie.path = path.empty() ? DefaultPath(target.path) : path;

    // Secure cookies come only over https; SameSite=None needs Secure; the
    // prefixes hold the cookie to what its name promises.
    if (cookie.secure && !target.https) return false;
    if (cookie.sameSite == CookieSameSite::Unrestricted && !cookie.secure) return false;
    if (cookie.name.rfind("__Secure-", 0) == 0 && !cookie.secure) return false;
    if (cookie.name.rfind("__Host-", 0) == 0 && (!cookie.secure || haveDomain || cookie.path != "/")) return false;

    // Max-Age wins over Expires; neither makes a session cookie.
    bool deletes = false;
    if (haveMaxAge) {
        if (maxAge <= 0) deletes = true;
        else cookie.expires = now + std::min(maxAge, kMaxLifetimeSeconds);
    } else if (haveExpires) {
        if (expires <= now) deletes = true;
        else cookie.expires = std::min(expires, now + kMaxLifetimeSeconds);
    }
    cookie.created = now;

    std::lock_guard<std::mutex> lock(mutex_);
    // http may not replace (or shadow) a Secure cookie (RFC 6265bis 5.7).
    if (!target.https) {
        for (const Cookie& existing : cookies_) {
            if (existing.secure && existing.name == cookie.name &&
                (DomainMatches(existing.domain, cookie) || DomainMatches(cookie.domain, existing)) &&
                PathMatches(cookie.path, existing.path))
                return false;
        }
    }
    auto same = std::find_if(cookies_.begin(), cookies_.end(), [&](const Cookie& c) {
        return c.name == cookie.name && c.domain == cookie.domain && c.path == cookie.path &&
               c.hostOnly == cookie.hostOnly;
    });
    bool changed = false;
    if (same != cookies_.end()) {
        const bool persistent = same->expires != 0;
        if (deletes) {
            cookies_.erase(same);
            if (persistent) dirty_ = true;
            return true;
        }
        cookie.created = same->created;
        if (persistent || cookie.expires != 0) changed = true;
        *same = std::move(cookie);
    } else {
        if (deletes) return false;
        changed = cookie.expires != 0;
        cookies_.push_back(std::move(cookie));
        EvictLocked(now);
    }
    if (changed) dirty_ = true;
    return true;
}

void CookieJar::EvictLocked(int64_t now) {
    auto expired = [now](const Cookie& c) { return c.expires != 0 && c.expires <= now; };
    if (std::any_of(cookies_.begin(), cookies_.end(), expired)) {
        cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(), expired), cookies_.end());
        dirty_ = true;
    }
    // The oldest go first: of the newest cookie's domain past its limit,
    // then of the whole jar.
    auto dropOldest = [&](auto belongs) {
        auto oldest = cookies_.end();
        for (auto it = cookies_.begin(); it != cookies_.end(); ++it)
            if (belongs(*it) && (oldest == cookies_.end() || it->created < oldest->created)) oldest = it;
        if (oldest == cookies_.end()) return;
        if (oldest->expires != 0) dirty_ = true;
        cookies_.erase(oldest);
    };
    if (!cookies_.empty()) {
        const std::string domain = cookies_.back().domain;
        auto inDomain = [&](const Cookie& c) { return c.domain == domain; };
        while (static_cast<size_t>(std::count_if(cookies_.begin(), cookies_.end(), inDomain)) > kMaxPerDomain)
            dropOldest(inDomain);
    }
    while (cookies_.size() > kMaxCookies) dropOldest([](const Cookie&) { return true; });
}

std::string CookieJar::HeaderFor(const std::string& url, CookieRequest request, int64_t now) const {
    if (request == CookieRequest::CrossSite) return std::string();
    const Target target = TargetOf(url);
    if (!target.ok) return std::string();
    std::vector<const Cookie*> chosen;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Cookie& cookie : cookies_) {
        if (cookie.expires != 0 && cookie.expires <= now) continue;
        if (cookie.secure && !target.https) continue;
        if (!DomainMatches(target.host, cookie) || !PathMatches(target.path, cookie.path)) continue;
        chosen.push_back(&cookie);
    }
    // Longer paths first, then the older cookie (RFC 6265 5.4).
    std::stable_sort(chosen.begin(), chosen.end(), [](const Cookie* a, const Cookie* b) {
        if (a->path.size() != b->path.size()) return a->path.size() > b->path.size();
        return a->created < b->created;
    });
    std::string header;
    for (const Cookie* cookie : chosen) {
        if (!header.empty()) header += "; ";
        header += cookie->name + "=" + cookie->value;
    }
    return header;
}

std::vector<Cookie> CookieJar::List(int64_t now) const {
    std::vector<Cookie> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Cookie& cookie : cookies_)
            if (cookie.expires == 0 || cookie.expires > now) out.push_back(cookie);
    }
    std::sort(out.begin(), out.end(), [](const Cookie& a, const Cookie& b) {
        if (a.domain != b.domain) return a.domain < b.domain;
        if (a.name != b.name) return a.name < b.name;
        return a.path < b.path;
    });
    return out;
}

size_t CookieJar::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cookies_.size();
}

void CookieJar::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cookies_.empty()) return;
    cookies_.clear();
    dirty_ = true;
}

size_t CookieJar::RemoveSite(const std::string& host) {
    const std::string site = SiteOf(host);
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t before = cookies_.size();
    cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(),
                                  [&](const Cookie& c) { return SiteOf(c.domain) == site; }),
                   cookies_.end());
    if (cookies_.size() != before) dirty_ = true;
    return before - cookies_.size();
}

bool CookieJar::TakeFlushRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dirty_ || flushRequested_ || path_.empty()) return false;
    flushRequested_ = true;
    return true;
}

// ===== THE FILE ==============================================================

void CookieJar::Load() {
    std::FILE* file = OpenFileUtf8(path_, "rb");
    if (!file) return;   // no file yet: an empty jar
    std::string text;
    char chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        text.append(chunk, got);
        if (text.size() > kMaxFileBytes) break;
    }
    std::fclose(file);
    if (text.size() > kMaxFileBytes) return;
    JSONParseResult parsed;
    const JSONValue doc = JSON::Parse(text, &parsed);
    if (!parsed.success || !doc.IsObject() || doc["ultrawebCookies"].GetInteger() != kFormatVersion) return;
    const int64_t now = Now();
    for (const JSONValue& item : doc["cookies"].GetElements()) {
        if (!item.IsObject()) continue;
        Cookie cookie;
        cookie.name = item["name"].GetString();
        cookie.value = item["value"].GetString();
        cookie.domain = Lower(item["domain"].GetString());
        cookie.path = item["path"].GetString("/");
        cookie.expires = item["expires"].GetInteger();
        cookie.created = item["created"].GetInteger();
        cookie.hostOnly = item["hostOnly"].GetBoolean(true);
        cookie.secure = item["secure"].GetBoolean();
        cookie.httpOnly = item["httpOnly"].GetBoolean();
        cookie.sameSite = SameSiteFrom(item["sameSite"].GetString());
        if (cookie.name.empty() || cookie.domain.empty() || cookie.path.empty() || cookie.path[0] != '/') continue;
        if (cookie.expires <= now) continue;   // expired while UltraWeb was closed
        cookies_.push_back(std::move(cookie));
    }
    EvictLocked(now);
    dirty_ = false;
}

bool CookieJar::Flush() {
    std::vector<Cookie> persistent;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        flushRequested_ = false;
        if (!dirty_ || path_.empty()) { dirty_ = false; return true; }
        dirty_ = false;
        const int64_t now = Now();
        for (const Cookie& cookie : cookies_)
            if (cookie.expires > now) persistent.push_back(cookie);
    }
    std::error_code ec;
    const std::filesystem::path target = PathFromUtf8(path_);
    if (persistent.empty()) {
        std::filesystem::remove(target, ec);
        return !ec;
    }
    JSONValue list = JSONValue::MakeArray();
    for (const Cookie& cookie : persistent) {
        JSONValue item = JSONValue::MakeObject();
        item.Set("name", cookie.name);
        item.Set("value", cookie.value);
        item.Set("domain", cookie.domain);
        item.Set("path", cookie.path);
        item.Set("expires", cookie.expires);
        item.Set("created", cookie.created);
        item.Set("hostOnly", cookie.hostOnly);
        item.Set("secure", cookie.secure);
        item.Set("httpOnly", cookie.httpOnly);
        item.Set("sameSite", SameSiteName(cookie.sameSite));
        list.Append(std::move(item));
    }
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("ultrawebCookies", kFormatVersion);
    doc.Set("cookies", std::move(list));
    const std::string text = JSON::Serialize(doc);

    std::filesystem::create_directories(target.parent_path(), ec);
    const std::string temporary = path_ + ".tmp";
    std::FILE* file = OpenFileUtf8(temporary, "wb");
    auto failed = [&]() {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;   // try again next time
        return false;
    };
    if (!file) return failed();
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::filesystem::remove(PathFromUtf8(temporary), ec);
        return failed();
    }
    std::filesystem::rename(PathFromUtf8(temporary), target, ec);
    if (ec) {
        std::filesystem::remove(PathFromUtf8(temporary), ec);
        return failed();
    }
    return true;
}

std::shared_ptr<CookieJar> SharedCookieJar() {
    static std::shared_ptr<CookieJar> jar = []() {
        const std::filesystem::path settings = UltraCanvasSettingsFolder();
        return std::make_shared<CookieJar>(settings.empty() ? std::string()
                                                            : PathToUtf8(settings / "UltraWeb" / "cookies.json"));
    }();
    return jar;
}

} // namespace UltraWeb
