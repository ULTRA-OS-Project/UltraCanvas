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
