// UltraCloud/core/UltraCloudSecrets.cpp
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudSecrets.h>

#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode / Decode

#ifdef ULTRACLOUD_USE_ULTRAVAULT
#include <UltraVault/UltraVault.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCloud {

namespace {

// Load (or create) the per-store obfuscation key.
std::vector<uint8_t> LoadOrCreateKey(const std::string& dir) {
    fs::path keyPath = fs::path(dir) / "cloud.key";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (fs::exists(keyPath, ec)) {
        std::ifstream is(keyPath, std::ios::binary);
        std::vector<uint8_t> key((std::istreambuf_iterator<char>(is)),
                                 std::istreambuf_iterator<char>());
        if (!key.empty()) return key;
    }
    std::vector<uint8_t> key(32);
    std::random_device rd;
    for (auto& b : key) b = static_cast<uint8_t>(rd() & 0xFF);
    std::ofstream os(keyPath, std::ios::binary | std::ios::trunc);
    if (os) os.write(reinterpret_cast<const char*>(key.data()),
                     static_cast<std::streamsize>(key.size()));
    fs::permissions(keyPath, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return key;
}

std::string Obfuscate(const std::vector<uint8_t>& key, const std::string& plain) {
    std::vector<uint8_t> bytes(plain.begin(), plain.end());
    for (std::size_t i = 0; i < bytes.size() && !key.empty(); ++i) bytes[i] ^= key[i % key.size()];
    return UltraNet_Base64Encode(bytes, /*wrap76Cols=*/false);
}

std::string Deobfuscate(const std::vector<uint8_t>& key, const std::string& encoded) {
    std::vector<uint8_t> bytes;
    UltraNet_Base64Decode(encoded, bytes);
    for (std::size_t i = 0; i < bytes.size() && !key.empty(); ++i) bytes[i] ^= key[i % key.size()];
    return std::string(bytes.begin(), bytes.end());
}

// "password\n<b64>\ntoken\n<b64>\n" — one file per account.
std::string SecretFile(const std::string& dir, const std::string& accountId) {
    std::string safe;
    for (char c : accountId) safe.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return (fs::path(dir) / (safe + ".secret")).string();
}

} // namespace

bool FileSecretStore::Store(const std::string& accountId, const Credentials& credentials) {
    if (accountId.empty()) return false;
    const auto key = LoadOrCreateKey(dir_);
    std::ofstream os(SecretFile(dir_, accountId), std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os << "username\n" << Obfuscate(key, credentials.username) << "\n"
       << "password\n" << Obfuscate(key, credentials.password) << "\n"
       << "token\n"    << Obfuscate(key, credentials.token)    << "\n"
       << "refresh\n"  << Obfuscate(key, credentials.refreshToken) << "\n"
       << "expires\n"  << Obfuscate(key, std::to_string(credentials.tokenExpiresAt)) << "\n";
    std::error_code ec;
    fs::permissions(SecretFile(dir_, accountId),
                    fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return static_cast<bool>(os);
}

bool FileSecretStore::Retrieve(const std::string& accountId, Credentials& out) const {
    out = Credentials{};
    std::ifstream is(SecretFile(dir_, accountId), std::ios::binary);
    if (!is) return false;
    const auto key = LoadOrCreateKey(dir_);
    std::string name, value;
    while (std::getline(is, name) && std::getline(is, value)) {
        if (name == "username")      out.username = Deobfuscate(key, value);
        else if (name == "password") out.password = Deobfuscate(key, value);
        else if (name == "token")    out.token    = Deobfuscate(key, value);
        else if (name == "refresh")  out.refreshToken = Deobfuscate(key, value);
        else if (name == "expires")  out.tokenExpiresAt = std::strtoll(Deobfuscate(key, value).c_str(), nullptr, 10);
    }
    return true;
}

bool FileSecretStore::Remove(const std::string& accountId) {
    std::error_code ec;
    return fs::remove(SecretFile(dir_, accountId), ec);
}

#ifdef ULTRACLOUD_USE_ULTRAVAULT
namespace {
std::string VaultKey(const std::string& accountId, const char* what) {
    return "cloud." + accountId + "." + what;
}
} // namespace

bool VaultSecretStore::Store(const std::string& accountId, const Credentials& credentials) {
    if (!UltraVault::IsAvailable()) return false;
    bool ok = true;
    ok = UltraVault::Put(VaultKey(accountId, "username"),
                         UltraVault::SecretValue::FromString(credentials.username)) && ok;
    ok = UltraVault::Put(VaultKey(accountId, "password"),
                         UltraVault::SecretValue::FromString(credentials.password)) && ok;
    ok = UltraVault::Put(VaultKey(accountId, "token"),
                         UltraVault::SecretValue::FromString(credentials.token)) && ok;
    ok = UltraVault::Put(VaultKey(accountId, "refresh"),
                         UltraVault::SecretValue::FromString(credentials.refreshToken)) && ok;
    ok = UltraVault::Put(VaultKey(accountId, "expires"),
                         UltraVault::SecretValue::FromString(std::to_string(credentials.tokenExpiresAt))) && ok;
    return ok;
}

bool VaultSecretStore::Retrieve(const std::string& accountId, Credentials& out) const {
    out = Credentials{};
    if (!UltraVault::IsAvailable()) return false;
    UltraVault::SecretValue v;
    bool any = false;
    if (UltraVault::Get(VaultKey(accountId, "username"), v)) { out.username = v.AsString(); any = true; }
    if (UltraVault::Get(VaultKey(accountId, "password"), v)) { out.password = v.AsString(); any = true; }
    if (UltraVault::Get(VaultKey(accountId, "token"), v))    { out.token    = v.AsString(); any = true; }
    if (UltraVault::Get(VaultKey(accountId, "refresh"), v))  { out.refreshToken = v.AsString(); any = true; }
    if (UltraVault::Get(VaultKey(accountId, "expires"), v))
        out.tokenExpiresAt = std::strtoll(v.AsString().c_str(), nullptr, 10);
    return any;
}

bool VaultSecretStore::Remove(const std::string& accountId) {
    if (!UltraVault::IsAvailable()) return false;
    bool any = false;
    for (const char* what : {"username", "password", "token", "refresh", "expires"})
        any = static_cast<bool>(UltraVault::Delete(VaultKey(accountId, what))) || any;
    return any;
}
#endif

} // namespace UltraCloud
