// Tests/UltraCloud/test_onedrive.cpp
// OneDrive provider against a fake HTTP function: item URLs, paged listing,
// simple vs. session upload (chunks with Content-Range), share links.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudOneDrive.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCloud;

namespace {
UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }
void Answer(UltraNetResponse& resp, int status, const std::string& body) {
    resp.statusCode = status; resp.body.assign(body.begin(), body.end());
}
std::string Body(const UltraNetHttpRequest& req) { return std::string(req.body.begin(), req.body.end()); }
std::string WriteFile(const std::string& name, const std::string& content) {
    const std::string dir = (std::filesystem::temp_directory_path() / "ultracloud-onedrive").string();
    std::filesystem::create_directories(dir);
    std::ofstream(dir + "/" + name, std::ios::binary) << content;
    return dir + "/" + name;
}
} // namespace

TEST(onedrive_item_urls) {
    REQUIRE_EQ(OneDriveItemUrl("/", "children"),
               std::string("https://graph.microsoft.com/v1.0/me/drive/root/children"));
    REQUIRE_EQ(OneDriveItemUrl("/Docs/Q3 report.pdf", "content"),
               std::string("https://graph.microsoft.com/v1.0/me/drive/root:/Docs/Q3%20report.pdf:/content"));
}

TEST(onedrive_list_follows_next_link) {
    std::vector<std::string> urls;
    HttpFn fake = [&urls](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        urls.push_back(req.url);
        if (req.url.find("skiptoken") != std::string::npos)
            Answer(resp, 200, R"({"value":[{"name":"b.txt","size":2,"file":{}}]})");
        else
            Answer(resp, 200, R"({"value":[{"name":"Sub","folder":{"childCount":1}},{"name":"a.txt","size":1,"file":{},"lastModifiedDateTime":"2026-09-04T10:00:00Z"}],"@odata.nextLink":"https://graph.microsoft.com/v1.0/me/drive/root/children?$skiptoken=x"})");
        return Ok();
    };
    OneDriveProvider od(fake);
    Account a; Credentials c; c.token = "tok";
    std::vector<Entry> entries;
    REQUIRE(od.List(a, c, "/", entries));
    REQUIRE_EQ(urls.size(), (size_t)2);
    REQUIRE(urls[0].find("root/children?$select=") != std::string::npos);
    REQUIRE_EQ(entries.size(), (size_t)3);
    REQUIRE(entries[0].isDirectory);
    REQUIRE_EQ(entries[0].path, std::string("/Sub"));
    REQUIRE_EQ(entries[1].modified, std::string("2026-09-04T10:00:00Z"));
}

TEST(onedrive_small_upload_is_a_put) {
    std::string url, method;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        url = req.url; method = req.method == UltraNetHttpMethod::Put ? "PUT" : "other";
        REQUIRE_EQ(Body(req), std::string("small"));
        Answer(resp, 201, R"({"id":"1"})");
        return Ok();
    };
    OneDriveProvider od(fake);
    Account a; Credentials c; c.token = "tok";
    REQUIRE(od.Upload(a, c, WriteFile("small.txt", "small"), "/Shared/small.txt"));
    REQUIRE_EQ(method, std::string("PUT"));
    REQUIRE(url.find("root:/Shared/small.txt:/content") != std::string::npos);
}

TEST(onedrive_large_upload_uses_a_session_in_chunks) {
    std::vector<std::string> ranges;
    bool sessionOpened = false;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        if (req.url.find("createUploadSession") != std::string::npos) {
            sessionOpened = true;
            REQUIRE(Body(req).find("\"@microsoft.graph.conflictBehavior\":\"replace\"") != std::string::npos);
            Answer(resp, 200, R"({"uploadUrl":"https://up.example/session/1"})");
            return Ok();
        }
        REQUIRE_EQ(req.url, std::string("https://up.example/session/1"));
        REQUIRE(req.headers.Get("Authorization").empty());   // session URL is pre-authorised
        ranges.push_back(req.headers.Get("Content-Range"));
        Answer(resp, ranges.size() == 3 ? 201 : 202, "{}");
        return Ok();
    };
    // 25 MiB with the default limits → 10 + 10 + 5 MiB chunks.
    const std::string big(25 * 1024 * 1024, 'x');
    OneDriveProvider od(fake);
    Account a; Credentials c; c.token = "tok";
    REQUIRE(od.Upload(a, c, WriteFile("big.bin", big), "/big.bin"));
    REQUIRE(sessionOpened);
    REQUIRE_EQ(ranges.size(), (size_t)3);
    REQUIRE_EQ(ranges[0], std::string("bytes 0-10485759/26214400"));
    REQUIRE_EQ(ranges[2], std::string("bytes 20971520-26214399/26214400"));

    // Small limits: the same 25-byte layout as the other providers' tests.
    ranges.clear();
    od.SetUploadLimits(/*simple=*/20, /*chunk=*/10);
    REQUIRE(od.Upload(a, c, WriteFile("small-big.bin", "0123456789abcdefghijKLMNO"), "/sb.bin"));
    REQUIRE_EQ(ranges.size(), (size_t)3);
    REQUIRE_EQ(ranges[2], std::string("bytes 20-24/25"));
}

TEST(onedrive_share_link) {
    std::string body, url;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        url = req.url; body = Body(req);
        Answer(resp, 201, R"({"id":"perm1","link":{"webUrl":"https://1drv.ms/u/s!abc","type":"view"}})");
        return Ok();
    };
    OneDriveProvider od(fake);
    Account a; Credentials c; c.token = "tok";
    ShareLinkOptions opts; opts.password = "pw"; opts.expiresAt = 1'800'000'000;
    ShareLink link;
    REQUIRE(od.CreateShareLink(a, c, "/Shared/q3.pdf", opts, link));
    REQUIRE(url.find("root:/Shared/q3.pdf:/createLink") != std::string::npos);
    REQUIRE(body.find("\"scope\":\"anonymous\"") != std::string::npos);
    REQUIRE(body.find("\"password\":\"pw\"") != std::string::npos);
    REQUIRE(body.find("\"expirationDateTime\":\"2027-01-15T") != std::string::npos);
    REQUIRE_EQ(link.url, std::string("https://1drv.ms/u/s!abc"));
    REQUIRE_EQ(link.id, std::string("perm1"));
}
