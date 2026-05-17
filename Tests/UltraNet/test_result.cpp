// Tests/UltraNet/test_result.cpp
// UltraNetResult basics + UltraNetEndpoint::ToString.
#include "test_framework.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetSocket.h>

#include <string>

TEST(result_ok_is_truthy) {
    auto r = UltraNetResult::Ok();
    CHECK(bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::Success);
    CHECK(r.message.empty());
}

TEST(result_error_is_falsy_and_keeps_message) {
    auto r = UltraNetResult::Error(UltraNetResultCode::HostNotFound,
                                   "no such host");
    CHECK(!bool(r));
    REQUIRE_EQ(r.code, UltraNetResultCode::HostNotFound);
    REQUIRE_EQ(r.message, std::string{"no such host"});
}

TEST(handle_invalid_is_zero) {
    REQUIRE_EQ(UltraNetInvalidHandle, static_cast<UltraNetHandle>(0));
}

TEST(endpoint_to_string_ipv4) {
    UltraNetEndpoint ep;
    ep.address = "127.0.0.1"; ep.port = 8080; ep.isIpv6 = false;
    REQUIRE_EQ(ep.ToString(), std::string{"127.0.0.1:8080"});
}

TEST(endpoint_to_string_ipv6_brackets) {
    UltraNetEndpoint ep;
    ep.address = "::1"; ep.port = 8080; ep.isIpv6 = true;
    REQUIRE_EQ(ep.ToString(), std::string{"[::1]:8080"});
}

// ===== HTTP/3 detection =====
TEST(http3_capability_check_returns_bool) {
    UltraNet_Initialize();
    // Whatever the answer is, it has to be consistent.
    const bool a = UltraNet_HasHttp3();
    const bool b = UltraNet_HasHttp3();
    REQUIRE_EQ(a, b);
}

TEST(http3_backend_info_lists_protocols) {
    UltraNet_Initialize();
    const std::string info = UltraNet_GetBackendInfo();
    CHECK(info.find("libcurl/") != std::string::npos);
    // When HTTP/3 is present the marker appears; when not, it must not.
    if (UltraNet_HasHttp3()) {
        CHECK(info.find("+HTTP3") != std::string::npos);
    } else {
        CHECK(info.find("+HTTP3") == std::string::npos);
    }
}

TEST(http3_config_flags_default_off) {
    UltraNetConfig c = UltraNetConfig::Default();
    CHECK(!c.enableHttp3);
    CHECK(!c.http3Only);
    // HTTP/2 stays the default upgrade target.
    CHECK(c.enableHttp2);
}
