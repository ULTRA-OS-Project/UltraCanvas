// core/UltraVault/UltraVaultDeviceKeyVault.cpp
// An application's own vault on UltraVault, unlocked by a device key — see
// the header. UI-free and free of UltraNet, like the rest of the module, so
// the headless consumers and the test binary link it as they are.
// Version: 0.1.0 - moved here from UltraMail's CredentialVault 0.6.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraVault/UltraVaultDeviceKeyVault.h"

#include "UltraCrypt/UltraCryptCore.h"   // UltraCrypt_RandomBytes

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraVault {

namespace {

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
// The old vault XOR-ed each secret against a key stored in the same directory
// and wrote one "base64(account) TAB base64(xor(secret))" line per account.
// Nothing here writes that format; it exists to read it once and delete it.

constexpr const char* kLegacyKeyFile   = "vault.key";
constexpr const char* kLegacyCredsFile = "creds.dat";

// RFC 4648 Base64 decoder for those lines. The framework's codec lives in
// UltraCanvasTextUtils, which is linked into (and exported by) the UltraCanvas
// library; UltraVault stays off that library on purpose — the same link-time
// split that keeps UltraCrypt UI-free — so this private copy is deliberate.
// Lenient like UltraCanvas::Base64Decode: whitespace ignored, padding optional.
std::string LegacyBase64Decode(const std::string& text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(text.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int v = value(c);
        if (v < 0) continue;   // whitespace or stray byte
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
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
        creds[LegacyBase64Decode(line.substr(0, tab))] =
            LegacyXor(LegacyBase64Decode(line.substr(tab + 1)), key);
    }
    return creds;
}

// Map an UltraVault result to the reason the caller reports.
UnlockStatus StatusFor(const Result& r) {
    switch (r.code) {
        case ResultCode::Success:            return UnlockStatus::Ok;
        case ResultCode::AccessDenied:       return UnlockStatus::WrongPassphrase;
        case ResultCode::BackendUnavailable: return UnlockStatus::Unavailable;
        case ResultCode::IoError:            return UnlockStatus::IoError;
        case ResultCode::Locked:             return UnlockStatus::Locked;
        default:                             return UnlockStatus::IoError;
    }
}

constexpr const char* kAccessSuffix  = ".oauth.access";
constexpr const char* kRefreshSuffix = ".oauth.refresh";
constexpr const char* kExpiresSuffix = ".oauth.expires";

} // namespace

std::string DeviceKeyVault::KeyFor(const std::string& account) const {
    return profile_.keyPrefix + account;
}

std::string DeviceKeyVault::VaultPath() const {
    return (fs::path(dir_) / profile_.vaultFileName).string();
}

bool DeviceKeyVault::Exists() const {
    std::error_code ec;
    return fs::exists(fs::path(dir_) / profile_.vaultFileName, ec);
}

UnlockStatus DeviceKeyVault::Unlock(const std::string& passphrase) {
    // An empty passphrase derives a key anyone could reproduce, which would
    // put us back where the 0.1 vault was. Refuse it outright. A vault with
    // no directory or no file name has nowhere to go.
    if (passphrase.empty()) return UnlockStatus::WrongPassphrase;
    if (dir_.empty() || profile_.vaultFileName.empty()) return UnlockStatus::IoError;

    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) return UnlockStatus::IoError;

    // Initialize() is idempotent per process and will not reconfigure an open
    // vault, so close any previous one before adopting this passphrase.
    Shutdown();

    Config config;
    config.backend    = Backend::File;
    config.filePath   = VaultPath();
    config.passphrase = passphrase;   // wiped in place by Initialize()

    const Result r = Initialize(config);
    if (!r.IsOk()) {
        unlocked_ = false;
        return StatusFor(r);
    }
    unlocked_ = true;

    MigrateLegacy();
    return UnlockStatus::Ok;
}

void DeviceKeyVault::Lock() {
    if (!unlocked_) return;
    Shutdown();   // wipes the decrypted store and derived key
    unlocked_ = false;
}

std::string DeviceKeyVault::DeviceKeyPath() const {
    return (fs::path(dir_) / kDeviceKeyFile).string();
}

bool DeviceKeyVault::TryAutoUnlock() {
    if (unlocked_) return true;
    if (dir_.empty()) return false;

    // A stored device key: unlock silently with it.
    std::error_code ec;
    if (fs::exists(DeviceKeyPath(), ec)) {
        std::ifstream in(DeviceKeyPath(), std::ios::binary);
        std::string pass((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        while (!pass.empty() && (pass.back() == '\n' || pass.back() == '\r')) pass.pop_back();
        return !pass.empty() && Unlock(pass) == UnlockStatus::Ok;
    }

    // No device key. If a vault already exists it was made with a master
    // password we don't have — the caller must prompt once, then persist the
    // key. Only create a fresh vault + key when there is nothing to migrate.
    if (Exists()) return false;

    const std::string pass = RandomPassphrase();
    if (pass.empty()) return false;                 // no secure RNG on this build
    if (!PersistDeviceKey(pass)) return false;      // could not write the key file
    if (Unlock(pass) == UnlockStatus::Ok) return true;
    // Creating the vault failed: drop the key file so a retry is not blocked.
    fs::remove(DeviceKeyPath(), ec);
    return false;
}

bool DeviceKeyVault::PersistDeviceKey(const std::string& passphrase) {
    if (passphrase.empty() || dir_.empty()) return false;
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

int DeviceKeyVault::MigrateLegacy() {
    std::error_code ec;
    auto legacy = ReadLegacyVault(dir_);
    if (legacy.empty()) {
        // The 0.1 code wrote its key file on the first read, so a folder that
        // never held a secret still carries one. With no creds.dat there is
        // nothing it could decrypt; drop it rather than leave a stray key.
        if (!fs::exists(fs::path(dir_) / kLegacyCredsFile, ec))
            fs::remove(fs::path(dir_) / kLegacyKeyFile, ec);
        return 0;
    }

    int carried = 0;
    for (const auto& [account, secret] : legacy) {
        if (account.empty()) continue;
        if (Put(KeyFor(account), SecretValue::FromString(secret)).IsOk())
            ++carried;
    }
    // Only drop the old files once every secret is safely in the new vault;
    // a partial migration keeps them so nothing is lost.
    if (carried == static_cast<int>(legacy.size())) {
        fs::remove(fs::path(dir_) / kLegacyCredsFile, ec);
        fs::remove(fs::path(dir_) / kLegacyKeyFile, ec);
    }
    return carried;
}

bool DeviceKeyVault::Store(const std::string& account, const std::string& secret) {
    if (account.empty() || !unlocked_) return false;
    if (!Put(KeyFor(account), SecretValue::FromString(secret)).IsOk())
        return false;
    RemoveOAuthTokens(account);   // one sign-in method per account
    return true;
}

bool DeviceKeyVault::StoreOAuthTokens(const std::string& account, const OAuthTokens& tokens) {
    if (account.empty() || !unlocked_ || tokens.Empty()) return false;
    const std::string base = KeyFor(account);
    auto put = [&](const char* suffix, const std::string& value) {
        return Put(base + suffix, SecretValue::FromString(value)).IsOk();
    };
    if (!put(kAccessSuffix, tokens.accessToken) ||
        !put(kRefreshSuffix, tokens.refreshToken) ||
        !put(kExpiresSuffix, std::to_string(tokens.expiresAt)))
        return false;
    Delete(base);   // the password slot, if the account had one
    return true;
}

bool DeviceKeyVault::RetrieveOAuthTokens(const std::string& account, OAuthTokens& out) const {
    out = OAuthTokens{};
    if (account.empty() || !unlocked_) return false;
    const std::string base = KeyFor(account);
    SecretValue v;
    if (!Get(base + kAccessSuffix, v).IsOk()) return false;
    out.accessToken = v.AsString();
    if (Get(base + kRefreshSuffix, v).IsOk()) out.refreshToken = v.AsString();
    if (Get(base + kExpiresSuffix, v).IsOk())
        out.expiresAt = std::strtoll(v.AsString().c_str(), nullptr, 10);
    return !out.Empty();
}

bool DeviceKeyVault::HasOAuthTokens(const std::string& account) const {
    OAuthTokens ignore;
    return RetrieveOAuthTokens(account, ignore);
}

bool DeviceKeyVault::RemoveOAuthTokens(const std::string& account) {
    if (account.empty() || !unlocked_) return false;
    const std::string base = KeyFor(account);
    const bool had = Delete(base + kAccessSuffix).IsOk();
    Delete(base + kRefreshSuffix);
    Delete(base + kExpiresSuffix);
    return had;
}

SignInMethod DeviceKeyVault::MethodFor(const std::string& account) const {
    if (HasOAuthTokens(account)) return SignInMethod::OAuth2;
    if (Has(account)) return SignInMethod::Password;
    return SignInMethod::None;
}

bool DeviceKeyVault::Retrieve(const std::string& account, std::string& out) const {
    out.clear();
    if (account.empty() || !unlocked_) return false;
    SecretValue value;
    if (!Get(KeyFor(account), value).IsOk()) return false;
    out = value.AsString();
    return true;
}

bool DeviceKeyVault::Has(const std::string& account) const {
    std::string ignore;
    return Retrieve(account, ignore);
}

bool DeviceKeyVault::Remove(const std::string& account) {
    if (account.empty() || !unlocked_) return false;
    return Delete(KeyFor(account)).IsOk();
}

} // namespace UltraVault
