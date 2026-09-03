// Tests/UltraCloud/test_secrets.cpp
// FileSecretStore: round trip, no plaintext on disk, removal.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudSecrets.h>

#include <filesystem>
#include <fstream>
#include <string>

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

TEST(file_secret_store_round_trip) {
    const std::string dir = TempDir("secrets");
    FileSecretStore store(dir);
    Credentials in; in.username = "erika"; in.password = "s3cr3t-pass"; in.token = "";
    REQUIRE(store.Store("nextcloud-erika", in));

    Credentials out;
    REQUIRE(store.Retrieve("nextcloud-erika", out));
    REQUIRE_EQ(out.username, std::string("erika"));
    REQUIRE_EQ(out.password, std::string("s3cr3t-pass"));
    REQUIRE_EQ(out.token, std::string(""));

    // Not plaintext on disk.
    bool plaintextFound = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::ifstream is(entry.path(), std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        if (content.find("s3cr3t-pass") != std::string::npos) plaintextFound = true;
    }
    REQUIRE(!plaintextFound);

    REQUIRE(store.Remove("nextcloud-erika"));
    REQUIRE(!store.Retrieve("nextcloud-erika", out));
    REQUIRE(!store.Remove("nextcloud-erika"));
}
