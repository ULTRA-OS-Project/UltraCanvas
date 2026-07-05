// include/UltraNet/UltraNetUrl.h
// URL parsing, building, percent encoding, and query-string composition.
// Backed by libcurl's curl_url_* API (RFC 3986 compliant).
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <map>
#include <string>

struct UltraNetUrlComponents {
    std::string scheme;
    std::string username;
    std::string password;
    std::string host;
    int port = -1;                  // -1 if default for scheme
    std::string path;
    std::string query;
    std::string fragment;
    std::map<std::string, std::string> queryParams;
};

UltraNetResult UltraNet_ParseUrl(
    const std::string& url,
    UltraNetUrlComponents& outComponents);

std::string UltraNet_BuildUrl(const UltraNetUrlComponents& components);

std::string UltraNet_UrlEncode(const std::string& input);
std::string UltraNet_UrlDecode(const std::string& input);

std::string UltraNet_BuildQueryString(
    const std::map<std::string, std::string>& params);
