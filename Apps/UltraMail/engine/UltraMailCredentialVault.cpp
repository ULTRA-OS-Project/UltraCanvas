// Apps/UltraMail/engine/UltraMailCredentialVault.cpp
// Version: 0.4.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailCredentialVault.h"

#include <UltraVault/UltraVault.h>
#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode / Decode

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

constexpr const char* kVaultFile = "ultramail.vault";

// ---- 0.1-format reader (kept only to migrate away from it) ----------------
// The old vault XOR-ed each secret against a key stored in the same directory.
// Nothing here writes that format; it exists to read it once and delete it.

constexpr const char* kLegacyKeyFile   = "vault.key";
constexpr const char* kLegacyCredsFile = "creds.dat";

std::string FromBytes(const std::vector<uint8_t>& b) {
    return std::string(b.begin(), b.end());
}

std::string UnB64(const std::string& s) {
    std::vector<uint8_t> out;
    UltraNet_Base64Decode(s, out);
    return FromBytes(out);
}

std::string LegacyXor(const std::string& data, const std::vector<uint8_t>& key) {
    std::string out = data;
    if (key.empty()) return out;
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
    return out;
}

std::map<std::string, std::string> ReadLegacyVault(const std::string& dir) {
    std::map<std::string, std::string> creds;
    std::error_code ec;
    const fs::path keyPath   = fs::path(dir) / kLegacyKeyFile;
    const fs::path credsPath = fs::path(dir) / kLegacyCredsFile;
    if (!fs::exists(keyPath, ec) || !fs::exists(credsPath, ec)) return creds;

    std::ifstream ks(keyPath, std::ios::binary);
    std::vector<uint8_t> key((std::istreambuf_iterator<char>(ks)),
                             std::istreambuf_iterator<char>());

    std::ifstream is(credsPath);
    std::string line;
    while (std::getline(is, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        creds[UnB64(line.substr(0, tab))] =
            LegacyXor(UnB64(line.substr(tab + 1)), key);
    }
    return creds;
}

// Map an UltraVault result to the reason the caller reports.
VaultStatus StatusFor(const UltraVault::Result& r) {
    switch (r.code) {
        case UltraVault::ResultCode::Success:            return VaultStatus::Ok;
        case UltraVault::ResultCode::AccessDenied:       return VaultStatus::WrongPassphrase;
        case UltraVault::ResultCode::BackendUnavailable: return VaultStatus::Unavailable;
        case UltraVault::ResultCode::IoError:            return VaultStatus::IoError;
        case UltraVault::ResultCode::Locked:             return VaultStatus::Locked;
        default:                                         return VaultStatus::IoError;
    }
}

} // namespace

std::string CredentialVault::KeyFor(const std::string& account) {
    // UltraVault's namespaced convention: "<vendor>.<app>.<purpose>".
    return "mail.ultramail." + account;
}

std::string CredentialVault::VaultPath() const {
    return (fs::path(dir_) / kVaultFile).string();
}

bool CredentialVault::Exists() const {
    std::error_code ec;
    return fs::exists(fs::path(dir_) / kVaultFile, ec);
}

VaultStatus CredentialVault::Unlock(const std::string& passphrase) {
    // An empty passphrase derives a key anyone could reproduce, which would
    // put us back where the 0.1 vault was. Refuse it outright.
    if (passphrase.empty()) return VaultStatus::WrongPassphrase;

    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) return VaultStatus::IoError;

    // Initialize() is idempotent per process and will not reconfigure an open
    // vault, so close any previous one before adopting this passphrase.
    UltraVault::Shutdown();

    UltraVault::Config config;
    config.backend    = UltraVault::Backend::File;
    config.filePath   = VaultPath();
    config.passphrase = passphrase;   // wiped in place by Initialize()

    const UltraVault::Result r = UltraVault::Initialize(config);
    if (!r.IsOk()) {
        unlocked_ = false;
        return StatusFor(r);
    }
    unlocked_ = true;

    MigrateLegacy();
    return VaultStatus::Ok;
}

void CredentialVault::Lock() {
    if (!unlocked_) return;
    UltraVault::Shutdown();   // wipes the decrypted store and derived key
    unlocked_ = false;
}

int CredentialVault::MigrateLegacy() {
    auto legacy = ReadLegacyVault(dir_);
    if (legacy.empty()) return 0;

    int carried = 0;
    for (const auto& [account, secret] : legacy) {
        if (account.empty()) continue;
        if (UltraVault::Put(KeyFor(account),
                            UltraVault::SecretValue::FromString(secret)).IsOk())
            ++carried;
    }
    // Only drop the old files once every secret is safely in the new vault;
    // a partial migration keeps them so nothing is lost.
    if (carried == static_cast<int>(legacy.size())) {
        std::error_code ec;
        fs::remove(fs::path(dir_) / kLegacyCredsFile, ec);
        fs::remove(fs::path(dir_) / kLegacyKeyFile, ec);
    }
    return carried;
}

bool CredentialVault::Store(const std::string& account, const std::string& secret) {
    if (account.empty() || !unlocked_) return false;
    return UltraVault::Put(KeyFor(account),
                           UltraVault::SecretValue::FromString(secret)).IsOk();
}

bool CredentialVault::Retrieve(const std::string& account, std::string& out) const {
    out.clear();
    if (account.empty() || !unlocked_) return false;
    UltraVault::SecretValue value;
    if (!UltraVault::Get(KeyFor(account), value).IsOk()) return false;
    out = value.AsString();
    return true;
}

bool CredentialVault::Has(const std::string& account) const {
    std::string ignore;
    return Retrieve(account, ignore);
}

bool CredentialVault::Remove(const std::string& account) {
    if (account.empty() || !unlocked_) return false;
    return UltraVault::Delete(KeyFor(account)).IsOk();
}

} // namespace UltraMail
