// Apps/UltraWeb/host/UltraWebFetch.cpp
// An app's fetch: the rules and the UltraNet service (UltraWebFetch.h).
// Version: 0.2.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebFetch.h"

#include "ultraweb.h"   // UltraWeb/guest: the result codes and methods

#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>

namespace UltraWeb {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

bool IsHttpScheme(const std::string& scheme) { return scheme == "http" || scheme == "https"; }

std::string SchemeOf(const std::string& url) {
    UltraNetUrlComponents parts;
    return UltraNet_ParseUrl(url, parts) ? Lower(parts.scheme) : std::string();
}

const char* MethodName(uint32_t method) {
    switch (method) {
        case UC_METHOD_GET:    return "GET";
        case UC_METHOD_POST:   return "POST";
        case UC_METHOD_PUT:    return "PUT";
        case UC_METHOD_DELETE: return "DELETE";
        case UC_METHOD_PATCH:  return "PATCH";
        case UC_METHOD_HEAD:   return "HEAD";
        default:               return nullptr;
    }
}

// The media type without its parameters: "text/plain; charset=utf-8" →
// "text/plain".
std::string Essence(const std::string& contentType) {
    return Lower(Trim(contentType.substr(0, contentType.find(';'))));
}

// What a page may send to another origin without a CORS preflight.
bool IsSimpleRequest(const FetchRequest& request) {
    if (request.method == "GET" || request.method == "HEAD") return true;
    if (request.method != "POST") return false;
    if (request.contentType.empty()) return true;
    const std::string type = Essence(request.contentType);
    return type == "text/plain" || type == "application/x-www-form-urlencoded" || type == "multipart/form-data";
}

// Every value of a header, joined as HTTP joins them.
std::string HeaderValue(const FetchHeaders& headers, const std::string& lowerName, bool& found) {
    std::string value;
    found = false;
    for (const auto& [name, v] : headers) {
        if (Lower(name) != lowerName) continue;
        value += (found ? ", " : "") + v;
        found = true;
    }
    return value;
}

std::vector<std::string> SplitList(const std::string& list) {
    std::vector<std::string> items;
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        if (comma == std::string::npos) comma = list.size();
        const std::string item = Lower(Trim(list.substr(at, comma - at)));
        if (!item.empty()) items.push_back(item);
        at = comma + 1;
    }
    return items;
}

bool IsForbiddenResponseHeader(const std::string& lowerName) {
    return lowerName == "set-cookie" || lowerName == "set-cookie2";
}

bool IsSafelistedResponseHeader(const std::string& lowerName) {
    static const char* const kSafelisted[] = {"cache-control", "content-language", "content-length", "content-type",
                                              "expires", "last-modified", "pragma"};
    for (const char* name : kSafelisted) {
        if (lowerName == name) return true;
    }
    return false;
}

} // namespace

namespace FetchRules {

std::string OriginOf(const std::string& url) {
    UltraNetUrlComponents parts;
    if (!UltraNet_ParseUrl(url, parts)) return {};
    const std::string scheme = Lower(parts.scheme);
    if (!IsHttpScheme(scheme) || parts.host.empty()) return {};
    std::string origin = scheme + "://" + Lower(parts.host);
    const int defaultPort = scheme == "https" ? 443 : 80;
    if (parts.port > 0 && parts.port != defaultPort) origin += ":" + std::to_string(parts.port);
    return origin;
}

int32_t Prepare(const std::string& appAddress, const std::string& reference, uint32_t method,
                std::vector<uint8_t> body, std::string contentType, FetchRequest& out, std::string& reason) {
    out = FetchRequest{};
    if (reference.size() > kMaxUrlBytes) { reason = "the URL is longer than 8 KB"; return UC_ERR_LIMIT; }
    if (body.size() > kMaxRequestBytes) { reason = "the body is larger than 4 MB"; return UC_ERR_LIMIT; }
    const char* methodName = MethodName(method);
    if (!methodName) { reason = "unknown method " + std::to_string(method); return UC_ERR_STATE; }
    out.method = methodName;
    if (!body.empty() && (out.method == "GET" || out.method == "HEAD")) {
        reason = out.method + " carries no body";
        return UC_ERR_STATE;
    }
    if (contentType.size() > kMaxContentTypeBytes) { reason = "the content type is longer than 256 bytes"; return UC_ERR_LIMIT; }
    // It goes into a header line as it is: nothing that could end the line.
    for (unsigned char c : contentType) {
        if (c < 0x20 || c > 0x7e) { reason = "the content type has a character a header cannot carry"; return UC_ERR_STATE; }
    }
    if (!body.empty() && contentType.empty()) contentType = "text/plain;charset=UTF-8";

    const std::string appOrigin = OriginOf(appAddress);
    std::string url;
    if (!appOrigin.empty()) {
        std::string resolved;
        if (!UltraNet_ResolveUrl(appAddress, reference, resolved)) { reason = "\"" + reference + "\" is not a URL"; return UC_ERR_STATE; }
        url = resolved;
    } else {
        // An app from a file or about: has nothing to resolve against.
        UltraNetUrlComponents parts;
        if (!UltraNet_ParseUrl(reference, parts)) {
            reason = "an app opened from a file or about: has no origin, so it can fetch absolute http(s) URLs only";
            return UC_ERR_DENIED;
        }
        url = reference;
    }
    // Drop the fragment, which is never sent, and normalise the URL.
    std::string normalised;
    if (!UltraNet_ResolveUrl(url, "", normalised)) { reason = "\"" + reference + "\" is not a URL"; return UC_ERR_STATE; }
    url = normalised;

    UltraNetUrlComponents parts;
    if (!UltraNet_ParseUrl(url, parts)) { reason = "\"" + reference + "\" is not a URL"; return UC_ERR_STATE; }
    const std::string scheme = Lower(parts.scheme);
    if (!IsHttpScheme(scheme)) { reason = scheme + ": URLs cannot be fetched; only http and https"; return UC_ERR_DENIED; }
    if (!parts.username.empty() || !parts.password.empty()) {
        reason = "a URL with a user name or password in it cannot be fetched";
        return UC_ERR_DENIED;
    }
    if (SchemeOf(appAddress) == "https" && scheme == "http") {
        reason = "an https app cannot fetch over plain http (" + url + ")";
        return UC_ERR_DENIED;
    }

    out.url = url;
    out.body = std::move(body);
    out.contentType = std::move(contentType);
    out.origin = appOrigin.empty() ? "null" : appOrigin;
    out.sameOrigin = !appOrigin.empty() && OriginOf(url) == appOrigin;
    out.sendOrigin = !out.sameOrigin || (out.method != "GET" && out.method != "HEAD");
    if (!out.sameOrigin && !IsSimpleRequest(out)) {
        reason = out.method + (out.contentType.empty() ? std::string() : " " + Essence(out.contentType))
                 + " to another origin needs a CORS preflight, which this UltraWeb does not send yet";
        return UC_ERR_DENIED;
    }
    return UC_OK;
}

int32_t Admit(const FetchRequest& request, const FetchResponse& response, FetchHeaders& visible,
              std::string& reason) {
    visible.clear();
    if (response.tooLarge) { reason = "the response is larger than 16 MB"; return UC_ERR_LIMIT; }
    if (!response.error.empty() || response.status == 0) {
        reason = response.error.empty() ? "no answer" : response.error;
        return UC_ERR_NETWORK;
    }
    if (response.status < 100 || response.status > 599) {
        reason = "the server answered status " + std::to_string(response.status);
        return UC_ERR_NETWORK;
    }
    // A redirect may have gone somewhere the request itself could not.
    const std::string finalUrl = response.finalUrl.empty() ? request.url : response.finalUrl;
    const std::string finalScheme = SchemeOf(finalUrl);
    if (!IsHttpScheme(finalScheme)) { reason = "redirected to " + finalUrl + ", which is not http(s)"; return UC_ERR_DENIED; }
    if (request.origin.rfind("https://", 0) == 0 && finalScheme == "http") {
        reason = "redirected to plain http (" + finalUrl + ")";
        return UC_ERR_DENIED;
    }
    const bool sameOrigin = request.sameOrigin && OriginOf(finalUrl) == request.origin;

    bool found = false;
    if (!sameOrigin) {
        const std::string allowed = Trim(HeaderValue(response.headers, "access-control-allow-origin", found));
        if (!found || (allowed != "*" && (request.origin == "null" || allowed != request.origin))) {
            reason = finalUrl + " does not allow origin " + request.origin + " (Access-Control-Allow-Origin: "
                     + (found ? allowed : std::string("none")) + ")";
            return UC_ERR_DENIED;
        }
    }
    std::vector<std::string> exposed;
    bool exposeAll = sameOrigin;
    if (!sameOrigin) {
        exposed = SplitList(HeaderValue(response.headers, "access-control-expose-headers", found));
        // No credentials are ever sent, so "*" means every header.
        exposeAll = std::find(exposed.begin(), exposed.end(), "*") != exposed.end();
    }
    for (const auto& [name, value] : response.headers) {
        const std::string lowerName = Lower(name);
        if (IsForbiddenResponseHeader(lowerName)) continue;
        if (exposeAll || IsSafelistedResponseHeader(lowerName)
            || std::find(exposed.begin(), exposed.end(), lowerName) != exposed.end()) {
            visible.emplace_back(lowerName, value);
        }
    }
    return UC_OK;
}

} // namespace FetchRules

FetchService MakeNetworkFetch(std::function<void(std::function<void()>)> post) {
    return [post](const FetchRequest& request, std::function<void(FetchResponse)> done) -> std::function<void()> {
        UltraNetHttpRequest http;
        http.url = request.url;
        if (request.method == "GET") http.method = UltraNetHttpMethod::Get;
        else if (request.method == "HEAD") http.method = UltraNetHttpMethod::Head;
        else if (request.method == "POST") http.method = UltraNetHttpMethod::Post;
        else if (request.method == "PUT") http.method = UltraNetHttpMethod::Put;
        else if (request.method == "DELETE") http.method = UltraNetHttpMethod::Delete;
        else http.method = UltraNetHttpMethod::Patch;
        http.body = request.body;
        if (!request.contentType.empty()) http.headers.Set("Content-Type", request.contentType);
        if (request.sendOrigin) http.headers.Set("Origin", request.origin);
        http.options.timeoutMs = FetchRules::kTimeoutMs;
        http.options.maxReceiveSize = int64_t(FetchRules::kMaxResponseBytes);
        http.options.followRedirects = true;
        http.options.maxRedirects = FetchRules::kMaxRedirects;

        // A cancelled request still calls back (as "Cancelled"); the flag,
        // read on the UI thread where the cancel also runs, drops it.
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        const UltraNetHandle handle = UltraNet_HttpRequestAsync(http, [post, done, cancelled](const UltraNetResponse& r) {
            FetchResponse out;
            out.status = r.statusCode;
            out.error = r.transferError;
            if (out.status == 0 && out.error.empty()) out.error = r.statusMessage.empty() ? "no answer" : r.statusMessage;
            out.tooLarge = r.exceededReceiveLimit;
            out.finalUrl = r.finalUrl;
            out.headers = r.headers.Entries();
            out.body = r.body;
            post([done, cancelled, out = std::move(out)]() mutable {
                if (!cancelled->load()) done(std::move(out));
            });
        });
        return [handle, cancelled]() {
            cancelled->store(true);
            if (handle != UltraNetInvalidHandle) UltraNet_CancelRequest(handle);
        };
    };
}

} // namespace UltraWeb
