// MiioPacket.cpp
// Xiaomi miIO packets: framing, checksum and encryption.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "MiioPacket.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <algorithm>
#include <cstring>

namespace UltraCanvas {
namespace SmartHome {
namespace Miio {

namespace {

void PutU16(Bytes& out, size_t at, uint16_t v) {
    out[at] = static_cast<uint8_t>(v >> 8);
    out[at + 1] = static_cast<uint8_t>(v);
}

void PutU32(Bytes& out, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

uint16_t GetU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t GetU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// MD5 is fixed by the protocol; UltraCrypt offers it for exactly this.
bool Md5(const uint8_t* data, size_t size, Block16& out, std::string& error) {
    UltraCryptHashOptions legacy;
    legacy.allowLegacy = true;
    std::vector<uint8_t> digest;
    const UltraCryptResult r = UltraCrypt_Hash(UltraCryptHashAlgorithm::MD5, data, size, digest, legacy);
    if (!r || digest.size() != out.size()) {
        error = "MD5 failed: " + r.message;
        return false;
    }
    std::copy(digest.begin(), digest.end(), out.begin());
    return true;
}

// MD5(header bytes 0-15 | token | body)
bool Checksum(const uint8_t* packet, size_t size, const Token& token, Block16& out, std::string& error) {
    Bytes material;
    material.reserve(size);
    material.insert(material.end(), packet, packet + 16);
    material.insert(material.end(), token.bytes.begin(), token.bytes.end());
    material.insert(material.end(), packet + kHeaderSize, packet + size);
    return Md5(material.data(), material.size(), out, error);
}

} // namespace

bool Token::FromHex(std::string_view hex, Token& out) {
    if (hex.size() != 32) return false;
    Token parsed;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 16; ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        parsed.bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    out = parsed;
    return true;
}

std::string Token::ToHex() const {
    static const char* kDigits = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : bytes) {
        hex.push_back(kDigits[b >> 4]);
        hex.push_back(kDigits[b & 0x0F]);
    }
    return hex;
}

Bytes HelloRequest() {
    Bytes hello(kHeaderSize, 0xFF);
    PutU16(hello, 0, kMagic);
    PutU16(hello, 2, static_cast<uint16_t>(kHeaderSize));
    return hello;
}

bool ParseHeader(const uint8_t* data, size_t size, Header& out, std::string& error) {
    if (!data || size < kHeaderSize) {
        error = "datagram of " + std::to_string(size) + " bytes is shorter than a miIO header";
        return false;
    }
    if (GetU16(data) != kMagic) {
        error = "not a miIO packet (bad magic)";
        return false;
    }
    Header h;
    h.length = GetU16(data + 2);
    if (h.length != size) {
        error = "length field " + std::to_string(h.length) + " does not match the datagram's " +
                std::to_string(size) + " bytes";
        return false;
    }
    h.unknown = GetU32(data + 4);
    h.deviceId = GetU32(data + 8);
    h.stamp = GetU32(data + 12);
    std::memcpy(h.checksum.data(), data + 16, 16);
    out = h;
    return true;
}

bool DeriveKeys(const Token& token, Block16& key, Block16& iv, std::string& error) {
    if (!Md5(token.bytes.data(), token.bytes.size(), key, error)) return false;
    uint8_t keyAndToken[32];
    std::memcpy(keyAndToken, key.data(), 16);
    std::memcpy(keyAndToken + 16, token.bytes.data(), 16);
    const bool ok = Md5(keyAndToken, sizeof keyAndToken, iv, error);
    UltraCrypt_SecureZero(keyAndToken, sizeof keyAndToken);
    return ok;
}

bool Encode(const Token& token, uint32_t deviceId, uint32_t stamp, std::string_view payload,
            Bytes& out, std::string& error) {
    Block16 key, iv;
    if (!DeriveKeys(token, key, iv, error)) return false;
    UltraCryptSecureBuffer aesKey(key.data(), key.size());
    UltraCryptCbcParams params;
    params.iv.assign(iv.begin(), iv.end());
    params.padding = UltraCryptCbcPadding::Pkcs7;
    std::vector<uint8_t> body;
    const UltraCryptResult r = UltraCrypt_AesCbcEncrypt(aesKey, params, payload.data(), payload.size(), body);
    UltraCrypt_SecureZero(key.data(), key.size());
    if (!r) {
        error = "encryption failed: " + r.message;
        return false;
    }
    if (kHeaderSize + body.size() > 0xFFFF) {
        error = "message too long for one miIO packet";
        return false;
    }

    Bytes packet(kHeaderSize + body.size(), 0);
    PutU16(packet, 0, kMagic);
    PutU16(packet, 2, static_cast<uint16_t>(packet.size()));
    PutU32(packet, 4, 0);
    PutU32(packet, 8, deviceId);
    PutU32(packet, 12, stamp);
    std::copy(body.begin(), body.end(), packet.begin() + kHeaderSize);
    Block16 sum;
    if (!Checksum(packet.data(), packet.size(), token, sum, error)) return false;
    std::copy(sum.begin(), sum.end(), packet.begin() + 16);
    out = std::move(packet);
    return true;
}

bool Decode(const Token& token, const uint8_t* data, size_t size, Header& header,
            std::string& payload, std::string& error) {
    Header h;
    if (!ParseHeader(data, size, h, error)) return false;
    if (size == kHeaderSize) {
        header = h;
        payload.clear();
        return true;
    }
    if ((size - kHeaderSize) % 16 != 0) {
        error = "encrypted body is not a whole number of AES blocks";
        return false;
    }
    Block16 expected;
    if (!Checksum(data, size, token, expected, error)) return false;
    if (!UltraCrypt_ConstantTimeEquals(expected.data(), expected.size(), h.checksum.data(), h.checksum.size())) {
        error = "checksum mismatch (wrong token, or a damaged packet)";
        return false;
    }

    Block16 key, iv;
    if (!DeriveKeys(token, key, iv, error)) return false;
    UltraCryptSecureBuffer aesKey(key.data(), key.size());
    UltraCrypt_SecureZero(key.data(), key.size());
    UltraCryptCbcParams params;
    params.iv.assign(iv.begin(), iv.end());
    params.padding = UltraCryptCbcPadding::Pkcs7;
    UltraCryptSecureBuffer plain;
    const UltraCryptResult r = UltraCrypt_AesCbcDecrypt(aesKey, params, data + kHeaderSize, size - kHeaderSize, plain);
    if (!r) {
        error = "decryption failed: " + r.message;
        return false;
    }
    std::string text(reinterpret_cast<const char*>(plain.Data()), plain.GetSize());
    while (!text.empty() && text.back() == '\0') text.pop_back();
    header = h;
    payload = std::move(text);
    return true;
}

} // namespace Miio
} // namespace SmartHome
} // namespace UltraCanvas
