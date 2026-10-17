// core/UltraCanvasOfficeEncryption.cpp
// ECMA-376 agile decryption and encryption of password-protected Office
// packages. See UltraCanvasOfficeEncryption.h; [MS-OFFCRYPTO] 2.3.4.10-2.3.4.15
// is the specification each step below names.
// Version: 1.1.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasCompoundFile.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraCanvasPathUtf8.h"   // OpenFileUtf8
#include "tinyxml2.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace UltraCanvas {

namespace {

constexpr size_t kSegmentSize = 4096;
// [MS-OFFCRYPTO] caps spinCount at 10 000 000; a larger one is a damaged (or
// hostile) header asking for minutes of hashing.
constexpr uint32_t kMaxSpinCount = 10000000;

// Block keys, [MS-OFFCRYPTO] 2.3.4.11 and 2.3.4.14.
const uint8_t kVerifierInputBlock[8] = {0xfe, 0xa7, 0xd2, 0x76, 0x3b, 0x4b, 0x9e, 0x79};
const uint8_t kVerifierValueBlock[8] = {0xd7, 0xaa, 0x0f, 0x6d, 0x30, 0x61, 0x34, 0x4e};
const uint8_t kKeyValueBlock[8]      = {0x14, 0x6e, 0x0b, 0xe7, 0xab, 0xac, 0xd0, 0xd6};
const uint8_t kHmacKeyBlock[8]       = {0x5f, 0xb2, 0xad, 0x01, 0x0c, 0xb9, 0xe1, 0xf6};
const uint8_t kHmacValueBlock[8]     = {0xa0, 0x67, 0x7f, 0x02, 0xb2, 0x2c, 0x84, 0x33};

struct Fail {
    OfficeDecryptStatus status;
    std::string message;
};

// What a <keyData> or <p:encryptedKey> element says about its cipher.
struct CipherParams {
    UltraCryptHashAlgorithm hash = UltraCryptHashAlgorithm::SHA512;
    size_t hashSize = 0;
    size_t keyBytes = 0;
    size_t blockSize = 0;
    std::vector<uint8_t> salt;
};

// The element name without its namespace prefix ("p:encryptedKey" ->
// "encryptedKey"): writers choose their own prefixes.
const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e->Name();
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* parent, const char* localName) {
    for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement())
        if (std::strcmp(LocalName(e), localName) == 0) return e;
    return nullptr;
}

std::string Attr(const tinyxml2::XMLElement* e, const char* name) {
    const char* v = e->Attribute(name);
    return v ? v : "";
}

std::vector<uint8_t> AttrBase64(const tinyxml2::XMLElement* e, const char* name) {
    return Base64Decode(Attr(e, name));
}

bool ReadCipherParams(const tinyxml2::XMLElement* e, CipherParams& out, Fail& fail) {
    if (Attr(e, "cipherAlgorithm") != "AES") {
        fail = {OfficeDecryptStatus::Unsupported,
                "The file is encrypted with " + Attr(e, "cipherAlgorithm") +
                ", not AES, which this program cannot decrypt"};
        return false;
    }
    if (Attr(e, "cipherChaining") != "ChainingModeCBC") {
        fail = {OfficeDecryptStatus::Unsupported,
                "The file is encrypted in " + Attr(e, "cipherChaining") +
                " mode, not CBC, which this program cannot decrypt"};
        return false;
    }
    const std::string hash = Attr(e, "hashAlgorithm");
    if (hash == "SHA512") {
        out.hash = UltraCryptHashAlgorithm::SHA512;
    } else if (hash == "SHA256") {
        out.hash = UltraCryptHashAlgorithm::SHA256;
    } else if (hash == "SHA1") {
        // Office 2010's default. Read only: EncryptOfficePackage writes SHA-512.
        out.hash = UltraCryptHashAlgorithm::SHA1;
    } else {
        fail = {OfficeDecryptStatus::Unsupported,
                "The file's encryption is keyed with " + hash +
                ", which this program does not read"};
        return false;
    }
    out.hashSize = UltraCrypt_GetDigestSize(out.hash);
    const int keyBits = e->IntAttribute("keyBits", 0);
    const int blockSize = e->IntAttribute("blockSize", 0);
    const int hashSize = e->IntAttribute("hashSize", 0);
    out.salt = AttrBase64(e, "saltValue");
    if ((keyBits != 128 && keyBits != 192 && keyBits != 256) || blockSize != 16 ||
        static_cast<size_t>(hashSize) != out.hashSize || out.salt.empty()) {
        fail = {OfficeDecryptStatus::Damaged,
                "The file's encryption header names a key, block or hash size that does not fit "
                "its cipher"};
        return false;
    }
    out.keyBytes = static_cast<size_t>(keyBits) / 8;
    out.blockSize = static_cast<size_t>(blockSize);
    return true;
}

bool Hash(UltraCryptHashAlgorithm algorithm, const void* data, size_t size,
          std::vector<uint8_t>& out) {
    // SHA-1 is UltraCrypt's legacy digest, refused unless asked for. Asked
    // for here, and only here: Office 2010 keys its files with it, and
    // reading them back is all it is used for - its collision weakness does
    // not reach a password hash or a block-key derivation.
    UltraCryptHashOptions options;
    options.allowLegacy = algorithm == UltraCryptHashAlgorithm::SHA1;
    return static_cast<bool>(UltraCrypt_Hash(algorithm, data, size, out, options));
}

bool Hash(UltraCryptHashAlgorithm algorithm, const std::vector<uint8_t>& data,
          std::vector<uint8_t>& out) {
    return Hash(algorithm, data.data(), data.size(), out);
}

// [MS-OFFCRYPTO] 2.3.4.11: a hash cut to `size` bytes, or padded with 0x36.
void FitTo(std::vector<uint8_t>& bytes, size_t size) {
    if (bytes.size() > size) {
        UltraCrypt_SecureZero(bytes.data() + size, bytes.size() - size);
        bytes.resize(size);
    } else {
        bytes.resize(size, 0x36);
    }
}

// IV for the key data's blocks: H(salt + blockKey), fitted to the block size.
bool DeriveIv(const CipherParams& p, const uint8_t* blockKey, size_t blockKeySize,
              std::vector<uint8_t>& iv) {
    std::vector<uint8_t> input = p.salt;
    input.insert(input.end(), blockKey, blockKey + blockKeySize);
    if (!Hash(p.hash, input, iv)) return false;
    FitTo(iv, p.blockSize);
    return true;
}

// The password as UTF-16LE, in a buffer that is wiped. A byte that is not
// part of valid UTF-8 becomes U+FFFD; a typed password never has one.
UltraCryptSecureBuffer PasswordUtf16(const std::string& utf8) {
    UltraCryptSecureBuffer out(utf8.size() * 4);
    size_t n = 0;
    auto put = [&](uint32_t unit) {
        out.Data()[n++] = static_cast<uint8_t>(unit & 0xFF);
        out.Data()[n++] = static_cast<uint8_t>(unit >> 8);
    };
    for (size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (len > 1) {
            bool valid = i + len <= utf8.size();
            uint32_t v = c & (0x7Fu >> len);
            for (size_t k = 1; valid && k < len; ++k) {
                const auto cc = static_cast<uint8_t>(utf8[i + k]);
                valid = (cc & 0xC0) == 0x80;
                v = (v << 6) | (cc & 0x3Fu);
            }
            if (valid) cp = v; else len = 1;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put(0xD800 + (cp >> 10));
            put(0xDC00 + (cp & 0x3FF));
        } else {
            put(cp);
        }
    }
    out.Resize(n);
    return out;
}

// [MS-OFFCRYPTO] 2.3.4.11: H0 = H(salt + password), Hn = H(iterator + Hn-1).
bool PasswordHash(const CipherParams& p, const std::string& password, uint32_t spinCount,
                  UltraCryptSecureBuffer& out) {
    UltraCryptSecureBuffer utf16 = PasswordUtf16(password);
    std::vector<uint8_t> input = p.salt;
    input.insert(input.end(), utf16.Data(), utf16.Data() + utf16.GetSize());
    std::vector<uint8_t> h;
    const bool firstOk = Hash(p.hash, input, h);
    UltraCrypt_SecureZero(input.data(), input.size());
    if (!firstOk) return false;

    std::vector<uint8_t> round(4 + h.size());
    for (uint32_t i = 0; i < spinCount; ++i) {
        round[0] = static_cast<uint8_t>(i);
        round[1] = static_cast<uint8_t>(i >> 8);
        round[2] = static_cast<uint8_t>(i >> 16);
        round[3] = static_cast<uint8_t>(i >> 24);
        std::memcpy(round.data() + 4, h.data(), h.size());
        if (!Hash(p.hash, round, h)) {
            UltraCrypt_SecureZero(round.data(), round.size());
            return false;
        }
    }
    UltraCrypt_SecureZero(round.data(), round.size());
    out = UltraCryptSecureBuffer(h.data(), h.size());
    UltraCrypt_SecureZero(h.data(), h.size());
    return true;
}

// The key for one of the password key encryptor's values: H(Hn + blockKey).
bool BlockKey(const CipherParams& p, const UltraCryptSecureBuffer& passwordHash,
              const uint8_t* blockKey, UltraCryptSecureBuffer& out) {
    std::vector<uint8_t> input(passwordHash.Data(), passwordHash.Data() + passwordHash.GetSize());
    input.insert(input.end(), blockKey, blockKey + 8);
    std::vector<uint8_t> key;
    const bool ok = Hash(p.hash, input, key);
    UltraCrypt_SecureZero(input.data(), input.size());
    if (!ok) return false;
    FitTo(key, p.keyBytes);
    out = UltraCryptSecureBuffer(key.data(), key.size());
    UltraCrypt_SecureZero(key.data(), key.size());
    return true;
}

// Decrypts one of the header's short values (unpadded CBC) and cuts it to
// `size` bytes.
bool DecryptValue(const UltraCryptSecureBuffer& key, const std::vector<uint8_t>& iv,
                  const std::vector<uint8_t>& ciphertext, size_t size,
                  UltraCryptSecureBuffer& out) {
    UltraCryptCbcParams cbc;
    cbc.iv = iv;
    cbc.padding = UltraCryptCbcPadding::NoPadding;
    if (ciphertext.empty() || !UltraCrypt_AesCbcDecrypt(key, cbc, ciphertext.data(),
                                                        ciphertext.size(), out))
        return false;
    if (out.GetSize() < size) return false;
    out.Resize(size);
    return true;
}

OfficeDecryptStatus Refuse(const Fail& fail, std::vector<uint8_t>& outPackage,
                           std::string& error) {
    outPackage.clear();
    error = fail.message;
    return fail.status;
}

// Everything DecryptOfficePackage does; with verifyOnly, it stops once the
// password verifier has matched and returns Decrypted without decrypting.
OfficeDecryptStatus Decrypt(const std::vector<uint8_t>& encryptionInfo,
                            const std::vector<uint8_t>& encryptedPackage,
                            const std::string& password, bool verifyOnly,
                            std::vector<uint8_t>& outPackage, std::string& error);

bool ReadStreams(const UCCompoundFileReader& cfb, std::vector<uint8_t>& info,
                 std::vector<uint8_t>& package, std::string& error) {
    if (cfb.ReadStream("EncryptionInfo", info) && cfb.ReadStream("EncryptedPackage", package))
        return true;
    error = "The encrypted file is damaged: its streams cannot be read";
    return false;
}

} // namespace

bool IsEncryptedOfficePackage(const UCCompoundFileReader& compoundFile) {
    return compoundFile.HasStream("EncryptionInfo") && compoundFile.HasStream("EncryptedPackage");
}

bool IsEncryptedOfficePackage(const std::string& filePath) {
    if (!UCCompoundFileReader::HasSignature(filePath)) return false;
    UCCompoundFileReader cfb;
    return cfb.Open(filePath) && IsEncryptedOfficePackage(cfb);
}

OfficeDecryptStatus DecryptOfficePackage(const std::vector<uint8_t>& encryptionInfo,
                                         const std::vector<uint8_t>& encryptedPackage,
                                         const std::string& password,
                                         std::vector<uint8_t>& outPackage,
                                         std::string& error) {
    return Decrypt(encryptionInfo, encryptedPackage, password, false, outPackage, error);
}

namespace {

OfficeDecryptStatus Decrypt(const std::vector<uint8_t>& encryptionInfo,
                            const std::vector<uint8_t>& encryptedPackage,
                            const std::string& password, bool verifyOnly,
                            std::vector<uint8_t>& outPackage, std::string& error) {
    outPackage.clear();
    error.clear();
    if (!UltraCrypt_IsAvailable()) {
        return Refuse({OfficeDecryptStatus::NoCryptoBackend,
                       "This program was built without cryptography, so it cannot open "
                       "password-protected files"}, outPackage, error);
    }

    // [MS-OFFCRYPTO] 2.3.4.10: version 4.4, reserved 0x40, then the XML.
    if (encryptionInfo.size() < 8) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's encryption header is truncated"},
                      outPackage, error);
    }
    const unsigned major = encryptionInfo[0] | (encryptionInfo[1] << 8);
    const unsigned minor = encryptionInfo[2] | (encryptionInfo[3] << 8);
    if (major != 4 || minor != 4) {
        const bool standard = minor == 2 && major >= 2 && major <= 4;
        return Refuse({OfficeDecryptStatus::Unsupported,
                       standard ? "The file uses Office 2007's encryption, which this program "
                                  "cannot open; save it again from Office 2010 or later"
                                : "The file uses an encryption this program does not know"},
                      outPackage, error);
    }
    const std::string xml(encryptionInfo.begin() + 8, encryptionInfo.end());
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's encryption header is not valid XML"},
                      outPackage, error);
    }
    const tinyxml2::XMLElement* root = doc.RootElement();
    const tinyxml2::XMLElement* keyData = Child(root, "keyData");
    const tinyxml2::XMLElement* integrity = Child(root, "dataIntegrity");
    const tinyxml2::XMLElement* encryptedKey = nullptr;
    bool certificateOnly = false;
    if (const tinyxml2::XMLElement* encryptors = Child(root, "keyEncryptors")) {
        for (auto* e = encryptors->FirstChildElement(); e; e = e->NextSiblingElement()) {
            const std::string uri = Attr(e, "uri");
            if (uri.size() >= 9 && uri.compare(uri.size() - 9, 9, "/password") == 0) {
                encryptedKey = Child(e, "encryptedKey");
            } else {
                certificateOnly = true;
            }
        }
    }
    if (!encryptedKey) {
        return Refuse({certificateOnly ? OfficeDecryptStatus::Unsupported : OfficeDecryptStatus::Damaged,
                       certificateOnly ? "The file is encrypted for a certificate, not with a password"
                                       : "The file's encryption header has no password key"},
                      outPackage, error);
    }
    if (!keyData) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's encryption header has no key data"},
                      outPackage, error);
    }
    if (!integrity) {
        // Office always writes it. Without it a changed file would decrypt
        // into changed contents unnoticed, so it is not opened.
        return Refuse({OfficeDecryptStatus::Unsupported,
                       "The file carries no integrity check, so it is not opened"},
                      outPackage, error);
    }

    Fail fail{OfficeDecryptStatus::Damaged, ""};
    CipherParams keyParams, dataParams;
    if (!ReadCipherParams(encryptedKey, keyParams, fail) || !ReadCipherParams(keyData, dataParams, fail))
        return Refuse(fail, outPackage, error);
    const uint32_t spinCount = encryptedKey->UnsignedAttribute("spinCount", 0);
    if (spinCount > kMaxSpinCount) {
        return Refuse({OfficeDecryptStatus::Damaged,
                       "The file's encryption header asks for more key-derivation rounds than "
                       "Office ever writes"}, outPackage, error);
    }
    if (password.empty()) {
        return Refuse({OfficeDecryptStatus::PasswordRequired,
                       "The file is password-protected; enter its password to open it"},
                      outPackage, error);
    }

    const Fail cryptoFailed{OfficeDecryptStatus::Damaged,
                            "The file's encryption header could not be processed"};

    // 2.3.4.11 - 2.3.4.13: the password key, its verifier, the intermediate key.
    UltraCryptSecureBuffer passwordHash;
    if (!PasswordHash(keyParams, password, spinCount, passwordHash))
        return Refuse(cryptoFailed, outPackage, error);
    std::vector<uint8_t> keyIv = keyParams.salt;
    FitTo(keyIv, keyParams.blockSize);

    UltraCryptSecureBuffer inputKey, valueKey, keyKey;
    if (!BlockKey(keyParams, passwordHash, kVerifierInputBlock, inputKey) ||
        !BlockKey(keyParams, passwordHash, kVerifierValueBlock, valueKey) ||
        !BlockKey(keyParams, passwordHash, kKeyValueBlock, keyKey))
        return Refuse(cryptoFailed, outPackage, error);
    passwordHash.Clear();

    UltraCryptSecureBuffer verifierInput, verifierValue;
    if (!DecryptValue(inputKey, keyIv, AttrBase64(encryptedKey, "encryptedVerifierHashInput"),
                      keyParams.salt.size(), verifierInput) ||
        !DecryptValue(valueKey, keyIv, AttrBase64(encryptedKey, "encryptedVerifierHashValue"),
                      keyParams.hashSize, verifierValue)) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's password verifier is malformed"},
                      outPackage, error);
    }
    std::vector<uint8_t> verifierHash;
    if (!Hash(keyParams.hash, verifierInput.Data(), verifierInput.GetSize(), verifierHash))
        return Refuse(cryptoFailed, outPackage, error);
    if (!UltraCrypt_ConstantTimeEquals(verifierHash.data(), verifierHash.size(),
                                       verifierValue.Data(), verifierValue.GetSize())) {
        return Refuse({OfficeDecryptStatus::WrongPassword, "The password is not correct"},
                      outPackage, error);
    }
    if (verifyOnly) return OfficeDecryptStatus::Decrypted;

    UltraCryptSecureBuffer intermediateKey;
    if (!DecryptValue(keyKey, keyIv, AttrBase64(encryptedKey, "encryptedKeyValue"),
                      dataParams.keyBytes, intermediateKey)) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's encrypted key is malformed"},
                      outPackage, error);
    }

    // 2.3.4.14: the HMAC over the whole EncryptedPackage stream, before any of
    // it is decrypted.
    std::vector<uint8_t> hmacKeyIv, hmacValueIv;
    UltraCryptSecureBuffer hmacKey, hmacValue;
    if (!DeriveIv(dataParams, kHmacKeyBlock, 8, hmacKeyIv) ||
        !DeriveIv(dataParams, kHmacValueBlock, 8, hmacValueIv))
        return Refuse(cryptoFailed, outPackage, error);
    if (!DecryptValue(intermediateKey, hmacKeyIv, AttrBase64(integrity, "encryptedHmacKey"),
                      dataParams.hashSize, hmacKey) ||
        !DecryptValue(intermediateKey, hmacValueIv, AttrBase64(integrity, "encryptedHmacValue"),
                      dataParams.hashSize, hmacValue)) {
        return Refuse({OfficeDecryptStatus::Damaged, "The file's integrity check is malformed"},
                      outPackage, error);
    }
    std::vector<uint8_t> mac;
    if (!UltraCrypt_Hmac(dataParams.hash, hmacKey, encryptedPackage.data(),
                         encryptedPackage.size(), mac))
        return Refuse(cryptoFailed, outPackage, error);
    if (!UltraCrypt_ConstantTimeEquals(mac.data(), mac.size(), hmacValue.Data(), hmacValue.GetSize())) {
        return Refuse({OfficeDecryptStatus::Tampered,
                       "The file was changed after it was saved: its integrity check does not "
                       "match, so it is not opened"}, outPackage, error);
    }

    // 2.3.4.15: StreamSize, then 4096-byte segments, segment i's IV the hash
    // of the key data's salt and i.
    if (encryptedPackage.size() < 8) {
        return Refuse({OfficeDecryptStatus::Damaged, "The encrypted package is truncated"},
                      outPackage, error);
    }
    uint64_t streamSize = 0;
    for (int i = 7; i >= 0; --i) streamSize = (streamSize << 8) | encryptedPackage[static_cast<size_t>(i)];
    const size_t cipherSize = encryptedPackage.size() - 8;
    if (cipherSize % 16 != 0 || streamSize > cipherSize) {
        return Refuse({OfficeDecryptStatus::Damaged, "The encrypted package is truncated"},
                      outPackage, error);
    }
    std::vector<uint8_t> package;
    package.reserve(cipherSize);
    for (size_t offset = 0, segment = 0; offset < cipherSize; offset += kSegmentSize, ++segment) {
        const size_t length = cipherSize - offset < kSegmentSize ? cipherSize - offset : kSegmentSize;
        const uint8_t index[4] = {static_cast<uint8_t>(segment), static_cast<uint8_t>(segment >> 8),
                                  static_cast<uint8_t>(segment >> 16), static_cast<uint8_t>(segment >> 24)};
        UltraCryptCbcParams cbc;
        cbc.padding = UltraCryptCbcPadding::NoPadding;
        UltraCryptSecureBuffer plain;
        if (!DeriveIv(dataParams, index, 4, cbc.iv) ||
            !UltraCrypt_AesCbcDecrypt(intermediateKey, cbc, encryptedPackage.data() + 8 + offset,
                                      length, plain))
            return Refuse(cryptoFailed, outPackage, error);
        package.insert(package.end(), plain.Data(), plain.Data() + plain.GetSize());
    }
    package.resize(static_cast<size_t>(streamSize));
    outPackage = std::move(package);
    return OfficeDecryptStatus::Decrypted;
}

} // namespace

OfficeDecryptStatus DecryptOfficePackage(const UCCompoundFileReader& compoundFile,
                                         const std::string& password,
                                         std::vector<uint8_t>& outPackage,
                                         std::string& error) {
    outPackage.clear();
    error.clear();
    if (!IsEncryptedOfficePackage(compoundFile)) return OfficeDecryptStatus::NotEncrypted;
    std::vector<uint8_t> info, package;
    if (!ReadStreams(compoundFile, info, package, error)) return OfficeDecryptStatus::Damaged;
    return Decrypt(info, package, password, false, outPackage, error);
}

OfficeDecryptStatus DecryptOfficePackageFile(const std::string& filePath,
                                             const std::string& password,
                                             std::vector<uint8_t>& outPackage,
                                             std::string& error) {
    outPackage.clear();
    error.clear();
    if (!UCCompoundFileReader::HasSignature(filePath)) return OfficeDecryptStatus::NotEncrypted;
    UCCompoundFileReader cfb;
    if (!cfb.Open(filePath)) {
        error = cfb.GetLastError();
        return OfficeDecryptStatus::Damaged;
    }
    return DecryptOfficePackage(cfb, password, outPackage, error);
}


OfficeDecryptStatus CheckOfficePassword(const std::string& filePath, const std::string& password,
                                        std::string& error) {
    error.clear();
    if (!UCCompoundFileReader::HasSignature(filePath)) return OfficeDecryptStatus::NotEncrypted;
    UCCompoundFileReader cfb;
    if (!cfb.Open(filePath)) {
        error = cfb.GetLastError();
        return OfficeDecryptStatus::Damaged;
    }
    if (!IsEncryptedOfficePackage(cfb)) return OfficeDecryptStatus::NotEncrypted;
    std::vector<uint8_t> info, package, unused;
    if (!ReadStreams(cfb, info, package, error)) return OfficeDecryptStatus::Damaged;
    return Decrypt(info, package, password, true, unused, error);
}

// ===== WRITING =====

namespace {

// What Office 2013 and later write: AES-256, SHA-512, 16-byte salts.
CipherParams WriterParams() {
    CipherParams p;
    p.hash = UltraCryptHashAlgorithm::SHA512;
    p.hashSize = 64;
    p.keyBytes = 32;
    p.blockSize = 16;
    return p;
}
constexpr uint32_t kWriterSpinCount = 100000;

bool EncryptValue(const UltraCryptSecureBuffer& key, const std::vector<uint8_t>& iv,
                  const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    UltraCryptCbcParams cbc;
    cbc.iv = iv;
    cbc.padding = UltraCryptCbcPadding::NoPadding;
    return static_cast<bool>(UltraCrypt_AesCbcEncrypt(key, cbc, data, size, out));
}

// The cipher attributes <keyData> and <p:encryptedKey> share.
std::string CipherAttributes(const CipherParams& p) {
    return "saltSize=\"" + std::to_string(p.salt.size()) + "\" blockSize=\"" +
           std::to_string(p.blockSize) + "\" keyBits=\"" + std::to_string(p.keyBytes * 8) +
           "\" hashSize=\"" + std::to_string(p.hashSize) +
           "\" cipherAlgorithm=\"AES\" cipherChaining=\"ChainingModeCBC\" hashAlgorithm=\"SHA512\" "
           "saltValue=\"" + Base64Encode(p.salt, false) + "\"";
}

// The UNICODE-LP-P4 strings of the DataSpaces streams ([MS-OFFCRYPTO] 2.1.2):
// a byte length, UTF-16LE, padded to four bytes.
void PutLengthPrefixed(std::vector<uint8_t>& out, const std::string& ascii) {
    const uint32_t bytes = static_cast<uint32_t>(ascii.size() * 2);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(bytes >> (8 * i)));
    for (char c : ascii) { out.push_back(static_cast<uint8_t>(c)); out.push_back(0); }
    while (out.size() % 4) out.push_back(0);
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Reader, updater and writer version 1.0 ([MS-OFFCRYPTO] 2.1.5).
void PutVersions(std::vector<uint8_t>& out) {
    for (int i = 0; i < 3; ++i) Put32(out, 1);
}

// The \x06DataSpaces storage that says the package is encrypted
// ([MS-OFFCRYPTO] 2.1.5-2.1.8, 2.2.6); Office will not open the file without it.
void AddDataSpaces(UCCompoundFileWriter& cfb) {
    std::vector<uint8_t> version;
    PutLengthPrefixed(version, "Microsoft.Container.DataSpaces");
    PutVersions(version);
    cfb.AddStream("\x06" "DataSpaces/Version", version);

    std::vector<uint8_t> entry;   // one map entry: EncryptedPackage -> StrongEncryptionDataSpace
    Put32(entry, 1);              // reference components
    Put32(entry, 0);              // a stream
    PutLengthPrefixed(entry, "EncryptedPackage");
    PutLengthPrefixed(entry, "StrongEncryptionDataSpace");
    std::vector<uint8_t> map;
    Put32(map, 8);                // header length
    Put32(map, 1);                // entries
    Put32(map, static_cast<uint32_t>(entry.size() + 4));
    map.insert(map.end(), entry.begin(), entry.end());
    cfb.AddStream("\x06" "DataSpaces/DataSpaceMap", map);

    std::vector<uint8_t> definition;
    Put32(definition, 8);
    Put32(definition, 1);
    PutLengthPrefixed(definition, "StrongEncryptionTransform");
    cfb.AddStream("\x06" "DataSpaces/DataSpaceInfo/StrongEncryptionDataSpace", definition);

    std::vector<uint8_t> id;
    PutLengthPrefixed(id, "{FF9A3F03-56EF-4613-BDD5-5A41C1D07246}");
    std::vector<uint8_t> primary;
    Put32(primary, static_cast<uint32_t>(8 + id.size()));   // header length up to the ID
    Put32(primary, 1);                                      // a password transform
    primary.insert(primary.end(), id.begin(), id.end());
    PutLengthPrefixed(primary, "Microsoft.Container.EncryptionTransform");
    PutVersions(primary);
    Put32(primary, 0);   // EncryptionName: empty
    Put32(primary, 0);   // EncryptionBlockSize
    Put32(primary, 0);   // CipherMode
    Put32(primary, 4);   // Reserved
    cfb.AddStream("\x06" "DataSpaces/TransformInfo/StrongEncryptionTransform/\x06" "Primary", primary);
}

} // namespace

bool EncryptOfficePackage(const std::vector<uint8_t>& package, const std::string& password,
                          std::vector<uint8_t>& outFile, std::string& error) {
    outFile.clear();
    error.clear();
    if (password.empty()) {
        error = "A password is needed to save a password-protected file";
        return false;
    }
    if (!UltraCrypt_IsAvailable()) {
        error = "This program was built without cryptography, so it cannot save "
                "password-protected files";
        return false;
    }
    const std::string failed = "The file could not be encrypted";
    CipherParams keyParams = WriterParams(), dataParams = WriterParams();
    keyParams.salt.resize(16);
    dataParams.salt.resize(16);
    UltraCryptSecureBuffer intermediateKey, hmacKey, verifierInput;
    if (!UltraCrypt_RandomBytes(keyParams.salt, 16) || !UltraCrypt_RandomBytes(dataParams.salt, 16) ||
        !UltraCrypt_RandomSecureBuffer(dataParams.keyBytes, intermediateKey) ||
        !UltraCrypt_RandomSecureBuffer(dataParams.hashSize, hmacKey) ||
        !UltraCrypt_RandomSecureBuffer(keyParams.salt.size(), verifierInput)) {
        error = failed + ": no random numbers";
        return false;
    }

    // The package: StreamSize, then 4096-byte segments, the last padded to
    // the block, each with its own IV.
    std::vector<uint8_t> encrypted(8);
    const uint64_t streamSize = package.size();
    for (int i = 0; i < 8; ++i) encrypted[static_cast<size_t>(i)] = static_cast<uint8_t>(streamSize >> (8 * i));
    for (size_t offset = 0, segment = 0; offset < package.size(); offset += kSegmentSize, ++segment) {
        const size_t length = std::min(kSegmentSize, package.size() - offset);
        std::vector<uint8_t> chunk(package.begin() + static_cast<std::ptrdiff_t>(offset),
                                   package.begin() + static_cast<std::ptrdiff_t>(offset + length));
        chunk.resize((length + 15) / 16 * 16, 0);
        const uint8_t index[4] = {static_cast<uint8_t>(segment), static_cast<uint8_t>(segment >> 8),
                                  static_cast<uint8_t>(segment >> 16), static_cast<uint8_t>(segment >> 24)};
        std::vector<uint8_t> iv, cipher;
        if (!DeriveIv(dataParams, index, 4, iv) ||
            !EncryptValue(intermediateKey, iv, chunk.data(), chunk.size(), cipher)) {
            error = failed;
            return false;
        }
        encrypted.insert(encrypted.end(), cipher.begin(), cipher.end());
    }

    // The integrity HMAC over that stream, and its key, both wrapped.
    std::vector<uint8_t> mac, hmacKeyIv, hmacValueIv, encryptedHmacKey, encryptedHmacValue;
    if (!UltraCrypt_Hmac(dataParams.hash, hmacKey, encrypted.data(), encrypted.size(), mac) ||
        !DeriveIv(dataParams, kHmacKeyBlock, 8, hmacKeyIv) ||
        !DeriveIv(dataParams, kHmacValueBlock, 8, hmacValueIv) ||
        !EncryptValue(intermediateKey, hmacKeyIv, hmacKey.Data(), hmacKey.GetSize(), encryptedHmacKey) ||
        !EncryptValue(intermediateKey, hmacValueIv, mac.data(), mac.size(), encryptedHmacValue)) {
        error = failed;
        return false;
    }

    // The password key, the verifier and the wrapped intermediate key.
    UltraCryptSecureBuffer passwordHash, inputKey, valueKey, keyKey;
    if (!PasswordHash(keyParams, password, kWriterSpinCount, passwordHash) ||
        !BlockKey(keyParams, passwordHash, kVerifierInputBlock, inputKey) ||
        !BlockKey(keyParams, passwordHash, kVerifierValueBlock, valueKey) ||
        !BlockKey(keyParams, passwordHash, kKeyValueBlock, keyKey)) {
        error = failed;
        return false;
    }
    passwordHash.Clear();
    std::vector<uint8_t> keyIv = keyParams.salt, verifierHash, encryptedInput, encryptedValue,
                         encryptedKeyValue;
    FitTo(keyIv, keyParams.blockSize);
    if (!Hash(keyParams.hash, verifierInput.Data(), verifierInput.GetSize(), verifierHash) ||
        !EncryptValue(inputKey, keyIv, verifierInput.Data(), verifierInput.GetSize(), encryptedInput) ||
        !EncryptValue(valueKey, keyIv, verifierHash.data(), verifierHash.size(), encryptedValue) ||
        !EncryptValue(keyKey, keyIv, intermediateKey.Data(), intermediateKey.GetSize(), encryptedKeyValue)) {
        error = failed;
        return false;
    }

    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<encryption xmlns=\"http://schemas.microsoft.com/office/2006/encryption\" "
        "xmlns:p=\"http://schemas.microsoft.com/office/2006/keyEncryptor/password\" "
        "xmlns:c=\"http://schemas.microsoft.com/office/2006/keyEncryptor/certificate\">"
        "<keyData " + CipherAttributes(dataParams) + "/>"
        "<dataIntegrity encryptedHmacKey=\"" + Base64Encode(encryptedHmacKey, false) +
        "\" encryptedHmacValue=\"" + Base64Encode(encryptedHmacValue, false) + "\"/>"
        "<keyEncryptors><keyEncryptor uri=\"http://schemas.microsoft.com/office/2006/keyEncryptor/password\">"
        "<p:encryptedKey spinCount=\"" + std::to_string(kWriterSpinCount) + "\" " +
        CipherAttributes(keyParams) +
        " encryptedVerifierHashInput=\"" + Base64Encode(encryptedInput, false) +
        "\" encryptedVerifierHashValue=\"" + Base64Encode(encryptedValue, false) +
        "\" encryptedKeyValue=\"" + Base64Encode(encryptedKeyValue, false) + "\"/>"
        "</keyEncryptor></keyEncryptors></encryption>";
    std::vector<uint8_t> info = {0x04, 0x00, 0x04, 0x00, 0x40, 0x00, 0x00, 0x00};   // 4.4, reserved 0x40
    info.insert(info.end(), xml.begin(), xml.end());

    UCCompoundFileWriter cfb;
    AddDataSpaces(cfb);
    cfb.AddStream("EncryptionInfo", std::move(info));
    cfb.AddStream("EncryptedPackage", std::move(encrypted));
    outFile = cfb.Build();
    return true;
}

bool EncryptOfficePackageToFile(const std::vector<uint8_t>& package, const std::string& password,
                                const std::string& filePath, std::string& error) {
    std::vector<uint8_t> file;
    if (!EncryptOfficePackage(package, password, file, error)) return false;
    std::FILE* f = OpenFileUtf8(filePath, "wb");
    if (!f) {
        error = "Cannot create file: " + filePath;
        return false;
    }
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    if (std::fclose(f) != 0 || !ok) {
        error = "Write failed (disk full?): " + filePath;
        return false;
    }
    return true;
}

} // namespace UltraCanvas
