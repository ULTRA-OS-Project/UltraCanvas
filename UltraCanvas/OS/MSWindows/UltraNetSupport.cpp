// OS/MSWindows/UltraNetSupport.cpp
// Windows glue for UltraNet: the operating system's proxy, from the Internet
// settings the signed-in user has (Settings > Network > Proxy - the same
// WinHTTP reads for curl.exe and the system's own programs):
//
//   - "Automatically detect settings" (WPAD) and "Use setup script" (a PAC
//     address) are evaluated by WinHTTP for the address being opened - for
//     web addresses (http, https, ws, wss), which is what a PAC script answers
//     for. The PAC address an application's own settings give
//     (UltraNetProxyConfig::autoConfigUrl) is evaluated the same way.
//   - Otherwise the manual proxy: one "host:port" for every protocol, or
//     "http=...;https=...;ftp=...;socks=..." per protocol; SOCKS carries what
//     has no entry of its own (as the Internet settings do), and the bypass
//     list (with "<local>") is the exceptions.
//
// Version: 0.2.0 - per scheme, SOCKS, WPAD and PAC through WinHttpGetProxyForUrl
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

    std::string Utf16ToUtf8(const wchar_t* w) {
        if (!w) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) return {};
        std::string out(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToUtf16(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (len <= 0) return {};
        std::wstring out(static_cast<std::size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
        return out;
    }

    std::string Lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    void SplitList(const std::string& src, std::vector<std::string>& out) {
        std::string token;
        auto flush = [&]() {
            if (!token.empty()) out.push_back(token);
            token.clear();
        };
        for (const char c : src) {
            if (c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') flush();
            else token += c;
        }
        flush();
    }

    std::string SchemeOf(const std::string& url) {
        const std::size_t sep = url.find("://");
        return sep == std::string::npos ? std::string() : Lower(url.substr(0, sep));
    }

    enum class Kind { Http, Https, Ftp, Other };
    Kind KindOf(const std::string& scheme) {
        if (scheme == "http" || scheme == "ws") return Kind::Http;
        if (scheme == "https" || scheme == "wss") return Kind::Https;
        if (scheme == "ftp" || scheme == "ftps" || scheme == "sftp") return Kind::Ftp;
        return Kind::Other;
    }

    // "host:port" (or "http://host:port", "[::1]:port") into `out`.
    bool ParseHostPort(std::string text, UltraNetProxyType type, UltraNetProxyConfig& out) {
        if (const std::size_t sep = text.find("://"); sep != std::string::npos) {
            const std::string scheme = Lower(text.substr(0, sep));
            if (scheme == "https") type = UltraNetProxyType::Https;
            else if (scheme.rfind("socks", 0) == 0) type = UltraNetProxyType::Socks5;
            text = text.substr(sep + 3);
        }
        while (!text.empty() && text.back() == '/') text.pop_back();
        if (text.empty()) return false;
        out = UltraNetProxyConfig{};
        out.type = type;
        std::string portText;
        if (text[0] == '[') {
            const std::size_t close = text.find(']');
            if (close == std::string::npos) return false;
            out.host = text.substr(1, close - 1);
            if (close + 1 < text.size() && text[close + 1] == ':') portText = text.substr(close + 2);
        } else {
            const std::size_t colon = text.rfind(':');
            out.host = text.substr(0, colon);
            if (colon != std::string::npos) portText = text.substr(colon + 1);
        }
        out.port = std::atoi(portText.c_str());
        if (out.port <= 0 || out.port > 65535)
            out.port = type == UltraNetProxyType::Socks5 ? 1080 : (type == UltraNetProxyType::Https ? 443 : 80);
        return !out.host.empty();
    }

    // The manual list: "host:port" (every web protocol), or per protocol.
    bool ParseIeProxyList(const std::string& list, Kind kind, UltraNetProxyConfig& out) {
        std::string all, http, https, ftp, socks;
        std::vector<std::string> parts;
        SplitList(list, parts);
        for (const std::string& p : parts) {
            const std::size_t eq = p.find('=');
            if (eq == std::string::npos) {
                if (all.empty()) all = p;
                continue;
            }
            const std::string scheme = Lower(p.substr(0, eq));
            const std::string hostPort = p.substr(eq + 1);
            if (scheme == "http") http = hostPort;
            else if (scheme == "https") https = hostPort;
            else if (scheme == "ftp") ftp = hostPort;
            else if (scheme == "socks") socks = hostPort;
        }
        std::string chosen;
        switch (kind) {
            case Kind::Http:  chosen = !http.empty() ? http : all; break;
            case Kind::Https: chosen = !https.empty() ? https : all; break;
            case Kind::Ftp:   chosen = !ftp.empty() ? ftp : all; break;
            case Kind::Other: break;
        }
        if (!chosen.empty()) return ParseHostPort(chosen, UltraNetProxyType::Http, out);
        if (!socks.empty()) return ParseHostPort(socks, UltraNetProxyType::Socks5, out);
        return false;
    }

    // One WinHTTP session for the PAC lookups: WinHTTP keeps the script it
    // fetched with the session, so it is downloaded once, not per lookup.
    std::mutex g_pacMutex;
    HINTERNET g_pacSession = nullptr;

    // WinHTTP's answer for `url` under WPAD and/or the PAC script at `pac`.
    // False when there is no answer (no WPAD server, the script cannot be
    // fetched): the manual settings apply then. True with NoProxy for DIRECT.
    bool PacLookup(const std::string& url, const std::wstring& pac, bool autoDetect,
                   UltraNetProxyConfig& out) {
        std::lock_guard<std::mutex> lk(g_pacMutex);
        if (!g_pacSession) {
            g_pacSession = WinHttpOpen(L"UltraNet", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!g_pacSession) return false;
        }
        WINHTTP_AUTOPROXY_OPTIONS options{};
        if (!pac.empty()) {
            options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
            options.lpszAutoConfigUrl = pac.c_str();
        }
        if (autoDetect) {
            options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
            options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
        }
        options.fAutoLogonIfChallenged = TRUE;
        if (options.dwFlags == 0) return false;

        const std::wstring wideUrl = Utf8ToUtf16(url);
        WINHTTP_PROXY_INFO info{};
        if (!WinHttpGetProxyForUrl(g_pacSession, wideUrl.c_str(), &options, &info)) return false;
        out = UltraNetProxyConfig{};
        if (info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && info.lpszProxy) {
            // "host:port" or several, ';'- or space-separated: the first is
            // the one to try.
            std::vector<std::string> proxies;
            SplitList(Utf16ToUtf8(info.lpszProxy), proxies);
            if (proxies.empty() || !ParseIeProxyList(proxies.front(), Kind::Http, out))
                out = UltraNetProxyConfig{};
            if (info.lpszProxyBypass) {
                std::vector<std::string> bypass;
                SplitList(Utf16ToUtf8(info.lpszProxyBypass), bypass);
                out.noProxyHosts.insert(out.noProxyHosts.end(), bypass.begin(), bypass.end());
            }
        }
        if (info.lpszProxy) GlobalFree(info.lpszProxy);
        if (info.lpszProxyBypass) GlobalFree(info.lpszProxyBypass);
        return true;
    }

} // namespace

void UltraNet_DetectSystemProxyFor(const std::string& url, const std::string& autoConfigUrl,
                                   UltraNetProxyConfig& outProxy) {
    outProxy = UltraNetProxyConfig{};
    const Kind kind = KindOf(SchemeOf(url));

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
    const bool haveIe = WinHttpGetIEProxyConfigForCurrentUser(&ie) != FALSE;
    std::wstring pac = Utf8ToUtf16(autoConfigUrl);
    bool autoDetect = false;
    if (haveIe && autoConfigUrl.empty()) {
        if (ie.lpszAutoConfigUrl && *ie.lpszAutoConfigUrl) pac = ie.lpszAutoConfigUrl;
        autoDetect = ie.fAutoDetect != FALSE;
    }

    bool decided = false;
    if ((!pac.empty() || autoDetect) && (kind == Kind::Http || kind == Kind::Https)) {
        // WinHTTP evaluates http(s) addresses only: a WebSocket is asked
        // about as the page address it upgrades.
        std::string webUrl = url;
        const std::string scheme = SchemeOf(url);
        if (scheme == "ws") webUrl = "http" + url.substr(2);
        else if (scheme == "wss") webUrl = "https" + url.substr(3);
        decided = PacLookup(webUrl, pac, autoDetect, outProxy);
    }

    if (!decided && haveIe && ie.lpszProxy && *ie.lpszProxy) {
        if (ParseIeProxyList(Utf16ToUtf8(ie.lpszProxy), kind, outProxy)) {
            if (ie.lpszProxyBypass && *ie.lpszProxyBypass) {
                std::vector<std::string> tokens;
                SplitList(Utf16ToUtf8(ie.lpszProxyBypass), tokens);
                outProxy.noProxyHosts.insert(outProxy.noProxyHosts.end(), tokens.begin(), tokens.end());
            }
        } else {
            outProxy = UltraNetProxyConfig{};
        }
    }
    if (haveIe) {
        if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
        if (ie.lpszProxy)         GlobalFree(ie.lpszProxy);
        if (ie.lpszProxyBypass)   GlobalFree(ie.lpszProxyBypass);
    }
}

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    UltraNet_DetectSystemProxyFor("https://example.com/", std::string(), outProxy);
}

bool UltraNet_CanEvaluateProxyScript() {
    return true;
}

#endif // _WIN32 || _WIN64
