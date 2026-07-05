// OS/MacOS/UltraNetSupport.mm
// macOS glue for UltraNet: system-proxy detection via CFNetwork.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#if defined(__APPLE__)

#import <CFNetwork/CFNetwork.h>
#import <CoreFoundation/CoreFoundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

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

} // namespace

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    outProxy = UltraNetProxyConfig{};

    CFDictionaryRef proxies = CFNetworkCopySystemProxySettings();
    if (!proxies) return;

    auto getString = [&](CFStringRef key) -> std::string {
        return CFStringToUtf8((CFStringRef)CFDictionaryGetValue(proxies, key));
    };
    auto getInt = [&](CFStringRef key, int fb) -> int {
        return CFNumberToInt((CFNumberRef)CFDictionaryGetValue(proxies, key), fb);
    };

    // Prefer HTTPS, fall back to HTTP.
    CFNumberRef httpsEnabled =
        (CFNumberRef)CFDictionaryGetValue(proxies, kCFNetworkProxiesHTTPSEnable);
    CFNumberRef httpEnabled  =
        (CFNumberRef)CFDictionaryGetValue(proxies, kCFNetworkProxiesHTTPEnable);

    if (CFNumberToInt(httpsEnabled, 0) != 0) {
        outProxy.type = UltraNetProxyType::Https;
        outProxy.host = getString(kCFNetworkProxiesHTTPSProxy);
        outProxy.port = getInt(kCFNetworkProxiesHTTPSPort, 443);
    } else if (CFNumberToInt(httpEnabled, 0) != 0) {
        outProxy.type = UltraNetProxyType::Http;
        outProxy.host = getString(kCFNetworkProxiesHTTPProxy);
        outProxy.port = getInt(kCFNetworkProxiesHTTPPort, 80);
    }

    CFArrayRef exceptions =
        (CFArrayRef)CFDictionaryGetValue(proxies, kCFNetworkProxiesExceptionsList);
    if (exceptions) {
        CFIndex n = CFArrayGetCount(exceptions);
        for (CFIndex i = 0; i < n; ++i) {
            CFStringRef s = (CFStringRef)CFArrayGetValueAtIndex(exceptions, i);
            std::string token = CFStringToUtf8(s);
            if (!token.empty()) outProxy.noProxyHosts.push_back(token);
        }
    }

    CFRelease(proxies);
    if (outProxy.host.empty()) outProxy = UltraNetProxyConfig{};
}

#endif // __APPLE__
