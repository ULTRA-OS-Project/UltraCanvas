// Tests/UltraCloud/test_nextcloud.cpp
// Nextcloud helpers: DAV URL, OCS form body, OCS answer parsing; and the
// provider's share request against a fake HTTP function.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudNextcloud.h>

#include <string>

using namespace UltraCloud;

TEST(nextcloud_dav_url_puts_files_under_the_user) {
    REQUIRE_EQ(NextcloudDavUrl("https://cloud.example.com/", "erika", "Docs/Q3 report.pdf"),
               std::string("https://cloud.example.com/remote.php/dav/files/erika/Docs/Q3%20report.pdf"));
    REQUIRE_EQ(NextcloudShareApiUrl("https://cloud.example.com"),
               std::string("https://cloud.example.com/ocs/v2.php/apps/files_sharing/api/v1/shares?format=json"));
}

TEST(ocs_share_form_carries_the_options) {
    ShareLinkOptions plain;
    REQUIRE_EQ(BuildOcsShareForm("Docs/a b.pdf", plain),
               std::string("path=%2FDocs%2Fa%20b.pdf&shareType=3&permissions=1"));
    ShareLinkOptions full;
    full.readOnly = false; full.password = "p w"; full.expiresAt = 1'800'000'000; full.label = "Mail";
    const std::string form = BuildOcsShareForm("/x", full);
    REQUIRE(form.find("permissions=15") != std::string::npos);
    REQUIRE(form.find("password=p%20w") != std::string::npos);
    REQUIRE(form.find("expireDate=2027-01-15") != std::string::npos);
    REQUIRE(form.find("label=Mail") != std::string::npos);
}

TEST(ocs_answer_parsing) {
    ShareLink link; std::string error;
    REQUIRE(ParseOcsShareResponse(
        R"({"ocs":{"meta":{"status":"ok","statuscode":200,"message":"OK"},)"
        R"("data":{"id":42,"url":"https://cloud.example.com/s/AbC123","token":"AbC123"}}})",
        link, error));
    REQUIRE_EQ(link.url, std::string("https://cloud.example.com/s/AbC123"));
    REQUIRE_EQ(link.id, std::string("42"));

    REQUIRE(!ParseOcsShareResponse(
        R"({"ocs":{"meta":{"status":"failure","statuscode":404,"message":"Wrong path"},"data":[]}})",
        link, error));
    REQUIRE(error.find("Wrong path") != std::string::npos);
    REQUIRE(!ParseOcsShareResponse("<html>login</html>", link, error));
}

TEST(nextcloud_provider_posts_to_the_ocs_api) {
    std::string method, url, ocsHeader, body;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        method = req.method == UltraNetHttpMethod::Post ? "POST" : "other";
        url = req.url; ocsHeader = req.headers.Get("OCS-APIRequest");
        body.assign(req.body.begin(), req.body.end());
        const std::string answer =
            R"({"ocs":{"meta":{"statuscode":200},"data":{"id":"7","url":"https://cloud.example.com/s/xyz"}}})";
        resp.statusCode = 200;
        resp.body.assign(answer.begin(), answer.end());
        UltraNetResult r; r.success = true; return r;
    };
    NextcloudProvider nc(fake);
    Account a; a.providerId = "nextcloud"; a.serverUrl = "https://cloud.example.com"; a.username = "erika";
    Credentials c; c.username = "erika"; c.password = "app-password";
    ShareLink link;
    ShareLinkOptions opts; opts.expiresAt = 1'800'000'000;
    REQUIRE(nc.CreateShareLink(a, c, "/Shared/q3.pdf", opts, link));
    REQUIRE_EQ(method, std::string("POST"));
    REQUIRE_EQ(url, NextcloudShareApiUrl(a.serverUrl));
    REQUIRE_EQ(ocsHeader, std::string("true"));
    REQUIRE(body.find("path=%2FShared%2Fq3.pdf") != std::string::npos);
    REQUIRE_EQ(link.url, std::string("https://cloud.example.com/s/xyz"));
    REQUIRE_EQ(link.id, std::string("7"));
    REQUIRE_EQ(link.expiresAt, (int64_t)1'800'000'000);
}
