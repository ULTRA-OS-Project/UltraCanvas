// Apps/UltraMail/engine/UltraMailCredentialVault.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailCredentialVault.h"

#include <UltraNet/UltraNetMime.h>   // UltraNet_Base64Encode / Decode

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraMail {

namespace {

std::vector<uint8_t> ToBytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}
std::string FromBytes(const std::vector<uint8_t>& b) {
    return std::string(b.begin(), b.end());
}

std::string B64(const std::string& s) {
    return UltraNet_Base64Encode(ToBytes(s), /*wrap76Cols=*/false);
}
std::string UnB64(const std::string& s) {
    std::vector<uint8_t> out;
    UltraNet_Base64Decode(s, out);
    return FromBytes(out);
}

// Load (or create) the per-vault obfuscation key.
std::vector<uint8_t> LoadOrCreateKey(const std::string& dir) {
    fs::path keyPath = fs::path(dir) / "vault.key";
    std::error_code ec;
    fs::create_directories(dir, ec);

    if (fs::exists(keyPath, ec)) {
        std::ifstream is(keyPath, std::ios::binary);
        std::vector<uint8_t> key((std::istreambuf_iterator<char>(is)),
                                 std::istreambuf_iterator<char>());
        if (!key.empty()) return key;
    }
    // Generate a fresh 32-byte key.
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

std::string Xor(const std::string& data, const std::vector<uint8_t>& key) {
    std::string out = data;
    if (key.empty()) return out;
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^ key[i % key.size()]);
    return out;
}

fs::path CredsPath(const std::string& dir) { return fs::path(dir) / "creds.dat"; }

// Load all account -> plaintext-secret pairs.
std::map<std::string, std::string> LoadAll(const std::string& dir,
                                           const std::vector<uint8_t>& key) {
    std::map<std::string, std::string> creds;
    std::ifstream is(CredsPath(dir));
    std::string line;
    while (std::getline(is, line)) {
        std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string account = UnB64(line.substr(0, tab));
        std::string secret  = Xor(UnB64(line.substr(tab + 1)), key);
        creds[account] = secret;
    }
    return creds;
}

bool SaveAll(const std::string& dir, const std::vector<uint8_t>& key,
             const std::map<std::string, std::string>& creds) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream os(CredsPath(dir), std::ios::trunc);
    if (!os) return false;
    for (const auto& [account, secret] : creds)
        os << B64(account) << '\t' << B64(Xor(secret, key)) << '\n';
    fs::permissions(CredsPath(dir), fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return static_cast<bool>(os);
}

} // namespace

bool CredentialVault::Store(const std::string& account, const std::string& secret) {
    if (account.empty()) return false;
    auto key = LoadOrCreateKey(dir_);
    auto creds = LoadAll(dir_, key);
    creds[account] = secret;
    return SaveAll(dir_, key, creds);
}

bool CredentialVault::Retrieve(const std::string& account, std::string& out) const {
    out.clear();
    auto key = LoadOrCreateKey(dir_);
    auto creds = LoadAll(dir_, key);
    auto it = creds.find(account);
    if (it == creds.end()) return false;
    out = it->second;
    return true;
}

bool CredentialVault::Has(const std::string& account) const {
    std::string ignore;
    return Retrieve(account, ignore);
}

bool CredentialVault::Remove(const std::string& account) {
    auto key = LoadOrCreateKey(dir_);
    auto creds = LoadAll(dir_, key);
    auto it = creds.find(account);
    if (it == creds.end()) return false;
    creds.erase(it);
    SaveAll(dir_, key, creds);
    return true;
}

} // namespace UltraMail
