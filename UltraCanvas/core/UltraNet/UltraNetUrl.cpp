// core/UltraNet/UltraNetUrl.cpp
// URL utilities backed by libcurl's curl_url_* API.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetUrl.h"

#include <curl/curl.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <utility>

namespace {

    struct CurlUrlDeleter {
        void operator()(CURLU* u) const { if (u) curl_url_cleanup(u); }
    };
    using CurlUrlPtr = std::unique_ptr<CURLU, CurlUrlDeleter>;

    struct CurlStringDeleter {
        void operator()(char* s) const { if (s) curl_free(s); }
    };
    using CurlStringPtr = std::unique_ptr<char, CurlStringDeleter>;

    // Returns the curl-owned string for `part` or empty if missing.
    std::string GetUrlPart(CURLU* u, CURLUPart part) {
        char* raw = nullptr;
        if (curl_url_get(u, part, &raw, 0) != CURLUE_OK || !raw) return {};
        CurlStringPtr owned(raw);
        return std::string(raw);
    }

    void ParseQuery(const std::string& query,
                    std::map<std::string, std::string>& out) {
        if (query.empty()) return;
        std::size_t i = 0;
        while (i < query.size()) {
            std::size_t amp = query.find('&', i);
            if (amp == std::string::npos) amp = query.size();
            std::string pair = query.substr(i, amp - i);
            std::size_t eq = pair.find('=');
            std::string k, v;
            if (eq == std::string::npos) { k = pair; }
            else { k = pair.substr(0, eq); v = pair.substr(eq + 1); }
            out[UltraNet_UrlDecode(k)] = UltraNet_UrlDecode(v);
            i = amp + 1;
        }
    }
}

UltraNetResult UltraNet_ParseUrl(const std::string& url,
                                 UltraNetUrlComponents& out) {
    out = {};
    if (url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "empty URL");
    }
    CurlUrlPtr u(curl_url());
    if (!u) {
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_url() failed");
    }
    CURLUcode rc = curl_url_set(u.get(), CURLUPART_URL, url.c_str(), 0);
    if (rc != CURLUE_OK) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "invalid URL");
    }
    out.scheme   = GetUrlPart(u.get(), CURLUPART_SCHEME);
    out.username = GetUrlPart(u.get(), CURLUPART_USER);
    out.password = GetUrlPart(u.get(), CURLUPART_PASSWORD);
    out.host     = GetUrlPart(u.get(), CURLUPART_HOST);
    out.path     = GetUrlPart(u.get(), CURLUPART_PATH);
    out.query    = GetUrlPart(u.get(), CURLUPART_QUERY);
    out.fragment = GetUrlPart(u.get(), CURLUPART_FRAGMENT);

    const std::string portStr = GetUrlPart(u.get(), CURLUPART_PORT);
    out.port = portStr.empty() ? -1 : std::atoi(portStr.c_str());

    ParseQuery(out.query, out.queryParams);
    return UltraNetResult::Ok();
}

std::string UltraNet_BuildUrl(const UltraNetUrlComponents& c) {
    CurlUrlPtr u(curl_url());
    if (!u) return {};

    auto setPart = [&](CURLUPart part, const std::string& v) {
        if (!v.empty()) curl_url_set(u.get(), part, v.c_str(), 0);
    };
    setPart(CURLUPART_SCHEME, c.scheme);
    setPart(CURLUPART_USER, c.username);
    setPart(CURLUPART_PASSWORD, c.password);
    setPart(CURLUPART_HOST, c.host);
    if (c.port >= 0) {
        std::string p = std::to_string(c.port);
        curl_url_set(u.get(), CURLUPART_PORT, p.c_str(), 0);
    }
    setPart(CURLUPART_PATH, c.path);

    std::string query = c.query.empty() && !c.queryParams.empty()
                            ? UltraNet_BuildQueryString(c.queryParams)
                            : c.query;
    setPart(CURLUPART_QUERY, query);
    setPart(CURLUPART_FRAGMENT, c.fragment);

    char* raw = nullptr;
    if (curl_url_get(u.get(), CURLUPART_URL, &raw, 0) != CURLUE_OK || !raw) return {};
    CurlStringPtr owned(raw);
    return std::string(raw);
}

std::string UltraNet_UrlEncode(const std::string& input) {
    if (input.empty()) return {};
    CURL* easy = curl_easy_init();
    if (!easy) return {};
    char* raw = curl_easy_escape(easy, input.c_str(),
                                 static_cast<int>(input.size()));
    std::string out;
    if (raw) {
        out = raw;
        curl_free(raw);
    }
    curl_easy_cleanup(easy);
    return out;
}

std::string UltraNet_UrlDecode(const std::string& input) {
    if (input.empty()) return {};
    CURL* easy = curl_easy_init();
    if (!easy) return {};
    int outLen = 0;
    char* raw = curl_easy_unescape(easy, input.c_str(),
                                   static_cast<int>(input.size()), &outLen);
    std::string out;
    if (raw) {
        out.assign(raw, raw + outLen);
        curl_free(raw);
    }
    curl_easy_cleanup(easy);
    return out;
}

std::string UltraNet_BuildQueryString(
    const std::map<std::string, std::string>& params) {
    std::ostringstream os;
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) os << '&';
        os << UltraNet_UrlEncode(k) << '=' << UltraNet_UrlEncode(v);
        first = false;
    }
    return os.str();
}
