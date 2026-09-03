// Tests/UltraCloud/test_service.cpp
// CloudService: add an account (id, default, secrets), UploadAndShare end to
// end against a fake Nextcloud (MKCOL, PUT, OCS POST in order).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloud.h>
#include <UltraCloud/UltraCloudMemory.h>
#include <UltraCloud/UltraCloudNextcloud.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraCloud;
namespace fs = std::filesystem;

namespace {
std::string TempDir(const std::string& tag) {
    fs::path p = fs::temp_directory_path() / ("ultracloud-test-" + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}
} // namespace

TEST(service_upload_and_share_through_default_account) {
    std::vector<std::string> calls;   // "METHOD url"
    HttpFn fake = [&calls](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        std::string m = req.method == UltraNetHttpMethod::Post ? "POST"
                      : req.method == UltraNetHttpMethod::Put  ? "PUT"
                      : !req.customMethod.empty() ? req.customMethod : "GET";
        calls.push_back(m + " " + req.url);
        UltraNetResult r; r.success = true;
        if (m == "MKCOL") { resp.statusCode = 405; r.success = false; r.message = "exists"; return r; }
        if (m == "PUT")   { resp.statusCode = 201; return r; }
        if (m == "POST") {
            const std::string answer =
                R"({"ocs":{"meta":{"statuscode":200},"data":{"id":1,"url":"https://cloud.example.com/s/LINK"}}})";
            resp.statusCode = 200; resp.body.assign(answer.begin(), answer.end());
            return r;
        }
        resp.statusCode = 207;
        const std::string ms = "<d:multistatus xmlns:d=\"DAV:\"></d:multistatus>";
        resp.body.assign(ms.begin(), ms.end());
        return r;
    };
    RegisterProvider(std::make_shared<NextcloudProvider>(fake));

    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-service", ":memory:"));
    FileSecretStore secrets(TempDir("service-secrets"));
    CloudService service(accounts, secrets);

    Account a; a.providerId = "nextcloud"; a.serverUrl = "https://cloud.example.com"; a.username = "erika";
    Credentials c; c.password = "app-pw";
    REQUIRE(service.AddAccount(a, c, /*verify=*/true));
    REQUIRE_EQ(a.accountId, std::string("nextcloud-erika-cloud-example-com"));
    REQUIRE(a.isDefault);
    REQUIRE_EQ(a.remoteFolder, std::string("/Shared from ULTRA OS"));
    Credentials stored;
    REQUIRE(secrets.Retrieve(a.accountId, stored));
    REQUIRE_EQ(stored.username, std::string("erika"));
    REQUIRE_EQ(stored.password, std::string("app-pw"));
    calls.clear();

    const std::string dir = TempDir("service-files");
    const std::string local = dir + "/Q3 report.pdf";
    std::ofstream(local) << "%PDF-1.4 demo";

    ShareLink link; std::string remotePath;
    Account def;
    REQUIRE(accounts.GetDefault(def));
    REQUIRE(service.UploadAndShare(def.accountId, local, "", {}, link, &remotePath));
    REQUIRE_EQ(link.url, std::string("https://cloud.example.com/s/LINK"));
    REQUIRE_EQ(remotePath, std::string("/Shared from ULTRA OS/Q3 report.pdf"));
    REQUIRE_EQ(calls.size(), (size_t)3);
    REQUIRE(calls[0].rfind("MKCOL ", 0) == 0);
    REQUIRE(calls[0].find("/remote.php/dav/files/erika/Shared%20from%20ULTRA%20OS") != std::string::npos);
    REQUIRE(calls[1].rfind("PUT ", 0) == 0);
    REQUIRE(calls[1].find("Q3%20report.pdf") != std::string::npos);
    REQUIRE(calls[2].rfind("POST ", 0) == 0);

    REQUIRE(service.RemoveAccount(a.accountId));
    REQUIRE(!secrets.Retrieve(a.accountId, stored));
}

TEST(service_reports_missing_provider_and_account) {
    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-service2", ":memory:"));
    FileSecretStore secrets(TempDir("service2-secrets"));
    CloudService service(accounts, secrets);
    Account a; a.providerId = "no-such-provider"; a.username = "x";
    REQUIRE(service.AddAccount(a, {}, false).code == ResultCode::Unsupported);
    std::vector<Entry> entries;
    REQUIRE(service.List("missing", "/", entries).code == ResultCode::NotFound);
}

TEST(memory_provider_round_trip) {
    RegisterBuiltInProviders();
    MemoryProvider::Clear();
    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-memory", ":memory:"));
    FileSecretStore secrets(TempDir("memory-secrets"));
    CloudService service(accounts, secrets);

    Account a; a.providerId = "memory"; a.username = "demo"; a.displayName = "Demo";
    REQUIRE(service.AddAccount(a, {}, true));
    MemoryProvider::Seed(a.accountId, "/Documents", -1);
    MemoryProvider::Seed(a.accountId, "/Documents/notes.txt", 12, "today");
    MemoryProvider::Seed(a.accountId, "/photo.jpg", 2048);

    std::vector<Entry> root;
    REQUIRE(service.List(a.accountId, "/", root));
    REQUIRE_EQ(root.size(), (size_t)2);
    REQUIRE(root[0].isDirectory);
    REQUIRE_EQ(root[0].name, std::string("Documents"));
    REQUIRE_EQ(root[1].name, std::string("photo.jpg"));

    std::vector<Entry> docs;
    REQUIRE(service.List(a.accountId, "/Documents", docs));
    REQUIRE_EQ(docs.size(), (size_t)1);
    REQUIRE_EQ(docs[0].path, std::string("/Documents/notes.txt"));

    ShareLink link;
    REQUIRE(service.CreateShareLink(a.accountId, "/photo.jpg", {}, link));
    REQUIRE(link.url.rfind("https://demo.ultra-os.local/s/", 0) == 0);
    REQUIRE(service.CreateShareLink(a.accountId, "/missing", {}, link).code == ResultCode::NotFound);

    const std::string dir = TempDir("memory-files");
    std::ofstream(dir + "/up.bin") << "12345";
    std::string remote;
    REQUIRE(service.UploadAndShare(a.accountId, dir + "/up.bin", "/Documents", {}, link, &remote));
    REQUIRE_EQ(remote, std::string("/Documents/up.bin"));
    REQUIRE(service.List(a.accountId, "/Documents", docs));
    REQUIRE_EQ(docs.size(), (size_t)2);
}
