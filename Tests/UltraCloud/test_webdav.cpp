// Tests/UltraCloud/test_webdav.cpp
// WebDAV helpers: paths, URLs, PROPFIND parsing, public-folder links; and the
// provider's requests against a fake HTTP function.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudWebDav.h>

#include <string>
#include <vector>

using namespace UltraCloud;

TEST(paths_and_urls) {
    REQUIRE_EQ(NormalizePath(""), std::string("/"));
    REQUIRE_EQ(NormalizePath("a/b/"), std::string("/a/b"));
    REQUIRE_EQ(NormalizePath("/a/b"), std::string("/a/b"));
    REQUIRE_EQ(EncodePath("/Shared from Mail/Q3 report.pdf"),
               std::string("/Shared%20from%20Mail/Q3%20report.pdf"));
    REQUIRE_EQ(JoinUrl("https://dav.example.org/", "/x"), std::string("https://dav.example.org/x"));
    REQUIRE_EQ(JoinUrl("https://dav.example.org", "x"), std::string("https://dav.example.org/x"));
    REQUIRE_EQ(PublicFolderLink("https://files.example.org/pub/", "docs/a b.txt"),
               std::string("https://files.example.org/pub/docs/a%20b.txt"));
    REQUIRE_EQ(PublicFolderLink("", "/x"), std::string(""));
}

TEST(multistatus_parsing_skips_container_and_sorts_folders_first) {
    const std::string xml =
        "<?xml version=\"1.0\"?>"
        "<d:multistatus xmlns:d=\"DAV:\">"
        " <d:response><d:href>/remote.php/dav/files/erika/Docs/</d:href>"
        "  <d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat>"
        " </d:response>"
        " <d:response><d:href>/remote.php/dav/files/erika/Docs/report%20Q3.pdf</d:href>"
        "  <d:propstat><d:prop><d:resourcetype/><d:getcontentlength>12345</d:getcontentlength>"
        "  <d:getlastmodified>Wed, 03 Sep 2026 10:00:00 GMT</d:getlastmodified></d:prop></d:propstat>"
        " </d:response>"
        " <d:response><d:href>/remote.php/dav/files/erika/Docs/Archive/</d:href>"
        "  <d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat>"
        " </d:response>"
        "</d:multistatus>";
    std::vector<Entry> entries = ParseMultistatus(xml, "/Docs");
    REQUIRE_EQ(entries.size(), (size_t)2);
    REQUIRE_EQ(entries[0].name, std::string("Archive"));
    REQUIRE(entries[0].isDirectory);
    REQUIRE_EQ(entries[0].path, std::string("/Docs/Archive"));
    REQUIRE_EQ(entries[1].name, std::string("report Q3.pdf"));
    REQUIRE(!entries[1].isDirectory);
    REQUIRE_EQ(entries[1].size, (int64_t)12345);
    REQUIRE_EQ(entries[1].modified, std::string("Wed, 03 Sep 2026 10:00:00 GMT"));
    REQUIRE_EQ(entries[1].path, std::string("/Docs/report Q3.pdf"));
}

TEST(multistatus_parsing_tolerates_uppercase_prefix_and_root) {
    const std::string xml =
        "<D:multistatus xmlns:D=\"DAV:\">"
        "<D:response><D:href>http://dav.example.org/</D:href><D:propstat><D:prop>"
        "<D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"
        "<D:response><D:href>http://dav.example.org/notes.txt</D:href><D:propstat><D:prop>"
        "<D:resourcetype/><D:getcontentlength>7</D:getcontentlength></D:prop></D:propstat></D:response>"
        "</D:multistatus>";
    std::vector<Entry> entries = ParseMultistatus(xml, "/");
    REQUIRE_EQ(entries.size(), (size_t)1);
    REQUIRE_EQ(entries[0].path, std::string("/notes.txt"));
    REQUIRE_EQ(entries[0].size, (int64_t)7);
}

namespace {
struct Captured { std::string method; std::string url; std::string depth; std::string body; std::string auth; };
} // namespace

TEST(webdav_provider_sends_propfind_with_basic_auth) {
    Captured cap;
    HttpFn fake = [&cap](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        cap.method = req.customMethod.empty() ? "GET/PUT/POST" : req.customMethod;
        cap.url = req.url;
        cap.depth = req.headers.Get("Depth");
        cap.auth = req.options.credentials.username + ":" + req.options.credentials.password;
        const std::string body =
            "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/dav/</d:href>"
            "<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat>"
            "</d:response><d:response><d:href>/dav/a.txt</d:href><d:propstat><d:prop>"
            "<d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>";
        resp.statusCode = 207;
        resp.body.assign(body.begin(), body.end());
        UltraNetResult r; r.success = true; return r;
    };
    WebDavProvider dav(fake);
    Account a; a.providerId = "webdav"; a.serverUrl = "https://dav.example.org/dav"; a.username = "max";
    Credentials c; c.username = "max"; c.password = "pw";
    std::vector<Entry> entries;
    REQUIRE(dav.List(a, c, "/", entries));
    REQUIRE_EQ(cap.method, std::string("PROPFIND"));
    REQUIRE_EQ(cap.url, std::string("https://dav.example.org/dav/"));
    REQUIRE_EQ(cap.depth, std::string("1"));
    REQUIRE_EQ(cap.auth, std::string("max:pw"));
    REQUIRE_EQ(entries.size(), (size_t)1);
    REQUIRE_EQ(entries[0].name, std::string("a.txt"));
}

TEST(webdav_provider_maps_http_errors) {
    HttpFn unauthorized = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        resp.statusCode = 401;
        UltraNetResult r; r.success = false; r.message = "HTTP 401"; return r;
    };
    WebDavProvider dav(unauthorized);
    Account a; a.providerId = "webdav"; a.serverUrl = "https://dav.example.org";
    Credentials c; c.username = "max"; c.password = "wrong";
    Result r = dav.Verify(a, c);
    REQUIRE(r.code == ResultCode::AuthFailed);
    REQUIRE_EQ(r.httpStatus, 401);
}

TEST(webdav_share_link_needs_public_base_url) {
    WebDavProvider dav([](const UltraNetHttpRequest&, UltraNetResponse&) {
        UltraNetResult r; r.success = true; return r;
    });
    Account a; a.providerId = "webdav"; a.serverUrl = "https://dav.example.org";
    Credentials c;
    ShareLink link;
    REQUIRE(dav.CreateShareLink(a, c, "/x.pdf", {}, link).code == ResultCode::Unsupported);
    a.publicBaseUrl = "https://files.example.org/public";
    REQUIRE(dav.CreateShareLink(a, c, "/my file.pdf", {}, link));
    REQUIRE_EQ(link.url, std::string("https://files.example.org/public/my%20file.pdf"));
}
