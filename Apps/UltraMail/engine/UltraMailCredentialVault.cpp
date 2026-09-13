// Apps/UltraMail/engine/UltraMailCredentialVault.cpp
// Version: 0.6.0 - device-key auto-unlock (Thunderbird-style, no prompt)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailCredentialVault.h"

#include <UltraVault/UltraVault.h>
#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode / Decode
#include <UltraCrypt/UltraCryptCore.h>   // UltraCrypt_RandomBytes

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

constexpr const char* kVaultFile = "ultramail.vault";
// The device key holds a random passphrase so the vault unlocks without a
// prompt (see the header). NOT "vault.key" — that name belongs to the 0.1
// legacy format below and would confuse its migration.
constexpr const char* kDeviceKeyFile = "device.key";

// A random passphrase (hex of 32 crypto-random bytes) for a new vault's device
// key; empty if secure randomness is unavailable.
std::string RandomPassphrase() {
    std::vector<uint8_t> bytes;
    if (!UltraCrypt_RandomBytes(bytes, 32)) return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 0xF]); }
    return out;
}

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

std::string CredentialVault::DeviceKeyPath() const {
    return (fs::path(dir_) / kDeviceKeyFile).string();
}

bool CredentialVault::TryAutoUnlock() {
    if (unlocked_) return true;

    // A stored device key: unlock silently with it.
    std::error_code ec;
    if (fs::exists(DeviceKeyPath(), ec)) {
        std::ifstream in(DeviceKeyPath(), std::ios::binary);
        std::string pass((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        while (!pass.empty() && (pass.back() == '\n' || pass.back() == '\r')) pass.pop_back();
        return !pass.empty() && Unlock(pass) == VaultStatus::Ok;
    }

    // No device key. If a vault already exists it was made with a master
    // password we don't have — the caller must prompt once, then persist the
    // key. Only create a fresh vault + key when there is nothing to migrate.
    if (Exists()) return false;

    const std::string pass = RandomPassphrase();
    if (pass.empty()) return false;                 // no secure RNG on this build
    if (!PersistDeviceKey(pass)) return false;      // could not write the key file
    if (Unlock(pass) == VaultStatus::Ok) return true;
    // Creating the vault failed: drop the key file so a retry is not blocked.
    fs::remove(DeviceKeyPath(), ec);
    return false;
}

bool CredentialVault::PersistDeviceKey(const std::string& passphrase) {
    if (passphrase.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    { std::ofstream out(DeviceKeyPath(), std::ios::binary | std::ios::trunc);
      if (!out) return false;
      out << passphrase;
      if (!out) return false; }
    // Owner-only: the local key is the only thing standing between the folder
    // and the secrets, so keep it off other users (Thunderbird's key4.db posture).
    fs::permissions(DeviceKeyPath(),
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return true;
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
    if (!UltraVault::Put(KeyFor(account),
                         UltraVault::SecretValue::FromString(secret)).IsOk())
        return false;
    RemoveOAuthTokens(account);   // one sign-in method per account
    return true;
}

namespace {
constexpr const char* kAccessSuffix  = ".oauth.access";
constexpr const char* kRefreshSuffix = ".oauth.refresh";
constexpr const char* kExpiresSuffix = ".oauth.expires";
} // namespace

bool CredentialVault::StoreOAuthTokens(const std::string& account, const OAuthTokens& tokens) {
    if (account.empty() || !unlocked_ || tokens.Empty()) return false;
    const std::string base = KeyFor(account);
    auto put = [&](const char* suffix, const std::string& value) {
        return UltraVault::Put(base + suffix,
                               UltraVault::SecretValue::FromString(value)).IsOk();
    };
    if (!put(kAccessSuffix, tokens.accessToken) ||
        !put(kRefreshSuffix, tokens.refreshToken) ||
        !put(kExpiresSuffix, std::to_string(tokens.expiresAt)))
        return false;
    UltraVault::Delete(base);   // the password slot, if the account had one
    return true;
}

bool CredentialVault::RetrieveOAuthTokens(const std::string& account, OAuthTokens& out) const {
    out = OAuthTokens{};
    if (account.empty() || !unlocked_) return false;
    const std::string base = KeyFor(account);
    UltraVault::SecretValue v;
    if (!UltraVault::Get(base + kAccessSuffix, v).IsOk()) return false;
    out.accessToken = v.AsString();
    if (UltraVault::Get(base + kRefreshSuffix, v).IsOk()) out.refreshToken = v.AsString();
    if (UltraVault::Get(base + kExpiresSuffix, v).IsOk())
        out.expiresAt = std::strtoll(v.AsString().c_str(), nullptr, 10);
    return !out.Empty();
}

bool CredentialVault::HasOAuthTokens(const std::string& account) const {
    OAuthTokens ignore;
    return RetrieveOAuthTokens(account, ignore);
}

bool CredentialVault::RemoveOAuthTokens(const std::string& account) {
    if (account.empty() || !unlocked_) return false;
    const std::string base = KeyFor(account);
    const bool had = UltraVault::Delete(base + kAccessSuffix).IsOk();
    UltraVault::Delete(base + kRefreshSuffix);
    UltraVault::Delete(base + kExpiresSuffix);
    return had;
}

SignInMethod CredentialVault::MethodFor(const std::string& account) const {
    if (HasOAuthTokens(account)) return SignInMethod::OAuth2;
    if (Has(account)) return SignInMethod::Password;
    return SignInMethod::None;
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
