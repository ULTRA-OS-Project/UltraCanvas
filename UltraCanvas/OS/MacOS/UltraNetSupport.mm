// OS/MacOS/UltraNetSupport.mm
// macOS glue for UltraNet: the operating system's proxy, as System Settings >
// Network > Proxies has it, through CFNetwork - which also applies the
// exceptions ("Bypass proxy settings for these hosts") and says when a PAC
// script answers. A PAC script (the system's "Automatic proxy configuration",
// or the address an application's settings give, UltraNetProxyConfig::
// autoConfigUrl) is fetched directly and run by CFNetwork for the address
// being opened; it is kept five minutes.
//
// "Secure web proxy (HTTPS)" is a plain HTTP proxy that carries https://
// traffic (CONNECT), not a proxy reached over TLS - 0.1.0 took it for the
// latter. SOCKS carries what has no web proxy.
//
// Version: 0.2.0 - per address through CFNetworkCopyProxiesForURL; SOCKS; PAC
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#if defined(__APPLE__)

#import <CFNetwork/CFNetwork.h>
#import <CoreFoundation/CoreFoundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

    std::string CFStringToUtf8(CFStringRef s) {
        if (!s) return {};
        CFIndex len = CFStringGetLength(s);
        CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
        std::string out(static_cast<std::size_t>(maxBytes), '\0');
        if (!CFStringGetCString(s, out.data(), maxBytes, kCFStringEncodingUTF8)) {
            return {};
        }
        out.resize(std::char_traits<char>::length(out.c_str()));
        return out;
    }

    int CFNumberToInt(CFNumberRef n, int fallback) {
        if (!n) return fallback;
        int v = fallback;
        CFNumberGetValue(n, kCFNumberIntType, &v);
        return v;
    }

    std::string Lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // CFNetwork knows web proxies for http and https addresses only: a
    // WebSocket is asked about as the page address it upgrades.
    std::string WebUrlFor(const std::string& url) {
        const std::size_t sep = url.find("://");
        const std::string scheme = sep == std::string::npos ? std::string() : Lower(url.substr(0, sep));
        if (scheme == "ws") return "http" + url.substr(2);
        if (scheme == "wss") return "https" + url.substr(3);
        return url;
    }

    CFURLRef MakeUrl(const std::string& url) {
        return CFURLCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(url.data()),
                                    static_cast<CFIndex>(url.size()), kCFStringEncodingUTF8, nullptr);
    }

    // A PAC script's text, fetched directly (never through the proxy it is
    // about to choose) and kept five minutes.
    std::string FetchScript(const std::string& pacUrl) {
        struct Entry { std::string text; std::chrono::steady_clock::time_point at; };
        static std::mutex mutex;
        static std::map<std::string, Entry> scripts;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lk(mutex);
            auto it = scripts.find(pacUrl);
            if (it != scripts.end() && now - it->second.at < std::chrono::minutes(5)) return it->second.text;
        }
        UltraNetHttpOptions options;
        options.proxy = UltraNetProxyConfig::DirectConnection();
        options.timeoutMs = 10000;
        options.maxReceiveSize = 1024 * 1024;
        UltraNetResponse response;
        std::string text;
        if (UltraNet_HttpGet(pacUrl, response, options) && response.statusCode / 100 == 2)
            text.assign(response.body.begin(), response.body.end());
        std::lock_guard<std::mutex> lk(mutex);
        scripts[pacUrl] = Entry{text, now};
        return text;
    }

    // The first usable proxy of a CFNetwork answer (an array of proxy
    // dictionaries, best first). True when it decided - a proxy, or None
    // (direct); false when it only points at a PAC script or says nothing.
    bool FromProxyList(CFArrayRef list, const std::string& url, int depth, UltraNetProxyConfig& out);

    bool FromScript(const std::string& script, const std::string& url, int depth, UltraNetProxyConfig& out) {
        if (script.empty() || depth > 1) return false;
        CFStringRef text = CFStringCreateWithBytes(kCFAllocatorDefault,
                                                   reinterpret_cast<const UInt8*>(script.data()),
                                                   static_cast<CFIndex>(script.size()),
                                                   kCFStringEncodingUTF8, false);
        CFURLRef target = MakeUrl(url);
        bool decided = false;
        if (text && target) {
            CFErrorRef error = nullptr;
            CFArrayRef answer = CFNetworkCopyProxiesForAutoConfigurationScript(text, target, &error);
            if (answer) {
                decided = FromProxyList(answer, url, depth + 1, out);
                CFRelease(answer);
            }
            if (error) CFRelease(error);
        }
        if (text) CFRelease(text);
        if (target) CFRelease(target);
        return decided;
    }

    bool FromProxyList(CFArrayRef list, const std::string& url, int depth, UltraNetProxyConfig& out) {
        const CFIndex n = list ? CFArrayGetCount(list) : 0;
        for (CFIndex i = 0; i < n; ++i) {
            CFDictionaryRef proxy = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
            if (!proxy) continue;
            CFStringRef type = (CFStringRef)CFDictionaryGetValue(proxy, kCFProxyTypeKey);
            if (!type) continue;
            if (CFEqual(type, kCFProxyTypeNone)) {
                out = UltraNetProxyConfig{};
                return true;
            }
            if (CFEqual(type, kCFProxyTypeAutoConfigurationURL)) {
                CFURLRef pac = (CFURLRef)CFDictionaryGetValue(proxy, kCFProxyAutoConfigurationURLKey);
                const std::string pacUrl = pac ? CFStringToUtf8(CFURLGetString(pac)) : std::string();
                if (FromScript(FetchScript(pacUrl), url, depth, out)) return true;
                continue;
            }
            if (CFEqual(type, kCFProxyTypeAutoConfigurationJavaScript)) {
                CFStringRef js = (CFStringRef)CFDictionaryGetValue(proxy, kCFProxyAutoConfigurationJavaScriptKey);
                if (FromScript(CFStringToUtf8(js), url, depth, out)) return true;
                continue;
            }
            UltraNetProxyType ourType = UltraNetProxyType::Http;
            int defaultPort = 80;
            if (CFEqual(type, kCFProxyTypeHTTP) || CFEqual(type, kCFProxyTypeHTTPS)) {
                ourType = UltraNetProxyType::Http;
            } else if (CFEqual(type, kCFProxyTypeSOCKS)) {
                ourType = UltraNetProxyType::Socks5;
                defaultPort = 1080;
            } else {
                continue;   // an FTP proxy speaks FTP, not something libcurl tunnels through
            }
            const std::string host = CFStringToUtf8((CFStringRef)CFDictionaryGetValue(proxy, kCFProxyHostNameKey));
            if (host.empty()) continue;
            out = UltraNetProxyConfig{};
            out.type = ourType;
            out.host = host;
            out.port = CFNumberToInt((CFNumberRef)CFDictionaryGetValue(proxy, kCFProxyPortNumberKey), defaultPort);
            const std::string user = CFStringToUtf8((CFStringRef)CFDictionaryGetValue(proxy, kCFProxyUsernameKey));
            if (!user.empty()) {
                out.credentials.type = UltraNetAuthType::Basic;
                out.credentials.username = user;
                out.credentials.password =
                    CFStringToUtf8((CFStringRef)CFDictionaryGetValue(proxy, kCFProxyPasswordKey));
            }
            return true;
        }
        return false;
    }

} // namespace

void UltraNet_DetectSystemProxyFor(const std::string& url, const std::string& autoConfigUrl,
                                   UltraNetProxyConfig& outProxy) {
    outProxy = UltraNetProxyConfig{};
    const std::string webUrl = WebUrlFor(url);

    if (!autoConfigUrl.empty() && FromScript(FetchScript(autoConfigUrl), webUrl, 0, outProxy)) return;
    outProxy = UltraNetProxyConfig{};

    CFDictionaryRef settings = CFNetworkCopySystemProxySettings();
    if (!settings) return;
    CFURLRef target = MakeUrl(webUrl);
    if (target) {
        CFArrayRef proxies = CFNetworkCopyProxiesForURL(target, settings);
        if (proxies) {
            if (!FromProxyList(proxies, webUrl, 0, outProxy)) outProxy = UltraNetProxyConfig{};
            CFRelease(proxies);
        }
        CFRelease(target);
    }
    CFRelease(settings);
}

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    UltraNet_DetectSystemProxyFor("https://example.com/", std::string(), outProxy);
}

bool UltraNet_CanEvaluateProxyScript() {
    return true;
}

#endif // __APPLE__
