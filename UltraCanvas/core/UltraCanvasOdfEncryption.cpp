// core/UltraCanvasOdfEncryption.cpp
// Password-protected OpenDocument packages - see UltraCanvasOdfEncryption.h.
// Version: 1.1.0 - ODF 1.4: AES-GCM (IV in front, tag at the end), Argon2id with its lanes, and
//                  the whole package in one encrypted entry
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#include "UltraCanvasOdfEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"   // Base64Decode, TryParseFloat
#include "UltraCanvasZipPackage.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace UltraCanvas {

namespace {

constexpr const char* kManifest = "META-INF/manifest.xml";
constexpr uint32_t kMaxIterations = 10000000;          // a hostile file's PBKDF2 cost stays bounded
constexpr size_t kMaxEntryBytes = size_t(1) << 30;     // what one entry may inflate to without a size

const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e->Name();
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

bool Is(const tinyxml2::XMLElement* e, const char* localName) {
    return e && std::strcmp(LocalName(e), localName) == 0;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* parent, const char* localName) {
    for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement())
        if (Is(e, localName)) return e;
    return nullptr;
}

std::string Attr(const tinyxml2::XMLElement* e, const char* localName) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strrchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, localName) == 0) return a->Value();
    }
    return "";
}

// A whole number attribute; -1 when absent or not a number.
int64_t AttrNumber(const tinyxml2::XMLElement* e, const char* localName) {
    const std::string text = Attr(e, localName);
    if (text.empty()) return -1;
    for (char c : text)
        if (c < '0' || c > '9') return -1;
    double value = 0.0;
    if (!TryParseFloat(text, value) || value < 0.0 || value > 9.0e15) return -1;
    return static_cast<int64_t>(value);
}

bool EndsWith(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// One encrypted entry, as its manifest:encryption-data describes it.
struct EncryptedEntry {
    std::string path;
    int64_t size = -1;                   // manifest:size: the entry's size once inflated
    std::string checksumType;
    std::vector<uint8_t> checksum;
    std::string algorithm;
    std::vector<uint8_t> iv;
    std::string startKey;                // start-key-generation-name; empty: SHA-1 (ODF 1.0 - 1.1)
    std::string derivation;
    int64_t iterations = -1;             // PBKDF2's
    int64_t argon2Passes = -1, argon2MemoryKiB = -1, argon2Lanes = -1;   // Argon2id's (ODF 1.4)
    std::vector<uint8_t> salt;
    int64_t keySize = -1;                // the derived key's size
};

struct Manifest {
    bool odf = false;                    // a manifest was read
    bool wholePackage = false;           // ODF 1.4's single encrypted package
    std::vector<EncryptedEntry> entries;
};

bool ReadManifest(const UCZipPackageReader& zip, Manifest& out, std::string& xml) {
    tinyxml2::XMLDocument doc;
    if (!zip.ReadEntry(kManifest, xml) || doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS ||
        !doc.RootElement())
        return false;
    out.odf = true;
    for (auto* entry = doc.RootElement()->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
        if (!Is(entry, "file-entry")) continue;
        const std::string path = Attr(entry, "full-path");
        if (path == "encrypted-package") out.wholePackage = true;
        const tinyxml2::XMLElement* data = Child(entry, "encryption-data");
        if (!data) continue;
        EncryptedEntry e;
        e.path = path;
        e.size = AttrNumber(entry, "size");
        e.checksumType = Attr(data, "checksum-type");
        e.checksum = Base64Decode(Attr(data, "checksum"));
        const tinyxml2::XMLElement* algorithm = Child(data, "algorithm");
        e.algorithm = Attr(algorithm, "algorithm-name");
        e.iv = Base64Decode(Attr(algorithm, "initialisation-vector"));
        e.startKey = Attr(Child(data, "start-key-generation"), "start-key-generation-name");
        const tinyxml2::XMLElement* derivation = Child(data, "key-derivation");
        e.derivation = Attr(derivation, "key-derivation-name");
        e.iterations = AttrNumber(derivation, "iteration-count");
        // ODF 1.4 names them manifest:argon2-*, LibreOffice 24.2 loext:argon2-*.
        e.argon2Passes = AttrNumber(derivation, "argon2-iterations");
        e.argon2MemoryKiB = AttrNumber(derivation, "argon2-memory");
        e.argon2Lanes = AttrNumber(derivation, "argon2-lanes");
        e.salt = Base64Decode(Attr(derivation, "salt"));
        e.keySize = AttrNumber(derivation, "key-size");
        out.entries.push_back(std::move(e));
    }
    return true;
}

// The cipher's key size in bytes; 0 for one this reader does not implement.
// CBC is XML Encryption 1.0's (ODF 1.2), GCM 1.1's (ODF 1.4).
size_t AesKeyBytes(const std::string& algorithm) {
    if (algorithm == "http://www.w3.org/2001/04/xmlenc#aes256-cbc") return 32;
    if (algorithm == "http://www.w3.org/2001/04/xmlenc#aes192-cbc") return 24;
    if (algorithm == "http://www.w3.org/2001/04/xmlenc#aes128-cbc") return 16;
    if (algorithm == "http://www.w3.org/2009/xmlenc11#aes256-gcm") return 32;
    if (algorithm == "http://www.w3.org/2009/xmlenc11#aes192-gcm") return 24;
    if (algorithm == "http://www.w3.org/2009/xmlenc11#aes128-gcm") return 16;
    return 0;
}

bool IsGcm(const std::string& algorithm) {
    return algorithm.rfind("http://www.w3.org/2009/xmlenc11#aes", 0) == 0 && EndsWith(algorithm, "-gcm");
}

bool IsArgon2id(const std::string& derivation) {
    return EndsWith(Lower(derivation), "argon2id");   // ODF 1.4's #argon2id, LibreOffice's ...:argon2id
}

// A hostile file's Argon2id cost stays bounded: LibreOffice asks for 3 passes
// over 64 MiB in 4 lanes.
constexpr int64_t kMaxArgon2MemoryKiB = 1 << 20;   // 1 GiB
constexpr int64_t kMaxArgon2Passes = 64;
constexpr int64_t kMaxArgon2Lanes = 64;

bool StartKeyHash(const std::string& name, UltraCryptHashAlgorithm& out) {
    const std::string n = Lower(name);
    if (n.empty() || n == "sha1" || EndsWith(n, "#sha1")) {
        out = UltraCryptHashAlgorithm::SHA1;
        return true;
    }
    if (n == "sha256" || EndsWith(n, "#sha256")) {
        out = UltraCryptHashAlgorithm::SHA256;
        return true;
    }
    if (n == "sha512" || EndsWith(n, "#sha512")) {
        out = UltraCryptHashAlgorithm::SHA512;
        return true;
    }
    return false;
}

// The checksum's digest; false for no checksum or one this reader does not know.
bool ChecksumHash(const std::string& name, UltraCryptHashAlgorithm& out) {
    const std::string n = Lower(name);
    if (n == "sha256/1k" || EndsWith(n, "#sha256-1k")) {
        out = UltraCryptHashAlgorithm::SHA256;
        return true;
    }
    if (n == "sha1/1k" || EndsWith(n, "#sha1-1k")) {
        out = UltraCryptHashAlgorithm::SHA1;
        return true;
    }
    return false;
}

struct Refusal {
    OfficeDecryptStatus status;
    std::string message;
};

// Why the package cannot be decrypted here, before any key is derived; an
// empty message when every entry uses what this reader implements.
Refusal CheckSupported(const Manifest& manifest) {
    if (manifest.wholePackage) {
        bool found = false;
        for (const EncryptedEntry& e : manifest.entries) found = found || e.path == "encrypted-package";
        if (!found)
            return {OfficeDecryptStatus::Damaged,
                    "The file's encrypted package has no encryption data in its manifest"};
    }
    for (const EncryptedEntry& e : manifest.entries) {
        const std::string algorithm = Lower(e.algorithm);
        if (AesKeyBytes(e.algorithm) == 0) {
            if (algorithm.find("blowfish") != std::string::npos)
                return {OfficeDecryptStatus::Unsupported,
                        "The file is encrypted with Blowfish, as OpenOffice.org 1 to 3.1 saved it, which this "
                        "program cannot open"};
            return {OfficeDecryptStatus::Unsupported,
                    "The file is encrypted with " + (e.algorithm.empty() ? std::string("no named cipher")
                                                                         : e.algorithm) +
                        ", which this program does not know"};
        }
        const std::string derivation = Lower(e.derivation);
        if (derivation.empty()) {
            return {OfficeDecryptStatus::Damaged,
                    "The file's encryption data for " + e.path + " names no key derivation"};
        }
        if (IsArgon2id(e.derivation)) {
            if (e.argon2Passes <= 0 || e.argon2MemoryKiB <= 0 || e.argon2Lanes <= 0) {
                return {OfficeDecryptStatus::Damaged,
                        "The file's Argon2id parameters for " + e.path + " are missing"};
            }
            if (e.argon2Passes > kMaxArgon2Passes || e.argon2MemoryKiB > kMaxArgon2MemoryKiB ||
                e.argon2Lanes > kMaxArgon2Lanes) {
                return {OfficeDecryptStatus::Unsupported,
                        "The file's encryption asks for more Argon2id memory or passes than this program allows"};
            }
        } else if (derivation != "pbkdf2" && !EndsWith(derivation, "#pbkdf2")) {
            return {OfficeDecryptStatus::Unsupported,
                    "The file's password key is derived with " + e.derivation +
                        ", which this program does not know"};
        }
        UltraCryptHashAlgorithm hash;
        if (!StartKeyHash(e.startKey, hash)) {
            return {OfficeDecryptStatus::Unsupported,
                    "The file's password is hashed with " + e.startKey + ", which this program does not know"};
        }
        if (e.iterations > static_cast<int64_t>(kMaxIterations)) {
            return {OfficeDecryptStatus::Unsupported,
                    "The file's encryption asks for more key-derivation rounds than this program allows"};
        }
    }
    return {OfficeDecryptStatus::Decrypted, ""};
}

OfficeDecryptStatus Refuse(const Refusal& refusal, std::string& error) {
    error = refusal.message;
    return refusal.status;
}

// Decrypts one entry; with verifyOnly it stops once the checksum has said
// whether the password is right. `out` is the entry as its reader expects it.
OfficeDecryptStatus DecryptEntry(const UCZipPackageReader& zip, const EncryptedEntry& e,
                                 const std::string& password, bool verifyOnly, std::vector<uint8_t>& out,
                                 std::string& error) {
    out.clear();
    const std::string damaged = "The encrypted entry " + e.path + " is damaged";
    std::vector<uint8_t> cipher;
    const size_t block = UltraCrypt_GetAesBlockSize();
    const bool gcm = IsGcm(e.algorithm);
    const bool argon2 = IsArgon2id(e.derivation);
    constexpr size_t kGcmTag = 16;
    if (!zip.ReadEntry(e.path, cipher)) return Refuse({OfficeDecryptStatus::Damaged, damaged + " or missing"}, error);
    const bool shapeOk = gcm ? !e.iv.empty() && cipher.size() >= e.iv.size() + kGcmTag
                             : !cipher.empty() && cipher.size() % block == 0 && e.iv.size() == block;
    if (!shapeOk || (!argon2 && e.iterations <= 0)) {
        return Refuse({OfficeDecryptStatus::Damaged, damaged}, error);
    }

    UltraCryptHashOptions legacy;
    legacy.allowLegacy = true;   // SHA-1: a start key and PBKDF2's PRF - key derivations, both
    UltraCryptHashAlgorithm startHash = UltraCryptHashAlgorithm::SHA256;
    StartKeyHash(e.startKey, startHash);
    std::vector<uint8_t> startDigest;
    if (!UltraCrypt_Hash(startHash, password.data(), password.size(), startDigest, legacy)) {
        return Refuse({OfficeDecryptStatus::NoCryptoBackend, "The password could not be hashed"}, error);
    }
    const UltraCryptSecureBuffer startKey(startDigest.data(), startDigest.size());
    UltraCrypt_SecureZero(startDigest.data(), startDigest.size());

    const size_t keyBytes = e.keySize > 0 ? static_cast<size_t>(e.keySize) : AesKeyBytes(e.algorithm);
    if (keyBytes != AesKeyBytes(e.algorithm)) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's encryption names a key size its cipher does not take"},
                      error);
    }
    UltraCryptSecureBuffer key;
    UltraCryptResult derived = UltraCryptResult::Ok();
    if (argon2) {
        // ODF 1.4: Argon2id (version 0x13, no secret, no associated data)
        // over the start key - libargon2's argon2id_ctx, as LibreOffice calls it.
        UltraCryptKdfParams params;
        params.salt = e.salt;
        params.iterations = static_cast<uint32_t>(e.argon2Passes);
        params.memoryKiB = static_cast<uint32_t>(e.argon2MemoryKiB);
        params.parallelism = static_cast<uint32_t>(e.argon2Lanes);
        params.outputLength = keyBytes;
        derived = UltraCrypt_DeriveKeyFromPassword(startKey, params, key);
    } else {
        derived = UltraCrypt_DeriveKeyPbkdf2(UltraCryptHashAlgorithm::SHA1, startKey, e.salt,
                                             static_cast<uint32_t>(e.iterations), keyBytes, key, legacy);
    }
    if (!derived) {
        return Refuse({derived.code == UltraCryptResultCode::BackendUnavailable ? OfficeDecryptStatus::NoCryptoBackend
                                                                                : OfficeDecryptStatus::Damaged,
                       "The password key could not be derived"},
                      error);
    }

    if (gcm) {
        // XML Encryption 1.1's layout: the IV in front of the ciphertext, the
        // tag at its end. The tag covers it all, so it says whether the
        // password is right - and nothing is released if it is not.
        UltraCryptGcmParams params;
        params.iv.assign(cipher.begin(), cipher.begin() + static_cast<std::ptrdiff_t>(e.iv.size()));
        UltraCryptSecureBuffer opened;
        const UltraCryptResult result = UltraCrypt_AesGcmDecrypt(key, params, cipher.data() + e.iv.size(),
                                                                 cipher.size() - e.iv.size(), opened);
        if (!result) {
            if (result.code == UltraCryptResultCode::AuthenticationFailed)
                return Refuse({OfficeDecryptStatus::WrongPassword, "The password is not correct"}, error);
            return Refuse({OfficeDecryptStatus::Damaged, damaged}, error);
        }
        if (verifyOnly) return OfficeDecryptStatus::Decrypted;
        const size_t limit = e.size >= 0 ? static_cast<size_t>(e.size) : kMaxEntryBytes;
        if (UCZipInflateRaw(opened.Data(), opened.GetSize(), out, limit) &&
            (e.size < 0 || out.size() == static_cast<size_t>(e.size))) {
            return OfficeDecryptStatus::Decrypted;
        }
        if (e.size >= 0 && opened.GetSize() == static_cast<size_t>(e.size)) {
            out.assign(opened.Data(), opened.Data() + opened.GetSize());
            return OfficeDecryptStatus::Decrypted;
        }
        out.clear();
        return Refuse({OfficeDecryptStatus::Damaged, damaged}, error);
    }

    UltraCryptCbcParams cbc;
    cbc.iv = e.iv;
    cbc.padding = UltraCryptCbcPadding::NoPadding;   // XML Encryption's padding: only its last byte counts
    UltraCryptSecureBuffer plain;
    if (!UltraCrypt_AesCbcDecrypt(key, cbc, cipher.data(), cipher.size(), plain)) {
        return Refuse({OfficeDecryptStatus::Damaged, damaged}, error);
    }
    const size_t pad = plain.Data()[plain.GetSize() - 1];
    const Refusal wrong{OfficeDecryptStatus::WrongPassword, "The password is not correct"};
    if (pad == 0 || pad > block || pad > plain.GetSize()) return Refuse(wrong, error);
    const size_t length = plain.GetSize() - pad;

    UltraCryptHashAlgorithm checksumHash;
    const bool hasChecksum = ChecksumHash(e.checksumType, checksumHash) && !e.checksum.empty();
    if (hasChecksum) {
        std::vector<uint8_t> digest;
        if (!UltraCrypt_Hash(checksumHash, plain.Data(), std::min<size_t>(length, 1024), digest, legacy)) {
            return Refuse({OfficeDecryptStatus::NoCryptoBackend, "The checksum could not be computed"}, error);
        }
        if (!UltraCrypt_ConstantTimeEquals(digest.data(), digest.size(), e.checksum.data(), e.checksum.size())) {
            return Refuse(wrong, error);
        }
    }
    if (verifyOnly && hasChecksum) return OfficeDecryptStatus::Decrypted;

    // Deflated before it was encrypted; an entry stored as it is (as some
    // writers keep an already compressed picture) is taken when its size
    // matches the manifest's.
    const size_t limit = e.size >= 0 ? static_cast<size_t>(e.size) : kMaxEntryBytes;
    if (UCZipInflateRaw(plain.Data(), length, out, limit) &&
        (e.size < 0 || out.size() == static_cast<size_t>(e.size))) {
        return OfficeDecryptStatus::Decrypted;
    }
    if (e.size >= 0 && length == static_cast<size_t>(e.size)) {
        out.assign(plain.Data(), plain.Data() + length);
        return OfficeDecryptStatus::Decrypted;
    }
    out.clear();
    // Without a checksum, an entry that does not inflate is what a wrong
    // password gives.
    return Refuse(hasChecksum ? Refusal{OfficeDecryptStatus::Damaged, damaged} : wrong, error);
}

bool IsZipFile(const std::string& filePath) {
    std::FILE* file = OpenFileUtf8(filePath, "rb");
    if (!file) return false;
    unsigned char magic[4] = {};
    const size_t got = std::fread(magic, 1, sizeof(magic), file);
    std::fclose(file);
    return got == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
}

// The manifest as an unencrypted package carries it: no encryption-data.
std::string PlainManifest(const std::string& xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) return xml;
    for (auto* entry = doc.RootElement()->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
        for (auto* child = entry->FirstChildElement(); child;) {
            auto* next = child->NextSiblingElement();
            if (Is(child, "encryption-data")) entry->DeleteChild(child);
            child = next;
        }
    }
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr(), printer.CStrSize() > 0 ? printer.CStrSize() - 1 : 0);
}

} // namespace

bool IsEncryptedOdfPackage(const UCZipPackageReader& package) {
    Manifest manifest;
    std::string xml;
    return ReadManifest(package, manifest, xml) && (manifest.wholePackage || !manifest.entries.empty());
}

bool IsEncryptedOdfPackage(const std::string& filePath) {
    if (!IsZipFile(filePath)) return false;
    UCZipPackageReader zip;
    return zip.Open(filePath) && IsEncryptedOdfPackage(zip);
}

OfficeDecryptStatus DecryptOdfPackage(const UCZipPackageReader& package, const std::string& password,
                                      std::vector<uint8_t>& outPackage, std::string& error) {
    outPackage.clear();
    error.clear();
    Manifest manifest;
    std::string manifestXml;
    if (!ReadManifest(package, manifest, manifestXml) || (!manifest.wholePackage && manifest.entries.empty()))
        return OfficeDecryptStatus::NotEncrypted;
    if (!UltraCrypt_IsAvailable()) {
        return Refuse({OfficeDecryptStatus::NoCryptoBackend,
                       "This program was built without cryptography, so it cannot open password-protected files"},
                      error);
    }
    const Refusal unsupported = CheckSupported(manifest);
    if (!unsupported.message.empty()) return Refuse(unsupported, error);
    if (password.empty()) {
        return Refuse({OfficeDecryptStatus::PasswordRequired,
                       "The file is password-protected; enter its password to open it"},
                      error);
    }

    // ODF 1.4's whole-package encryption: the one entry is the package.
    if (manifest.wholePackage) {
        for (const EncryptedEntry& e : manifest.entries) {
            if (e.path != "encrypted-package") continue;
            std::vector<uint8_t> inner;
            const OfficeDecryptStatus status = DecryptEntry(package, e, password, false, inner, error);
            if (status != OfficeDecryptStatus::Decrypted) return status;
            UCZipPackageReader check;
            if (inner.size() < 4 || inner[0] != 'P' || inner[1] != 'K' ||
                !check.OpenFromMemory(inner)) {
                return Refuse({OfficeDecryptStatus::Damaged, "The decrypted package is not a valid ZIP package"},
                              error);
            }
            outPackage = std::move(inner);
            return OfficeDecryptStatus::Decrypted;
        }
    }

    std::map<std::string, std::vector<uint8_t>> plain;
    for (const EncryptedEntry& e : manifest.entries) {
        std::vector<uint8_t> bytes;
        const OfficeDecryptStatus status = DecryptEntry(package, e, password, false, bytes, error);
        if (status != OfficeDecryptStatus::Decrypted) return status;
        plain[e.path] = std::move(bytes);
    }

    // The package again, as an unencrypted one is laid out: mimetype first
    // and stored, then the entries in their order.
    UCZipPackageWriter writer;
    writer.OpenInMemory();
    std::string mimetype;
    if (package.ReadEntry("mimetype", mimetype)) writer.AddEntry("mimetype", mimetype, false);
    for (const std::string& name : package.EntryNames()) {
        if (name == "mimetype") continue;
        const auto decrypted = plain.find(name);
        bool added = false;
        if (decrypted != plain.end()) {
            added = writer.AddEntry(name, decrypted->second.data(), decrypted->second.size());
        } else if (name == kManifest) {
            added = writer.AddEntry(name, PlainManifest(manifestXml));
        } else {
            std::vector<uint8_t> bytes;
            if (!package.ReadEntry(name, bytes)) {
                return Refuse({OfficeDecryptStatus::Damaged, "The package entry " + name + " cannot be read"}, error);
            }
            added = writer.AddEntry(name, bytes.data(), bytes.size());
        }
        if (!added) return Refuse({OfficeDecryptStatus::Damaged, "The decrypted package could not be built"}, error);
    }
    if (!writer.Finalize()) return Refuse({OfficeDecryptStatus::Damaged, "The decrypted package could not be built"}, error);
    outPackage = writer.TakeBytes();
    return OfficeDecryptStatus::Decrypted;
}

OfficeDecryptStatus DecryptOdfPackageFile(const std::string& filePath, const std::string& password,
                                          std::vector<uint8_t>& outPackage, std::string& error) {
    outPackage.clear();
    error.clear();
    if (!IsZipFile(filePath)) return OfficeDecryptStatus::NotEncrypted;
    UCZipPackageReader zip;
    if (!zip.Open(filePath)) return OfficeDecryptStatus::NotEncrypted;   // the format's reader says why
    return DecryptOdfPackage(zip, password, outPackage, error);
}

OfficeDecryptStatus CheckOdfPassword(const std::string& filePath, const std::string& password, std::string& error) {
    error.clear();
    if (!IsZipFile(filePath)) return OfficeDecryptStatus::NotEncrypted;
    UCZipPackageReader zip;
    Manifest manifest;
    std::string xml;
    if (!zip.Open(filePath) || !ReadManifest(zip, manifest, xml) ||
        (!manifest.wholePackage && manifest.entries.empty()))
        return OfficeDecryptStatus::NotEncrypted;
    if (!UltraCrypt_IsAvailable()) {
        return Refuse({OfficeDecryptStatus::NoCryptoBackend,
                       "This program was built without cryptography, so it cannot open password-protected files"},
                      error);
    }
    const Refusal unsupported = CheckSupported(manifest);
    if (!unsupported.message.empty()) return Refuse(unsupported, error);
    if (password.empty()) {
        return Refuse({OfficeDecryptStatus::PasswordRequired,
                       "The file is password-protected; enter its password to open it"},
                      error);
    }
    // One key derivation: content.xml's (its checksum or its GCM tag), or
    // the whole package's.
    const EncryptedEntry* probe = &manifest.entries.front();
    for (const EncryptedEntry& e : manifest.entries)
        if (e.path == "content.xml" || e.path == "encrypted-package") probe = &e;
    std::vector<uint8_t> unused;
    return DecryptEntry(zip, *probe, password, true, unused, error);
}

} // namespace UltraCanvas
