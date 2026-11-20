// core/UltraNet/UltraNetSha1.h
// SHA-1 for the one place UltraNet's own WebSocket client needs it: the
// Sec-WebSocket-Accept header, by which a server shows it read the key of
// this handshake (RFC 6455 4.2.2 - the hash is the protocol's, not a choice,
// and nothing is protected by it). Internal; not part of the public surface,
// and not a digest to use for anything else.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ultranet_internal {

// The SHA-1 (FIPS 180-4) of `size` bytes at `data`.
inline std::array<uint8_t, 20> Sha1(const void* data, std::size_t size) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const auto rotl = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    const auto compress = [&](const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            const uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };

    const auto* bytes = static_cast<const uint8_t*>(data);
    std::size_t at = 0;
    for (; at + 64 <= size; at += 64) compress(bytes + at);
    // The rest, the 0x80, zeros, and the length in bits as 64 bits.
    uint8_t tail[128] = {};
    const std::size_t rest = size - at;
    for (std::size_t i = 0; i < rest; ++i) tail[i] = bytes[at + i];
    tail[rest] = 0x80;
    const std::size_t padded = rest < 56 ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int i = 0; i < 8; ++i) tail[padded - 1 - i] = static_cast<uint8_t>(bits >> (8 * i));
    compress(tail);
    if (padded == 128) compress(tail + 64);

    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[4 * i]     = static_cast<uint8_t>(h[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(h[i]);
    }
    return out;
}

} // namespace ultranet_internal
