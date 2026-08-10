// Tests/UltraNet/ApiStatus/ProbeUrl.cpp
// Probes for UltraNet/UltraNetUrl.h — parsing, building, percent coding and
// query-string composition. All pure functions, so all verifiable offline.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetUrl.h>

#include <map>
#include <string>

using namespace ultranet_apistatus;

namespace {
constexpr const char* kArea = "URL";
}

ULTRANET_PROBE(kArea, UltraNet_ParseUrl) {
    UltraNetUrlComponents c;
    const UltraNetResult r = UltraNet_ParseUrl(
        "https://alice:s3cret@api.example.com:8443/v1/items?q=hello%20world&page=2#frag",
        c);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT(c.scheme == "https");
    PROBE_EXPECT(c.username == "alice");
    PROBE_EXPECT(c.password == "s3cret");
    PROBE_EXPECT(c.host == "api.example.com");
    PROBE_EXPECT(c.port == 8443);
    PROBE_EXPECT(c.path == "/v1/items");
    PROBE_EXPECT(c.fragment == "frag");
    PROBE_EXPECT(c.queryParams.size() == 2);
    PROBE_EXPECT(c.queryParams.at("q") == "hello world");   // decoded
    PROBE_EXPECT(c.queryParams.at("page") == "2");

    // Rejection path.
    UltraNetUrlComponents bad;
    const UltraNetResult rejected = UltraNet_ParseUrl("not a url at all", bad);
    PROBE_EXPECT(!rejected);
    PROBE_EXPECT(rejected.code == UltraNetResultCode::InvalidUrl);

    const UltraNetResult empty = UltraNet_ParseUrl("", bad);
    PROBE_EXPECT(!empty && empty.code == UltraNetResultCode::InvalidUrl);

    return Working("all components split out, query params percent-decoded, "
                   "malformed and empty input rejected as InvalidUrl");
}

ULTRANET_PROBE(kArea, UltraNet_BuildUrl) {
    UltraNetUrlComponents c;
    c.scheme = "https";
    c.host   = "api.example.com";
    c.port   = 8443;
    c.path   = "/v1/items";
    c.queryParams = {{"page", "2"}, {"q", "hello world"}};
    c.fragment = "frag";

    const std::string url = UltraNet_BuildUrl(c);
    PROBE_EXPECT(!url.empty());

    // Round-trip: re-parsing must give back what went in.
    UltraNetUrlComponents back;
    PROBE_EXPECT(static_cast<bool>(UltraNet_ParseUrl(url, back)));
    PROBE_EXPECT(back.scheme == "https");
    PROBE_EXPECT(back.host == "api.example.com");
    PROBE_EXPECT(back.port == 8443);
    PROBE_EXPECT(back.path == "/v1/items");
    PROBE_EXPECT(back.fragment == "frag");
    PROBE_EXPECT(back.queryParams.at("q") == "hello world");
    PROBE_EXPECT(back.queryParams.at("page") == "2");

    return Working("built \"" + url + "\"; survives a parse round trip");
}

ULTRANET_PROBE(kArea, UltraNet_UrlEncode) {
    PROBE_EXPECT(UltraNet_UrlEncode("hello world") == "hello%20world");
    PROBE_EXPECT(UltraNet_UrlEncode("a+b&c=d") == "a%2Bb%26c%3Dd");
    PROBE_EXPECT(UltraNet_UrlEncode("safe-._~") == "safe-._~");
    PROBE_EXPECT(UltraNet_UrlEncode("").empty());
    return Working("RFC 3986 escaping; unreserved characters left alone");
}

ULTRANET_PROBE(kArea, UltraNet_UrlDecode) {
    PROBE_EXPECT(UltraNet_UrlDecode("hello%20world") == "hello world");
    PROBE_EXPECT(UltraNet_UrlDecode("a%2Bb%26c%3Dd") == "a+b&c=d");
    PROBE_EXPECT(UltraNet_UrlDecode("").empty());

    const std::string original = "\xC3\xA4\xC3\xB6 / ? # &";   // UTF-8 payload
    PROBE_EXPECT(UltraNet_UrlDecode(UltraNet_UrlEncode(original)) == original);
    return Working("decodes escapes and round-trips UTF-8 through UltraNet_UrlEncode");
}

ULTRANET_PROBE(kArea, UltraNet_BuildQueryString) {
    const std::map<std::string, std::string> params = {
        {"b", "two words"},
        {"a", "1"},
        {"c", "x&y"},
    };
    const std::string qs = UltraNet_BuildQueryString(params);
    // std::map iterates in key order, so the output is deterministic.
    PROBE_EXPECT(qs == "a=1&b=two%20words&c=x%26y");
    PROBE_EXPECT(UltraNet_BuildQueryString({}).empty());
    return Working("keys in sorted order, values percent-encoded: \"" + qs + "\"");
}
