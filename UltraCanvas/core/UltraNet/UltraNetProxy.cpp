// core/UltraNet/UltraNetProxy.cpp
// Which proxy a connection goes through: the setting (a request's own, else
// the module-wide one), the operating system's proxy for System - asked once
// a minute per scheme and host - the exceptions, and the machine itself, which
// is always reached directly. Portable; the operating system is asked through
// UltraNet_DetectSystemProxyFor in OS/<Platform>/UltraNetSupport.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// The scheme and the host of `url`, both lower case: "imaps://me@Mail.Example:993/"
// is ("imaps", "mail.example"); an IPv6 host without its brackets.
void SchemeAndHost(const std::string& url, std::string& scheme, std::string& host) {
    scheme.clear();
    host.clear();
    std::size_t start = 0;
    const std::size_t sep = url.find("://");
    if (sep != std::string::npos) {
        scheme = Lower(url.substr(0, sep));
        start = sep + 3;
    }
    std::size_t end = url.find_first_of("/?#", start);
    if (end == std::string::npos) end = url.size();
    std::string authority = url.substr(start, end - start);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t close = authority.find(']');
        host = authority.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    } else {
        host = authority.substr(0, authority.find(':'));
    }
    host = Lower(host);
    while (!host.empty() && host.back() == '.') host.pop_back();   // "example.com."
}

// a.b.c.d -> true and its value
bool ParseIpv4(const std::string& s, std::uint32_t& out) {
    std::uint32_t value = 0;
    int parts = 0;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find('.', i);
        if (j == std::string::npos) j = s.size();
        const std::string part = s.substr(i, j - i);
        if (part.empty() || part.size() > 3) return false;
        int n = 0;
        for (const char c : part) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + (c - '0');
        }
        if (n > 255) return false;
        value = (value << 8) | static_cast<std::uint32_t>(n);
        ++parts;
        i = j + 1;
        if (j == s.size()) break;
    }
    if (parts != 4) return false;
    out = value;
    return true;
}

bool IsLoopback(const std::string& host) {
    if (host == "localhost" || host == "::1" || host == "0:0:0:0:0:0:0:1") return true;
    if (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0) return true;
    std::uint32_t ip = 0;
    return ParseIpv4(host, ip) && (ip >> 24) == 127;
}

// * any run, ? one character; both lower case already.
bool GlobMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// One exception entry against one host.
bool EntryMatches(std::string entry, const std::string& host) {
    entry = Lower(Trim(entry));
    if (entry.empty()) return false;
    if (entry == "*") return true;
    if (entry == "<local>") return host.find('.') == std::string::npos && host.find(':') == std::string::npos;
    // "http://example.com" and "example.com:8080" as people write them: the
    // host part is what is meant.
    if (const std::size_t sep = entry.find("://"); sep != std::string::npos) entry = entry.substr(sep + 3);
    if (!entry.empty() && entry[0] == '[') {
        const std::size_t close = entry.find(']');
        entry = entry.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    } else if (std::count(entry.begin(), entry.end(), ':') == 1) {
        entry = entry.substr(0, entry.find(':'));
    }
    while (!entry.empty() && entry.back() == '/') entry.pop_back();
    if (entry.empty()) return false;

    // An IPv4 range
    if (const std::size_t slash = entry.find('/'); slash != std::string::npos) {
        std::uint32_t net = 0, ip = 0;
        const std::string bitsText = entry.substr(slash + 1);
        if (!ParseIpv4(entry.substr(0, slash), net) || !ParseIpv4(host, ip) || bitsText.empty()) return false;
        int bits = 0;
        for (const char c : bitsText) {
            if (c < '0' || c > '9') return false;
            bits = bits * 10 + (c - '0');
            if (bits > 32) return false;
        }
        const std::uint32_t mask = bits == 0 ? 0u : (0xFFFFFFFFu << (32 - bits));
        return (net & mask) == (ip & mask);
    }
    if (entry.find_first_of("*?") != std::string::npos) {
        if (GlobMatch(entry, host)) return true;
        // "*.example.com" covers example.com itself as well
        return entry.size() > 2 && entry[0] == '*' && entry[1] == '.' && host == entry.substr(2);
    }
    if (entry[0] == '.') {
        const std::string bare = entry.substr(1);
        return host == bare ||
               (host.size() > entry.size() && host.compare(host.size() - entry.size(), entry.size(), entry) == 0);
    }
    // A name covers its subdomains (as curl's no_proxy does); an address is
    // matched as it stands.
    if (host == entry) return true;
    std::uint32_t ip = 0;
    if (ParseIpv4(entry, ip)) return false;
    const std::string suffix = "." + entry;
    return host.size() > suffix.size() &&
           host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The operating system's answers, a minute each, per scheme, host and PAC
// address. A PAC script answers per address, the rest per scheme; keying on
// the host covers both without asking the system on every request.
struct SystemCache {
    std::mutex mutex;
    struct Entry {
        UltraNetProxyConfig proxy;
        std::chrono::steady_clock::time_point at;
    };
    std::map<std::string, Entry> entries;
};

SystemCache& Cache() {
    static SystemCache cache;
    return cache;
}

constexpr std::chrono::seconds kSystemTtl{60};

UltraNetProxyConfig SystemProxyFor(const std::string& url, const std::string& scheme,
                                   const std::string& host, const std::string& autoConfigUrl) {
    const std::string key = scheme + "\n" + host + "\n" + autoConfigUrl;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(Cache().mutex);
        auto it = Cache().entries.find(key);
        if (it != Cache().entries.end() && now - it->second.at < kSystemTtl) return it->second.proxy;
    }
    // Asked outside the lock: WinHTTP or CFNetwork may fetch a PAC script.
    UltraNetProxyConfig found;
    UltraNet_DetectSystemProxyFor(url, autoConfigUrl, found);
    std::lock_guard<std::mutex> lk(Cache().mutex);
    if (Cache().entries.size() > 512) Cache().entries.clear();
    Cache().entries[key] = SystemCache::Entry{found, now};
    return found;
}

bool IsConcrete(UltraNetProxyType t) {
    return t == UltraNetProxyType::Http || t == UltraNetProxyType::Https ||
           t == UltraNetProxyType::Socks4 || t == UltraNetProxyType::Socks5;
}

} // namespace

bool UltraNet_HostMatchesNoProxy(const std::string& host, const std::vector<std::string>& patterns) {
    std::string h = Lower(Trim(host));
    if (!h.empty() && h[0] == '[') h = h.substr(1, h.find(']') == std::string::npos ? std::string::npos : h.find(']') - 1);
    while (!h.empty() && h.back() == '.') h.pop_back();
    if (h.empty()) return false;
    for (const std::string& p : patterns)
        if (EntryMatches(p, h)) return true;
    return false;
}

std::vector<std::string> UltraNet_ParseNoProxyList(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    auto flush = [&]() {
        const std::string t = Trim(current);
        if (!t.empty()) out.push_back(t);
        current.clear();
    };
    for (const char c : text) {
        if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) flush();
        else current += c;
    }
    flush();
    return out;
}

std::string UltraNet_DescribeProxy(const UltraNetProxyConfig& p) {
    if (!IsConcrete(p.type) || p.host.empty()) return {};
    std::ostringstream os;
    os.imbue(std::locale::classic());
    switch (p.type) {
        case UltraNetProxyType::Http:   os << "http://"; break;
        case UltraNetProxyType::Https:  os << "https://"; break;
        case UltraNetProxyType::Socks4: os << (p.remoteDns ? "socks4a://" : "socks4://"); break;
        case UltraNetProxyType::Socks5: os << (p.remoteDns ? "socks5h://" : "socks5://"); break;
        default: break;
    }
    const bool ipv6 = p.host.find(':') != std::string::npos && p.host[0] != '[';
    if (ipv6) os << '[' << p.host << ']';
    else os << p.host;
    if (p.port > 0) os << ':' << p.port;
    return os.str();
}

UltraNetProxyConfig UltraNet_ResolveProxy(const std::string& url, const UltraNetProxyConfig& requested) {
    const UltraNetProxyConfig direct;   // NoProxy: connect directly
    UltraNetProxyConfig choice = requested.IsEnabled() ? requested : UltraNet_GetGlobalProxy();
    if (choice.type == UltraNetProxyType::NoProxy || choice.type == UltraNetProxyType::Direct) return direct;

    std::string scheme, host;
    SchemeAndHost(url, scheme, host);
    // The machine itself is never reached through a proxy: a local model
    // server, a test server, the proxy's own port.
    if (!host.empty() && IsLoopback(host)) return direct;

    if (choice.type == UltraNetProxyType::System) {
        choice = SystemProxyFor(url, scheme, host, choice.autoConfigUrl);
        if (!IsConcrete(choice.type)) return direct;
    }
    if (!IsConcrete(choice.type) || choice.host.empty()) return direct;
    if (!host.empty() && UltraNet_HostMatchesNoProxy(host, choice.noProxyHosts)) return direct;
    if (choice.port <= 0) {
        switch (choice.type) {
            case UltraNetProxyType::Https:  choice.port = 443; break;
            case UltraNetProxyType::Socks4:
            case UltraNetProxyType::Socks5: choice.port = 1080; break;
            default:                        choice.port = 80; break;
        }
    }
    return choice;
}

void UltraNet_ForgetSystemProxy() {
    std::lock_guard<std::mutex> lk(Cache().mutex);
    Cache().entries.clear();
}
