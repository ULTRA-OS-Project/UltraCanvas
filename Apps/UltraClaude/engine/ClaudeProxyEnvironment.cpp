// Apps/UltraClaude/engine/ClaudeProxyEnvironment.cpp
// See ClaudeProxyEnvironment.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ClaudeProxyEnvironment.h"

#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <cctype>
#include <cstdio>
#include <string>

namespace UltraClaude {

namespace {

const char* const kHttpNames[] = {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy"};
const char* const kAllNames[] = {"ALL_PROXY", "all_proxy"};
const char* const kNoProxyNames[] = {"NO_PROXY", "no_proxy"};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string NameOf(const std::string& entry) {
    const std::size_t eq = entry.find('=', entry.empty() || entry[0] != '=' ? 0 : 1);
    return eq == std::string::npos ? entry : entry.substr(0, eq);
}

#ifdef ULTRACANVAS_HAS_NET
// user:password, as a URL's user information carries them.
std::string PercentEncode(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// "http://user:pass@proxy:3128" - the proxy as a URL with its sign-in.
std::string ProxyUrl(const UltraNetProxyConfig& p) {
    std::string url = UltraNet_DescribeProxy(p);
    if (url.empty() || p.credentials.username.empty()) return url;
    const std::size_t sep = url.find("://");
    if (sep == std::string::npos) return url;
    std::string userInfo = PercentEncode(p.credentials.username);
    if (!p.credentials.password.empty()) userInfo += ":" + PercentEncode(p.credentials.password);
    return url.substr(0, sep + 3) + userInfo + "@" + url.substr(sep + 3);
}

// The exceptions as NO_PROXY takes them: names and ".suffix"es, "*.x" as
// ".x"; a pattern it has no way to say ("192.168.*", "<local>") is left out.
// The machine itself first, as UltraNet always has it.
std::string NoProxyList(const std::vector<std::string>& hosts) {
    std::string list = "localhost,127.0.0.1,::1";
    for (std::string h : hosts) {
        if (h.size() > 2 && h[0] == '*' && h[1] == '.') h = h.substr(1);
        if (h.empty() || h == "<local>" || h.find_first_of("*?") != std::string::npos) continue;
        list += "," + h;
    }
    return list;
}

void SetBoth(EnvironmentChanges& out, const char* const (&names)[2], const std::string& value) {
    for (const char* n : names) out.set.emplace_back(n, value);
}

bool IsHttpKind(UltraNetProxyType t) {
    return t == UltraNetProxyType::Http || t == UltraNetProxyType::Https;
}

bool IsSocksKind(UltraNetProxyType t) {
    return t == UltraNetProxyType::Socks4 || t == UltraNetProxyType::Socks5;
}

// The proxy the CLI and its tools are to use, written in.
void WriteProxy(EnvironmentChanges& out, const UltraNetProxyConfig& p) {
    const std::string url = ProxyUrl(p);
    if (url.empty()) return;
    if (IsHttpKind(p.type)) {
        for (const char* n : kHttpNames) out.set.emplace_back(n, url);
        SetBoth(out, kNoProxyNames, NoProxyList(p.noProxyHosts));
    } else if (IsSocksKind(p.type)) {
        // The CLI takes an HTTP proxy only; the programs its tools run take
        // this one. An HTTP proxy left in the environment is not the one
        // chosen, so it goes.
        for (const char* n : kHttpNames) out.unset.emplace_back(n);
        SetBoth(out, kAllNames, url);
        SetBoth(out, kNoProxyNames, NoProxyList(p.noProxyHosts));
    }
}
#endif

bool EnvironmentHasProxy() {
    for (const char* n : kHttpNames)
        if (!UltraCanvas::GetEnvUtf8(n).empty()) return true;
    for (const char* n : kAllNames)
        if (!UltraCanvas::GetEnvUtf8(n).empty()) return true;
    return false;
}

} // namespace

#ifdef ULTRACANVAS_HAS_NET
EnvironmentChanges ProxyEnvironmentFor(const UltraNetProxyConfig& setting,
                                       const UltraNetProxyConfig& systemResolved,
                                       bool environmentHasProxy) {
    EnvironmentChanges out;
    switch (setting.type) {
        case UltraNetProxyType::NoProxy:
        case UltraNetProxyType::Direct:
            for (const char* n : kHttpNames) out.unset.emplace_back(n);
            for (const char* n : kAllNames) out.unset.emplace_back(n);
            return out;
        case UltraNetProxyType::System:
            if (!environmentHasProxy) WriteProxy(out, systemResolved);
            return out;
        default:
            WriteProxy(out, setting);
            return out;
    }
}
#endif

EnvironmentChanges ClaudeProxyEnvironment() {
#ifdef ULTRACANVAS_HAS_NET
    std::string api = UltraCanvas::GetEnvUtf8("ANTHROPIC_BASE_URL");
    if (api.empty()) api = "https://api.anthropic.com/";
    const UltraNetProxyConfig setting = UltraNet_GetGlobalProxy();
    const bool hasProxy = EnvironmentHasProxy();
    // The system is asked only when its answer is used.
    const UltraNetProxyConfig resolved =
        setting.type == UltraNetProxyType::System && !hasProxy ? UltraNet_ResolveProxy(api)
                                                                : UltraNetProxyConfig();
    return ProxyEnvironmentFor(setting, resolved, hasProxy);
#else
    return {};
#endif
}

std::vector<std::string> ApplyEnvironmentChanges(const std::vector<std::string>& current,
                                                 const EnvironmentChanges& changes,
                                                 bool ignoreCase) {
    auto same = [ignoreCase](const std::string& a, const std::string& b) {
        return ignoreCase ? Lower(a) == Lower(b) : a == b;
    };
    auto touched = [&](const std::string& name) {
        for (const auto& kv : changes.set)
            if (same(kv.first, name)) return true;
        for (const std::string& n : changes.unset)
            if (same(n, name)) return true;
        return false;
    };
    std::vector<std::string> out;
    out.reserve(current.size() + changes.set.size());
    for (const std::string& entry : current) {
        const std::string name = NameOf(entry);
        if (name.empty() || name[0] == '=' || !touched(name)) out.push_back(entry);
    }
    for (const auto& kv : changes.set) {
        // On Windows "HTTPS_PROXY" and "https_proxy" are one variable: written once.
        bool already = false;
        for (const auto& other : changes.set) {
            if (&other == &kv) break;
            if (same(other.first, kv.first)) already = true;
        }
        if (!already) out.push_back(kv.first + "=" + kv.second);
    }
    return out;
}

} // namespace UltraClaude
