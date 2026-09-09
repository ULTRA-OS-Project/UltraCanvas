// Tests/UltraCloud/test_googledrive.cpp
// Google Drive provider against a fake HTTP function: path → id resolution,
// listing, multipart create vs. media update, folder creation, share links.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudGoogleDrive.h>

#include <UltraNet/UltraNetUrl.h>

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
std::string Decoded(const std::string& url) { return UltraNet_UrlDecode(url); }

// A tiny fake Drive: root → Docs (folder f1) → report.pdf (file x1).
UltraNetResult FakeDrive(const UltraNetHttpRequest& req, UltraNetResponse& resp,
                         std::vector<std::string>& log) {
    const std::string url = Decoded(req.url);
    log.push_back((req.method == UltraNetHttpMethod::Post ? "POST " :
                   req.customMethod == "PATCH" ? "PATCH " : "GET ") + url);
    if (url.find("/files?q=") != std::string::npos) {
        if (url.find("'root' in parents and name = 'Docs'") != std::string::npos)
            Answer(resp, 200, R"({"files":[{"id":"f1","mimeType":"application/vnd.google-apps.folder"}]})");
        else if (url.find("'f1' in parents and name = 'report.pdf'") != std::string::npos)
            Answer(resp, 200, R"({"files":[{"id":"x1","mimeType":"application/pdf"}]})");
        else if (url.find("'f1' in parents and trashed = false") != std::string::npos &&
                 url.find("name =") == std::string::npos)
            Answer(resp, 200, R"({"files":[{"id":"x1","name":"report.pdf","mimeType":"application/pdf","size":"12","modifiedTime":"2026-09-04T10:00:00Z"},{"id":"f2","name":"Old","mimeType":"application/vnd.google-apps.folder"}]})");
        else
            Answer(resp, 200, R"({"files":[]})");
        return Ok();
    }
    if (url.find("/permissions") != std::string::npos) { Answer(resp, 200, R"({"id":"anyone1"})"); return Ok(); }
    if (url.find("/files/x1?fields=webViewLink") != std::string::npos) {
        Answer(resp, 200, R"({"webViewLink":"https://drive.google.com/file/d/x1/view?usp=drivesdk"})");
        return Ok();
    }
    if (url.find("upload/drive/v3/files") != std::string::npos) { Answer(resp, 200, R"({"id":"new"})"); return Ok(); }
    if (url.find("/drive/v3/files") != std::string::npos && req.method == UltraNetHttpMethod::Post) {
        Answer(resp, 200, R"({"id":"f3"})"); return Ok();
    }
    Answer(resp, 404, "{}");
    UltraNetResult r; r.success = false; r.message = "HTTP 404"; return r;
}
std::string WriteFile(const std::string& name, const std::string& content) {
    const std::string dir = (std::filesystem::temp_directory_path() / "ultracloud-gdrive").string();
    std::filesystem::create_directories(dir);
    std::ofstream(dir + "/" + name, std::ios::binary) << content;
    return dir + "/" + name;
}
} // namespace

TEST(googledrive_child_query_escapes_quotes) {
    REQUIRE_EQ(GoogleDriveChildQuery("root", "Erika's file"),
               std::string("'root' in parents and name = 'Erika\\'s file' and trashed = false"));
}

TEST(googledrive_resolves_paths_and_lists) {
    std::vector<std::string> log;
    GoogleDriveProvider gd([&log](const UltraNetHttpRequest& r, UltraNetResponse& s) { return FakeDrive(r, s, log); });
    Account a; Credentials c; c.token = "tok";
    std::string id;
    REQUIRE(gd.ResolveId(c, "/Docs/report.pdf", id));
    REQUIRE_EQ(id, std::string("x1"));
    REQUIRE(gd.ResolveId(c, "/Docs/missing.txt", id).code == ResultCode::NotFound);

    std::vector<Entry> entries;
    REQUIRE(gd.List(a, c, "/Docs", entries));
    REQUIRE_EQ(entries.size(), (size_t)2);
    REQUIRE(entries[0].isDirectory);
    REQUIRE_EQ(entries[0].path, std::string("/Docs/Old"));
    REQUIRE_EQ(entries[1].size, (int64_t)12);
}

TEST(googledrive_upload_creates_or_updates) {
    std::vector<std::string> log;
    std::string lastBody, lastContentType;
    GoogleDriveProvider gd([&](const UltraNetHttpRequest& r, UltraNetResponse& s) {
        lastBody = Body(r); lastContentType = r.headers.Get("Content-Type");
        return FakeDrive(r, s, log);
    });
    Account a; Credentials c; c.token = "tok";
    // New name → multipart create with the parent folder id.
    REQUIRE(gd.Upload(a, c, WriteFile("new.txt", "data"), "/Docs/new.txt"));
    REQUIRE(log.back().find("POST https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart") == 0);
    REQUIRE(lastContentType.find("multipart/related") != std::string::npos);
    REQUIRE(lastBody.find("\"parents\":[\"f1\"]") != std::string::npos);
    REQUIRE(lastBody.find("\r\n\r\ndata\r\n") != std::string::npos);
    // Existing name → media update in place.
    REQUIRE(gd.Upload(a, c, WriteFile("report.pdf", "pdf"), "/Docs/report.pdf"));
    REQUIRE(log.back().find("PATCH https://www.googleapis.com/upload/drive/v3/files/x1?uploadType=media") == 0);
}

TEST(googledrive_mkdir_and_share_link) {
    std::vector<std::string> log;
    GoogleDriveProvider gd([&log](const UltraNetHttpRequest& r, UltraNetResponse& s) { return FakeDrive(r, s, log); });
    Account a; Credentials c; c.token = "tok";
    REQUIRE(gd.MakeDirectory(a, c, "/Docs"));            // exists → no POST
    for (auto& l : log) REQUIRE(l.rfind("POST", 0) != 0);
    REQUIRE(gd.MakeDirectory(a, c, "/Docs/Archive"));    // missing → POST files
    REQUIRE(log.back().find("POST https://www.googleapis.com/drive/v3/files") == 0);

    ShareLink link;
    REQUIRE(gd.CreateShareLink(a, c, "/Docs/report.pdf", {}, link));
    REQUIRE_EQ(link.url, std::string("https://drive.google.com/file/d/x1/view?usp=drivesdk"));
    REQUIRE_EQ(link.id, std::string("anyone1"));
}

TEST(googledrive_large_upload_is_resumable) {
    std::vector<std::string> log, ranges, bodies;
    bool followRedirectsOff = true;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        const std::string url = Decoded(req.url);
        if (url.find("uploadType=resumable") != std::string::npos) {
            log.push_back(url);
            REQUIRE_EQ(req.headers.Get("X-Upload-Content-Length"), std::string("25"));
            resp.statusCode = 200;
            resp.headers.Set("Location", "https://www.googleapis.com/upload/drive/v3/files?upload_id=abc");
            return Ok();
        }
        if (url.find("upload_id=abc") != std::string::npos) {
            ranges.push_back(req.headers.Get("Content-Range"));
            bodies.push_back(Body(req));
            followRedirectsOff = followRedirectsOff && !req.options.followRedirects;
            const bool last = ranges.size() == 3;
            Answer(resp, last ? 200 : 308, last ? R"({"id":"new"})" : "");
            return Ok();
        }
        return FakeDrive(req, resp, log);
    };
    GoogleDriveProvider gd(fake);
    gd.SetUploadLimits(/*simple=*/20, /*chunk=*/10);
    Account a; Credentials c; c.token = "tok";
    // New file in /Docs (parent f1) → resumable create.
    REQUIRE(gd.Upload(a, c, WriteFile("big.bin", "0123456789abcdefghijKLMNO"), "/Docs/big.bin"));
    REQUIRE_EQ(log.back(), std::string("https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable"));
    REQUIRE_EQ(ranges.size(), (size_t)3);
    REQUIRE_EQ(ranges[0], std::string("bytes 0-9/25"));
    REQUIRE_EQ(ranges[2], std::string("bytes 20-24/25"));
    REQUIRE_EQ(bodies[2], std::string("KLMNO"));
    REQUIRE(followRedirectsOff);
    // Existing file → resumable update on its id.
    ranges.clear(); bodies.clear();
    REQUIRE(gd.Upload(a, c, WriteFile("report.pdf", "0123456789abcdefghijKLMNO"), "/Docs/report.pdf"));
    REQUIRE_EQ(log.back(), std::string("https://www.googleapis.com/upload/drive/v3/files/x1?uploadType=resumable"));
    REQUIRE_EQ(ranges.size(), (size_t)3);
}
