// MiioPacket.h
// Xiaomi miIO packets: framing, checksum and encryption.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
//
// miIO is the local protocol of Xiaomi's Wi-Fi devices (Mi / Mijia, Roborock,
// Dreame and Viomi vacuums, purifiers, lamps, plugs): UDP to port 54321, one
// JSON-RPC message per datagram. Every packet starts with a 32-byte header,
// all fields big-endian:
//
//   0  magic 0x2131        2  length (header + body)   4  unknown (0)
//   8  device id          12  timestamp (seconds, the device's clock)
//  16  checksum: MD5(header bytes 0-15 | token | body)
//
// The body is the JSON text, AES-128-CBC with PKCS#7 padding, keyed with
// key = MD5(token) and iv = MD5(key | token). The token is the device's
// 16-byte secret (32 hex digits) set when it was added to the Mi Home app.
//
// A *hello* - a header of 0xFF after magic and length 32 - is answered by any
// device with a bare header carrying its id and clock; that reply is both
// discovery and the handshake every exchange starts with.
//
// Pure bytes in, bytes out: no socket, no clock. MD5 and AES come from
// UltraCrypt. tests/MiioPacketTest.cpp checks a packet built with openssl and
// md5sum, independently of this code.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {
namespace Miio {

constexpr uint16_t kPort = 54321;
constexpr uint16_t kMagic = 0x2131;
constexpr size_t kHeaderSize = 32;

using Bytes = std::vector<uint8_t>;
using Block16 = std::array<uint8_t, 16>;

struct Token {
    Block16 bytes{};

    // 32 hex digits, either case. False (and `out` untouched) otherwise.
    static bool FromHex(std::string_view hex, Token& out);
    std::string ToHex() const;
};

struct Header {
    uint16_t length = 0;
    uint32_t unknown = 0;
    uint32_t deviceId = 0;
    uint32_t stamp = 0;
    Block16 checksum{};
};

// The 32-byte hello every device answers.
Bytes HelloRequest();

// Reads the header of a received datagram: magic, and a length that matches
// the datagram. A 32-byte datagram is a bare header (a hello or its reply).
bool ParseHeader(const uint8_t* data, size_t size, Header& out, std::string& error);

// key = MD5(token), iv = MD5(key | token).
bool DeriveKeys(const Token& token, Block16& key, Block16& iv, std::string& error);

// Encrypts `payload` and frames it for the device `deviceId` at the device's
// clock `stamp`.
bool Encode(const Token& token, uint32_t deviceId, uint32_t stamp, std::string_view payload,
            Bytes& out, std::string& error);

// Checks the checksum against the token and decrypts. Trailing NUL bytes some
// firmware appends to the JSON are dropped. A bare header decodes to an empty
// payload without a checksum check (a hello reply's checksum field carries
// the token on an unprovisioned device and 0x00 or 0xFF otherwise).
bool Decode(const Token& token, const uint8_t* data, size_t size, Header& header,
            std::string& payload, std::string& error);

} // namespace Miio
} // namespace SmartHome
} // namespace UltraCanvas
