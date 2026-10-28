// Tests/UltraCryptTests.cpp
// Unit tests for the UltraCrypt module. Self-contained: no test framework, no
// UI stack, so it runs headless anywhere UltraCrypt builds.
//
// Coverage follows Docs/Modules/UltraCrypt/README.md §7:
//  - published test vectors (FIPS 180-4 SHA-2, RFC 4231 HMAC-SHA-2)
//  - AEAD round-trip plus the four mandated negative cases (flipped ciphertext
//    bit, flipped AAD bit, wrong nonce, truncated tag), each asserting
//    AuthenticationFailed AND an empty output
//  - AES-CBC against FIPS 197 App. C and SP 800-38A F.2, PKCS #7 lengths,
//    and refused padding reported as DecryptionFailed with an empty output
//  - AES-CTR against SP 800-38A F.5, any message length, the 128-bit counter
//    carry, and PBKDF2 against RFC 7914 (SHA-256) and SHA-512 values
//  - KDF determinism, salt sensitivity and stored-parameter honesty
//  - Ed25519 against the RFC 8032 §7.1 vectors, plus a flipped message bit, a
//    flipped signature bit and a wrong key, each reported as SignatureInvalid
//  - secure-buffer semantics (move-only, wipe on destroy)
//  - X.509 and CMS, against a test authority made for these tests (openssl,
//    valid to 2126): what a certificate says, the trust verdicts (trusted,
//    untrusted, expired, not yet valid, wrong purpose), PKCS #12 (modern and
//    RC2, wrong password), detached signatures (holding, a changed byte of
//    the content or the signature, a key that is not the certificate's) and
//    enveloped data (opened by its recipient, refused by another)
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptCore.h"
#include "UltraCrypt/UltraCryptX509.h"
#include "UltraCanvasTextUtils.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static void CheckHex(const std::vector<uint8_t>& actual,
                     const std::string& expectedHex,
                     const std::string& what) {
    const std::string got = UltraCrypt_ToHex(actual);
    ++g_checks;
    if (got != expectedHex) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected %s\n    got      %s\n",
                    what.c_str(), expectedHex.c_str(), got.c_str());
    }
}

static UltraCryptSecureBuffer BufferFromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    UltraCrypt_FromHex(hex, bytes);
    return UltraCryptSecureBuffer(bytes.data(), bytes.size());
}

// ===== Hashing — FIPS 180-4 vectors =====
static void TestHashing() {
    std::printf("SHA-2 (FIPS 180-4 vectors)\n");
    const char* abc = "abc";
    std::vector<uint8_t> digest;

    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256,
                                            abc, 3, digest)),
          "SHA-256 of \"abc\" succeeds");
    CheckHex(digest,
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
             "SHA-256(\"abc\")");

    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA512,
                                            abc, 3, digest)),
          "SHA-512 of \"abc\" succeeds");
    CheckHex(digest,
             "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
             "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
             "SHA-512(\"abc\")");

    // Empty input must be accepted (and must not be confused with an error).
    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256,
                                            nullptr, 0, digest)),
          "SHA-256 of the empty input succeeds");
    CheckHex(digest,
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
             "SHA-256(\"\")");

    // Streaming must agree with one-shot.
    UltraCryptHasher hasher(UltraCryptHashAlgorithm::SHA256);
    Check(hasher.IsValid(), "streaming hasher constructs");
    hasher.Update("a", 1);
    hasher.Update("b", 1);
    hasher.Update("c", 1);
    CheckHex(hasher.Final(),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
             "streaming SHA-256 matches one-shot");
}

// ===== HMAC — RFC 4231 vectors =====
static void TestHmac() {
    std::printf("HMAC-SHA-2 (RFC 4231 vectors)\n");
    // Test case 1: key = 0x0b repeated 20 times, data = "Hi There".
    UltraCryptSecureBuffer key =
        BufferFromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    const char* data = "Hi There";
    std::vector<uint8_t> mac;

    Check(static_cast<bool>(UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256,
                                            key, data, 8, mac)),
          "HMAC-SHA-256 succeeds");
    CheckHex(mac,
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
             "RFC 4231 case 1, HMAC-SHA-256");

    Check(static_cast<bool>(UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA512,
                                            key, data, 8, mac)),
          "HMAC-SHA-512 succeeds");
    CheckHex(mac,
             "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
             "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
             "RFC 4231 case 1, HMAC-SHA-512");

    // Test case 2 uses a short key ("Jefe"), exercising the zero-pad path.
    std::string jefe = "Jefe";
    UltraCryptSecureBuffer shortKey = UltraCryptSecureBuffer(jefe.data(), jefe.size());
    const char* q = "what do ya want for nothing?";
    Check(static_cast<bool>(UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256,
                                            shortKey, q, std::strlen(q), mac)),
          "HMAC-SHA-256 with a short key succeeds");
    CheckHex(mac,
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
             "RFC 4231 case 2, HMAC-SHA-256");

    // Test case 3 uses a 131-byte key, exercising the hash-the-key path.
    UltraCryptSecureBuffer longKey(131);
    std::memset(longKey.Data(), 0xaa, longKey.GetSize());
    const char* t = "Test Using Larger Than Block-Size Key - Hash Key First";
    Check(static_cast<bool>(UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256,
                                            longKey, t, std::strlen(t), mac)),
          "HMAC-SHA-256 with an over-long key succeeds");
    CheckHex(mac,
             "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
             "RFC 4231 case 6, HMAC-SHA-256");
}

// ===== AEAD =====
static void TestAead() {
    std::printf("AEAD (XChaCha20-Poly1305)\n");
    UltraCryptSecureBuffer key;
    Check(static_cast<bool>(UltraCrypt_RandomSecureBuffer(32, key)),
          "random 32-byte key");

    const std::string message = "The document body that must stay confidential.";
    const std::vector<uint8_t> aad = {'U', 'C', 'D', 0x01, 0x01, 0x01, 0, 0};

    UltraCryptAeadParams seal;
    seal.associatedData = aad;
    Check(seal.algorithm == UltraCryptAeadAlgorithm::XChaCha20Poly1305,
          "XChaCha20-Poly1305 is the default algorithm");
    Check(seal.nonce.empty(), "nonce starts empty so the module generates it");

    std::vector<uint8_t> ciphertext;
    Check(static_cast<bool>(UltraCrypt_AeadSeal(key, seal, message.data(),
                                                message.size(), ciphertext)),
          "seal succeeds");
    Check(seal.nonce.size() == 24, "a 24-byte nonce was generated and returned");
    Check(ciphertext.size() == message.size() + 16, "ciphertext is plaintext + tag");
    Check(std::memcmp(ciphertext.data(), message.data(), message.size()) != 0,
          "ciphertext does not begin with the plaintext");

    // Two seals of the same plaintext must differ (fresh nonce each time).
    UltraCryptAeadParams seal2;
    seal2.associatedData = aad;
    std::vector<uint8_t> ciphertext2;
    UltraCrypt_AeadSeal(key, seal2, message.data(), message.size(), ciphertext2);
    Check(seal.nonce != seal2.nonce, "each seal generates a distinct nonce");
    Check(ciphertext != ciphertext2, "identical plaintext yields distinct ciphertext");

    // Round trip.
    UltraCryptAeadParams open = seal;
    UltraCryptSecureBuffer plain;
    Check(static_cast<bool>(UltraCrypt_AeadOpen(key, open, ciphertext.data(),
                                                ciphertext.size(), plain)),
          "open succeeds");
    Check(plain.GetSize() == message.size() &&
              std::memcmp(plain.Data(), message.data(), message.size()) == 0,
          "round-trip recovers the plaintext exactly");

    // ----- The four mandated negative cases -----
    auto expectAuthFailure = [&](UltraCryptAeadParams params,
                                 std::vector<uint8_t> data,
                                 const std::string& what) {
        UltraCryptSecureBuffer out;
        auto r = UltraCrypt_AeadOpen(key, params, data.data(), data.size(), out);
        Check(!r, what + " is rejected");
        Check(r.code == UltraCryptResultCode::AuthenticationFailed,
              what + " reports AuthenticationFailed");
        Check(out.IsEmpty(), what + " leaves the output empty");
    };

    std::vector<uint8_t> flipped = ciphertext;
    flipped[0] ^= 0x01;
    expectAuthFailure(seal, flipped, "a flipped ciphertext bit");

    UltraCryptAeadParams badAad = seal;
    badAad.associatedData[3] ^= 0x01;
    expectAuthFailure(badAad, ciphertext, "a flipped associated-data bit");

    UltraCryptAeadParams badNonce = seal;
    badNonce.nonce[0] ^= 0x01;
    expectAuthFailure(badNonce, ciphertext, "a wrong nonce");

    std::vector<uint8_t> truncated(ciphertext.begin(), ciphertext.end() - 1);
    expectAuthFailure(seal, truncated, "a truncated tag");

    // A wrong key must fail the same way, with no distinguishing detail.
    UltraCryptSecureBuffer wrongKey;
    UltraCrypt_RandomSecureBuffer(32, wrongKey);
    UltraCryptSecureBuffer out;
    auto wrong = UltraCrypt_AeadOpen(wrongKey, seal, ciphertext.data(),
                                     ciphertext.size(), out);
    Check(!wrong, "a wrong key is rejected");
    Check(wrong.code == UltraCryptResultCode::AuthenticationFailed,
          "a wrong key is indistinguishable from tampering");

    // Empty plaintext must still authenticate.
    UltraCryptAeadParams emptySeal;
    std::vector<uint8_t> emptyCt;
    Check(static_cast<bool>(UltraCrypt_AeadSeal(key, emptySeal, nullptr, 0, emptyCt)),
          "sealing an empty payload succeeds");
    Check(emptyCt.size() == 16, "an empty payload seals to just the tag");
    UltraCryptSecureBuffer emptyPlain;
    Check(static_cast<bool>(UltraCrypt_AeadOpen(key, emptySeal, emptyCt.data(),
                                                emptyCt.size(), emptyPlain)),
          "opening an empty payload succeeds");
    Check(emptyPlain.IsEmpty(), "an empty payload round-trips to empty");

    // A key of the wrong length must be refused, not silently padded.
    UltraCryptSecureBuffer shortKey(16);
    UltraCryptAeadParams p;
    std::vector<uint8_t> ct;
    auto ksr = UltraCrypt_AeadSeal(shortKey, p, message.data(), message.size(), ct);
    Check(!ksr && ksr.code == UltraCryptResultCode::InvalidKeySize,
          "a 16-byte key is refused with InvalidKeySize");
}

// ===== AES-CBC — FIPS 197 App. C and SP 800-38A F.2 vectors =====
static void TestAesCbc() {
    std::printf("AES-CBC (FIPS 197, SP 800-38A vectors)\n");
    const std::vector<uint8_t> zeroIv(16, 0);
    std::vector<uint8_t> ct;

    // FIPS 197 Appendix C: one block, so CBC with a zero IV is the bare cipher.
    struct Fips { const char* key; const char* ct; };
    const Fips fips[] = {
        {"000102030405060708090a0b0c0d0e0f",
         "69c4e0d86a7b0430d8cdb78070b4c55a"},
        {"000102030405060708090a0b0c0d0e0f1011121314151617",
         "dda97ca4864cdfe06eaf70a0ec0d7191"},
        {"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
         "8ea2b7ca516745bfeafc49904b496089"},
    };
    std::vector<uint8_t> block;
    UltraCrypt_FromHex("00112233445566778899aabbccddeeff", block);
    for (const Fips& v : fips) {
        const UltraCryptSecureBuffer key = BufferFromHex(v.key);
        const std::string bits = std::to_string(key.GetSize() * 8);
        UltraCryptCbcParams params;
        params.iv = zeroIv;
        params.padding = UltraCryptCbcPadding::NoPadding;
        Check(static_cast<bool>(UltraCrypt_AesCbcEncrypt(key, params, block.data(),
                                                         block.size(), ct)),
              "AES-" + bits + " encrypts one block");
        CheckHex(ct, v.ct, "FIPS 197 C AES-" + bits);
        UltraCryptSecureBuffer back;
        Check(UltraCrypt_AesCbcDecrypt(key, params, ct.data(), ct.size(), back) &&
                  back.GetSize() == 16 &&
                  std::memcmp(back.Data(), block.data(), 16) == 0,
              "FIPS 197 C AES-" + bits + " decrypts back");
    }

    // SP 800-38A F.2.1-F.2.6: four chained blocks.
    std::vector<uint8_t> plain, iv;
    UltraCrypt_FromHex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                       "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",
                       plain);
    UltraCrypt_FromHex("000102030405060708090a0b0c0d0e0f", iv);
    const Fips sp[] = {
        {"2b7e151628aed2a6abf7158809cf4f3c",
         "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
         "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7"},
        {"8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
         "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145a"
         "571b242012fb7ae07fa9baac3df102e008b0e27988598881d920a9e64f5615cd"},
        {"603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
         "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d"
         "39f23369a9d9bacfa530e26304231461b2eb05e2c39be9fcda6c19078c6a9d1b"},
    };
    for (const Fips& v : sp) {
        const UltraCryptSecureBuffer key = BufferFromHex(v.key);
        const std::string bits = std::to_string(key.GetSize() * 8);
        UltraCryptCbcParams params;
        params.iv = iv;
        params.padding = UltraCryptCbcPadding::NoPadding;
        Check(static_cast<bool>(UltraCrypt_AesCbcEncrypt(key, params, plain.data(),
                                                         plain.size(), ct)),
              "CBC-AES" + bits + " encrypts");
        CheckHex(ct, v.ct, "SP 800-38A CBC-AES" + bits + ".Encrypt");
        std::vector<uint8_t> expected;
        UltraCrypt_FromHex(v.ct, expected);
        UltraCryptSecureBuffer back;
        Check(UltraCrypt_AesCbcDecrypt(key, params, expected.data(), expected.size(), back) &&
                  back.GetSize() == plain.size() &&
                  std::memcmp(back.Data(), plain.data(), plain.size()) == 0,
              "SP 800-38A CBC-AES" + bits + ".Decrypt");
    }

    // PKCS #7: every length round-trips, and the output grows by 1-16 bytes.
    const UltraCryptSecureBuffer key =
        BufferFromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    bool lengthsOk = true, roundTripsOk = true;
    for (size_t n = 0; n <= 80; ++n) {
        std::vector<uint8_t> data(n);
        for (size_t i = 0; i < n; ++i) data[i] = static_cast<uint8_t>(i * 7 + n);
        UltraCryptCbcParams params;
        params.iv = iv;
        if (!UltraCrypt_AesCbcEncrypt(key, params, data.data(), n, ct)) { lengthsOk = false; continue; }
        lengthsOk = lengthsOk && ct.size() == (n / 16 + 1) * 16;
        UltraCryptSecureBuffer back;
        roundTripsOk = roundTripsOk &&
            UltraCrypt_AesCbcDecrypt(key, params, ct.data(), ct.size(), back) &&
            back.GetSize() == n && (n == 0 || std::memcmp(back.Data(), data.data(), n) == 0);
    }
    Check(lengthsOk, "PKCS #7 pads 0-80 bytes to the next whole block, always adding one");
    Check(roundTripsOk, "PKCS #7 round-trips 0-80 bytes");

    // A whole block of padding: the 16-byte plaintext gives 32 bytes whose
    // second block is the encryption of sixteen 0x10 bytes.
    {
        UltraCryptCbcParams params;
        params.iv = iv;
        Check(UltraCrypt_AesCbcEncrypt(key, params, plain.data(), 16, ct) && ct.size() == 32,
              "a 16-byte plaintext gets a full block of padding");
        CheckHex(std::vector<uint8_t>(ct.begin(), ct.begin() + 16),
                 "f58c4c04d6e5f1ba779eabfb5f7bfbd6", "the first block is unchanged by padding");
    }

    // An empty IV is generated, written back, and differs between calls.
    {
        UltraCryptCbcParams first, second;
        std::vector<uint8_t> ct2;
        Check(UltraCrypt_AesCbcEncrypt(key, first, plain.data(), plain.size(), ct) &&
                  UltraCrypt_AesCbcEncrypt(key, second, plain.data(), plain.size(), ct2),
              "encryption with no IV succeeds");
        Check(first.iv.size() == 16 && second.iv.size() == 16 && first.iv != second.iv,
              "a missing IV is generated fresh and written back");
        Check(ct != ct2, "two encryptions of one message under fresh IVs differ");
    }

    // Wrong padding: DecryptionFailed and nothing returned.
    {
        UltraCryptCbcParams params;
        params.iv = iv;
        UltraCrypt_AesCbcEncrypt(key, params, plain.data(), 20, ct);
        UltraCryptSecureBuffer back(4);
        const UltraCryptSecureBuffer wrongKey(32);
        auto r = UltraCrypt_AesCbcDecrypt(wrongKey, params, ct.data(), ct.size(), back);
        Check(!r && r.code == UltraCryptResultCode::DecryptionFailed,
              "a wrong key shows as DecryptionFailed");
        Check(back.IsEmpty(), "a failed decryption returns nothing");

        // Padding of sixteen 0x10 bytes is valid; one 0x11 (17) is not, nor is 0x00.
        std::vector<uint8_t> forged(16, 0x10);
        UltraCryptCbcParams raw;
        raw.iv = zeroIv;
        raw.padding = UltraCryptCbcPadding::NoPadding;
        std::vector<uint8_t> forgedCt;
        UltraCrypt_AesCbcEncrypt(key, raw, forged.data(), 16, forgedCt);
        UltraCryptCbcParams padded;
        padded.iv = zeroIv;
        Check(UltraCrypt_AesCbcDecrypt(key, padded, forgedCt.data(), 16, back) && back.IsEmpty(),
              "a block of sixteen 0x10 bytes is all padding");
        for (uint8_t badPad : {uint8_t(0x00), uint8_t(0x11), uint8_t(0xff)}) {
            forged.assign(16, 0x10);
            forged[15] = badPad;
            UltraCrypt_AesCbcEncrypt(key, raw, forged.data(), 16, forgedCt);
            r = UltraCrypt_AesCbcDecrypt(key, padded, forgedCt.data(), 16, back);
            Check(!r && r.code == UltraCryptResultCode::DecryptionFailed && back.IsEmpty(),
                  "a pad byte of " + std::to_string(badPad) + " is refused");
        }
        forged.assign(16, 0x04);
        forged[12] = 0x05;   // the first of four pad bytes is off
        UltraCrypt_AesCbcEncrypt(key, raw, forged.data(), 16, forgedCt);
        r = UltraCrypt_AesCbcDecrypt(key, padded, forgedCt.data(), 16, back);
        Check(!r && r.code == UltraCryptResultCode::DecryptionFailed,
              "padding with one byte off is refused");
    }

    // Argument checks.
    {
        UltraCryptCbcParams params;
        params.iv = iv;
        UltraCryptSecureBuffer back;
        const UltraCryptSecureBuffer shortKey(20);
        Check(UltraCrypt_AesCbcEncrypt(shortKey, params, plain.data(), 16, ct).code ==
                  UltraCryptResultCode::InvalidKeySize,
              "a 20-byte key is InvalidKeySize");
        params.iv.assign(12, 0);
        Check(UltraCrypt_AesCbcEncrypt(key, params, plain.data(), 16, ct).code ==
                  UltraCryptResultCode::InvalidNonceSize,
              "a 12-byte IV is InvalidNonceSize");
        Check(UltraCrypt_AesCbcDecrypt(key, params, plain.data(), 16, back).code ==
                  UltraCryptResultCode::InvalidNonceSize,
              "decryption checks the IV size too");
        params.iv = iv;
        Check(UltraCrypt_AesCbcDecrypt(key, params, plain.data(), 17, back).code ==
                  UltraCryptResultCode::InvalidArgument,
              "a ciphertext of 17 bytes is InvalidArgument");
        Check(UltraCrypt_AesCbcDecrypt(key, params, nullptr, 0, back).code ==
                  UltraCryptResultCode::InvalidArgument,
              "an empty ciphertext cannot hold PKCS #7 padding");
        params.padding = UltraCryptCbcPadding::NoPadding;
        Check(UltraCrypt_AesCbcEncrypt(key, params, plain.data(), 15, ct).code ==
                  UltraCryptResultCode::InvalidArgument,
              "without padding a partial block is InvalidArgument");
        Check(UltraCrypt_AesCbcEncrypt(key, params, nullptr, 0, ct) && ct.empty(),
              "without padding an empty plaintext encrypts to nothing");
    }
}

// ===== KDF =====
static void TestKdf() {
    std::printf("Argon2id key derivation\n");
    // Deliberately cheap parameters: these tests exercise behaviour, not cost.
    auto makeParams = []() {
        UltraCryptKdfParams p = UltraCrypt_RecommendedKdfParams();
        p.iterations = 2;
        p.memoryKiB  = 8 * 1024;   // 8 MiB keeps the suite fast
        return p;
    };

    auto recommended = UltraCrypt_RecommendedKdfParams();
    Check(recommended.iterations == 3 && recommended.memoryKiB == 64 * 1024 &&
              recommended.parallelism == 1,
          "recommended parameters are 3 passes / 64 MiB / parallelism 1");

    std::string pw = "correct horse battery staple";
    UltraCryptSecureBuffer password(pw.data(), pw.size());

    UltraCryptKdfParams p1 = makeParams();
    UltraCryptSecureBuffer k1;
    Check(static_cast<bool>(UltraCrypt_DeriveKeyFromPassword(password, p1, k1)),
          "derivation succeeds");
    Check(p1.salt.size() == UltraCrypt_GetKdfSaltSize(),
          "a salt was generated and written back to the caller's params");
    Check(k1.GetSize() == 32, "derived key is 32 bytes");

    // Same password + same salt + same cost => same key.
    UltraCryptKdfParams p2 = makeParams();
    p2.salt = p1.salt;
    UltraCryptSecureBuffer k2;
    UltraCrypt_DeriveKeyFromPassword(password, p2, k2);
    Check(UltraCrypt_ConstantTimeEquals(k1.Data(), k1.GetSize(),
                                        k2.Data(), k2.GetSize()),
          "derivation is deterministic for identical inputs");

    // A different salt must give a different key.
    UltraCryptKdfParams p3 = makeParams();
    UltraCryptSecureBuffer k3;
    UltraCrypt_DeriveKeyFromPassword(password, p3, k3);
    Check(p3.salt != p1.salt, "a fresh derivation generates a fresh salt");
    Check(!UltraCrypt_ConstantTimeEquals(k1.Data(), k1.GetSize(),
                                         k3.Data(), k3.GetSize()),
          "a different salt yields a different key");

    // A different password must give a different key.
    std::string other = "correct horse battery stapl3";
    UltraCryptSecureBuffer otherPw(other.data(), other.size());
    UltraCryptKdfParams p4 = makeParams();
    p4.salt = p1.salt;
    UltraCryptSecureBuffer k4;
    UltraCrypt_DeriveKeyFromPassword(otherPw, p4, k4);
    Check(!UltraCrypt_ConstantTimeEquals(k1.Data(), k1.GetSize(),
                                         k4.Data(), k4.GetSize()),
          "a different password yields a different key");

    // Cost parameters must be honoured, not silently normalised: a different
    // iteration count must produce a different key from the same salt.
    UltraCryptKdfParams p5 = makeParams();
    p5.salt = p1.salt;
    p5.iterations = 3;
    UltraCryptSecureBuffer k5;
    UltraCrypt_DeriveKeyFromPassword(password, p5, k5);
    Check(!UltraCrypt_ConstantTimeEquals(k1.Data(), k1.GetSize(),
                                         k5.Data(), k5.GetSize()),
          "a different iteration count yields a different key");

    // parallelism != 1 must be refused rather than silently ignored, so that a
    // stored parameter set always describes how the key was really derived.
    UltraCryptKdfParams bad = makeParams();
    bad.parallelism = 4;
    UltraCryptSecureBuffer unused;
    auto br = UltraCrypt_DeriveKeyFromPassword(password, bad, unused);
    Check(!br && br.code == UltraCryptResultCode::NotSupported,
          "parallelism != 1 is refused rather than silently ignored");

    // Zero cost parameters must be refused.
    UltraCryptKdfParams zero = makeParams();
    zero.iterations = 0;
    auto zr = UltraCrypt_DeriveKeyFromPassword(password, zero, unused);
    Check(!zr && zr.code == UltraCryptResultCode::InvalidArgument,
          "zero iterations is refused");
}

// ===== Ed25519 — RFC 8032 §7.1 vectors =====
static void TestSignatures() {
    std::printf("Ed25519 (RFC 8032 vectors)\n");
    const auto alg = UltraCryptSignatureAlgorithm::Ed25519;
    Check(UltraCrypt_IsSignatureAvailable(alg), "Ed25519 is available");
    Check(UltraCrypt_GetSignaturePublicKeySize(alg) == 32 &&
              UltraCrypt_GetSignatureSecretKeySize(alg) == 64 &&
              UltraCrypt_GetSignatureSeedSize(alg) == 32 &&
              UltraCrypt_GetSignatureSize(alg) == 64,
          "Ed25519 sizes are 32 / 64 / 32 / 64");

    struct Vector {
        const char* seed;
        const char* publicKey;
        const char* message;
        const char* signature;
    };
    // RFC 8032 §7.1: TEST 1 (empty message), TEST 2 (one byte), TEST 3 (two
    // bytes), TEST SHA(abc) (the 64-byte SHA-512 of "abc" as the message).
    static const Vector kVectors[] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
         "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
         "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
         "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
        {"833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
         "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
         "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"},
    };

    int index = 0;
    for (const Vector& v : kVectors) {
        const std::string what = "RFC 8032 vector " + std::to_string(++index);
        UltraCryptSecureBuffer seed = BufferFromHex(v.seed);
        std::vector<uint8_t> message, expectedSig;
        UltraCrypt_FromHex(v.message, message);
        UltraCrypt_FromHex(v.signature, expectedSig);

        std::vector<uint8_t> pk;
        UltraCryptSecureBuffer sk;
        Check(static_cast<bool>(UltraCrypt_SignatureKeyPairFromSeed(alg, seed, pk, sk)),
              what + ": key pair from seed");
        CheckHex(pk, v.publicKey, what + ": public key");
        Check(sk.GetSize() == 64, what + ": secret key is 64 bytes");

        std::vector<uint8_t> fromSecret;
        Check(static_cast<bool>(UltraCrypt_GetSignaturePublicKey(alg, sk, fromSecret)) &&
                  fromSecret == pk,
              what + ": the secret key yields the same public key");

        std::vector<uint8_t> sig;
        Check(static_cast<bool>(UltraCrypt_Sign(alg, sk, message.data(),
                                                message.size(), sig)),
              what + ": sign");
        CheckHex(sig, v.signature, what + ": signature");

        Check(static_cast<bool>(UltraCrypt_Verify(alg, pk, message.data(),
                                                  message.size(), expectedSig)),
              what + ": verify");
    }

    // Negative cases, all on vector 3 (a two-byte message).
    UltraCryptSecureBuffer seed = BufferFromHex(kVectors[2].seed);
    std::vector<uint8_t> pk, message, sig;
    UltraCryptSecureBuffer sk;
    UltraCrypt_SignatureKeyPairFromSeed(alg, seed, pk, sk);
    UltraCrypt_FromHex(kVectors[2].message, message);
    UltraCrypt_FromHex(kVectors[2].signature, sig);

    auto expectInvalid = [&](const std::vector<uint8_t>& key,
                             const std::vector<uint8_t>& msg,
                             const std::vector<uint8_t>& s,
                             const std::string& what) {
        auto r = UltraCrypt_Verify(alg, key, msg.data(), msg.size(), s);
        Check(!r, what + " is rejected");
        Check(r.code == UltraCryptResultCode::SignatureInvalid,
              what + " reports SignatureInvalid");
    };

    std::vector<uint8_t> flippedMsg = message;
    flippedMsg[0] ^= 0x01;
    expectInvalid(pk, flippedMsg, sig, "a flipped message bit");

    std::vector<uint8_t> flippedSig = sig;
    flippedSig[10] ^= 0x01;
    expectInvalid(pk, message, flippedSig, "a flipped signature bit");

    std::vector<uint8_t> otherPk;
    UltraCryptSecureBuffer otherSk;
    Check(static_cast<bool>(UltraCrypt_GenerateSignatureKeyPair(alg, otherPk, otherSk)),
          "a fresh key pair is generated");
    Check(otherPk != pk, "a fresh key pair differs from the vector's");
    expectInvalid(otherPk, message, sig, "a wrong public key");

    std::vector<uint8_t> empty;
    expectInvalid(pk, empty, sig, "a signature over a different message");

    // Wrong lengths are refused before anything is checked.
    std::vector<uint8_t> shortKey(pk.begin(), pk.end() - 1);
    auto ks = UltraCrypt_Verify(alg, shortKey, message.data(), message.size(), sig);
    Check(!ks && ks.code == UltraCryptResultCode::InvalidKeySize,
          "a 31-byte public key is refused with InvalidKeySize");
    std::vector<uint8_t> shortSig(sig.begin(), sig.end() - 1);
    auto ss = UltraCrypt_Verify(alg, pk, message.data(), message.size(), shortSig);
    Check(!ss && ss.code == UltraCryptResultCode::InvalidArgument,
          "a 63-byte signature is refused with InvalidArgument");
    UltraCryptSecureBuffer shortSecret(32);
    std::vector<uint8_t> unused;
    auto sk32 = UltraCrypt_Sign(alg, shortSecret, message.data(), message.size(), unused);
    Check(!sk32 && sk32.code == UltraCryptResultCode::InvalidKeySize,
          "a 32-byte secret key is refused with InvalidKeySize");
    UltraCryptSecureBuffer shortSeed(16);
    auto sd = UltraCrypt_SignatureKeyPairFromSeed(alg, shortSeed, pk, sk);
    Check(!sd && sd.code == UltraCryptResultCode::InvalidKeySize,
          "a 16-byte seed is refused with InvalidKeySize");

    // Signing is deterministic: the same key and message give the same bytes.
    std::vector<uint8_t> again;
    UltraCrypt_Sign(alg, sk, message.data(), message.size(), again);
    Check(again == sig, "signing is deterministic");

    // A fresh pair round-trips a longer message and the empty message.
    const std::string text(100000, 'x');
    std::vector<uint8_t> longSig;
    Check(static_cast<bool>(UltraCrypt_Sign(alg, otherSk, text.data(), text.size(), longSig)) &&
              static_cast<bool>(UltraCrypt_Verify(alg, otherPk, text.data(), text.size(), longSig)),
          "a fresh pair signs and verifies a 100 kB message");
    std::vector<uint8_t> emptySig;
    Check(static_cast<bool>(UltraCrypt_Sign(alg, otherSk, nullptr, 0, emptySig)) &&
              static_cast<bool>(UltraCrypt_Verify(alg, otherPk, nullptr, 0, emptySig)),
          "a fresh pair signs and verifies the empty message");

    // File variants: the signature over a file equals the one over its bytes.
    const std::string path = "ultracrypt-sign-test.bin";
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        std::vector<uint8_t> fileSig;
        Check(static_cast<bool>(UltraCrypt_SignFile(alg, otherSk, path, fileSig)) &&
                  fileSig == longSig,
              "SignFile signs the file's bytes");
        Check(static_cast<bool>(UltraCrypt_VerifyFile(alg, otherPk, path, longSig)),
              "VerifyFile accepts the file's signature");
        if (std::FILE* g = std::fopen(path.c_str(), "ab")) {
            std::fputc('!', g);
            std::fclose(g);
        }
        auto vf = UltraCrypt_VerifyFile(alg, otherPk, path, longSig);
        Check(!vf && vf.code == UltraCryptResultCode::SignatureInvalid,
              "VerifyFile rejects a file with a byte appended");
        std::remove(path.c_str());
    } else {
        Check(false, "could not create the signature test file");
    }
    auto missing = UltraCrypt_VerifyFile(alg, otherPk, "no-such-file.bin", longSig);
    Check(!missing && missing.code == UltraCryptResultCode::InvalidArgument,
          "VerifyFile reports a missing file as InvalidArgument");
}

// ===== Secure buffer and helpers =====
static void TestSecureBuffer() {
    std::printf("Secure buffer, random and helpers\n");
    UltraCryptSecureBuffer a(8);
    std::memset(a.Data(), 0xAB, 8);
    UltraCryptSecureBuffer b = std::move(a);
    Check(a.IsEmpty(), "a moved-from buffer is empty");
    Check(b.GetSize() == 8 && b.Data()[0] == 0xAB, "the moved-to buffer owns the bytes");

    UltraCryptSecureBuffer c = b.Clone();
    Check(UltraCrypt_ConstantTimeEquals(b.Data(), b.GetSize(), c.Data(), c.GetSize()),
          "Clone copies the contents");
    c.Clear();
    Check(c.IsEmpty(), "Clear releases the buffer");

    std::string secret = "hunter2";
    UltraCryptSecureBuffer adopted = UltraCryptSecureBuffer::AdoptString(secret);
    Check(adopted.GetSize() == 7, "AdoptString takes the bytes");
    Check(secret.empty(), "AdoptString clears the source string");

    Check(!UltraCrypt_ConstantTimeEquals("abc", 3, "abcd", 4),
          "different lengths never compare equal");
    Check(UltraCrypt_ConstantTimeEquals("abc", 3, "abc", 3),
          "identical buffers compare equal");
    Check(!UltraCrypt_ConstantTimeEquals("abc", 3, "abd", 3),
          "differing buffers compare unequal");

    std::vector<uint8_t> r1, r2;
    Check(static_cast<bool>(UltraCrypt_RandomBytes(r1, 32)), "random bytes succeed");
    UltraCrypt_RandomBytes(r2, 32);
    Check(r1 != r2, "successive random draws differ");

    std::string uuid;
    Check(static_cast<bool>(UltraCrypt_GenerateUuidV4(uuid)), "UUID generation succeeds");
    Check(uuid.size() == 36, "UUID is 36 characters");
    Check(uuid[14] == '4', "UUID reports version 4");
    Check(uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b',
          "UUID reports the RFC 4122 variant");

    std::vector<uint8_t> roundTrip;
    Check(UltraCrypt_FromHex("00ff10", roundTrip) && roundTrip.size() == 3 &&
              roundTrip[1] == 0xFF,
          "hex decoding works");
    Check(UltraCrypt_ToHex(roundTrip) == "00ff10", "hex encoding round-trips");
    Check(!UltraCrypt_FromHex("0g", roundTrip), "invalid hex is rejected");
    Check(!UltraCrypt_FromHex("abc", roundTrip), "odd-length hex is rejected");
}


// ===== SHA-1 and HMAC-SHA-1 (FIPS 180-4 / RFC 2202) =====
static void TestSha1() {
    std::printf("SHA-1 and HMAC-SHA-1 (legacy, TOTP path)\n");
    std::vector<uint8_t> digest;

    // Plain SHA-1 must be refused unless legacy use is opted into.
    auto refused = UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA1, "abc", 3, digest);
    Check(!refused && refused.code == UltraCryptResultCode::NotSupported,
          "plain SHA-1 is refused without allowLegacy");

    UltraCryptHashOptions legacy;
    legacy.allowLegacy = true;
    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA1,
                                            "abc", 3, digest, legacy)),
          "plain SHA-1 succeeds with allowLegacy");
    CheckHex(digest, "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1(\"abc\")");

    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA1,
                                            nullptr, 0, digest, legacy)),
          "SHA-1 of the empty input succeeds");
    CheckHex(digest, "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1(\"\")");

    // A multi-block message exercises the padding and length-encoding paths.
    const std::string long56 =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Check(static_cast<bool>(UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA1,
                                            long56.data(), long56.size(),
                                            digest, legacy)),
          "SHA-1 of a 56-byte message succeeds");
    CheckHex(digest, "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
             "SHA-1 of the 56-byte FIPS vector");

    std::string million(1000000, 'a');
    UltraCryptHasher hasher(UltraCryptHashAlgorithm::SHA1);
    hasher.Update(million.data(), million.size());
    CheckHex(hasher.Final(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
             "streaming SHA-1 of one million 'a'");

    // HMAC-SHA-1 needs no opt-in: RFC 2202 case 1.
    UltraCryptSecureBuffer key =
        BufferFromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    std::vector<uint8_t> mac;
    Check(static_cast<bool>(UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA1,
                                            key, "Hi There", 8, mac)),
          "HMAC-SHA-1 succeeds without allowLegacy");
    CheckHex(mac, "b617318655057264e28bc0b6fb378c8ef146be00",
             "RFC 2202 case 1, HMAC-SHA-1");

    std::string jefe = "Jefe";
    UltraCryptSecureBuffer shortKey(jefe.data(), jefe.size());
    const char* q = "what do ya want for nothing?";
    UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA1, shortKey, q, std::strlen(q), mac);
    CheckHex(mac, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79",
             "RFC 2202 case 2, HMAC-SHA-1");

    UltraCryptSecureBuffer longKey(80);
    std::memset(longKey.Data(), 0xaa, longKey.GetSize());
    const char* t5 = "Test Using Larger Than Block-Size Key - Hash Key First";
    UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA1, longKey, t5, std::strlen(t5), mac);
    CheckHex(mac, "aa4ae5e15272d00e95705637ce8a3b55ed402112",
             "RFC 2202 case 6, HMAC-SHA-1 (over-long key)");
}

// ===== HKDF (RFC 5869) =====
static void TestHkdf() {
    std::printf("HKDF (RFC 5869 vectors)\n");
    // Test Case 1: SHA-256, 22-byte IKM, 13-byte salt, 10-byte info, L = 42.
    UltraCryptSecureBuffer ikm =
        BufferFromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    std::vector<uint8_t> salt, info;
    UltraCrypt_FromHex("000102030405060708090a0b0c", salt);
    UltraCrypt_FromHex("f0f1f2f3f4f5f6f7f8f9", info);

    UltraCryptSecureBuffer okm;
    Check(static_cast<bool>(UltraCrypt_DeriveKeyHkdf(
              UltraCryptHashAlgorithm::SHA256, ikm, salt, info, 42, okm)),
          "HKDF succeeds");
    std::vector<uint8_t> okmBytes(okm.Data(), okm.Data() + okm.GetSize());
    CheckHex(okmBytes,
             "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
             "34007208d5b887185865",
             "RFC 5869 Test Case 1 (SHA-256)");

    // Test Case 3: empty salt and empty info.
    UltraCryptSecureBuffer okm3;
    Check(static_cast<bool>(UltraCrypt_DeriveKeyHkdf(
              UltraCryptHashAlgorithm::SHA256, ikm, {}, {}, 42, okm3)),
          "HKDF with empty salt and info succeeds");
    std::vector<uint8_t> okm3Bytes(okm3.Data(), okm3.Data() + okm3.GetSize());
    CheckHex(okm3Bytes,
             "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
             "9d201395faa4b61a96c8",
             "RFC 5869 Test Case 3 (empty salt/info)");

    // Length limits and rejections.
    UltraCryptSecureBuffer tooLong;
    auto over = UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256,
                                         ikm, salt, info, 255 * 32 + 1, tooLong);
    Check(!over && over.code == UltraCryptResultCode::InvalidArgument,
          "HKDF refuses more than 255 * HashLen bytes");

    UltraCryptSecureBuffer sha1Okm;
    auto sha1Refused = UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA1,
                                                ikm, salt, info, 32, sha1Okm);
    Check(!sha1Refused && sha1Refused.code == UltraCryptResultCode::NotSupported,
          "HKDF-SHA-1 is refused");

    // Different info must give different output from the same IKM.
    UltraCryptSecureBuffer a, b;
    std::vector<uint8_t> infoA{'a'}, infoB{'b'};
    UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, ikm, salt, infoA, 32, a);
    UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, ikm, salt, infoB, 32, b);
    Check(!UltraCrypt_ConstantTimeEquals(a.Data(), a.GetSize(), b.Data(), b.GetSize()),
          "different info yields different key material");
}

// ===== AES-CTR — SP 800-38A F.5 vectors =====
static void TestAesCtr() {
    std::printf("AES-CTR (SP 800-38A F.5 vectors)\n");
    std::vector<uint8_t> plain, counter;
    UltraCrypt_FromHex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                       "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",
                       plain);
    UltraCrypt_FromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", counter);
    struct Vector { const char* key; const char* ct; };
    const Vector sp[] = {
        {"2b7e151628aed2a6abf7158809cf4f3c",
         "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
         "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee"},
        {"603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
         "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5"
         "2b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6"},
    };
    for (const Vector& v : sp) {
        const UltraCryptSecureBuffer key = BufferFromHex(v.key);
        const std::string bits = std::to_string(key.GetSize() * 8);
        UltraCryptCtrParams params;
        params.iv = counter;
        std::vector<uint8_t> ct;
        Check(static_cast<bool>(UltraCrypt_AesCtrEncrypt(key, params, plain.data(), plain.size(), ct)),
              "CTR-AES" + bits + " encrypts");
        CheckHex(ct, v.ct, "SP 800-38A F.5 CTR-AES" + bits);
        UltraCryptSecureBuffer back;
        Check(UltraCrypt_AesCtrDecrypt(key, params, ct.data(), ct.size(), back) &&
                  back.GetSize() == plain.size() && std::memcmp(back.Data(), plain.data(), plain.size()) == 0,
              "CTR-AES" + bits + " decrypts back");
        // Any length, not just whole blocks: a prefix of the key stream.
        std::vector<uint8_t> part;
        Check(UltraCrypt_AesCtrEncrypt(key, params, plain.data(), 37, part) && part.size() == 37 &&
                  std::memcmp(part.data(), ct.data(), 37) == 0,
              "CTR-AES" + bits + " encrypts a 37-byte message as the prefix");
    }

    // The counter carries across all 128 bits: from ff..ff on to 00..00.
    {
        const UltraCryptSecureBuffer key = BufferFromHex("2b7e151628aed2a6abf7158809cf4f3c");
        UltraCryptCtrParams params;
        params.iv.assign(16, 0xff);
        const std::vector<uint8_t> zeros(80, 0);
        std::vector<uint8_t> ct;
        UltraCrypt_AesCtrEncrypt(key, params, zeros.data(), zeros.size(), ct);
        CheckHex(ct,
                 "8af2860142f786f409307c1a3f7eaaac7df76b0c1ab899b33e42f047b91b546f"
                 "57127d4034b1bebfaef466b9c7726fc6973f2ef34879e2027f1734303ff21f89"
                 "469c7fcb75d5d9a1b418cb997b09a185",
                 "CTR counter wraps from ff..ff to 00..00");
    }

    // A fresh random IV when none is given, and the refusals.
    {
        const UltraCryptSecureBuffer key = BufferFromHex("2b7e151628aed2a6abf7158809cf4f3c");
        UltraCryptCtrParams first, second;
        std::vector<uint8_t> a, b;
        Check(UltraCrypt_AesCtrEncrypt(key, first, plain.data(), 16, a) &&
                  UltraCrypt_AesCtrEncrypt(key, second, plain.data(), 16, b) && first.iv.size() == 16 &&
                  first.iv != second.iv && a != b,
              "CTR draws a fresh 16-byte IV each time none is given");
        UltraCryptCtrParams bad;
        bad.iv.assign(12, 0);
        std::vector<uint8_t> ct;
        Check(UltraCrypt_AesCtrEncrypt(key, bad, plain.data(), 16, ct).code == UltraCryptResultCode::InvalidNonceSize,
              "CTR refuses a 12-byte IV");
        UltraCryptSecureBuffer back;
        Check(UltraCrypt_AesCtrDecrypt(key, bad, plain.data(), 16, back).code ==
                  UltraCryptResultCode::InvalidNonceSize && back.IsEmpty(),
              "CTR decrypt refuses a 12-byte IV, output empty");
        const UltraCryptSecureBuffer shortKey = BufferFromHex("2b7e151628aed2a6");
        Check(UltraCrypt_AesCtrEncrypt(shortKey, first, plain.data(), 16, ct).code ==
                  UltraCryptResultCode::InvalidKeySize,
              "CTR refuses an 8-byte key");
        Check(UltraCrypt_AesCtrEncrypt(key, first, nullptr, 0, ct) && ct.empty(), "CTR encrypts nothing to nothing");
    }
}

// ===== PBKDF2 — RFC 7914 §11 (SHA-256) and the SHA-512 values =====
static void TestPbkdf2() {
    std::printf("PBKDF2 (RFC 7914 vectors, SHA-512)\n");
    auto derive = [](UltraCryptHashAlgorithm algorithm, const std::string& password, const std::string& salt,
                     uint32_t iterations, size_t length, std::vector<uint8_t>& out) {
        const UltraCryptSecureBuffer pw(password.data(), password.size());
        UltraCryptSecureBuffer key;
        auto r = UltraCrypt_DeriveKeyPbkdf2(algorithm, pw, std::vector<uint8_t>(salt.begin(), salt.end()), iterations,
                                            length, key);
        out.assign(key.Data(), key.Data() + key.GetSize());
        return r;
    };
    std::vector<uint8_t> out;
    Check(static_cast<bool>(derive(UltraCryptHashAlgorithm::SHA256, "passwd", "salt", 1, 64, out)),
          "PBKDF2-SHA-256 succeeds");
    CheckHex(out,
             "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
             "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
             "RFC 7914 PBKDF2-HMAC-SHA-256, c = 1");
    derive(UltraCryptHashAlgorithm::SHA256, "Password", "NaCl", 80000, 64, out);
    CheckHex(out,
             "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
             "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
             "RFC 7914 PBKDF2-HMAC-SHA-256, c = 80000");
    derive(UltraCryptHashAlgorithm::SHA512, "password", "salt", 1, 64, out);
    CheckHex(out,
             "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252"
             "c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce",
             "PBKDF2-HMAC-SHA-512, c = 1");
    derive(UltraCryptHashAlgorithm::SHA512, "password", "salt", 2, 64, out);
    CheckHex(out,
             "e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53c"
             "f76cab2868a39b9f7840edce4fef5a82be67335c77a6068e04112754f27ccf4e",
             "PBKDF2-HMAC-SHA-512, c = 2");
    // Past one block (T_2), with an empty salt.
    derive(UltraCryptHashAlgorithm::SHA512, "pass", "", 3, 70, out);
    CheckHex(out,
             "4a3c00e64ca6fe13a0c8e1b595603603e94a1fbf2539e7c650708ac19a39d911"
             "804e73d74ce3990cea73023e14adae7fba5bd2d1dccfd4c4af055cc9407e28d6"
             "a6a1f5119ca4",
             "PBKDF2-HMAC-SHA-512, 70 bytes from an empty salt");

    Check(derive(UltraCryptHashAlgorithm::SHA1, "p", "s", 1, 20, out).code == UltraCryptResultCode::NotSupported &&
              out.empty(),
          "PBKDF2-SHA-1 is refused");
    Check(derive(UltraCryptHashAlgorithm::SHA256, "p", "s", 0, 32, out).code ==
              UltraCryptResultCode::InvalidArgument,
          "PBKDF2 refuses zero iterations");
    Check(derive(UltraCryptHashAlgorithm::SHA256, "p", "s", 1, 0, out).code ==
              UltraCryptResultCode::InvalidArgument,
          "PBKDF2 refuses a zero output length");
}

// ===== Base32 / Base64 (RFC 4648) =====
// The codecs live in UltraCanvasUtils (UltraCanvasTextUtils.h). UltraCrypt
// keeps one wrapper, UltraCrypt_Base32Decode, which decodes into a zeroizing
// buffer by calling them. The RFC vectors are run through both, so a change to
// either the codec or the wrapper is caught here.
static void TestEncodings() {
    std::printf("Base32 / Base64 (RFC 4648 vectors)\n");
    const char* inputs[]  = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* base64[]  = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    const char* base32[]  = {"", "MY======", "MZXQ====", "MZXW6===",
                             "MZXW6YQ=", "MZXW6YTB", "MZXW6YTBOI======"};

    for (int i = 0; i < 7; ++i) {
        const std::string in = inputs[i];
        const std::vector<uint8_t> bytes(in.begin(), in.end());

        ++g_checks;
        const std::string got64 = UltraCanvas::Base64Encode(bytes, /*wrap=*/false);
        if (got64 != base64[i]) {
            ++g_failures;
            std::printf("  FAIL: base64(\"%s\") expected %s got %s\n",
                        in.c_str(), base64[i], got64.c_str());
        }
        ++g_checks;
        const std::string got32 = UltraCanvas::Base32Encode(bytes);
        if (got32 != base32[i]) {
            ++g_failures;
            std::printf("  FAIL: base32(\"%s\") expected %s got %s\n",
                        in.c_str(), base32[i], got32.c_str());
        }

        const std::vector<uint8_t> back64 = UltraCanvas::Base64Decode(base64[i]);
        Check(back64 == bytes, "base64 round-trips \"" + in + "\"");

        std::vector<uint8_t> back32;
        Check(UltraCanvas::Base32Decode(base32[i], back32) && back32 == bytes,
              "base32 round-trips \"" + in + "\"");

        UltraCryptSecureBuffer secure;
        UltraCrypt_Base32Decode(base32[i], secure);
        // Guard the compare: memcmp with a null pointer is undefined even for
        // a zero length, and an empty decode legitimately yields no buffer.
        Check(secure.GetSize() == in.size() &&
                  (in.empty() ||
                   std::memcmp(secure.Data(), in.data(), in.size()) == 0),
              "the secure-buffer wrapper round-trips \"" + in + "\"");
    }

    // Seeds are commonly shown lower-case, in space-separated groups, unpadded.
    UltraCryptSecureBuffer seed;
    Check(static_cast<bool>(UltraCrypt_Base32Decode("mzxw 6ytb oi", seed)),
          "base32 accepts lower case, spaces and no padding");
    Check(seed.GetSize() == 6 && std::memcmp(seed.Data(), "foobar", 6) == 0,
          "a loosely typed seed decodes correctly");

    // Strictness, through both entry points.
    std::vector<uint8_t> junk;
    Check(!UltraCanvas::Base32Decode("MZXW6YTB1", junk),
          "the codec rejects a character outside the alphabet");
    Check(!UltraCanvas::Base32Decode("MZXW6===Y", junk),
          "the codec rejects data after padding");
    UltraCryptSecureBuffer bad;
    auto r = UltraCrypt_Base32Decode("MZXW6YTB1", bad);
    Check(!r && r.code == UltraCryptResultCode::InvalidArgument,
          "the wrapper reports a rejected decode as InvalidArgument");
    Check(bad.IsEmpty(), "a rejected secure decode yields nothing");

    // Base64Decode is lenient by long-standing contract (it skips characters it
    // does not recognise), so there is deliberately no rejection case for it.
}

// ===== Key agreement — RFC 7748 §6.1 vector, Ed25519 conversion =====
static void TestKeyExchange() {
    std::printf("Key agreement (X25519)\n");
    Check(UltraCrypt_GetKeyExchangeKeySize() == 32, "X25519 keys are 32 bytes");

    const UltraCryptSecureBuffer aliceSk =
        BufferFromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const UltraCryptSecureBuffer bobSk =
        BufferFromHex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    std::vector<uint8_t> alicePk, bobPk;
    UltraCrypt_FromHex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", alicePk);
    UltraCrypt_FromHex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bobPk);

    UltraCryptSecureBuffer ab, ba;
    Check(static_cast<bool>(UltraCrypt_KeyExchange(aliceSk, bobPk, ab)), "Alice derives the secret");
    Check(static_cast<bool>(UltraCrypt_KeyExchange(bobSk, alicePk, ba)), "Bob derives the secret");
    const std::string expected = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
    CheckHex(std::vector<uint8_t>(ab.Data(), ab.Data() + ab.GetSize()), expected, "RFC 7748 shared secret (Alice)");
    CheckHex(std::vector<uint8_t>(ba.Data(), ba.Data() + ba.GetSize()), expected, "RFC 7748 shared secret (Bob)");

    // A low-order point would give an all-zero secret; it is refused.
    std::vector<uint8_t> zeroPoint(32, 0);
    UltraCryptSecureBuffer unused;
    auto low = UltraCrypt_KeyExchange(aliceSk, zeroPoint, unused);
    Check(!low && low.code == UltraCryptResultCode::InvalidArgument, "a low-order public key is refused");
    std::vector<uint8_t> shortPk(31, 9);
    auto shortResult = UltraCrypt_KeyExchange(aliceSk, shortPk, unused);
    Check(!shortResult && shortResult.code == UltraCryptResultCode::InvalidKeySize,
          "a 31-byte public key is refused with InvalidKeySize");

    // One Ed25519 identity per side: the converted pairs agree.
    const auto alg = UltraCryptSignatureAlgorithm::Ed25519;
    std::vector<uint8_t> edPkA, edPkB, xPkA, xPkB;
    UltraCryptSecureBuffer edSkA, edSkB, xSkA, xSkB;
    Check(UltraCrypt_GenerateSignatureKeyPair(alg, edPkA, edSkA) &&
          UltraCrypt_GenerateSignatureKeyPair(alg, edPkB, edSkB), "two identities are generated");
    Check(UltraCrypt_SignaturePublicKeyToKeyExchange(alg, edPkA, xPkA) &&
          UltraCrypt_SignaturePublicKeyToKeyExchange(alg, edPkB, xPkB), "public keys convert");
    Check(UltraCrypt_SignatureSecretKeyToKeyExchange(alg, edSkA, xSkA) &&
          UltraCrypt_SignatureSecretKeyToKeyExchange(alg, edSkB, xSkB), "secret keys convert");
    UltraCryptSecureBuffer sAB, sBA;
    Check(UltraCrypt_KeyExchange(xSkA, xPkB, sAB) && UltraCrypt_KeyExchange(xSkB, xPkA, sBA),
          "converted identities agree on a key");
    Check(sAB.GetSize() == 32 && UltraCrypt_ConstantTimeEquals(sAB.Data(), sAB.GetSize(), sBA.Data(), sBA.GetSize()),
          "both sides hold the same secret");

    std::vector<uint8_t> ephPk;
    UltraCryptSecureBuffer ephSk, sEph1, sEph2;
    Check(static_cast<bool>(UltraCrypt_GenerateKeyExchangeKeyPair(ephPk, ephSk)), "an X25519 pair is generated");
    Check(UltraCrypt_KeyExchange(ephSk, xPkA, sEph1) && UltraCrypt_KeyExchange(xSkA, ephPk, sEph2) &&
          UltraCrypt_ConstantTimeEquals(sEph1.Data(), 32, sEph2.Data(), 32),
          "an ephemeral key agrees with an identity");

    std::vector<uint8_t> shortEd(edPkA.begin(), edPkA.end() - 1), out;
    auto conv = UltraCrypt_SignaturePublicKeyToKeyExchange(alg, shortEd, out);
    Check(!conv && conv.code == UltraCryptResultCode::InvalidKeySize, "a 31-byte Ed25519 key is refused");
}

// ===== X.509 and CMS =====
// A test authority, Alice's mail certificate (RSA, emailProtection, also as
// PKCS #12 with password "geheim", AES and RC2) and a TLS server certificate
// (P-256, serverAuth only) - made with openssl for these tests alone.
static const char* kCaPem = R"PEM(-----BEGIN CERTIFICATE-----
MIIBszCCAVqgAwIBAgIUCgwesYfljEA4IIB8WZrGRH48tTYwCgYIKoZIzj0EAwIw
NzEYMBYGA1UECgwPVWx0cmFDcnlwdCBUZXN0MRswGQYDVQQDDBJVbHRyYUNyeXB0
IFRlc3QgQ0EwIBcNMjYxMDI3MjAyMzQ0WhgPMjEyNjEwMDMyMDIzNDRaMDcxGDAW
BgNVBAoMD1VsdHJhQ3J5cHQgVGVzdDEbMBkGA1UEAwwSVWx0cmFDcnlwdCBUZXN0
IENBMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEVm2uLSKHP2cZtdbgxjCqHCQ4
UkSrFNN5cFmJEYWL6Nfa9MCQF09QaxsXYeqeY0TyHoJMIk7u68tJGredDU8WJaNC
MEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFLHK
ExSYvC1GQpDNQMJJWLoa+j4JMAoGCCqGSM49BAMCA0cAMEQCIDVh/1rKNyTNHWbN
uZKpSsx5t5WukxCkjMxNRswehiYoAiAe3Srm0xrRAk2RrQPZRVHQ4Jn9RKXbg6bU
m0Zx1zyOLw==
-----END CERTIFICATE-----
)PEM";
static const char* kAlicePem = R"PEM(-----BEGIN CERTIFICATE-----
MIIC0jCCAnigAwIBAgIUBNDhddFTCsgVpUIdjd+XiWJcwJowCgYIKoZIzj0EAwIw
NzEYMBYGA1UECgwPVWx0cmFDcnlwdCBUZXN0MRswGQYDVQQDDBJVbHRyYUNyeXB0
IFRlc3QgQ0EwIBcNMjYxMDI3MjAyMzQ0WhgPMjEyNjEwMDMyMDIzNDRaMDoxFjAU
BgNVBAMMDUFsaWNlIEV4YW1wbGUxIDAeBgkqhkiG9w0BCQEWEWFsaWNlQGV4YW1w
bGUuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoof2q7U3YKlx
jPrx5ZpY6xd+PGBFBR8RvvpOY3WpOkGTOV1jKpLl61emRUsuxhfUC1kvATheLOz2
qBQKXRXJ2sjC5ulwejXzmBldQUkGpYa+p5uSIrFwfzvRTvyDhSwx7aGlpkZLJRj8
yn0A5sO+LFtr6CyviYkEJAg6C9jTmsvXCY0aRhcEKJS9MfTbmj8fGLPS6SewnFhs
mz+wt4e6NF50gpphmcgBp0QFwSNkRC6GTQFQr9qgq0PpUJo/UnRlbnyZ99pvc6hc
hobfOfM3FhSYMYlsyk9E4GqHEUsYveYZuHfCrKeo+mDuOZql/f5fXznjJettqPam
rWfet4pNJQIDAQABo4GRMIGOMAkGA1UdEwQCMAAwDgYDVR0PAQH/BAQDAgWgMBMG
A1UdJQQMMAoGCCsGAQUFBwMEMBwGA1UdEQQVMBOBEUFsaWNlQEV4YW1wbGUuQ09N
MB0GA1UdDgQWBBSOPiI6Lss1G2K/Un+k8x3VEi6ySDAfBgNVHSMEGDAWgBSxyhMU
mLwtRkKQzUDCSVi6Gvo+CTAKBggqhkjOPQQDAgNIADBFAiEA9HCZx2hgHoMEt4dT
hsK+dV1f3Pa8IjYIEBhLbMvApisCIBvhZdw34Xm6bSU918T3tDup7GTqcp1OgPXP
SVAkffSJ
-----END CERTIFICATE-----
)PEM";
static const char* kAliceKeyPem = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCih/artTdgqXGM
+vHlmljrF348YEUFHxG++k5jdak6QZM5XWMqkuXrV6ZFSy7GF9QLWS8BOF4s7Pao
FApdFcnayMLm6XB6NfOYGV1BSQalhr6nm5IisXB/O9FO/IOFLDHtoaWmRkslGPzK
fQDmw74sW2voLK+JiQQkCDoL2NOay9cJjRpGFwQolL0x9NuaPx8Ys9LpJ7CcWGyb
P7C3h7o0XnSCmmGZyAGnRAXBI2RELoZNAVCv2qCrQ+lQmj9SdGVufJn32m9zqFyG
ht858zcWFJgxiWzKT0TgaocRSxi95hm4d8Ksp6j6YO45mqX9/l9fOeMl622o9qat
Z963ik0lAgMBAAECggEAKG/V5/YT40189hOoM9UBKupjKp/UBDhs6JwrfRTUx+zx
NVw99CVspS8fK83PAxOCGtmzN2VADwLcwkgMJQkq+a8pyJwLMyixpKWo3TmPOQcL
b7VluBJ39/vYsUNjcHJksKffR/1krEPpfvJ3UzKfobcUI4VcngYUHOfcEz78tJ38
8phSc5/9fOYKL62OXlP0nFugmf3K7OgE3MzAg+2MUKYaTHITMgXMYE9Ur/qh2uHi
z0ntNu2eLCa8dgJZLVjd/5b1Ux5q+6NUBrtOnO5pM+nbaWBSZl7owIDpMbRkb6I5
wB/z32p/untXmiRsiYpNACXx46r8fxY8AKNU4fTv+QKBgQDhB+uj4T9UU3+QOWPt
Fafmepvw+lEJ40ZiExrrfiNRLfMPCWwuHzmFGri490S5VAEBJcCSzuRW8C+EvTlX
UXK9+f+gJPaLsGFkEg9j3DbMl8kX/e36Far6Sy8NU3/5nvIdQIp7Im2oX7o7XH9A
Q6DYn7bCPj2WTzoSti4bn9MrqQKBgQC45huEZEc5oBPX/iHPmgNI1Mo7jKI8j6VK
pMGE1nJoRrXVRo5qTaA5x/STVnBa3ZewBU0E+QWQH554BlQcg9Z17LcVqtekSPIz
fd64BGlo0fAuiS/RKhmVaqlxQuWOFWKQ/0kFb2mFfz+yyhPgweRvljeIvP3QGIRn
fQIKclxjHQKBgDVDQqsPbYXwHYltjBFZxtvuuWpYVUYZ3VbpXr3OpV1ZmuY8PLbN
/TdcE8zz+fhDwdkbrnERN0q8nwK0JxZb2OTOf4rQNOrrXKHrBGmGfhH++0IKgIFp
D4cetIxa7zeOP5Bc7momw/3Oaq4SPqxfVXrwkrl3MeyZ5Az5wgAX3EvBAoGAYRYS
ZNXywENZqBR81HFeQCPbfKE/nuUeOa+BHassr6xRVgeSSCcydqSTu7mrjUrFl1tx
MadAUIkJwKQyDjb7qTf+epYWXOEgrADvrVhDsRJrN0Q5o26juF0lU594wb5g3xns
0bnKgppqbArHnUCyHhWWW2pBK79z5pMxOyMJd9ECgYA8waZncE3uFUK4f1sBwp/x
vKmIIPI9Jf3Y7Rt5LOPTmeOkv4gtW0vz2w7dnTO/IhRmtnDO2UlGK58mjPrWtaiH
sRFcbZC25rDpE9zVwq6UYufi2L0UUMSAzlUvauQhtFDd6S5ThRKW8yJ+4YYuLxly
kJP4rk3JYSB6A7jufkhdwQ==
-----END PRIVATE KEY-----
)PEM";
static const char* kTlsPem = R"PEM(-----BEGIN CERTIFICATE-----
MIIB5jCCAYugAwIBAgIUBNDhddFTCsgVpUIdjd+XiWJcwJswCgYIKoZIzj0EAwIw
NzEYMBYGA1UECgwPVWx0cmFDcnlwdCBUZXN0MRswGQYDVQQDDBJVbHRyYUNyeXB0
IFRlc3QgQ0EwIBcNMjYxMDI3MjAyMzQ0WhgPMjEyNjEwMDMyMDIzNDRaMBoxGDAW
BgNVBAMMD3Rscy5leGFtcGxlLmNvbTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IA
BGUixn8itnX+qZW+AgIvUeJHocNYViSuqi8K9TMYOIAvzRbXpZ3b/x1QPE/O6Ynh
yhBfkMX4/z8O3AhJbnic40KjgY8wgYwwCQYDVR0TBAIwADAOBgNVHQ8BAf8EBAMC
BaAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwGgYDVR0RBBMwEYIPdGxzLmV4YW1wbGUu
Y29tMB0GA1UdDgQWBBT6FEz4Qd2VmBki+LLTIz9WvsVGrjAfBgNVHSMEGDAWgBSx
yhMUmLwtRkKQzUDCSVi6Gvo+CTAKBggqhkjOPQQDAgNJADBGAiEAhhJbHKDkcW6x
OMFZ1GsEpuWgnoxVYNcrOG4UVkkMKpwCIQDtNCOU8KuDA+nZBQPmS9dffbFVcvvj
+VKFBynqt/+bpA==
-----END CERTIFICATE-----
)PEM";
static const char* kTlsKeyPem = R"PEM(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQguRY/v8GRjQ6J3cg0
Mig0JWT88KWKgYTadfZlv9PA3ouhRANCAARlIsZ/IrZ1/qmVvgICL1HiR6HDWFYk
rqovCvUzGDiAL80W16Wd2/8dUDxPzumJ4coQX5DF+P8/DtwISW54nONC
-----END PRIVATE KEY-----
)PEM";
static const char* kAliceP12 =
    "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"
    ;
static const char* kAliceLegacyP12 =
    "MIIK+QIBAzCCCr8GCSqGSIb3DQEHAaCCCrAEggqsMIIKqDCCBV8GCSqGSIb3DQEHBqCCBVAwggVM"
    "AgEAMIIFRQYJKoZIhvcNAQcBMBwGCiqGSIb3DQEMAQYwDgQI9jE+pxp+pqoCAggAgIIFGB6lkeGS"
    "OWBXXwMmIbtqFJMtkysJYz86nOKamEAkjeR908TJKJoiS1aNRbyYnS1p8Q89Cel2s9t1FGHRVFmC"
    "V8JPl/FECaelBe4P8ED5UcQ6vJrD9iSYwzEIpbZeJU8KyUKhgWSMpVfMjeHq/QvBalFedGOvnRnO"
    "0KErdNCriln50HW6zUyAozSW76tCWj1bFZ+yltb9Aq2JqfoSdHGAMJVjkmnvVU02JXJttte1Mvdl"
    "eeU68hudCpkk2RTgDfDfHIza2psIuGrAlNivEUF4aKOvhIaR7NKkVtmLZ9ef+gwFhUPve5h/hW6u"
    "1yl51vjjcgv+lyg0831TEcSAsopbJ6fPmccjej1fWy8UPziEhi/Gb4ACauY4l8fURLPIZI1Apdqb"
    "QGu7BwNjkrhmDPVhMoiXN8Q15AOyMY1T3CBrmNv5z2cb6TqKnhqnVqDowjG8FlAukOoHwbSO0S3G"
    "HbQSw8YfT1piSbCFscL9qUEUYYA73Tc0J7EUCjlLTzwT0qq44rDLQSe0apOK6qh7zt24ON6ztNc3"
    "ivIIlaUZDJ7OMxEcONMkDSNoQo3vk4O48Rp/ognq8owbsdiJFoYym7ix//YKFddwHgcv/ZxC5g4u"
    "lNN9aohGjeBeK1rRdF7Y5WDCkK+QtfYMii5eJnN34gPhWdaay/JYOeerVNYQJumaZ0aIUTE+ZNIQ"
    "Yb6ZSASRSPOYMhLVtwc1f+VJmBRBtqDLAOQqyW/G1sQ6utH1ywUILT0XZC6HDejNu7LvzVWFu/4i"
    "RukE+2RC/AM8s6wQblsncU9jGGtlyowH5yRwn94sT1NvfQt1A9z2IKvfQ/e2SzT35A5dx+/dz8Er"
    "IUB9x9ia7/jCmwC9YXqTjjNBzBY60B+gYLG3tDsmb/2kxnUDTTP09/63As2MLjqwsM+TKtbnDCzT"
    "sZfyy3FVJzydM36JuvupYFgCB+s0GViqWGOlAyxldzdAF1oXJJOuO72TtB8uMv+TWMLYVScjFZX/"
    "FlcmJmlCm4nDoSrlAn+kAwrt7Fqv2iufu4WFCel9Mbsz83xCSVGoWuVJaeOxVpw3KT9Jjegv0c8Y"
    "Ddu8sJpUx9+9rnD4C6kpweWymET5cumO5CwL2a1E1YtkRIVg4Q4kzAyb7NCrCGj47DivlcY8EEP7"
    "b13spP3w+HtS/WlNZL973nBVolj2NVlJWwso5Aay1LTD/jHmDgAGS424Fh0KZipgkVTXcZDVk44P"
    "DTDGab3o6OdfGsmw3mVNuCccIO3TgZJfgsHrl4Eb7cv+gv3nPbtHZsdjHe2Zn7CBm3XJnEeqIH1E"
    "EAOTj8722byBjWFRGzdhUM60XGITkB6wZFiV927yOWhXnF5ph449C7AedOJq+ZU8yvwajwh3T5Dp"
    "8C0TgQZUDR8bKYi1k4gLC7txFbI1s8GVVAkE3k1w4Vw8xJ1/jXpRqTpwQVB1rNmGYBXHnvImBRgL"
    "UAr1sSYNAPdf3HmiM2Gg3mbSeWHK1rSE+3tMhpXpbm9FwL3/jgbBob4Ru++HcyWBVhWDybgSzPZ0"
    "Mgk2fv3tiVOy+1r6VvgOOWtAEXtKZugZ+s7K48p5R+XXKgOjctoPtsEJNwn0CQbZuiUYtzUCB2Tc"
    "97Aj9LWQtHigOXtkQ+lQeeaC6yD0WAsECxopVAwotwomDi44YjwA1OKBX7TRR7dWRbZbE+CCKObx"
    "XsnAZ4mxrg9vgHB74FBn9U4meF9jHZp0P8uQ8fD0R5QXlZwAifck83nuS2rgMIIFQQYJKoZIhvcN"
    "AQcBoIIFMgSCBS4wggUqMIIFJgYLKoZIhvcNAQwKAQKgggTuMIIE6jAcBgoqhkiG9w0BDAEDMA4E"
    "COS8guQAsNWjAgIIAASCBMiTie2ukiHj8bmgKBTiimG/+ItDREPUU/7rcuiaNIgLXTwejPhozVZ8"
    "um3QwDqnFhurvD/hFZ8ElxU9VZGlIoVGU5LKAgT0MfN94C/0AZxaX4NgGJJ66rUdVC+QoWyovGw+"
    "5X1s/95cVNkteoNX3P9fPjJMlDg8w49P6AGRyxz3cJ5I7xPEZuDKSeC9LDcwjkOxDnGbpBR2Y1Ev"
    "dNhK7mU8p0UW7295CtabNb7WZoh7SlkqQW30oFnjUTjkM5p69x8MqUvsSEV6R9oSGW8nyarJEADO"
    "ID/BSg66T0O4T01uAW1l1nMwoxunJl9uKYZZw2BCs4tNRCkevuo/EYXtqJYaIFpYHvD+2nXspH6T"
    "CpMxt2LqeZzJxef0P0QFqOQ3EVBBLmyUMujUkYnyfwbgidZwACZZcLHz5U8vf65Lnmg+pjrlpUwe"
    "lwmLLE8x998DfRpPYXBW1BKOQ56vPfDmyRqZG92X7mTA/5y99eURImAsDrDGbVzPobuiIy4E63mQ"
    "0uGrUpPoYF6V6qOcQF8KGcNKlXXWSj7YkJqy1casblw6B12e59JxUtXQneFwmyvW10A+L8335z4T"
    "k4q4laD8850Albiwlh3J+d34dbqZCo6qlgE8vmTdTmpaqhGDJwh6SgmNbqKLEz1/Z+GBAvjWZMLK"
    "KGar/zN+9H2Or0h6cNT3Wit57jqIAt+cTnLVNatXcWTmGnCpBsifcc9IfPR2lw/BjZLvn/qdgPZG"
    "L4scqd7jvWhx9lYZ/sWwLn6pZUTwcR1Kk96dCuinMsJH6s1QR/InhshKnWJKmBnP74twoHEOtJAN"
    "D7Jma8XiRNhbGQK/WdrqPf7vJgOfq2n3BZnW5LiUK27pacftDVufsfCTbQX39jcgHCXevkJjdYu8"
    "OSxw+oIgmJUG4KwROQ2gaj0klTpzI4DFjEwGQ9u54ZhVsRsHhPc2yOajmXRWigbyXWJwgo1NWZ2e"
    "8SYPGdUb6HuPBKw1GzDlqTzo+iXZWwqbP79GQI3T64J0GvL180oiSJNuUp/cKrabmJ1dtsVHiLTm"
    "sUCr1BIrOWvhKLIXtSXIyGpQUe5PiW9mlMpymWuus2J9EPUp045swbqZ1js5b6H/60LnCNlL8zWS"
    "1ZLe7leBYCPORTac+bLVyMvikbeXHgOX77W4KyZ66aV7ZT4/1uW8oZOxos4ACmgAhkzaMuIa7Hrb"
    "9XpaPhsDQK0JQByFhrG+N3IpJgOMrW6qTsoe3sgh4LdWllleMO9c5Ur5adqoMF14Xjqb9dHQ4NXm"
    "T6CE2FBiutTHFyosDwmUvJSV4f5+YaVGIIWX5qjofvwdbBe5X6JaDaftstjs4VE7Wdo2s2evPr/P"
    "SADt0ONkMlDzbkcS1TbbH6BFts+T56Ad3M4FqDr2X1C7KtqbDWlYNmMPyeW7SZcssjseiyRtiXP/"
    "8pmIS/AiestKup9sRNsUTABZHJJS+cEhh9oUObXMIed3xDGWQo6vEPXytxnuR6+ZlVod+/UEKm56"
    "LFQXnfxYmSwsCgutsAdU/Qc0zLLTKrvlL9t2ZW9ThddB5qGVOvedpGfPxybxWiP5H9E2lBEpt6sY"
    "LyRyyBdcHYPB8TZp1lVgg4OJzQQAZPq6BLmRY0x5EhtA7t9tglyqaa6dzHgxJTAjBgkqhkiG9w0B"
    "CRUxFgQUZHibnVC0jUXpB3RtNOXi0JTBl5owMTAhMAkGBSsOAwIaBQAEFPFmJun6LMV182PX6Jdo"
    "rPPbHQ3ABAgUI5AjXmHGLAICCAA="
    ;

static std::vector<uint8_t> Bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static UltraCryptX509Identity IdentityOf(const char* certificatePem, const char* keyPem) {
    UltraCryptX509Identity id;
    id.certificatePem = certificatePem;
    id.privateKeyPem = UltraCryptSecureBuffer(keyPem, std::strlen(keyPem));
    return id;
}

static UltraCryptTrust TrustOf(const std::string& cert, UltraCryptCertificatePurpose purpose, int64_t at = 0,
                               bool withTestRoot = true) {
    UltraCryptTrustOptions options;
    options.purpose = purpose;
    options.useSystemRoots = !withTestRoot;
    if (withTestRoot) options.extraRootsPem.push_back(kCaPem);
    options.at = at;
    UltraCryptTrustCheck check;
    UltraCrypt_CheckCertificate(cert, {}, options, check);
    return check.trust;
}

static void TestX509() {
    std::printf("X.509 and CMS\n");
    if (!UltraCrypt_IsX509Available()) {
        UltraCryptCertificateInfo info;
        const auto r = UltraCrypt_ReadCertificate(kAlicePem, info);
        Check(!r && r.code == UltraCryptResultCode::BackendUnavailable,
              "without OpenSSL 3 every call says BackendUnavailable");
        std::printf("  (built without OpenSSL 3: nothing more to check)\n");
        return;
    }

    // What a certificate says.
    UltraCryptCertificateInfo info;
    Check(static_cast<bool>(UltraCrypt_ReadCertificate(kAlicePem, info)), "a PEM certificate is read");
    Check(info.commonName == "Alice Example", "its common name");
    Check(info.emails.size() == 1 && info.emails[0] == "alice@example.com",
          "its address, lowercased, once (subject and alternative name)");
    Check(info.issuer == "UltraCrypt Test (UltraCrypt Test CA)", "its issuer, organisation and name");
    Check(info.notBefore > 1700000000 && info.notAfter > info.notBefore + 36000LL * 86400, "its validity period");
    Check(info.fingerprintSha256.size() == 95 && info.fingerprintSha256[2] == ':', "its SHA-256 fingerprint");
    {
        std::string body;
        for (const char* p = std::strstr(kAlicePem, "\n") + 1; *p && *p != '-'; ++p)
            if (*p != '\n') body += *p;
        const std::vector<uint8_t> der = UltraCanvas::Base64Decode(body);
        UltraCryptCertificateInfo fromDer;
        Check(UltraCrypt_ReadCertificate(std::string(der.begin(), der.end()), fromDer) &&
              fromDer.fingerprintSha256 == info.fingerprintSha256, "the same certificate as DER");
    }
    UltraCryptCertificateInfo none;
    const auto bad = UltraCrypt_ReadCertificate("not a certificate", none);
    Check(!bad && bad.code == UltraCryptResultCode::InvalidArgument, "text without a certificate is refused");

    // Trust.
    using P = UltraCryptCertificatePurpose;
    Check(TrustOf(kAlicePem, P::EmailSigning, 0, false) == UltraCryptTrust::Untrusted,
          "the test authority is trusted nowhere: Untrusted");
    Check(TrustOf(kAlicePem, P::EmailSigning) == UltraCryptTrust::Trusted, "with its root given: Trusted");
    Check(TrustOf(kAlicePem, P::EmailEncryption) == UltraCryptTrust::Trusted, "for encrypting too");
    Check(TrustOf(kTlsPem, P::EmailSigning) == UltraCryptTrust::WrongPurpose,
          "a TLS server certificate signing mail: WrongPurpose");
    Check(TrustOf(kTlsPem, P::Any) == UltraCryptTrust::Trusted, "the same for no purpose in particular: Trusted");
    Check(TrustOf(kAlicePem, P::EmailSigning, 7258118400LL) == UltraCryptTrust::Expired, "judged in 2200: Expired");
    Check(TrustOf(kAlicePem, P::EmailSigning, 946684800LL) == UltraCryptTrust::NotYetValid,
          "judged in 2000: NotYetValid");
    {
        UltraCryptTrustOptions options;
        options.useSystemRoots = false;
        UltraCryptTrustCheck check;
        UltraCrypt_CheckCertificate(kAlicePem, {kCaPem}, options, check);
        Check(check.trust == UltraCryptTrust::Untrusted && !check.reason.empty(),
              "an intermediate is not a root: Untrusted, with a reason");
    }

    // PKCS #12.
    const UltraCryptSecureBuffer password("geheim", 6);
    UltraCryptX509Identity alice;
    Check(static_cast<bool>(UltraCrypt_ImportPkcs12(UltraCanvas::Base64Decode(kAliceP12), password, alice)),
          "a PKCS #12 file opens with its password");
    UltraCryptCertificateInfo first;
    Check(UltraCrypt_ReadCertificate(alice.certificatePem, first) && first.fingerprintSha256 == info.fingerprintSha256,
          "its certificate comes first");
    Check(alice.certificatePem.find("BEGIN CERTIFICATE", alice.certificatePem.find("END CERTIFICATE")) != std::string::npos,
          "then its chain");
    Check(alice.privateKeyPem.GetSize() > 100 &&
          std::memcmp(alice.privateKeyPem.Data(), "-----BEGIN PRIVATE KEY-----", 27) == 0,
          "and its private key, PKCS #8 PEM");
    UltraCryptX509Identity legacy;
    Check(static_cast<bool>(UltraCrypt_ImportPkcs12(UltraCanvas::Base64Decode(kAliceLegacyP12), password, legacy)),
          "an older file (RC2) opens too");
    UltraCryptX509Identity wrong;
    const auto wrongPw = UltraCrypt_ImportPkcs12(UltraCanvas::Base64Decode(kAliceP12),
                                                 UltraCryptSecureBuffer("falsch", 6), wrong);
    Check(!wrongPw && wrongPw.code == UltraCryptResultCode::AuthenticationFailed && wrong.privateKeyPem.IsEmpty(),
          "a wrong password: AuthenticationFailed, and no key");
    const auto notP12 = UltraCrypt_ImportPkcs12(Bytes("not pkcs12"), password, wrong);
    Check(!notP12 && notP12.code == UltraCryptResultCode::InvalidArgument, "a file that is not PKCS #12: InvalidArgument");

    // Detached signatures.
    const std::vector<uint8_t> content = Bytes("Content-Type: text/plain\r\n\r\nHallo Bob.\r\n");
    std::vector<uint8_t> signature;
    Check(static_cast<bool>(UltraCrypt_CmsSignDetached(content, alice, signature)), "Alice signs");
    Check(UltraCrypt_CmsTypeOf(signature) == UltraCryptCmsType::SignedData, "it is signed data");
    UltraCryptCmsSignature found;
    Check(static_cast<bool>(UltraCrypt_CmsVerifyDetached(content, signature, found)), "the signature holds");
    UltraCryptCertificateInfo signer;
    Check(UltraCrypt_ReadCertificate(found.signerCertificatePem, signer) &&
          signer.fingerprintSha256 == info.fingerprintSha256, "it names Alice's certificate");
    Check(found.certificatesPem.size() == 2, "and carries her chain");
    {
        UltraCryptTrustOptions options;
        options.purpose = UltraCryptCertificatePurpose::EmailSigning;
        options.useSystemRoots = false;
        options.extraRootsPem.push_back(kCaPem);
        UltraCryptTrustCheck check;
        UltraCrypt_CheckCertificate(found.signerCertificatePem, found.certificatesPem, options, check);
        Check(check.trust == UltraCryptTrust::Trusted, "the signer is vouched for through what it carries");
    }
    std::vector<uint8_t> changed = content;
    changed[changed.size() - 3] ^= 0x01;
    UltraCryptCmsSignature changedFound;
    const auto changedResult = UltraCrypt_CmsVerifyDetached(changed, signature, changedFound);
    Check(!changedResult && changedResult.code == UltraCryptResultCode::SignatureInvalid,
          "a changed byte of the content: SignatureInvalid");
    Check(!changedFound.signerCertificatePem.empty(), "the signer is still named");
    std::vector<uint8_t> badSignature = signature;
    badSignature.back() ^= 0x01;
    const auto badSig = UltraCrypt_CmsVerifyDetached(content, badSignature, changedFound);
    Check(!badSig && badSig.code == UltraCryptResultCode::SignatureInvalid, "a changed byte of the signature: SignatureInvalid");
    const auto notSigned = UltraCrypt_CmsVerifyDetached(content, content, changedFound);
    Check(!notSigned && notSigned.code == UltraCryptResultCode::InvalidArgument, "bytes that are no signature: InvalidArgument");
    std::vector<uint8_t> unused;
    const auto mismatched = UltraCrypt_CmsSignDetached(content, IdentityOf(kAlicePem, kTlsKeyPem), unused);
    Check(!mismatched && mismatched.code == UltraCryptResultCode::InvalidArgument && unused.empty(),
          "a key that is not the certificate's signs nothing");

    // Enveloped data.
    std::vector<uint8_t> enveloped;
    Check(UltraCrypt_CmsEncrypt(content, {kAlicePem, kTlsPem}, enveloped) &&
          UltraCrypt_CmsTypeOf(enveloped) == UltraCryptCmsType::EnvelopedData,
          "encrypted to Alice and to an EC certificate");
    UltraCryptSecureBuffer opened;
    Check(UltraCrypt_CmsDecrypt(enveloped, alice, opened) && opened.GetSize() == content.size() &&
          std::memcmp(opened.Data(), content.data(), content.size()) == 0, "Alice opens it");
    UltraCryptSecureBuffer openedPem;
    Check(UltraCrypt_CmsDecrypt(enveloped, IdentityOf(kAlicePem, kAliceKeyPem), openedPem) &&
          openedPem.GetSize() == content.size(), "and so does her key as a PEM file holds it");
    UltraCryptSecureBuffer openedEc;
    Check(UltraCrypt_CmsDecrypt(enveloped, IdentityOf(kTlsPem, kTlsKeyPem), openedEc) &&
          openedEc.GetSize() == content.size(), "so does the EC recipient");
    std::vector<uint8_t> toAliceOnly;
    UltraCrypt_CmsEncrypt(content, {kAlicePem}, toAliceOnly);
    UltraCryptSecureBuffer refused;
    const auto notMine = UltraCrypt_CmsDecrypt(toAliceOnly, IdentityOf(kTlsPem, kTlsKeyPem), refused);
    Check(!notMine && notMine.code == UltraCryptResultCode::DecryptionFailed && refused.IsEmpty(),
          "another recipient: DecryptionFailed, and nothing");
    std::vector<uint8_t> noOne;
    Check(UltraCrypt_CmsEncrypt(content, {}, noOne).code == UltraCryptResultCode::InvalidArgument &&
          UltraCrypt_CmsEncrypt(content, {"garbage"}, noOne).code == UltraCryptResultCode::InvalidArgument,
          "no recipient, or one without a certificate: InvalidArgument");
    std::vector<uint8_t> attached;
    UltraCryptCmsSignature attachedFound;
    Check(UltraCrypt_CmsVerifyAttached(enveloped, attached, attachedFound).code == UltraCryptResultCode::InvalidArgument,
          "enveloped data is not signed data");
}

int main() {
    std::printf("UltraCrypt tests — backend: %s\n\n",
                UltraCrypt_GetBackendName().c_str());

    if (!UltraCrypt_IsAvailable()) {
        std::printf("FAIL: no crypto backend available; UltraCrypt cannot operate.\n");
        return 1;
    }

    TestHashing();
    TestSha1();
    TestHmac();
    TestHkdf();
    TestPbkdf2();
    TestEncodings();
    TestAead();
    TestAesCbc();
    TestAesCtr();
    TestKdf();
    TestSignatures();
    TestKeyExchange();
    TestSecureBuffer();
    TestX509();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
