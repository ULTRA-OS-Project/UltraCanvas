// OS/MSWindows/UltraNetSupport.cpp
// Windows glue for UltraNet: system-proxy detection via WinHTTP's IE proxy
// configuration (mirrors what curl.exe does on Windows).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>

#include <cstdlib>
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

    void SplitList(const std::string& src, char sep, std::vector<std::string>& out) {
        std::size_t i = 0;
        while (i < src.size()) {
            std::size_t j = src.find(sep, i);
            if (j == std::string::npos) j = src.size();
            std::string token = src.substr(i, j - i);
            while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
                token.erase(token.begin());
            }
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
                token.pop_back();
            }
            if (!token.empty()) out.push_back(token);
            i = j + 1;
        }
    }

    // IE's proxy server string can be either "host:port" (apply to all) or
    // "scheme=host:port;scheme=host:port" entries. Prefer the https entry.
    void ParseIeProxyList(const std::string& list, UltraNetProxyConfig& out) {
        std::string chosen;
        bool isExplicitlyHttps = false;
        std::vector<std::string> parts;
        SplitList(list, ';', parts);
        for (const auto& p : parts) {
            std::size_t eq = p.find('=');
            if (eq == std::string::npos) {
                if (chosen.empty()) chosen = p;     // bare host:port = catch-all
            } else {
                std::string scheme = p.substr(0, eq);
                std::string hostPort = p.substr(eq + 1);
                if (scheme == "https") { chosen = hostPort; isExplicitlyHttps = true; break; }
                if (scheme == "http"  && chosen.empty()) chosen = hostPort;
            }
        }
        if (chosen.empty()) return;

        out.type = isExplicitlyHttps ? UltraNetProxyType::Https : UltraNetProxyType::Http;
        std::size_t colon = chosen.find(':');
        if (colon != std::string::npos) {
            out.host = chosen.substr(0, colon);
            out.port = std::atoi(chosen.substr(colon + 1).c_str());
        } else {
            out.host = chosen;
            out.port = isExplicitlyHttps ? 443 : 80;
        }
    }

} // namespace

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    outProxy = UltraNetProxyConfig{};

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{};
    if (WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
        if (ie.lpszProxy && *ie.lpszProxy) {
            std::string list = Utf16ToUtf8(ie.lpszProxy);
            ParseIeProxyList(list, outProxy);
        }
        if (ie.lpszProxyBypass && *ie.lpszProxyBypass) {
            std::string bypass = Utf16ToUtf8(ie.lpszProxyBypass);
            // bypass list is separated by ';' or whitespace
            std::vector<std::string> tokens;
            SplitList(bypass, ';', tokens);
            outProxy.noProxyHosts.insert(outProxy.noProxyHosts.end(),
                                         tokens.begin(), tokens.end());
        }
        if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
        if (ie.lpszProxy)         GlobalFree(ie.lpszProxy);
        if (ie.lpszProxyBypass)   GlobalFree(ie.lpszProxyBypass);
    }
}

#endif // _WIN32 || _WIN64
