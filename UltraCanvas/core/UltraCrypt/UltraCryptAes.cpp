// core/UltraCrypt/UltraCryptAes.cpp
// AES (FIPS 197) and the CBC and CTR modes (NIST SP 800-38A), for reading and
// writing formats defined elsewhere that use them.
//
// libsodium has no AES block cipher at all (its AES-256-GCM is AES-NI only
// and does not expose the block function), so this one is ours, like SHA-1.
// It is bitsliced and constant-time: no table is indexed by a secret byte and
// no branch depends on one, so the key and the plaintext do not leak through
// the cache or through timing - the flaw of the usual table-driven AES. The
// S-box is Boyar and Peralta's 113-gate circuit (the one BearSSL's aes_ct
// uses); the rest of the round works on the bit planes directly.
//
// Layout: eight 64-bit planes, plane i holding bit i of every byte. Lane
// 16*b + 4*c + r is byte (row r, column c) of block b, so four blocks travel
// together - CBC decryption and CTR use all four, CBC encryption (a serial
// chain) only the first.
//
// Version: 0.4.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptCore.h"

#include <cstring>

namespace {

constexpr size_t kAesBlock     = 16;
constexpr size_t kLanesPerPass = 4;       // blocks per bitsliced pass
constexpr int    kMaxRounds    = 14;

// A 16-bit pattern repeated in every block's group of lanes.
constexpr uint64_t Rep(uint64_t pattern16) {
    return pattern16 * 0x0001000100010001ULL;
}

struct AesKeySchedule {
    uint64_t roundKey[kMaxRounds + 1][8];  // round keys as bit planes
    int      rounds = 0;
};

void WipeSchedule(AesKeySchedule& ks) {
    UltraCrypt_SecureZero(&ks, sizeof(ks));
}

// Bytes in, bit planes out: byte n goes to lane n (n < 64).
void Pack(const uint8_t* in, size_t count, uint64_t q[8]) {
    for (int i = 0; i < 8; ++i) q[i] = 0;
    for (size_t n = 0; n < count; ++n) {
        const uint64_t byte = in[n];
        for (int i = 0; i < 8; ++i) q[i] |= ((byte >> i) & 1u) << n;
    }
}

void Unpack(const uint64_t q[8], size_t count, uint8_t* out) {
    for (size_t n = 0; n < count; ++n) {
        uint32_t byte = 0;
        for (int i = 0; i < 8; ++i) byte |= static_cast<uint32_t>((q[i] >> n) & 1u) << i;
        out[n] = static_cast<uint8_t>(byte);
    }
}

// SubBytes on every lane at once (Boyar-Peralta, as in BearSSL's
// br_aes_ct_bitslice_Sbox). x0 is the most significant bit.
void SubBytes(uint64_t q[8]) {
    const uint64_t x0 = q[7], x1 = q[6], x2 = q[5], x3 = q[4];
    const uint64_t x4 = q[3], x5 = q[2], x6 = q[1], x7 = q[0];

    // Top linear transformation.
    const uint64_t y14 = x3 ^ x5;
    const uint64_t y13 = x0 ^ x6;
    const uint64_t y9  = x0 ^ x3;
    const uint64_t y8  = x0 ^ x5;
    const uint64_t t0  = x1 ^ x2;
    const uint64_t y1  = t0 ^ x7;
    const uint64_t y4  = y1 ^ x3;
    const uint64_t y12 = y13 ^ y14;
    const uint64_t y2  = y1 ^ x0;
    const uint64_t y5  = y1 ^ x6;
    const uint64_t y3  = y5 ^ y8;
    const uint64_t t1  = x4 ^ y12;
    const uint64_t y15 = t1 ^ x5;
    const uint64_t y20 = t1 ^ x1;
    const uint64_t y6  = y15 ^ x7;
    const uint64_t y10 = y15 ^ t0;
    const uint64_t y11 = y20 ^ y9;
    const uint64_t y7  = x7 ^ y11;
    const uint64_t y17 = y10 ^ y11;
    const uint64_t y19 = y10 ^ y8;
    const uint64_t y16 = t0 ^ y11;
    const uint64_t y21 = y13 ^ y16;
    const uint64_t y18 = x0 ^ y16;

    // Non-linear section: inversion in GF(2^4)^2.
    const uint64_t t2  = y12 & y15;
    const uint64_t t3  = y3 & y6;
    const uint64_t t4  = t3 ^ t2;
    const uint64_t t5  = y4 & x7;
    const uint64_t t6  = t5 ^ t2;
    const uint64_t t7  = y13 & y16;
    const uint64_t t8  = y5 & y1;
    const uint64_t t9  = t8 ^ t7;
    const uint64_t t10 = y2 & y7;
    const uint64_t t11 = t10 ^ t7;
    const uint64_t t12 = y9 & y11;
    const uint64_t t13 = y14 & y17;
    const uint64_t t14 = t13 ^ t12;
    const uint64_t t15 = y8 & y10;
    const uint64_t t16 = t15 ^ t12;
    const uint64_t t17 = t4 ^ t14;
    const uint64_t t18 = t6 ^ t16;
    const uint64_t t19 = t9 ^ t14;
    const uint64_t t20 = t11 ^ t16;
    const uint64_t t21 = t17 ^ y20;
    const uint64_t t22 = t18 ^ y19;
    const uint64_t t23 = t19 ^ y21;
    const uint64_t t24 = t20 ^ y18;

    const uint64_t t25 = t21 ^ t22;
    const uint64_t t26 = t21 & t23;
    const uint64_t t27 = t24 ^ t26;
    const uint64_t t28 = t25 & t27;
    const uint64_t t29 = t28 ^ t22;
    const uint64_t t30 = t23 ^ t24;
    const uint64_t t31 = t22 ^ t26;
    const uint64_t t32 = t31 & t30;
    const uint64_t t33 = t32 ^ t24;
    const uint64_t t34 = t23 ^ t33;
    const uint64_t t35 = t27 ^ t33;
    const uint64_t t36 = t24 & t35;
    const uint64_t t37 = t36 ^ t34;
    const uint64_t t38 = t27 ^ t36;
    const uint64_t t39 = t29 & t38;
    const uint64_t t40 = t25 ^ t39;

    const uint64_t t41 = t40 ^ t37;
    const uint64_t t42 = t29 ^ t33;
    const uint64_t t43 = t29 ^ t40;
    const uint64_t t44 = t33 ^ t37;
    const uint64_t t45 = t42 ^ t41;
    const uint64_t z0  = t44 & y15;
    const uint64_t z1  = t37 & y6;
    const uint64_t z2  = t33 & x7;
    const uint64_t z3  = t43 & y16;
    const uint64_t z4  = t40 & y1;
    const uint64_t z5  = t29 & y7;
    const uint64_t z6  = t42 & y11;
    const uint64_t z7  = t45 & y17;
    const uint64_t z8  = t41 & y10;
    const uint64_t z9  = t44 & y12;
    const uint64_t z10 = t37 & y3;
    const uint64_t z11 = t33 & y4;
    const uint64_t z12 = t43 & y13;
    const uint64_t z13 = t40 & y5;
    const uint64_t z14 = t29 & y2;
    const uint64_t z15 = t42 & y9;
    const uint64_t z16 = t45 & y14;
    const uint64_t z17 = t41 & y8;

    // Bottom linear transformation.
    const uint64_t t46 = z15 ^ z16;
    const uint64_t t47 = z10 ^ z11;
    const uint64_t t48 = z5 ^ z13;
    const uint64_t t49 = z9 ^ z10;
    const uint64_t t50 = z2 ^ z12;
    const uint64_t t51 = z2 ^ z5;
    const uint64_t t52 = z7 ^ z8;
    const uint64_t t53 = z0 ^ z3;
    const uint64_t t54 = z6 ^ z7;
    const uint64_t t55 = z16 ^ z17;
    const uint64_t t56 = z12 ^ t48;
    const uint64_t t57 = t50 ^ t53;
    const uint64_t t58 = z4 ^ t46;
    const uint64_t t59 = z3 ^ t54;
    const uint64_t t60 = t46 ^ t57;
    const uint64_t t61 = z14 ^ t57;
    const uint64_t t62 = t52 ^ t58;
    const uint64_t t63 = t49 ^ t58;
    const uint64_t t64 = z4 ^ t59;
    const uint64_t t65 = t61 ^ t62;
    const uint64_t t66 = z1 ^ t63;
    const uint64_t s0  = t59 ^ t63;
    const uint64_t s6  = t56 ^ ~t62;
    const uint64_t s7  = t48 ^ ~t60;
    const uint64_t t67 = t64 ^ t65;
    const uint64_t s3  = t53 ^ t66;
    const uint64_t s4  = t51 ^ t66;
    const uint64_t s5  = t47 ^ t65;
    const uint64_t s1  = t64 ^ ~s3;
    const uint64_t s2  = t55 ^ ~t67;

    q[7] = s0; q[6] = s1; q[5] = s2; q[4] = s3;
    q[3] = s4; q[2] = s5; q[1] = s6; q[0] = s7;
}

// y -> A^-1(y ^ 0x63), the inverse of the S-box's affine step:
// bit i = y[i+2] ^ y[i+5] ^ y[i+7] ^ (0x05 >> i & 1).
void InverseAffine(uint64_t q[8]) {
    uint64_t n[8];
    for (int i = 0; i < 8; ++i) n[i] = q[(i + 2) & 7] ^ q[(i + 5) & 7] ^ q[(i + 7) & 7];
    n[0] = ~n[0];
    n[2] = ~n[2];
    for (int i = 0; i < 8; ++i) q[i] = n[i];
}

// S(x) = A(x^-1) ^ 0x63, so S^-1(y) = T(S(T(y))) with T the map above.
void InvSubBytes(uint64_t q[8]) {
    InverseAffine(q);
    SubBytes(q);
    InverseAffine(q);
}

// Row r of every column; ShiftRows moves row r left by r columns.
constexpr uint64_t kRow0 = Rep(0x1111), kRow1 = Rep(0x2222);
constexpr uint64_t kRow2 = Rep(0x4444), kRow3 = Rep(0x8888);

uint64_t ShiftRowsPlane(uint64_t x) {
    const uint64_t r1 = x & kRow1, r2 = x & kRow2, r3 = x & kRow3;
    return (x & kRow0)
         | ((r1 >> 4) & Rep(0x0FFF)) | ((r1 << 12) & Rep(0xF000))
         | ((r2 >> 8) & Rep(0x00FF)) | ((r2 << 8)  & Rep(0xFF00))
         | ((r3 << 4) & Rep(0xFFF0)) | ((r3 >> 12) & Rep(0x000F));
}

uint64_t InvShiftRowsPlane(uint64_t x) {
    const uint64_t r1 = x & kRow1, r2 = x & kRow2, r3 = x & kRow3;
    return (x & kRow0)
         | ((r1 << 4) & Rep(0xFFF0)) | ((r1 >> 12) & Rep(0x000F))
         | ((r2 >> 8) & Rep(0x00FF)) | ((r2 << 8)  & Rep(0xFF00))
         | ((r3 >> 4) & Rep(0x0FFF)) | ((r3 << 12) & Rep(0xF000));
}

void ShiftRows(uint64_t q[8])    { for (int i = 0; i < 8; ++i) q[i] = ShiftRowsPlane(q[i]); }
void InvShiftRows(uint64_t q[8]) { for (int i = 0; i < 8; ++i) q[i] = InvShiftRowsPlane(q[i]); }

// Row r of the result is row r+1 of the input, within each column.
uint64_t NextRow(uint64_t x) {
    return ((x >> 1) & Rep(0x7777)) | ((x << 3) & Rep(0x8888));
}

// Multiplication by x in GF(2^8) mod x^8 + x^4 + x^3 + x + 1 (0x1b).
void Xtime(const uint64_t a[8], uint64_t out[8]) {
    out[0] = a[7];
    out[1] = a[0] ^ a[7];
    out[2] = a[1];
    out[3] = a[2] ^ a[7];
    out[4] = a[3] ^ a[7];
    out[5] = a[4];
    out[6] = a[5];
    out[7] = a[6];
}

// out_r = 2*a_r ^ 3*a_r+1 ^ a_r+2 ^ a_r+3 = 2*(a_r ^ a_r+1) ^ a_r+1 ^ a_r+2 ^ a_r+3
void MixColumns(uint64_t q[8]) {
    uint64_t r1[8], rest[8], t[8], twice[8];
    for (int i = 0; i < 8; ++i) {
        r1[i] = NextRow(q[i]);
        const uint64_t r2 = NextRow(r1[i]);
        rest[i] = r1[i] ^ r2 ^ NextRow(r2);
        t[i] = q[i] ^ r1[i];
    }
    Xtime(t, twice);
    for (int i = 0; i < 8; ++i) q[i] = twice[i] ^ rest[i];
}

// InvMixColumns = MixColumns after a_r ^= 4*(a_r ^ a_r+2) (the AES book's
// decomposition of the 14/11/13/9 matrix).
void InvMixColumns(uint64_t q[8]) {
    uint64_t t[8], twice[8], four[8];
    for (int i = 0; i < 8; ++i) t[i] = q[i] ^ NextRow(NextRow(q[i]));
    Xtime(t, twice);
    Xtime(twice, four);
    for (int i = 0; i < 8; ++i) q[i] ^= four[i];
    MixColumns(q);
}

void AddRoundKey(uint64_t q[8], const uint64_t rk[8]) {
    for (int i = 0; i < 8; ++i) q[i] ^= rk[i];
}

// SubWord for the key schedule: the same constant-time S-box on four bytes.
void SubWord(uint8_t w[4]) {
    uint64_t q[8];
    Pack(w, 4, q);
    SubBytes(q);
    Unpack(q, 4, w);
    UltraCrypt_SecureZero(q, sizeof(q));
}

// FIPS 197 §5.2. keySize is 16, 24 or 32.
void ExpandKey(const uint8_t* key, size_t keySize, AesKeySchedule& ks) {
    const int nk = static_cast<int>(keySize / 4);
    ks.rounds = nk + 6;
    const int words = 4 * (ks.rounds + 1);

    uint8_t w[4 * 4 * (kMaxRounds + 1)];
    std::memcpy(w, key, keySize);
    uint8_t rcon = 0x01;
    for (int i = nk; i < words; ++i) {
        uint8_t t[4];
        std::memcpy(t, w + 4 * (i - 1), 4);
        if (i % nk == 0) {
            const uint8_t first = t[0];
            t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = first;
            SubWord(t);
            t[0] ^= rcon;
            // rcon is public, so this branch leaks nothing.
            rcon = static_cast<uint8_t>((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0));
        } else if (nk > 6 && i % nk == 4) {
            SubWord(t);
        }
        for (int j = 0; j < 4; ++j) w[4 * i + j] = w[4 * (i - nk) + j] ^ t[j];
        UltraCrypt_SecureZero(t, sizeof(t));
    }

    for (int r = 0; r <= ks.rounds; ++r) {
        uint64_t* rk = ks.roundKey[r];
        Pack(w + 16 * r, 16, rk);
        for (int i = 0; i < 8; ++i) rk[i] = Rep(rk[i] & 0xFFFF);
    }
    UltraCrypt_SecureZero(w, sizeof(w));
}

// Encrypts / decrypts `blocks` (1..4) consecutive 16-byte blocks in place.
void EncryptBlocks(const AesKeySchedule& ks, uint8_t* data, size_t blocks) {
    uint64_t q[8];
    Pack(data, blocks * kAesBlock, q);
    AddRoundKey(q, ks.roundKey[0]);
    for (int r = 1; r < ks.rounds; ++r) {
        SubBytes(q);
        ShiftRows(q);
        MixColumns(q);
        AddRoundKey(q, ks.roundKey[r]);
    }
    SubBytes(q);
    ShiftRows(q);
    AddRoundKey(q, ks.roundKey[ks.rounds]);
    Unpack(q, blocks * kAesBlock, data);
    UltraCrypt_SecureZero(q, sizeof(q));
}

void DecryptBlocks(const AesKeySchedule& ks, uint8_t* data, size_t blocks) {
    uint64_t q[8];
    Pack(data, blocks * kAesBlock, q);
    AddRoundKey(q, ks.roundKey[ks.rounds]);
    for (int r = ks.rounds - 1; r > 0; --r) {
        InvShiftRows(q);
        InvSubBytes(q);
        AddRoundKey(q, ks.roundKey[r]);
        InvMixColumns(q);
    }
    InvShiftRows(q);
    InvSubBytes(q);
    AddRoundKey(q, ks.roundKey[0]);
    Unpack(q, blocks * kAesBlock, data);
    UltraCrypt_SecureZero(q, sizeof(q));
}

bool IsAesKeySize(size_t size) {
    return size == 16 || size == 24 || size == 32;
}

UltraCryptResult CheckCommon(const UltraCryptSecureBuffer& key,
                             const void* data, size_t size) {
    if (!IsAesKeySize(key.GetSize())) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidKeySize,
                                       "AES key must be 16, 24 or 32 bytes");
    }
    if (size > 0 && !data) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidArgument,
                                       "null data with non-zero size");
    }
    return UltraCryptResult::Ok();
}

} // namespace

size_t UltraCrypt_GetAesBlockSize() {
    return kAesBlock;
}

UltraCryptResult UltraCrypt_AesCbcEncrypt(const UltraCryptSecureBuffer& key,
                                          UltraCryptCbcParams& params,
                                          const void* plaintext, size_t size,
                                          std::vector<uint8_t>& outCiphertext) {
    outCiphertext.clear();
    auto check = CheckCommon(key, plaintext, size);
    if (!check) return check;
    const bool pad = params.padding == UltraCryptCbcPadding::Pkcs7;
    if (!pad && size % kAesBlock != 0) {
        return UltraCryptResult::Error(
            UltraCryptResultCode::InvalidArgument,
            "without padding the plaintext must be a whole number of 16-byte blocks");
    }
    if (params.iv.empty()) {
        // As with the AEAD nonce: a fresh random IV unless the caller supplies
        // one. A predictable CBC IV leaks whether two messages share a prefix.
        params.iv.assign(kAesBlock, 0);
        auto rr = UltraCrypt_RandomBytes(params.iv.data(), kAesBlock);
        if (!rr) { params.iv.clear(); return rr; }
    } else if (params.iv.size() != kAesBlock) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "the CBC IV must be 16 bytes");
    }

    const size_t padLen = pad ? kAesBlock - size % kAesBlock : 0;   // 1..16
    outCiphertext.resize(size + padLen);
    if (size > 0) std::memcpy(outCiphertext.data(), plaintext, size);
    for (size_t i = 0; i < padLen; ++i) {
        outCiphertext[size + i] = static_cast<uint8_t>(padLen);
    }

    AesKeySchedule ks;
    ExpandKey(key.Data(), key.GetSize(), ks);
    const uint8_t* chain = params.iv.data();
    for (size_t off = 0; off < outCiphertext.size(); off += kAesBlock) {
        uint8_t* block = outCiphertext.data() + off;
        for (size_t i = 0; i < kAesBlock; ++i) block[i] ^= chain[i];
        EncryptBlocks(ks, block, 1);
        chain = block;
    }
    WipeSchedule(ks);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_AesCbcDecrypt(const UltraCryptSecureBuffer& key,
                                          const UltraCryptCbcParams& params,
                                          const void* ciphertext, size_t size,
                                          UltraCryptSecureBuffer& outPlaintext) {
    outPlaintext.Clear();
    auto check = CheckCommon(key, ciphertext, size);
    if (!check) return check;
    if (params.iv.size() != kAesBlock) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "the CBC IV must be 16 bytes");
    }
    const bool pad = params.padding == UltraCryptCbcPadding::Pkcs7;
    if (size % kAesBlock != 0 || (pad && size == 0)) {
        return UltraCryptResult::Error(
            UltraCryptResultCode::InvalidArgument,
            "CBC ciphertext must be a whole, non-empty number of 16-byte blocks");
    }

    UltraCryptSecureBuffer plain(ciphertext, size);
    if (plain.GetSize() != size) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "could not allocate plaintext buffer");
    }
    const auto* in = static_cast<const uint8_t*>(ciphertext);

    // Every block's chaining value is ciphertext the caller already holds, so
    // the blocks are independent: decrypt them four at a time.
    AesKeySchedule ks;
    ExpandKey(key.Data(), key.GetSize(), ks);
    const size_t step = kLanesPerPass * kAesBlock;
    for (size_t off = 0; off < size; off += step) {
        const size_t bytes = (size - off < step) ? size - off : step;
        uint8_t* out = plain.Data() + off;
        DecryptBlocks(ks, out, bytes / kAesBlock);
        for (size_t i = 0; i < bytes; ++i) {
            const size_t at = off + i;
            out[i] ^= at < kAesBlock ? params.iv[at] : in[at - kAesBlock];
        }
    }
    WipeSchedule(ks);

    if (pad) {
        // Checked without a branch on the plaintext: the padding length is
        // secret until it is known to be valid, and a padding check that
        // takes longer on some inputs than others is the classic oracle.
        const uint8_t* tail = plain.Data() + size - kAesBlock;
        const uint32_t padLen = tail[kAesBlock - 1];
        uint32_t bad = ((padLen - 1u) >> 8) & 1u;              // padLen == 0
        bad |= ((static_cast<uint32_t>(kAesBlock) - padLen) >> 8) & 1u;  // > 16
        uint32_t diff = 0;
        for (uint32_t i = 0; i < kAesBlock; ++i) {
            // inPad is all-ones for the last padLen bytes, zero before them.
            const uint32_t fromEnd = 15u - i;
            const uint32_t inPad = 0u - ((fromEnd - padLen) >> 31);
            diff |= inPad & (tail[i] ^ padLen);
        }
        bad |= (0u - diff) >> 31;                              // any byte off
        if (bad) {
            plain.Clear();
            return UltraCryptResult::Error(
                UltraCryptResultCode::DecryptionFailed,
                "decryption failed: wrong key, or the data was altered");
        }
        plain.Resize(size - padLen);
    }
    outPlaintext = std::move(plain);
    return UltraCryptResult::Ok();
}

// ============================================================================
// CTR
// ============================================================================
namespace {

// Adds one to a 16-byte big-endian counter block.
void IncrementCounter(uint8_t counter[kAesBlock]) {
    for (size_t i = kAesBlock; i-- > 0;) {
        if (++counter[i] != 0) break;
    }
}

// XORs the key stream from `iv` on into `data` (in place), four blocks a pass.
void CtrXor(const UltraCryptSecureBuffer& key, const std::vector<uint8_t>& iv, uint8_t* data, size_t size) {
    AesKeySchedule ks;
    ExpandKey(key.Data(), key.GetSize(), ks);
    uint8_t counter[kAesBlock];
    std::memcpy(counter, iv.data(), kAesBlock);
    uint8_t stream[kLanesPerPass * kAesBlock];
    const size_t step = sizeof(stream);
    for (size_t off = 0; off < size; off += step) {
        const size_t bytes = (size - off < step) ? size - off : step;
        const size_t blocks = (bytes + kAesBlock - 1) / kAesBlock;
        for (size_t b = 0; b < blocks; ++b) {
            std::memcpy(stream + b * kAesBlock, counter, kAesBlock);
            IncrementCounter(counter);
        }
        EncryptBlocks(ks, stream, blocks);
        for (size_t i = 0; i < bytes; ++i) data[off + i] ^= stream[i];
    }
    UltraCrypt_SecureZero(stream, sizeof(stream));
    UltraCrypt_SecureZero(counter, sizeof(counter));
    WipeSchedule(ks);
}

} // namespace

UltraCryptResult UltraCrypt_AesCtrEncrypt(const UltraCryptSecureBuffer& key,
                                          UltraCryptCtrParams& params,
                                          const void* plaintext, size_t size,
                                          std::vector<uint8_t>& outCiphertext) {
    outCiphertext.clear();
    auto check = CheckCommon(key, plaintext, size);
    if (!check) return check;
    if (params.iv.empty()) {
        params.iv.assign(kAesBlock, 0);
        auto rr = UltraCrypt_RandomBytes(params.iv.data(), kAesBlock);
        if (!rr) { params.iv.clear(); return rr; }
    } else if (params.iv.size() != kAesBlock) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "the CTR IV must be 16 bytes");
    }
    outCiphertext.resize(size);
    if (size > 0) std::memcpy(outCiphertext.data(), plaintext, size);
    CtrXor(key, params.iv, outCiphertext.data(), size);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_AesCtrDecrypt(const UltraCryptSecureBuffer& key,
                                          const UltraCryptCtrParams& params,
                                          const void* ciphertext, size_t size,
                                          UltraCryptSecureBuffer& outPlaintext) {
    outPlaintext.Clear();
    auto check = CheckCommon(key, ciphertext, size);
    if (!check) return check;
    if (params.iv.size() != kAesBlock) {
        return UltraCryptResult::Error(UltraCryptResultCode::InvalidNonceSize,
                                       "the CTR IV must be 16 bytes");
    }
    UltraCryptSecureBuffer plain(ciphertext, size);
    if (plain.GetSize() != size) {
        return UltraCryptResult::Error(UltraCryptResultCode::InternalError,
                                       "could not allocate plaintext buffer");
    }
    CtrXor(key, params.iv, plain.Data(), size);
    outPlaintext = std::move(plain);
    return UltraCryptResult::Ok();
}
