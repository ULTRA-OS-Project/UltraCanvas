// OS/Linux/UltraNetSupport.cpp
// Linux glue for UltraNet: system-proxy detection from environment variables
// (http_proxy, https_proxy, no_proxy — both upper and lower case forms).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#ifdef __linux__

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

    const char* GetEnvEither(const char* lower, const char* upper) {
        const char* v = std::getenv(lower);
        if (v && *v) return v;
        v = std::getenv(upper);
        return (v && *v) ? v : nullptr;
    }

    void SplitCsv(const std::string& src, std::vector<std::string>& out) {
        std::size_t i = 0;
        while (i < src.size()) {
            std::size_t j = src.find(',', i);
            if (j == std::string::npos) j = src.size();
            std::string token = src.substr(i, j - i);
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
                token.erase(token.begin());
            }
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
                token.pop_back();
            }
            if (!token.empty()) out.push_back(token);
            i = j + 1;
        }
    }

    // Parses scheme://[user:pass@]host[:port] into the proxy config.
    void ParseProxyUrl(const std::string& url, UltraNetProxyConfig& out) {
        std::string rest = url;
        std::size_t schemeEnd = rest.find("://");
        std::string scheme;
        if (schemeEnd != std::string::npos) {
            scheme = rest.substr(0, schemeEnd);
            rest   = rest.substr(schemeEnd + 3);
        }
        if (scheme == "socks4")        out.type = UltraNetProxyType::Socks4;
        else if (scheme == "socks5")   out.type = UltraNetProxyType::Socks5;
        else if (scheme == "https")    out.type = UltraNetProxyType::Https;
        else                           out.type = UltraNetProxyType::Http;

        std::size_t at = rest.find('@');
        if (at != std::string::npos) {
            std::string userinfo = rest.substr(0, at);
            rest = rest.substr(at + 1);
            std::size_t colon = userinfo.find(':');
            if (colon != std::string::npos) {
                out.credentials.username = userinfo.substr(0, colon);
                out.credentials.password = userinfo.substr(colon + 1);
            } else {
                out.credentials.username = userinfo;
            }
            out.credentials.type = UltraNetAuthType::Basic;
        }
        std::size_t pathStart = rest.find('/');
        if (pathStart != std::string::npos) rest = rest.substr(0, pathStart);
        std::size_t portColon = rest.find(':');
        if (portColon != std::string::npos) {
            out.host = rest.substr(0, portColon);
            out.port = std::atoi(rest.substr(portColon + 1).c_str());
        } else {
            out.host = rest;
            out.port = (out.type == UltraNetProxyType::Https) ? 443 : 80;
        }
    }

} // namespace

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    outProxy = UltraNetProxyConfig{};
    const char* https = GetEnvEither("https_proxy", "HTTPS_PROXY");
    const char* http  = GetEnvEither("http_proxy",  "HTTP_PROXY");
    const char* noenv = GetEnvEither("no_proxy",    "NO_PROXY");
    const char* chosen = https ? https : http;
    if (!chosen) return;

    ParseProxyUrl(chosen, outProxy);
    if (noenv) SplitCsv(noenv, outProxy.noProxyHosts);
}

#endif // __linux__
