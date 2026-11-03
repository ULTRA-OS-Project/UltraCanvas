// OS/WASM/UltraCanvasWASMNetUrl.cpp
// The URL helpers of include/UltraNet/UltraNetUrl.h in a browser build,
// where UltraNet (and the libcurl behind its curl_url_* implementation) is
// compiled out: parsing and resolving go through the browser's WHATWG URL
// parser, percent encoding is done here with the same unreserved set as
// curl_easy_escape. What needs them in the browser is UltraWeb's runtime -
// its fetch rules and storage partitions (Apps/UltraWeb/browser).
//
// Differences from the libcurl build: the WHATWG parser lower-cases the
// host, adds the "/" path of an http(s) URL with none, and reports no port
// when the URL names the scheme's default one.
//
// Version: 0.1.0 - first version
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetUrl.h"

#include <emscripten.h>

#include <cstdlib>
#include <sstream>
#include <vector>

// "scheme\nusername\npassword\nhost\nport\npath\nquery\nfragment", or null
// when `url` does not parse. The parts are percent-encoded, so none holds a
// line break.
EM_JS(char*, uc_neturl_js_parse, (const char* url), {
    let u;
    try { u = new URL(UTF8ToString(url)); } catch (e) { return 0; }
    const parts = [u.protocol.slice(0, -1), u.username, u.password, u.hostname,
                   u.port, u.pathname, u.search.slice(1), u.hash.slice(1)];
    return stringToNewUTF8(parts.join("\n"));
});

// The absolute URL `reference` names against `base`; an empty reference is
// the base without its fragment. Null when either does not parse.
EM_JS(char*, uc_neturl_js_resolve, (const char* base, const char* reference), {
    try {
        const ref = UTF8ToString(reference);
        const u = new URL(ref, UTF8ToString(base));
        if (ref === "") u.hash = "";
        return stringToNewUTF8(u.href);
    } catch (e) {
        return 0;
    }
});

namespace {

std::string Take(char* text) {
    std::string out = text ? text : "";
    free(text);
    return out;
}

bool HasControlCharacter(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void ParseQuery(const std::string& query, std::map<std::string, std::string>& out) {
    size_t i = 0;
    while (i < query.size()) {
        size_t amp = query.find('&', i);
        if (amp == std::string::npos) amp = query.size();
        const std::string pair = query.substr(i, amp - i);
        const size_t eq = pair.find('=');
        if (eq == std::string::npos) out[UltraNet_UrlDecode(pair)] = "";
        else out[UltraNet_UrlDecode(pair.substr(0, eq))] = UltraNet_UrlDecode(pair.substr(eq + 1));
        i = amp + 1;
    }
}

} // namespace

UltraNetResult UltraNet_ParseUrl(const std::string& url, UltraNetUrlComponents& out) {
    out = {};
    if (url.empty()) return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "empty URL");
    if (HasControlCharacter(url)) return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "invalid URL");
    const std::string joined = Take(uc_neturl_js_parse(url.c_str()));
    if (joined.empty()) return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "invalid URL");
    std::vector<std::string> parts;
    std::istringstream lines(joined);
    for (std::string line; std::getline(lines, line, '\n');) parts.push_back(line);
    parts.resize(8);
    out.scheme = parts[0];
    out.username = parts[1];
    out.password = parts[2];
    out.host = parts[3];
    out.port = parts[4].empty() ? -1 : std::atoi(parts[4].c_str());
    out.path = parts[5];
    out.query = parts[6];
    out.fragment = parts[7];
    ParseQuery(out.query, out.queryParams);
    return UltraNetResult::Ok();
}

std::string UltraNet_BuildUrl(const UltraNetUrlComponents& c) {
    if (c.scheme.empty() || c.host.empty()) return {};
    std::string url = c.scheme + "://";
    if (!c.username.empty()) url += c.username + (c.password.empty() ? "" : ":" + c.password) + "@";
    url += c.host;
    if (c.port >= 0) url += ":" + std::to_string(c.port);
    url += c.path.empty() || c.path[0] != '/' ? "/" + c.path : c.path;
    const std::string query = c.query.empty() && !c.queryParams.empty() ? UltraNet_BuildQueryString(c.queryParams) : c.query;
    if (!query.empty()) url += "?" + query;
    if (!c.fragment.empty()) url += "#" + c.fragment;
    return url;
}

UltraNetResult UltraNet_ResolveUrl(const std::string& base, const std::string& reference, std::string& outUrl) {
    outUrl.clear();
    // The WHATWG parser drops tabs and line breaks quietly; a URL that goes
    // into a request line must not have had them at all.
    if (HasControlCharacter(base) || HasControlCharacter(reference)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "a URL cannot contain a control character");
    }
    if (base.empty()) return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "the base is not an absolute URL");
    outUrl = Take(uc_neturl_js_resolve(base.c_str(), reference.c_str()));
    if (outUrl.empty()) return UltraNetResult::Error(UltraNetResultCode::InvalidUrl, "invalid URL reference");
    return UltraNetResult::Ok();
}

std::string UltraNet_UrlEncode(const std::string& input) {
    static const char* const kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                                || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            out += char(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0f];
        }
    }
    return out;
}

std::string UltraNet_UrlDecode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const int high = HexValue(input[i + 1]), low = HexValue(input[i + 2]);
            if (high >= 0 && low >= 0) {
                out += char(high * 16 + low);
                i += 2;
                continue;
            }
        }
        out += input[i];
    }
    return out;
}

std::string UltraNet_BuildQueryString(const std::map<std::string, std::string>& params) {
    std::string out;
    for (const auto& [key, value] : params) {
        if (!out.empty()) out += '&';
        out += UltraNet_UrlEncode(key) + "=" + UltraNet_UrlEncode(value);
    }
    return out;
}
