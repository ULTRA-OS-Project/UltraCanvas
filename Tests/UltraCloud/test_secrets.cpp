// Tests/UltraCloud/test_secrets.cpp
// The secret stores: MemorySecretStore round trip, VaultSecretStore on an
// open UltraVault (and its refusal on a closed one), and the one-way
// migration of the obfuscated per-account files earlier builds wrote.
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudSecrets.h>
#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode (legacy files)
#include <UltraVault/UltraVault.h>

#include <cstdint>
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

Credentials SampleCredentials() {
    Credentials in; in.username = "erika"; in.password = "s3cr3t-pass"; in.token = "";
    in.refreshToken = "rt-1"; in.tokenExpiresAt = 1'900'000'000;
    return in;
}

void RequireSample(const Credentials& out) {
    REQUIRE_EQ(out.username, std::string("erika"));
    REQUIRE_EQ(out.password, std::string("s3cr3t-pass"));
    REQUIRE_EQ(out.token, std::string(""));
    REQUIRE_EQ(out.refreshToken, std::string("rt-1"));
    REQUIRE_EQ(out.tokenExpiresAt, (int64_t)1'900'000'000);
}
} // namespace

TEST(memory_secret_store_round_trip) {
    MemorySecretStore store;
    REQUIRE(!store.Store("", SampleCredentials()));
    REQUIRE(store.Store("nextcloud-erika", SampleCredentials()));

    Credentials out;
    REQUIRE(store.Retrieve("nextcloud-erika", out));
    RequireSample(out);

    REQUIRE(store.Remove("nextcloud-erika"));
    REQUIRE(!store.Retrieve("nextcloud-erika", out));
    REQUIRE(!store.Remove("nextcloud-erika"));
}

TEST(vault_secret_store_needs_an_open_vault) {
    UltraVault::Shutdown();
    VaultSecretStore store;
    Credentials out;
    REQUIRE(!store.Store("nextcloud-erika", SampleCredentials()));   // refused, not dropped
    REQUIRE(!store.Retrieve("nextcloud-erika", out));
    REQUIRE(!store.Remove("nextcloud-erika"));

    UltraVault::Config config;
    config.backend = UltraVault::Backend::Memory;
    REQUIRE(UltraVault::Initialize(config).IsOk());
    REQUIRE(store.Store("nextcloud-erika", SampleCredentials()));
    REQUIRE(store.Retrieve("nextcloud-erika", out));
    RequireSample(out);
    // Namespaced under the cloud vendor, one entry per field.
    UltraVault::SecretValue v;
    REQUIRE(UltraVault::Get("cloud.nextcloud-erika.password", v).IsOk());
    REQUIRE_EQ(v.AsString(), std::string("s3cr3t-pass"));

    REQUIRE(store.Remove("nextcloud-erika"));
    REQUIRE(!store.Retrieve("nextcloud-erika", out));
    REQUIRE(!store.Remove("nextcloud-erika"));
    UltraVault::Shutdown();
}

// The old FileSecretStore layout: "<id>.secret" holding field-name / value
// line pairs, the value XOR-ed against cloud.key and base64-encoded.
TEST(legacy_file_secrets_migrate_once) {
    const std::string dir = TempDir("legacy-secrets");
    const std::vector<uint8_t> key(32, 0x3C);
    {
        std::ofstream ks(fs::path(dir) / "cloud.key", std::ios::binary);
        ks.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
    }
    auto obfuscate = [&key](const std::string& plain) {
        std::vector<uint8_t> bytes(plain.begin(), plain.end());
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] ^= key[i % key.size()];
        return UltraNet_Base64Encode(bytes, false);
    };
    auto writeSecret = [&](const std::string& fileStem, const Credentials& c) {
        std::ofstream os(fs::path(dir) / (fileStem + ".secret"), std::ios::binary);
        os << "username\n" << obfuscate(c.username) << "\n"
           << "password\n" << obfuscate(c.password) << "\n"
           << "token\n"    << obfuscate(c.token)    << "\n"
           << "refresh\n"  << obfuscate(c.refreshToken) << "\n"
           << "expires\n"  << obfuscate(std::to_string(c.tokenExpiresAt)) << "\n";
    };
    writeSecret("nextcloud_erika", SampleCredentials());     // id "nextcloud-erika"
    Credentials other; other.username = "o"; other.token = "tok"; other.tokenExpiresAt = 5;
    writeSecret("dropbox_o", other);                          // id "dropbox-o"
    writeSecret("orphan", other);                             // no such account

    Account a; a.accountId = "nextcloud-erika";
    Account b; b.accountId = "dropbox-o";
    Account c; c.accountId = "never-stored";

    MemorySecretStore into;
    REQUIRE_EQ(MigrateLegacyFileSecrets(dir, {a, b, c}, into), 2);

    Credentials out;
    REQUIRE(into.Retrieve("nextcloud-erika", out));
    RequireSample(out);
    REQUIRE(into.Retrieve("dropbox-o", out));
    REQUIRE_EQ(out.token, std::string("tok"));
    REQUIRE_EQ(out.tokenExpiresAt, (int64_t)5);
    REQUIRE(!into.Retrieve("never-stored", out));

    // The migrated files are gone; the orphan and the key stay because a
    // secret file is still there.
    REQUIRE(!fs::exists(fs::path(dir) / "nextcloud_erika.secret"));
    REQUIRE(!fs::exists(fs::path(dir) / "dropbox_o.secret"));
    REQUIRE(fs::exists(fs::path(dir) / "orphan.secret"));
    REQUIRE(fs::exists(fs::path(dir) / "cloud.key"));

    // Second run: nothing left to carry; a store that refuses keeps the file.
    REQUIRE_EQ(MigrateLegacyFileSecrets(dir, {a, b, c}, into), 0);
    std::error_code ec;
    fs::remove(fs::path(dir) / "orphan.secret", ec);
    REQUIRE_EQ(MigrateLegacyFileSecrets(dir, {a, b, c}, into), 0);
    REQUIRE(!fs::exists(fs::path(dir) / "cloud.key"));
    REQUIRE(!fs::exists(dir));                                // emptied, so removed

    // No directory at all is not an error.
    REQUIRE_EQ(MigrateLegacyFileSecrets(dir, {a}, into), 0);
}
