// Apps/UltraAuthenticator/AccountExport.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "AccountExport.h"

#include "Plugins/Documents/UCDCryptoEnvelope.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraCanvas {
namespace Authenticator {
namespace {

constexpr size_t  kMagicSize  = 8;
constexpr size_t  kHeaderSize = 10;
constexpr uint8_t kVersion    = 1;
const char kMagic[kMagicSize] = {'U', 'C', 'A', 'E', 'X', 'P', 'R', 'T'};

void PutUint16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void PutUint32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

uint16_t ReadUint16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadUint32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::vector<uint8_t> BuildHeader() {
    std::vector<uint8_t> header;
    header.reserve(kHeaderSize);
    header.insert(header.end(), kMagic, kMagic + kMagicSize);
    header.push_back(kVersion);
    header.push_back(0);   // reserved
    return header;
}

// Wipes a buffer that held plaintext. UCDCrypto works in plain vectors, so the
// payload is briefly unprotected; overwriting it is the least this can do, and
// it is why the payload is assembled and destroyed inside one function rather
// than handed around.
void WipeVector(std::vector<uint8_t>& v) {
    if (!v.empty()) UltraCrypt_SecureZero(v.data(), v.size());
    v.clear();
}

std::string PassphraseToString(const UltraCryptSecureBuffer& passphrase) {
    return std::string(reinterpret_cast<const char*>(passphrase.Data()),
                       passphrase.GetSize());
}

void WipeString(std::string& s) {
    if (!s.empty()) UltraCrypt_SecureZero(&s[0], s.size());
    s.clear();
}

} // namespace

StoreResult SealAccountExport(const std::vector<UltraCryptSecureBuffer>& uris,
                              const UltraCryptSecureBuffer& passphrase,
                              const std::string& path) {
    if (passphrase.GetSize() == 0) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the export passphrase is empty");
    }
    if (uris.size() > kMaxExportAccounts) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "too many accounts to export");
    }

    // Assemble the payload, seal it, and wipe it — all before returning, so
    // the plaintext list of every seed exists for as short a time as possible.
    std::vector<uint8_t> payload;
    PutUint32LE(payload, static_cast<uint32_t>(uris.size()));
    for (const UltraCryptSecureBuffer& uri : uris) {
        if (uri.GetSize() == 0 || uri.GetSize() > kMaxExportUriBytes) {
            WipeVector(payload);
            return StoreResult::Error(StoreResultCode::InvalidArgument,
                                      "an account URI is empty or too long");
        }
        PutUint16LE(payload, static_cast<uint16_t>(uri.GetSize()));
        const uint8_t* bytes = static_cast<const uint8_t*>(uri.Data());
        payload.insert(payload.end(), bytes, bytes + uri.GetSize());
    }

    const std::vector<uint8_t> header = BuildHeader();

    std::string password = PassphraseToString(passphrase);
    std::vector<uint8_t> envelope;
    std::string error;
    const bool sealed = UCDCrypto::Seal(payload, password, header, envelope,
                                        error);
    WipeString(password);
    WipeVector(payload);

    if (!sealed) {
        return StoreResult::Error(StoreResultCode::InternalError,
                                  "could not encrypt the export: " + error);
    }

    // Atomic write, same reasoning as the vault: an interrupted save must not
    // leave a truncated file that looks like a usable backup.
    namespace fs = std::filesystem;
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return StoreResult::Error(StoreResultCode::IoError,
                                      "could not create '" + tempPath + "'");
        }
        out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
        out.write(reinterpret_cast<const char*>(envelope.data()),
                  static_cast<std::streamsize>(envelope.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            fs::remove(tempPath, ec);
            return StoreResult::Error(StoreResultCode::IoError,
                                      "could not write '" + tempPath + "'");
        }
    }

    std::error_code ec;
    fs::permissions(tempPath,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    fs::rename(tempPath, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        return StoreResult::Error(StoreResultCode::IoError,
                                  "could not save '" + path + "'");
    }
    return StoreResult::Ok();
}

StoreResult OpenAccountExport(const std::string& path,
                              const UltraCryptSecureBuffer& passphrase,
                              std::vector<UltraCryptSecureBuffer>& outUris) {
    outUris.clear();
    if (passphrase.GetSize() == 0) {
        return StoreResult::Error(StoreResultCode::InvalidArgument,
                                  "the passphrase is empty");
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        return StoreResult::Error(StoreResultCode::IoError,
                                  "could not read '" + path + "'");
    }
    if (size > kMaxExportFileBytes) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the file is too large to be a backup");
    }
    if (size < kHeaderSize + UCDCrypto::GetEnvelopeHeaderSize()) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the file is too short to be a backup");
    }

    std::vector<uint8_t> raw(static_cast<size_t>(size));
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return StoreResult::Error(StoreResultCode::IoError,
                                      "could not open '" + path + "'");
        }
        in.read(reinterpret_cast<char*>(raw.data()),
                static_cast<std::streamsize>(raw.size()));
        if (!in) {
            return StoreResult::Error(StoreResultCode::IoError,
                                      "could not read '" + path + "'");
        }
    }

    if (std::memcmp(raw.data(), kMagic, kMagicSize) != 0) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "this is not an authenticator backup file");
    }
    if (raw[kMagicSize] != kVersion) {
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "unsupported backup format version");
    }

    const std::vector<uint8_t> header(raw.begin(),
                                      raw.begin() + kHeaderSize);
    const std::vector<uint8_t> envelope(raw.begin() + kHeaderSize, raw.end());

    std::string password = PassphraseToString(passphrase);
    std::vector<uint8_t> payload;
    std::string error;
    const bool opened = UCDCrypto::Open(envelope, password, header, payload,
                                        error);
    WipeString(password);
    if (!opened) {
        WipeVector(payload);
        // The envelope refuses to say whether it was the passphrase or the
        // bytes; pass that through rather than guessing.
        return StoreResult::Error(StoreResultCode::AuthenticationFailed,
                                  "the passphrase is not correct, or the file "
                                  "has been altered");
    }

    // From here the payload is authenticated, but a file sealed by an older or
    // buggy writer could still be malformed, so every length is still checked
    // against what remains rather than trusted.
    if (payload.size() < 4) {
        WipeVector(payload);
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the backup contents are malformed");
    }
    const uint32_t count = ReadUint32LE(payload.data());
    if (count > kMaxExportAccounts) {
        WipeVector(payload);
        return StoreResult::Error(StoreResultCode::Corrupt,
                                  "the backup declares too many accounts");
    }

    size_t offset = 4;
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 2 > payload.size()) {
            WipeVector(payload);
            outUris.clear();
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the backup contents are truncated");
        }
        const uint16_t length = ReadUint16LE(payload.data() + offset);
        offset += 2;
        if (length == 0 || offset + length > payload.size()) {
            WipeVector(payload);
            outUris.clear();
            return StoreResult::Error(StoreResultCode::Corrupt,
                                      "the backup contents are truncated");
        }
        outUris.emplace_back(payload.data() + offset, length);
        offset += length;
    }

    WipeVector(payload);
    return StoreResult::Ok();
}

} // namespace Authenticator
} // namespace UltraCanvas
