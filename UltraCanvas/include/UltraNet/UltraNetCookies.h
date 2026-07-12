// include/UltraNet/UltraNetCookies.h
// Session API — persistent cookies + connection reuse + default headers.
// Backed by libcurl's CURLSH share interface plus the cookie engine.
// Version: 0.2.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"
#include "UltraNetHttp.h"

#include <cstdint>
#include <string>
#include <vector>

struct UltraNetSessionOptions {
    bool persistCookies = false;
    std::string cookieJarPath;       // empty = in-memory only
    bool reuseConnections = true;
    int maxConnectionsPerHost = 0;   // 0 = libcurl default
    int maxIdleTimeMs = 0;           // 0 = libcurl default
    UltraNetHttpHeaders defaultHeaders;
    UltraNetProxyConfig proxy;       // type==None → use global

    static UltraNetSessionOptions Default() { return {}; }
};

UltraNetHandle UltraNet_CreateSession(
    const UltraNetSessionOptions& options = UltraNetSessionOptions::Default());

void UltraNet_DestroySession(UltraNetHandle session);

UltraNetResult UltraNet_SessionHttpGet(
    UltraNetHandle session,
    const std::string& url,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_SessionHttpPost(
    UltraNetHandle session,
    const std::string& url,
    const std::vector<uint8_t>& body,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_SessionLoadCookies(
    UltraNetHandle session,
    const std::string& filePath);

UltraNetResult UltraNet_SessionSaveCookies(
    UltraNetHandle session,
    const std::string& filePath);
