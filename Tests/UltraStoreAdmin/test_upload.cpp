// Tests/UltraStoreAdmin/test_upload.cpp
// The publish folder onto a host, through a transport that only records:
// the order (catalog.json last), what is skipped as unchanged, what a broken
// upload leaves for the next one, the schemes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "admin_helpers.h"

#include "Uploader.h"

#include <filesystem>

using namespace UltraStoreAdmin;
using namespace admin_test;

namespace {

struct RecordingTransport : IUploadTransport {
    std::vector<std::string> folders, puts;
    std::string failOn;   // the relative path whose Put fails
    UltraStore::Result<void> EnsureFolder(const std::string& dir) override {
        folders.push_back(dir);
        return UltraStore::Result<void>::Succeed();
    }
    UltraStore::Result<void> Put(const std::string& relative, const std::string& local, uint64_t size) override {
        if (relative == failOn) return UltraStore::Result<void>::Failure(UltraStore::ErrorCode::NetworkError, "the server hung up");
        REQUIRE_EQ(static_cast<uint64_t>(ReadFile(local).size()), size);
        puts.push_back(relative);
        return UltraStore::Result<void>::Succeed();
    }
};

std::string PublishFolder(const std::string& dir) {
    const std::string out = Join(dir, "out");
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(Join(out, "packages")));
    std::filesystem::create_directories(UltraCanvas::PathFromUtf8(Join(out, "media/org.ultraos.hello")));
    REQUIRE(WriteFile(Join(out, "catalog.json"), "{\"schema\": 2}"));
    REQUIRE(WriteFile(Join(out, "catalog.json.sig"), "abcd"));
    REQUIRE(WriteFile(Join(out, "packages/" + std::string(64, 'a') + ".zip"), "zip bytes"));
    REQUIRE(WriteFile(Join(out, "media/org.ultraos.hello/icon.png"), "PNG"));
    return out;
}

} // namespace

TEST(the_catalog_goes_last_and_an_unchanged_file_is_not_sent_again) {
    const std::string dir = TempDir("upload");
    const std::string out = PublishFolder(dir);
    const auto files = FilesToSend(out);
    REQUIRE(files.Ok());
    REQUIRE_EQ(files.Value().size(), size_t(4));
    REQUIRE_EQ(files.Value()[0], std::string("media/org.ultraos.hello/icon.png"));
    REQUIRE_EQ(files.Value()[2], std::string("catalog.json.sig"));
    REQUIRE_EQ(files.Value()[3], std::string("catalog.json"));

    UploadOptions options;
    options.publishDirUtf8 = out;
    options.stateFileUtf8 = UploadStatePath(dir);
    size_t progressCalls = 0, lastTotal = 0;
    options.onProgress = [&](const UploadProgress& p) { ++progressCalls; lastTotal = p.filesTotal; };
    RecordingTransport t;
    const auto first = UploadPublishFolder(t, "sftp://deploy@host/srv/store/v1/", options);
    REQUIRE(first.Ok());
    REQUIRE_EQ(first.Value().filesSent, size_t(4));
    REQUIRE_EQ(first.Value().filesSkipped, size_t(0));
    REQUIRE_EQ(first.Value().bytesSent, uint64_t(13 + 4 + 9 + 3));
    REQUIRE_EQ(progressCalls, size_t(5));   // before each file and once at the end
    REQUIRE_EQ(lastTotal, size_t(4));
    REQUIRE_EQ(t.puts.back(), std::string("catalog.json"));
    REQUIRE_EQ(t.puts[2], std::string("catalog.json.sig"));
    // The folders, outermost first, once each.
    REQUIRE_EQ(t.folders.size(), size_t(3));
    REQUIRE_EQ(t.folders[0], std::string("media"));
    REQUIRE_EQ(t.folders[1], std::string("media/org.ultraos.hello"));
    REQUIRE_EQ(t.folders[2], std::string("packages"));

    // Nothing changed: nothing sent. The trailing slash of the URL does not make another target.
    RecordingTransport t2;
    const auto second = UploadPublishFolder(t2, "sftp://deploy@host/srv/store/v1", options);
    REQUIRE(second.Ok());
    REQUIRE_EQ(second.Value().filesSent, size_t(0));
    REQUIRE_EQ(second.Value().filesSkipped, size_t(4));
    REQUIRE(t2.puts.empty());

    // The catalog changed: it alone goes; a file gone from the folder is said, not deleted.
    REQUIRE(WriteFile(Join(out, "catalog.json"), "{\"schema\": 2, \"sequence\": 3}"));
    std::filesystem::remove(UltraCanvas::PathFromUtf8(Join(out, "media/org.ultraos.hello/icon.png")));
    RecordingTransport t3;
    const auto third = UploadPublishFolder(t3, "sftp://deploy@host/srv/store/v1", options);
    REQUIRE(third.Ok());
    REQUIRE_EQ(third.Value().filesSent, size_t(1));
    REQUIRE_EQ(t3.puts[0], std::string("catalog.json"));
    REQUIRE_EQ(third.Value().warnings.size(), size_t(1));
    REQUIRE(third.Value().warnings[0].find("icon.png") != std::string::npos);

    // Another target starts from nothing; --everything sends all to a known one.
    RecordingTransport t4;
    REQUIRE_EQ(UploadPublishFolder(t4, "https://other.example/store", options).Value().filesSent, size_t(3));
    options.everything = true;
    RecordingTransport t5;
    REQUIRE_EQ(UploadPublishFolder(t5, "sftp://deploy@host/srv/store/v1", options).Value().filesSent, size_t(3));
    RemoveDir(dir);
}

TEST(a_broken_upload_goes_on_from_where_it_stopped) {
    const std::string dir = TempDir("upload-resume");
    const std::string out = PublishFolder(dir);
    UploadOptions options;
    options.publishDirUtf8 = out;
    options.stateFileUtf8 = UploadStatePath(dir);
    RecordingTransport broken;
    broken.failOn = "packages/" + std::string(64, 'a') + ".zip";
    const auto first = UploadPublishFolder(broken, "ftps://host/store", options);
    REQUIRE(!first.Ok());
    REQUIRE(first.error.message.find("stopped at packages/") != std::string::npos);
    REQUIRE(first.error.detail.find("hung up") != std::string::npos);
    REQUIRE_EQ(broken.puts.size(), size_t(1));   // the icon went; the catalog never did
    RecordingTransport working;
    const auto second = UploadPublishFolder(working, "ftps://host/store", options);
    REQUIRE(second.Ok());
    REQUIRE_EQ(second.Value().filesSent, size_t(3));
    REQUIRE_EQ(second.Value().filesSkipped, size_t(1));
    REQUIRE_EQ(working.puts.back(), std::string("catalog.json"));
    // Without a state file nothing is remembered.
    options.stateFileUtf8.clear();
    RecordingTransport again;
    REQUIRE_EQ(UploadPublishFolder(again, "ftps://host/store", options).Value().filesSent, size_t(4));
    RemoveDir(dir);
}

TEST(the_schemes_the_uploader_speaks) {
    REQUIRE(SchemeOf("sftp://deploy@host/path") == UploadScheme::Sftp);
    REQUIRE(SchemeOf("FTPS://host/path") == UploadScheme::Ftps);
    REQUIRE(SchemeOf("ftp://host") == UploadScheme::Ftp);
    REQUIRE(SchemeOf("https://host/dav/store") == UploadScheme::Https);
    REQUIRE(SchemeOf("http://host/dav/store") == UploadScheme::Unknown);   // the password would go in clear
    REQUIRE(SchemeOf("/srv/store") == UploadScheme::Unknown);
    UploadTarget target;
    target.url = "http://host/store";
    REQUIRE(!CreateUltraNetTransport(target).Ok());
    target.url = "sftp://host/store";
    REQUIRE(CreateUltraNetTransport(target).Ok());
    target.url = "https://host/store";
    REQUIRE(CreateUltraNetTransport(target).Ok());
    REQUIRE_EQ(SchemeName(UploadScheme::Https), std::string("HTTPS PUT"));
    REQUIRE(!FilesToSend("/nowhere/at/all").Ok());
}
