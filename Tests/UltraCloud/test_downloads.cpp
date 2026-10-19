// Tests/UltraCloud/test_downloads.cpp
// Downloads go to disk as they arrive (HttpProviderBase::DownloadToFile):
// a cloud drive's file was held in memory whole and written afterwards, so a
// file larger than the memory a process could spare could not be fetched at
// all, and every download spent its own size in memory first. The fake
// transport here delivers an answer the way UltraNet's does when the request
// streams - onHeaders once the status is known, then the body in chunks
// through onDataChunk, `response.body` left empty.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "test_framework.h"

#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudWebDav.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraCloud;
namespace fs = std::filesystem;

namespace {

UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }

// Answers `status` with `chunks` streamed, as UltraNet does for a request
// that set onDataChunk. Returns what the transport would.
UltraNetResult Stream(const UltraNetHttpRequest& req, UltraNetResponse& resp, int status,
                      const std::vector<std::string>& chunks) {
    resp.statusCode = status;
    if (req.onHeaders && !req.onHeaders(resp)) {
        UltraNetResult r; r.success = false; r.code = UltraNetResultCode::Cancelled;
        r.message = "transfer ended by onHeaders";
        return r;
    }
    for (const std::string& c : chunks) {
        if (req.onDataChunk) req.onDataChunk(std::vector<uint8_t>(c.begin(), c.end()));
        else resp.body.insert(resp.body.end(), c.begin(), c.end());
    }
    return Ok();
}

std::string ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A clean folder of its own per test.
fs::path Folder(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ultracloud-downloads-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace

TEST(downloads_a_streamed_body_is_written_as_it_arrives) {
    bool streamed = false;
    int timeoutMs = 0, stallMs = 0;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        streamed = static_cast<bool>(req.onDataChunk);
        timeoutMs = req.options.timeoutMs;
        stallMs = req.options.stallTimeoutMs;
        return Stream(req, resp, 200, {"first ", "second ", "third"});
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    const fs::path dir = Folder("stream");
    const Result r = dropbox.Download(a, c, "/notes.txt", UltraCanvas::PathToUtf8(dir / "notes.txt"));
    REQUIRE(r);
    REQUIRE(streamed);
    REQUIRE_EQ(ReadAll(dir / "notes.txt"), std::string("first second third"));
    // As long as the file is large: no whole-transfer limit, a stall limit.
    REQUIRE_EQ(timeoutMs, -1);
    REQUIRE_EQ(stallMs, 30000);
    fs::remove_all(dir);
}

TEST(downloads_a_refusal_writes_nothing_and_keeps_its_reason) {
    HttpFn fake = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        return Stream(req, resp, 409,
                      {R"({"error_summary": "path/not_found/..", )", R"("error": {}})"});
    };
    DropboxProvider dropbox(fake);
    Account a; Credentials c; c.token = "tok";
    const fs::path dir = Folder("refused");
    const Result r = dropbox.Download(a, c, "/gone.txt", UltraCanvas::PathToUtf8(dir / "gone.txt"));
    REQUIRE(!r);
    REQUIRE(r.message.find("path/not_found") != std::string::npos);
    REQUIRE(!fs::exists(dir / "gone.txt"));
    fs::remove_all(dir);
}

TEST(downloads_a_transfer_lost_part_way_leaves_no_half_file) {
    HttpFn fake = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        Stream(req, resp, 200, {"the first half of the file"});
        UltraNetResult r; r.success = false; r.code = UltraNetResultCode::ConnectionReset;
        r.message = "Connection reset by peer";
        return r;
    };
    WebDavProvider webdav(fake);
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    const fs::path dir = Folder("lost");
    const Result r = webdav.Download(a, c, "/movie.mp4", UltraCanvas::PathToUtf8(dir / "movie.mp4"));
    REQUIRE(!r);
    REQUIRE(!fs::exists(dir / "movie.mp4"));
    fs::remove_all(dir);
}

TEST(downloads_a_file_that_cannot_be_made_ends_the_transfer) {
    bool bodyRead = false;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        UltraNetResult r = Stream(req, resp, 200, {});
        if (r) bodyRead = true;
        return r;
    };
    WebDavProvider webdav(fake);
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    const fs::path dir = Folder("nowhere");
    // A folder that is not there: the file cannot be opened.
    const Result r = webdav.Download(a, c, "/x.bin",
                                     UltraCanvas::PathToUtf8(dir / "missing" / "x.bin"));
    REQUIRE(r.code == ResultCode::IoError);
    REQUIRE(!bodyRead);
    fs::remove_all(dir);
}

// Google Drive says it is limiting requests with a 403 whose body names the
// reason. Streamed, that body still reaches the throttling check: the request
// is sent again and the file written from the second answer only.
TEST(downloads_a_throttled_download_is_asked_again_and_written_once) {
    int downloads = 0;
    HttpFn fake = [&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        if (req.url.find("alt=media") == std::string::npos) {
            const std::string list =
                    R"({"files":[{"id":"f1","name":"big.bin","mimeType":"application/octet-stream"}]})";
            resp.statusCode = 200;
            resp.body.assign(list.begin(), list.end());
            return Ok();
        }
        if (++downloads == 1) {
            resp.headers.Set("Retry-After", "1");
            return Stream(req, resp, 403,
                          {R"({"error":{"errors":[{"reason":"rateLimitExceeded"}],)",
                           R"("message":"Rate Limit Exceeded"}})"});
        }
        return Stream(req, resp, 200, {"payload"});
    };
    RateLimitPolicy policy;
    policy.maxRetries = 2;
    policy.wait = [](long long) { return true; };   // no real waiting in a test
    RateLimitPolicy previous = SetThreadRateLimitPolicy(policy);

    GoogleDriveProvider drive(fake);
    Account a; Credentials c; c.token = "tok";
    const fs::path dir = Folder("throttled");
    const Result r = drive.Download(a, c, "/big.bin", UltraCanvas::PathToUtf8(dir / "big.bin"));
    SetThreadRateLimitPolicy(std::move(previous));

    REQUIRE(r);
    REQUIRE_EQ(downloads, 2);
    REQUIRE_EQ(ReadAll(dir / "big.bin"), std::string("payload"));
    fs::remove_all(dir);
}

// A transport that hands the body over whole (no streaming) is still written.
TEST(downloads_a_body_handed_over_whole_is_written) {
    HttpFn fake = [](const UltraNetHttpRequest&, UltraNetResponse& resp) {
        resp.statusCode = 200;
        const std::string body = "all at once";
        resp.body.assign(body.begin(), body.end());
        return Ok();
    };
    WebDavProvider webdav(fake);
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    const fs::path dir = Folder("whole");
    REQUIRE(webdav.Download(a, c, "/a.txt", UltraCanvas::PathToUtf8(dir / "a.txt")));
    REQUIRE_EQ(ReadAll(dir / "a.txt"), std::string("all at once"));
    fs::remove_all(dir);
}
