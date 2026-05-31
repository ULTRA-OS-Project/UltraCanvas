// Apps/AnchorPoint/net/Sha256.h
// AnchorPoint - self-contained streaming SHA-256 (no OpenSSL dependency).
// Version: 0.1.0
// Author: AnchorPoint
//
// Used to verify end-to-end file integrity. When UltraNet/UltraVault bring a
// vetted crypto surface, this can be replaced by that; until then a small,
// dependency-free implementation keeps the M1 core standalone.
#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace AnchorPoint {

class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        bitlen_ = 0;
        bufLen_ = 0;
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    }

    void Update(const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        bitlen_ += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            size_t take = 64 - bufLen_;
            if (take > len) take = len;
            std::memcpy(buf_ + bufLen_, p, take);
            bufLen_ += take;
            p += take;
            len -= take;
            if (bufLen_ == 64) { Transform(buf_); bufLen_ = 0; }
        }
    }

    // Finalises and returns the 32-byte digest. Object should be Reset() to reuse.
    std::array<uint8_t, 32> Final() {
        uint64_t bits = bitlen_;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t zero = 0x00;
        while (bufLen_ != 56) Update(&zero, 1);
        uint8_t lenbe[8];
        for (int i = 0; i < 8; ++i) lenbe[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
        Update(lenbe, 8);

        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<uint8_t>(h_[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
        }
        return out;
    }

    static std::string ToHex(const std::array<uint8_t, 32>& d) {
        static const char* hx = "0123456789abcdef";
        std::string s(64, '0');
        for (int i = 0; i < 32; ++i) {
            s[i * 2]     = hx[d[i] >> 4];
            s[i * 2 + 1] = hx[d[i] & 0xF];
        }
        return s;
    }

private:
    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Transform(const uint8_t* chunk) {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(chunk[i * 4]) << 24) | (uint32_t(chunk[i * 4 + 1]) << 16) |
                   (uint32_t(chunk[i * 4 + 2]) << 8) | uint32_t(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    std::array<uint32_t, 8> h_;
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t bitlen_ = 0;
};

} // namespace AnchorPoint
