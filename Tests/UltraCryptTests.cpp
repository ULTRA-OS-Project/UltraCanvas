// Tests/UltraCryptTests.cpp
// Unit tests for the UltraCrypt module. Self-contained: no test framework, no
// UI stack, so it runs headless anywhere UltraCrypt builds.
//
// Coverage follows Docs/Modules/UltraCrypt/README.md §7:
//  - published test vectors (FIPS 180-4 SHA-2, RFC 4231 HMAC-SHA-2)
//  - AEAD round-trip plus the four mandated negative cases (flipped ciphertext
//    bit, flipped AAD bit, wrong nonce, truncated tag), each asserting
//    AuthenticationFailed AND an empty output
//  - KDF determinism, salt sensitivity and stored-parameter honesty
//  - Ed25519 against the RFC 8032 §7.1 vectors, plus a flipped message bit, a
//    flipped signature bit and a wrong key, each reported as SignatureInvalid
//  - secure-buffer semantics (move-only, wipe on destroy)
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptCore.h"
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
    TestEncodings();
    TestAead();
    TestKdf();
    TestSignatures();
    TestSecureBuffer();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
