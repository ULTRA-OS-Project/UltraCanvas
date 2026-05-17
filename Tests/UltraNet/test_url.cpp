// Tests/UltraNet/test_url.cpp
// URL parser / builder / encoder coverage. No network.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetUrl.h>

#include <map>
#include <string>

TEST(url_parse_basic) {
    UltraNet_Initialize();
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl(
        "https://example.com:8443/path/to?a=1&b=hello%20world#frag", c);
    REQUIRE(bool(r));
    REQUIRE_EQ(c.scheme, std::string{"https"});
    REQUIRE_EQ(c.host,   std::string{"example.com"});
    REQUIRE_EQ(c.port,   8443);
    REQUIRE_EQ(c.path,   std::string{"/path/to"});
    REQUIRE_EQ(c.query,  std::string{"a=1&b=hello%20world"});
    REQUIRE_EQ(c.fragment, std::string{"frag"});
    REQUIRE_EQ(c.queryParams["a"], std::string{"1"});
    REQUIRE_EQ(c.queryParams["b"], std::string{"hello world"});
}

TEST(url_parse_userinfo) {
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl("https://alice:secret@example.com/", c);
    REQUIRE(bool(r));
    REQUIRE_EQ(c.username, std::string{"alice"});
    REQUIRE_EQ(c.password, std::string{"secret"});
}

TEST(url_parse_default_port_is_minus_one) {
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl("https://example.com/", c);
    REQUIRE(bool(r));
    REQUIRE_EQ(c.port, -1);
}

TEST(url_parse_invalid_returns_error) {
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl("not a url", c);
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::InvalidUrl);
}

TEST(url_parse_empty_returns_error) {
    UltraNetUrlComponents c;
    auto r = UltraNet_ParseUrl("", c);
    CHECK(!bool(r));
}

TEST(url_encode_special_chars) {
    REQUIRE_EQ(UltraNet_UrlEncode("hello world"),     std::string{"hello%20world"});
    REQUIRE_EQ(UltraNet_UrlEncode("a/b?c=d&e"),       std::string{"a%2Fb%3Fc%3Dd%26e"});
    REQUIRE_EQ(UltraNet_UrlEncode(""),                std::string{""});
}

TEST(url_decode_special_chars) {
    REQUIRE_EQ(UltraNet_UrlDecode("hello%20world"),   std::string{"hello world"});
    REQUIRE_EQ(UltraNet_UrlDecode("a%2Fb%3Fc%3Dd"),   std::string{"a/b?c=d"});
}

TEST(url_encode_decode_roundtrip) {
    const std::string input = "Some-Text!@#$%^&*()_+={}[]|:;'\",<>?/`~";
    REQUIRE_EQ(UltraNet_UrlDecode(UltraNet_UrlEncode(input)), input);
}

TEST(url_build_query_string_sorts_alphabetically) {
    std::map<std::string, std::string> params;
    params["b"] = "second";
    params["a"] = "first";
    const std::string qs = UltraNet_BuildQueryString(params);
    // std::map iterates in key order
    REQUIRE_EQ(qs, std::string{"a=first&b=second"});
}

TEST(url_build_query_string_encodes_values) {
    std::map<std::string, std::string> params;
    params["msg"] = "hello world";
    REQUIRE_EQ(UltraNet_BuildQueryString(params),
               std::string{"msg=hello%20world"});
}

TEST(url_build_round_trips_with_parse) {
    UltraNetUrlComponents c;
    UltraNet_ParseUrl("http://example.com:8080/p?q=v", c);
    const std::string rebuilt = UltraNet_BuildUrl(c);
    UltraNetUrlComponents c2;
    UltraNet_ParseUrl(rebuilt, c2);
    REQUIRE_EQ(c2.host, c.host);
    REQUIRE_EQ(c2.port, c.port);
    REQUIRE_EQ(c2.path, c.path);
}
