// Tests/UltraCloud/test_dropbox.cpp
// Dropbox provider against a fake HTTP function: paths, paged listing, the
// upload arg header, share links incl. the "already exists" fallback.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudDropbox.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCloud;

namespace {
UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }
UltraNetResult Fail(const std::string& m) { UltraNetResult r; r.success = false; r.message = m; return r; }
void Answer(UltraNetResponse& resp, int status, const std::string& body) {
    resp.statusCode = status; resp.body.assign(body.begin(), body.end());
}
std::string Body(const UltraNetHttpRequest& req) { return std::string(req.body.begin(), req.body.end()); }
} // namespace

TEST(dropbox_paths) {
    REQUIRE_EQ(DropboxPath("/"), std::string(""));
    REQUIRE_EQ(DropboxPath(""), std::string(""));
    REQUIRE_EQ(DropboxPath("Docs/a.pdf"), std::string("/Docs/a.pdf"));
}

TEST(dropbox_list_follows_cursor) {
    std::vector<std::string> urls;
    HttpFn fake = [&urls](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        urls.push_back(req.url);
        REQUIRE_EQ(req.headers.Get("Authorization"), std::string("Bearer tok"));
        if (req.url.find("list_folder/continue") != std::string::npos) {
            REQUIRE(Body(req).find("\"cursor\"") != std::string::npos);
            Answer(resp, 200, R"({"entries":[{".tag":"file","name":"b.txt","path_display":"/Docs/b.txt","size":5,"server_modified":"2026-09-04T10:00:00Z"}],"has_more":false})");
        } else {
            REQUIRE(Body(req).find("\"path\":\"/Docs\"") != std::string::npos);
            Answer(resp, 200, R"({"entries":[{".tag":"folder","name":"Sub","path_display":"/Docs/Sub"},{".tag":"file","name":"a.txt","path_display":"/Docs/a.txt","size":3}],"cursor":"c1","has_more":true})");
        }
        return Ok();
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    std::vector<Entry> entries;
    REQUIRE(dropbox.List(a, c, "/Docs", entries));
    REQUIRE_EQ(urls.size(), (size_t)2);
    REQUIRE_EQ(entries.size(), (size_t)3);
    REQUIRE(entries[0].isDirectory);
    REQUIRE_EQ(entries[0].path, std::string("/Docs/Sub"));
    REQUIRE_EQ(entries[2].name, std::string("b.txt"));
    REQUIRE_EQ(entries[2].size, (int64_t)5);
}

TEST(dropbox_upload_sends_api_arg_header) {
    std::string argHeader, url, contentType;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        url = req.url; argHeader = req.headers.Get("Dropbox-API-Arg");
        contentType = req.headers.Get("Content-Type");
        REQUIRE_EQ(Body(req), std::string("hello"));
        Answer(resp, 200, R"({"name":"x.txt"})");
        return Ok();
    };
    const std::string dir = (std::filesystem::temp_directory_path() / "ultracloud-dropbox").string();
    std::filesystem::create_directories(dir);
    std::ofstream(dir + "/x.txt") << "hello";
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    REQUIRE(dropbox.Upload(a, c, dir + "/x.txt", "/Shared/x.txt"));
    REQUIRE(url.find("content.dropboxapi.com/2/files/upload") != std::string::npos);
    REQUIRE(argHeader.find("\"path\":\"/Shared/x.txt\"") != std::string::npos);
    REQUIRE(argHeader.find("\"mode\":\"overwrite\"") != std::string::npos);
    REQUIRE_EQ(contentType, std::string("application/octet-stream"));
}

TEST(dropbox_share_link_and_existing_link_fallback) {
    int calls = 0;
    HttpFn fake = [&calls](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        ++calls;
        if (req.url.find("create_shared_link_with_settings") != std::string::npos) {
            if (Body(req).find("/new.pdf") != std::string::npos) {
                REQUIRE(Body(req).find("\"requested_visibility\":\"public\"") != std::string::npos);
                Answer(resp, 200, R"({"url":"https://www.dropbox.com/scl/fi/abc/new.pdf?dl=0","id":"id:1"})");
                return Ok();
            }
            Answer(resp, 409, R"({"error_summary":"shared_link_already_exists/..","error":{".tag":"shared_link_already_exists"}})");
            return Fail("HTTP 409");
        }
        if (req.url.find("list_shared_links") != std::string::npos) {
            Answer(resp, 200, R"({"links":[{"url":"https://www.dropbox.com/scl/fi/old/old.pdf?dl=0","id":"id:2"}]})");
            return Ok();
        }
        Answer(resp, 500, "");
        return Fail("unexpected");
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    ShareLink link;
    REQUIRE(dropbox.CreateShareLink(a, c, "/new.pdf", {}, link));
    REQUIRE_EQ(link.id, std::string("id:1"));
    REQUIRE(dropbox.CreateShareLink(a, c, "/old.pdf", {}, link));
    REQUIRE_EQ(link.url, std::string("https://www.dropbox.com/scl/fi/old/old.pdf?dl=0"));
    REQUIRE_EQ(link.id, std::string("id:2"));
    REQUIRE_EQ(calls, 3);
}

TEST(dropbox_mkdir_conflict_is_ok_and_account_info) {
    HttpFn fake = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        if (req.url.find("create_folder_v2") != std::string::npos) {
            Answer(resp, 409, R"({"error_summary":"path/conflict/folder/..."})");
            return Fail("HTTP 409");
        }
        Answer(resp, 200, R"({"email":"erika@example.com","name":{"display_name":"Erika"}})");
        return Ok();
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    REQUIRE(dropbox.MakeDirectory(a, c, "/Shared"));
    std::string user, name;
    REQUIRE(dropbox.AccountInfo(a, c, user, name));
    REQUIRE_EQ(user, std::string("erika@example.com"));
    REQUIRE_EQ(name, std::string("Erika"));
}
