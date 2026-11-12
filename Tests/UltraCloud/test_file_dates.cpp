// Tests/UltraCloud/test_file_dates.cpp
// A copy to or from a drive keeps the file's dates: every provider sends the
// local file's modification date (and its creation date where the service
// keeps one) with an upload, and gives a downloaded file the date the server
// reports. Before, an upload was dated the moment it arrived and a download
// the moment it was written. Each test fakes the service's transport and
// checks the request carried the date, or the local copy got it.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "test_framework.h"
#include "test_request_body.h"

#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudFtp.h>
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudMemory.h>
#include <UltraCloud/UltraCloudOneDrive.h>
#include <UltraCloud/UltraCloudWebDav.h>
#include <UltraNet/UltraNetUrl.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasFileTimes.h"
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraCloud;
namespace fs = std::filesystem;

namespace {

// 2010-01-01 00:00:00 UTC - long before any test runs.
constexpr int64_t kThen = 1262304000;
const char* kThenRfc3339 = "2010-01-01T00:00:00Z";
const char* kThenHttp = "Fri, 01 Jan 2010 00:00:00 GMT";

UltraNetResult Ok() { UltraNetResult r; r.success = true; return r; }

fs::path Folder(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ultracloud-dates-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// A local file dated kThen.
std::string OldFile(const fs::path& dir, const std::string& name, const std::string& content) {
    const fs::path p = dir / name;
    { std::ofstream(p, std::ios::binary) << content; }
    UltraCanvas::FileTimes t;
    t.modified = UltraCanvas::FileTimes::FromSeconds(kThen);
    UltraCanvas::SetFileTimes(UltraCanvas::PathToUtf8(p), t);
    return UltraCanvas::PathToUtf8(p);
}

int64_t ModifiedSeconds(const std::string& path) {
    UltraCanvas::FileTimes t;
    UltraCanvas::GetFileTimes(path, t);
    return UltraCanvas::FileTimes::ToSeconds(t.modified);
}

// Answers `status` with `body` streamed, the headers set first, as UltraNet
// does for a download.
UltraNetResult Stream(const UltraNetHttpRequest& req, UltraNetResponse& resp, int status,
                      const std::string& body) {
    resp.statusCode = status;
    if (req.onHeaders && !req.onHeaders(resp)) {
        UltraNetResult r; r.success = false; r.code = UltraNetResultCode::Cancelled;
        return r;
    }
    if (req.onDataChunk) req.onDataChunk(std::vector<uint8_t>(body.begin(), body.end()));
    else resp.body.assign(body.begin(), body.end());
    return Ok();
}

void Json(UltraNetResponse& resp, int status, const std::string& body) {
    resp.statusCode = status;
    resp.body.assign(body.begin(), body.end());
}

} // namespace

// ---- WebDAV / Nextcloud -------------------------------------------------------

TEST(dates_webdav_upload_sends_x_oc_mtime) {
    const fs::path dir = Folder("webdav-up");
    std::string mtime;
    WebDavProvider dav([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        mtime = req.headers.Get("X-OC-Mtime");
        Json(resp, 201, "");
        return Ok();
    });
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    REQUIRE(dav.Upload(a, c, OldFile(dir, "a.txt", "x"), "/a.txt"));
    REQUIRE_EQ(mtime, std::to_string(kThen));
}

TEST(dates_webdav_download_takes_last_modified) {
    const fs::path dir = Folder("webdav-down");
    WebDavProvider dav([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        resp.headers.Set("Last-Modified", kThenHttp);
        return Stream(req, resp, 200, "bytes");
    });
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(dav.Download(a, c, "/a.txt", local));
    REQUIRE_EQ(ModifiedSeconds(local), kThen);
}

TEST(dates_webdav_download_without_a_date_keeps_the_file) {
    const fs::path dir = Folder("webdav-nodate");
    WebDavProvider dav([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        return Stream(req, resp, 200, "bytes");
    });
    Account a; a.serverUrl = "https://dav.example.org/files"; Credentials c;
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(dav.Download(a, c, "/a.txt", local));
    REQUIRE(ModifiedSeconds(local) > kThen);   // the moment it was written
}

// ---- Dropbox ------------------------------------------------------------------

TEST(dates_dropbox_upload_sets_client_modified) {
    const fs::path dir = Folder("dropbox-up");
    std::string arg;
    DropboxProvider db([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        arg = req.headers.Get("Dropbox-API-Arg");
        Json(resp, 200, R"({"name":"a.txt"})");
        return Ok();
    });
    Account a; Credentials c; c.token = "tok";
    REQUIRE(db.Upload(a, c, OldFile(dir, "a.txt", "x"), "/a.txt"));
    REQUIRE(arg.find(std::string("\"client_modified\":\"") + kThenRfc3339 + "\"") != std::string::npos);
}

TEST(dates_dropbox_download_takes_client_modified) {
    const fs::path dir = Folder("dropbox-down");
    DropboxProvider db([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        resp.headers.Set("Dropbox-API-Result",
                         std::string(R"({"name":"a.txt","client_modified":")") + kThenRfc3339 + "\"}");
        return Stream(req, resp, 200, "bytes");
    });
    Account a; Credentials c; c.token = "tok";
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(db.Download(a, c, "/a.txt", local));
    REQUIRE_EQ(ModifiedSeconds(local), kThen);
}

// ---- Google Drive -------------------------------------------------------------

namespace {
// The folder /Docs (id f1) holding a.txt (id x9), dated kThen.
UltraNetResult FakeDriveWithDates(const UltraNetHttpRequest& req, UltraNetResponse& resp,
                                  std::vector<std::string>& bodies) {
    const std::string url = UltraNet_UrlDecode(req.url);
    bodies.push_back(RequestBody(req));
    if (url.find("/files?q=") != std::string::npos) {
        if (url.find("name = 'Docs'") != std::string::npos)
            Json(resp, 200, R"({"files":[{"id":"f1","mimeType":"application/vnd.google-apps.folder"}]})");
        else if (url.find("name = 'a.txt'") != std::string::npos)
            Json(resp, 200, R"({"files":[{"id":"x9","mimeType":"text/plain"}]})");
        else
            Json(resp, 200, R"({"files":[]})");
        return Ok();
    }
    if (url.find("/files/x9?alt=media") != std::string::npos) return Stream(req, resp, 200, "bytes");
    if (url.find("/files/x9?fields=modifiedTime,createdTime") != std::string::npos) {
        Json(resp, 200, std::string(R"({"modifiedTime":")") + kThenRfc3339 +
                        R"(","createdTime":"2009-01-01T00:00:00.000Z"})");
        return Ok();
    }
    Json(resp, 200, R"({"id":"new"})");
    return Ok();
}
} // namespace

TEST(dates_googledrive_create_sends_modified_time) {
    const fs::path dir = Folder("gdrive-up");
    std::vector<std::string> bodies;
    GoogleDriveProvider gd([&](const UltraNetHttpRequest& r, UltraNetResponse& s) {
        return FakeDriveWithDates(r, s, bodies);
    });
    Account a; Credentials c; c.token = "tok";
    REQUIRE(gd.Upload(a, c, OldFile(dir, "new.txt", "data"), "/Docs/new.txt"));
    REQUIRE(bodies.back().find(std::string("\"modifiedTime\":\"") + kThenRfc3339 + "\"") !=
            std::string::npos);
}

TEST(dates_googledrive_replace_dates_the_file_after) {
    const fs::path dir = Folder("gdrive-replace");
    std::vector<std::string> bodies;
    std::vector<std::string> methods;
    GoogleDriveProvider gd([&](const UltraNetHttpRequest& r, UltraNetResponse& s) {
        methods.push_back(r.customMethod + " " + UltraNet_UrlDecode(r.url));
        return FakeDriveWithDates(r, s, bodies);
    });
    Account a; Credentials c; c.token = "tok";
    REQUIRE(gd.Upload(a, c, OldFile(dir, "a.txt", "data"), "/Docs/a.txt"));
    // The bytes in place (uploadType=media), then the date in the metadata.
    REQUIRE(methods.back().find("PATCH https://www.googleapis.com/drive/v3/files/x9") == 0);
    REQUIRE(bodies.back().find(std::string("\"modifiedTime\":\"") + kThenRfc3339 + "\"") !=
            std::string::npos);
    REQUIRE(bodies.back().find("createdTime") == std::string::npos);   // a replaced file keeps its own
}

TEST(dates_googledrive_download_takes_the_metadata_dates) {
    const fs::path dir = Folder("gdrive-down");
    std::vector<std::string> bodies;
    GoogleDriveProvider gd([&](const UltraNetHttpRequest& r, UltraNetResponse& s) {
        return FakeDriveWithDates(r, s, bodies);
    });
    Account a; Credentials c; c.token = "tok";
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(gd.Download(a, c, "/Docs/a.txt", local));
    REQUIRE_EQ(ModifiedSeconds(local), kThen);
    if (UltraCanvas::CanCopyCreationTime()) {
        UltraCanvas::FileTimes t;
        UltraCanvas::GetFileTimes(local, t);
        REQUIRE_EQ(UltraCanvas::FileTimes::ToSeconds(t.created), (int64_t)1230768000);
    }
}

// ---- OneDrive -----------------------------------------------------------------

TEST(dates_onedrive_small_upload_dates_the_item_after) {
    const fs::path dir = Folder("onedrive-up");
    std::vector<std::string> calls;
    std::string patchBody;
    OneDriveProvider od([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        calls.push_back((req.method == UltraNetHttpMethod::Put ? "PUT " : req.customMethod + " ") +
                        UltraNet_UrlDecode(req.url));
        if (req.customMethod == "PATCH") patchBody = RequestBody(req);
        Json(resp, 200, R"({"id":"1"})");
        return Ok();
    });
    Account a; Credentials c; c.token = "tok";
    REQUIRE(od.Upload(a, c, OldFile(dir, "a.txt", "small"), "/Shared/a.txt"));
    REQUIRE_EQ(calls.size(), (size_t)2);
    REQUIRE(calls[0].find("PUT ") == 0);
    REQUIRE(calls[1].find("PATCH ") == 0);
    REQUIRE(calls[1].find("root:/Shared/a.txt:") != std::string::npos);
    REQUIRE(patchBody.find(std::string("\"lastModifiedDateTime\":\"") + kThenRfc3339 + "\"") !=
            std::string::npos);
}

TEST(dates_onedrive_download_takes_file_system_info) {
    const fs::path dir = Folder("onedrive-down");
    OneDriveProvider od([&](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        if (req.url.find("/content") != std::string::npos) return Stream(req, resp, 200, "bytes");
        Json(resp, 200, std::string(R"({"fileSystemInfo":{"lastModifiedDateTime":")") +
                        kThenRfc3339 + R"(","createdDateTime":"2009-01-01T00:00:00Z"}})");
        return Ok();
    });
    Account a; Credentials c; c.token = "tok";
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(od.Download(a, c, "/Shared/a.txt", local));
    REQUIRE_EQ(ModifiedSeconds(local), kThen);
}

// ---- FTP / SFTP ---------------------------------------------------------------

TEST(dates_ftp_upload_asks_the_server_to_date_the_file) {
    const fs::path dir = Folder("ftp-up");
    int64_t asked = -1;
    FtpOps ops;
    ops.upload = [&](const std::string&, const std::string&, const UltraNetFtpOptions& opt) {
        asked = opt.setRemoteModificationTime;
        return Ok();
    };
    FtpProvider ftp(ops);
    Account a; a.serverUrl = "ftp://files.example.org"; Credentials c;
    REQUIRE(ftp.Upload(a, c, OldFile(dir, "a.txt", "x"), "/a.txt"));
    REQUIRE_EQ(asked, kThen);
}

TEST(dates_ftp_download_gives_the_copy_the_server_date) {
    const fs::path dir = Folder("ftp-down");
    FtpOps ops;
    ops.download = [&](const std::string&, const std::string& localPath, const UltraNetFtpOptions& opt) {
        { std::ofstream(UltraCanvas::PathFromUtf8(localPath), std::ios::binary) << "bytes"; }
        if (opt.onRemoteModificationTime) opt.onRemoteModificationTime(kThen);
        return Ok();
    };
    FtpProvider ftp(ops);
    Account a; a.serverUrl = "ftp://files.example.org"; Credentials c;
    const std::string local = UltraCanvas::PathToUtf8(dir / "a.txt");
    REQUIRE(ftp.Download(a, c, "/a.txt", local));
    REQUIRE_EQ(ModifiedSeconds(local), kThen);
}

// ---- The in-memory stand-in -----------------------------------------------------

TEST(dates_memory_round_trip_keeps_the_date) {
    MemoryProvider::Clear();
    const fs::path dir = Folder("memory");
    MemoryProvider mem;
    Account a; a.accountId = "dates"; Credentials c;
    REQUIRE(mem.Upload(a, c, OldFile(dir, "a.txt", "x"), "/a.txt"));
    std::vector<Entry> entries;
    REQUIRE(mem.List(a, c, "/", entries));
    REQUIRE_EQ(entries.size(), (size_t)1);
    REQUIRE_EQ(entries[0].modified, std::string(kThenRfc3339));
    const std::string back = UltraCanvas::PathToUtf8(dir / "back.txt");
    REQUIRE(mem.Download(a, c, "/a.txt", back));
    REQUIRE_EQ(ModifiedSeconds(back), kThen);
    MemoryProvider::Clear();
}
