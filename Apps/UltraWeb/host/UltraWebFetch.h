// Apps/UltraWeb/host/UltraWebFetch.h
// An app's fetch (uc_fetch in UltraWeb/guest/ultraweb.h): the request and
// response the bridge hands to the network, the rules a browser applies to
// them - origins, http from an https app, "simple" cross-origin requests,
// CORS, which response headers an app may read - and the network service
// that sends them through UltraNet.
//
// The rules are kept apart from the network so the bridge's tests can run
// them against a fake service, with no network at all.
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UltraWeb {

// Response headers as received: name, value. Names compare in any case.
using FetchHeaders = std::vector<std::pair<std::string, std::string>>;

struct FetchRequest {
    std::string url;                 // absolute, http or https
    std::string method;              // "GET", "POST", ...
    std::vector<uint8_t> body;
    std::string contentType;         // empty: no body
    std::string origin;              // the app's origin, or "null" when it has none
    bool sameOrigin = false;         // url is on the app's own origin
    bool sendOrigin = false;         // send an Origin header (cross-origin, or not GET/HEAD)
};

struct FetchResponse {
    int status = 0;                  // the HTTP status; 0 when no answer came
    std::string error;               // why the answer is missing or incomplete; empty when whole
    bool tooLarge = false;           // the body went over FetchRules::kMaxResponseBytes
    std::string finalUrl;            // after redirects; empty = the request's url
    FetchHeaders headers;
    std::vector<uint8_t> body;
};

// Starts a request. `done` runs once, on the UI thread - or never, once the
// returned function (which cancels the request) has run.
using FetchService = std::function<std::function<void()>(const FetchRequest& request,
                                                         std::function<void(FetchResponse)> done)>;

namespace FetchRules {

constexpr size_t kMaxResponseBytes = 16u << 20;
constexpr size_t kMaxRequestBytes = 4u << 20;
constexpr size_t kMaxUrlBytes = 8192;
constexpr size_t kMaxContentTypeBytes = 256;
constexpr int kTimeoutMs = 30000;
constexpr int kMaxRedirects = 5;

// "https://example.org:8443" for an http(s) URL - scheme and host in lower
// case, a default port left out - and "" for anything else, which has no
// origin (a file, about:demo).
std::string OriginOf(const std::string& url);

// Builds the request an app asked for, or refuses it before anything is
// sent. `appAddress` is the address the app was loaded from; a relative
// `reference` is resolved against it. `method` is a UC_METHOD_*. Returns
// UC_OK, or a UC_ERR_* with `reason` saying why, for the app's console.
int32_t Prepare(const std::string& appAddress, const std::string& reference, uint32_t method,
                std::vector<uint8_t> body, std::string contentType, FetchRequest& out, std::string& reason);

// Decides what of a finished response the app may see: UC_OK, with the
// headers it may read in `visible` (names in lower case), or UC_ERR_NETWORK,
// UC_ERR_LIMIT or UC_ERR_DENIED with `reason`.
int32_t Admit(const FetchRequest& request, const FetchResponse& response, FetchHeaders& visible,
              std::string& reason);

} // namespace FetchRules

// The real service: UltraNet's async HTTP, TLS verified, no cookies, at most
// kMaxRedirects redirects, kTimeoutMs and kMaxResponseBytes. `post` runs a
// task on the UI thread (PostToUIThread).
FetchService MakeNetworkFetch(std::function<void(std::function<void()>)> post);

} // namespace UltraWeb
