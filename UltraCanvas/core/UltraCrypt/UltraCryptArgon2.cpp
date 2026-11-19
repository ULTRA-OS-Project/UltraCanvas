// core/UltraCrypt/UltraCryptArgon2.cpp
// Argon2id (RFC 9106, version 0x13) with any number of lanes - see
// UltraCryptArgon2.h. libsodium's crypto_pwhash computes Argon2id with one lane
// only; files written elsewhere (LibreOffice's ODF 1.4 encryption: 4 lanes)
// need more. BLAKE2b is libsodium's; the memory filling, the BlaMka
// compression and the indexing follow the RFC and its reference
// implementation (libargon2), against whose output Tests/UltraCryptTests.cpp
// checks this one.
//
// Argon2id is data-independent (Argon2i) in the first two slices of the first
// pass and data-dependent (Argon2d) after that, as the RFC defines it; the
// memory addresses it touches there depend on the password, which is the
// algorithm's documented trade-off, not a flaw of this code.
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCryptArgon2.h"
#include "UltraCrypt/UltraCryptCore.h"   // UltraCrypt_SecureZero

#include <cstring>
#include <new>

#ifdef ULTRACRYPT_HAVE_SODIUM
#include <sodium.h>
#endif

namespace UltraCryptInternal {

#ifdef ULTRACRYPT_HAVE_SODIUM
namespace {

constexpr uint32_t kVersion = 0x13;
constexpr uint32_t kTypeId = 2;            // Argon2id
constexpr uint32_t kSyncPoints = 4;        // slices per pass
constexpr size_t kBlockWords = 128;        // 1024-byte blocks
constexpr size_t kBlockBytes = kBlockWords * 8;
constexpr size_t kAddressesPerBlock = kBlockWords;

struct Block {
    uint64_t v[kBlockWords];
};

void StoreLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint64_t LoadLe64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

void StoreLe64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// BLAKE2b of `in` with an output of `outLength` (1-64) bytes, prefixed by
// `prefix` when given.
void Blake2b(uint8_t* out, size_t outLength, const uint8_t* prefix, size_t prefixLength,
             const uint8_t* in, size_t inLength) {
    crypto_generichash_blake2b_state state;
    crypto_generichash_blake2b_init(&state, nullptr, 0, outLength);
    if (prefixLength) crypto_generichash_blake2b_update(&state, prefix, prefixLength);
    if (inLength) crypto_generichash_blake2b_update(&state, in, inLength);
    crypto_generichash_blake2b_final(&state, out, outLength);
    UltraCrypt_SecureZero(&state, sizeof(state));
}

// H' (RFC 9106 §3.3): BLAKE2b of any output length.
void HashLong(uint8_t* out, size_t outLength, const uint8_t* in, size_t inLength) {
    uint8_t length[4];
    StoreLe32(length, static_cast<uint32_t>(outLength));
    if (outLength <= 64) {
        Blake2b(out, outLength, length, sizeof(length), in, inLength);
        return;
    }
    uint8_t v[64];
    Blake2b(v, 64, length, sizeof(length), in, inLength);
    std::memcpy(out, v, 32);
    out += 32;
    size_t remaining = outLength - 32;
    while (remaining > 64) {
        Blake2b(v, 64, nullptr, 0, v, 64);
        std::memcpy(out, v, 32);
        out += 32;
        remaining -= 32;
    }
    Blake2b(v, remaining, nullptr, 0, v, 64);
    std::memcpy(out, v, remaining);
    UltraCrypt_SecureZero(v, sizeof(v));
}

inline uint64_t Rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

// BLAKE2b's G with BlaMka's multiplication (RFC 9106 §3.6).
inline uint64_t BlaMka(uint64_t x, uint64_t y) {
    const uint64_t m = 0xFFFFFFFFull;
    return x + y + 2 * ((x & m) * (y & m));
}

inline void Gb(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d) {
    a = BlaMka(a, b); d = Rotr(d ^ a, 32);
    c = BlaMka(c, d); b = Rotr(b ^ c, 24);
    a = BlaMka(a, b); d = Rotr(d ^ a, 16);
    c = BlaMka(c, d); b = Rotr(b ^ c, 63);
}

// The permutation P on sixteen words of the block, picked by `at`.
inline void Permute(uint64_t* w, const size_t at[16]) {
    Gb(w[at[0]], w[at[4]], w[at[8]], w[at[12]]);
    Gb(w[at[1]], w[at[5]], w[at[9]], w[at[13]]);
    Gb(w[at[2]], w[at[6]], w[at[10]], w[at[14]]);
    Gb(w[at[3]], w[at[7]], w[at[11]], w[at[15]]);
    Gb(w[at[0]], w[at[5]], w[at[10]], w[at[15]]);
    Gb(w[at[1]], w[at[6]], w[at[11]], w[at[12]]);
    Gb(w[at[2]], w[at[7]], w[at[8]], w[at[13]]);
    Gb(w[at[3]], w[at[4]], w[at[9]], w[at[14]]);
}

// The compression G (RFC 9106 §3.5): next = P(prev ^ ref) ^ prev ^ ref, and
// from the second pass on XORed into what `next` held (version 0x13).
// `next` may be `ref` (the address generator does that).
void FillBlock(const Block& prev, const Block& ref, Block& next, bool withXor) {
    Block r, keep;
    for (size_t i = 0; i < kBlockWords; ++i) r.v[i] = prev.v[i] ^ ref.v[i];
    keep = r;
    if (withXor)
        for (size_t i = 0; i < kBlockWords; ++i) keep.v[i] ^= next.v[i];
    for (size_t row = 0; row < 8; ++row) {           // rows: words 16 row .. 16 row + 15
        size_t at[16];
        for (size_t k = 0; k < 16; ++k) at[k] = 16 * row + k;
        Permute(r.v, at);
    }
    for (size_t col = 0; col < 8; ++col) {           // columns: two words from each row
        size_t at[16];
        for (size_t k = 0; k < 8; ++k) {
            at[2 * k] = 2 * col + 16 * k;
            at[2 * k + 1] = 2 * col + 16 * k + 1;
        }
        Permute(r.v, at);
    }
    for (size_t i = 0; i < kBlockWords; ++i) next.v[i] = keep.v[i] ^ r.v[i];
    UltraCrypt_SecureZero(&r, sizeof(r));
    UltraCrypt_SecureZero(&keep, sizeof(keep));
}

struct Instance {
    Block* memory = nullptr;
    uint32_t passes = 0, lanes = 0, memoryBlocks = 0, segmentLength = 0, laneLength = 0;
};

void NextAddresses(Block& address, Block& input, const Block& zero) {
    input.v[6]++;
    FillBlock(zero, input, address, false);
    FillBlock(zero, address, address, false);
}

// The column a block refers to within its reference lane (RFC 9106 §3.4.2).
uint32_t IndexAlpha(const Instance& in, uint32_t pass, uint32_t slice, uint32_t index,
                    uint32_t pseudoRand, bool sameLane) {
    uint32_t areaSize;
    if (pass == 0) {
        if (slice == 0) areaSize = index - 1;                      // every block so far but the previous
        else if (sameLane) areaSize = slice * in.segmentLength + index - 1;
        else areaSize = slice * in.segmentLength + (index == 0 ? uint32_t(-1) : 0u);
    } else {
        if (sameLane) areaSize = in.laneLength - in.segmentLength + index - 1;
        else areaSize = in.laneLength - in.segmentLength + (index == 0 ? uint32_t(-1) : 0u);
    }
    uint64_t relative = pseudoRand;
    relative = (relative * relative) >> 32;
    relative = areaSize - 1 - ((uint64_t(areaSize) * relative) >> 32);
    uint32_t start = 0;
    if (pass != 0) start = (slice == kSyncPoints - 1) ? 0 : (slice + 1) * in.segmentLength;
    return static_cast<uint32_t>((start + relative) % in.laneLength);
}

void FillSegment(const Instance& in, uint32_t pass, uint32_t lane, uint32_t slice) {
    const bool dataIndependent = pass == 0 && slice < kSyncPoints / 2;   // Argon2id
    Block address{}, input{}, zero{};
    if (dataIndependent) {
        input.v[0] = pass;
        input.v[1] = lane;
        input.v[2] = slice;
        input.v[3] = in.memoryBlocks;
        input.v[4] = in.passes;
        input.v[5] = kTypeId;
    }
    uint32_t start = 0;
    if (pass == 0 && slice == 0) {
        start = 2;   // the first two blocks of a lane come from H0
        if (dataIndependent) NextAddresses(address, input, zero);
    }
    uint32_t current = lane * in.laneLength + slice * in.segmentLength + start;
    uint32_t previous = (current % in.laneLength == 0) ? current + in.laneLength - 1 : current - 1;
    for (uint32_t i = start; i < in.segmentLength; ++i, ++current, ++previous) {
        if (current % in.laneLength == 1) previous = current - 1;
        uint64_t pseudoRand;
        if (dataIndependent) {
            if (i % kAddressesPerBlock == 0) NextAddresses(address, input, zero);
            pseudoRand = address.v[i % kAddressesPerBlock];
        } else {
            pseudoRand = in.memory[previous].v[0];
        }
        uint32_t refLane = static_cast<uint32_t>((pseudoRand >> 32) % in.lanes);
        if (pass == 0 && slice == 0) refLane = lane;
        const uint32_t refIndex = IndexAlpha(in, pass, slice, i, static_cast<uint32_t>(pseudoRand), refLane == lane);
        const Block& ref = in.memory[size_t(in.laneLength) * refLane + refIndex];
        FillBlock(in.memory[previous], ref, in.memory[current], pass != 0);
    }
    UltraCrypt_SecureZero(&address, sizeof(address));
    UltraCrypt_SecureZero(&input, sizeof(input));
}

} // namespace
#endif

bool Argon2idRaw(const uint8_t* password, size_t passwordLength, const uint8_t* salt, size_t saltLength,
                 uint32_t passes, uint32_t memoryKiB, uint32_t lanes, uint8_t* out, size_t outLength) {
#ifdef ULTRACRYPT_HAVE_SODIUM
    if (lanes == 0 || lanes > 0xFFFFFF || passes == 0 || outLength < 4 || outLength > 0xFFFFFFFFu ||
        saltLength < 8 || saltLength > 0xFFFFFFFFu || passwordLength > 0xFFFFFFFFu ||
        memoryKiB < 8u * lanes)
        return false;
    if (sodium_init() < 0) return false;

    Instance in;
    in.passes = passes;
    in.lanes = lanes;
    uint32_t blocks = memoryKiB;
    if (blocks < 2 * kSyncPoints * lanes) blocks = 2 * kSyncPoints * lanes;
    in.segmentLength = blocks / (lanes * kSyncPoints);
    in.memoryBlocks = in.segmentLength * lanes * kSyncPoints;
    in.laneLength = in.segmentLength * kSyncPoints;
    in.memory = new (std::nothrow) Block[in.memoryBlocks];
    if (!in.memory) return false;

    // H0 (RFC 9106 §3.2): the parameters and inputs, each length-prefixed.
    uint8_t h0[64 + 8];
    {
        crypto_generichash_blake2b_state state;
        crypto_generichash_blake2b_init(&state, nullptr, 0, 64);
        auto put32 = [&state](uint32_t v) {
            uint8_t le[4];
            StoreLe32(le, v);
            crypto_generichash_blake2b_update(&state, le, 4);
        };
        put32(lanes);
        put32(static_cast<uint32_t>(outLength));
        put32(memoryKiB);
        put32(passes);
        put32(kVersion);
        put32(kTypeId);
        put32(static_cast<uint32_t>(passwordLength));
        if (passwordLength) crypto_generichash_blake2b_update(&state, password, passwordLength);
        put32(static_cast<uint32_t>(saltLength));
        crypto_generichash_blake2b_update(&state, salt, saltLength);
        put32(0);   // no secret
        put32(0);   // no associated data
        crypto_generichash_blake2b_final(&state, h0, 64);
        UltraCrypt_SecureZero(&state, sizeof(state));
    }

    // The first two blocks of every lane: H'(H0 || column || lane).
    uint8_t bytes[kBlockBytes];
    for (uint32_t lane = 0; lane < lanes; ++lane) {
        for (uint32_t column = 0; column < 2; ++column) {
            StoreLe32(h0 + 64, column);
            StoreLe32(h0 + 68, lane);
            HashLong(bytes, kBlockBytes, h0, sizeof(h0));
            Block& b = in.memory[size_t(lane) * in.laneLength + column];
            for (size_t w = 0; w < kBlockWords; ++w) b.v[w] = LoadLe64(bytes + 8 * w);
        }
    }
    UltraCrypt_SecureZero(h0, sizeof(h0));

    for (uint32_t pass = 0; pass < passes; ++pass)
        for (uint32_t slice = 0; slice < kSyncPoints; ++slice)
            for (uint32_t lane = 0; lane < lanes; ++lane) FillSegment(in, pass, lane, slice);

    // The last column of every lane, XORed, hashed to the output.
    Block final = in.memory[in.laneLength - 1];
    for (uint32_t lane = 1; lane < lanes; ++lane) {
        const Block& last = in.memory[size_t(lane) * in.laneLength + in.laneLength - 1];
        for (size_t w = 0; w < kBlockWords; ++w) final.v[w] ^= last.v[w];
    }
    for (size_t w = 0; w < kBlockWords; ++w) StoreLe64(bytes + 8 * w, final.v[w]);
    HashLong(out, outLength, bytes, kBlockBytes);

    UltraCrypt_SecureZero(bytes, sizeof(bytes));
    UltraCrypt_SecureZero(&final, sizeof(final));
    UltraCrypt_SecureZero(in.memory, sizeof(Block) * in.memoryBlocks);
    delete[] in.memory;
    return true;
#else
    (void)password; (void)passwordLength; (void)salt; (void)saltLength;
    (void)passes; (void)memoryKiB; (void)lanes; (void)out; (void)outLength;
    return false;
#endif
}

} // namespace UltraCryptInternal
