// UltraCloud/core/UltraCloudSecrets.cpp
// Version: 0.3.0 - file store replaced by the vault + a one-way migration
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudSecrets.h>

#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Decode (legacy files)
#include <UltraVault/UltraVault.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCloud {

// ---- UltraVault -------------------------------------------------------------

namespace {
std::string VaultKey(const std::string& accountId, const char* what) {
    return "cloud." + accountId + "." + what;
}
} // namespace

bool VaultSecretStore::Store(const std::string& accountId, const Credentials& credentials) {
    if (accountId.empty() || !UltraVault::IsAvailable()) return false;
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
    if (accountId.empty() || !UltraVault::IsAvailable()) return false;
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
    if (accountId.empty() || !UltraVault::IsAvailable()) return false;
    bool any = false;
    for (const char* what : {"username", "password", "token", "refresh", "expires"})
        any = static_cast<bool>(UltraVault::Delete(VaultKey(accountId, what))) || any;
    return any;
}

// ---- Memory -----------------------------------------------------------------

bool MemorySecretStore::Store(const std::string& accountId, const Credentials& credentials) {
    if (accountId.empty()) return false;
    secrets_[accountId] = credentials;
    return true;
}

bool MemorySecretStore::Retrieve(const std::string& accountId, Credentials& out) const {
    out = Credentials{};
    auto it = secrets_.find(accountId);
    if (it == secrets_.end()) return false;
    out = it->second;
    return true;
}

bool MemorySecretStore::Remove(const std::string& accountId) {
    return secrets_.erase(accountId) > 0;
}

// ---- Legacy file store (read once to migrate, never written) ----------------
// The old store kept one "<accountId>.secret" per account — pairs of lines,
// a field name then its value XOR-ed against cloud.key and base64-encoded —
// with the key in the same directory. Nothing writes that format any more.

namespace {

constexpr const char* kLegacyKeyFile = "cloud.key";

std::vector<uint8_t> ReadLegacyKey(const std::string& dir) {
    std::ifstream is(fs::path(dir) / kLegacyKeyFile, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(is)),
                                std::istreambuf_iterator<char>());
}

std::string Deobfuscate(const std::vector<uint8_t>& key, const std::string& encoded) {
    std::vector<uint8_t> bytes;
    UltraNet_Base64Decode(encoded, bytes);
    for (std::size_t i = 0; i < bytes.size() && !key.empty(); ++i) bytes[i] ^= key[i % key.size()];
    return std::string(bytes.begin(), bytes.end());
}

fs::path LegacySecretFile(const std::string& dir, const std::string& accountId) {
    std::string safe;
    for (char c : accountId) safe.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return fs::path(dir) / (safe + ".secret");
}

bool ReadLegacySecret(const fs::path& file, const std::vector<uint8_t>& key, Credentials& out) {
    out = Credentials{};
    std::ifstream is(file, std::ios::binary);
    if (!is) return false;
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

bool AnyLegacySecretLeft(const std::string& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.path().extension() == ".secret") return true;
    return false;
}

} // namespace

int MigrateLegacyFileSecrets(const std::string& directory,
                             const std::vector<Account>& accounts,
                             ISecretStore& into) {
    std::error_code ec;
    if (directory.empty() || !fs::is_directory(directory, ec)) return 0;
    const std::vector<uint8_t> key = ReadLegacyKey(directory);
    if (key.empty()) return 0;   // no key: the files cannot be read, leave them

    int carried = 0;
    for (const Account& account : accounts) {
        if (account.accountId.empty()) continue;
        const fs::path file = LegacySecretFile(directory, account.accountId);
        if (!fs::exists(file, ec)) continue;
        Credentials credentials;
        if (!ReadLegacySecret(file, key, credentials)) continue;
        // Only drop the file once the secret is safely in the store; a store
        // that refuses (a vault still locked) keeps it for the next start.
        if (!into.Store(account.accountId, credentials)) continue;
        fs::remove(file, ec);
        ++carried;
    }
    if (!AnyLegacySecretLeft(directory)) {
        fs::remove(fs::path(directory) / kLegacyKeyFile, ec);
        fs::remove(directory, ec);   // only succeeds when nothing else is in it
    }
    return carried;
}

} // namespace UltraCloud
