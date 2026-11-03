// core/UltraCrypt/UltraCryptPublicKey.cpp
// RSA (RS256) and ECDSA P-256 (ES256) signature checks (UltraCryptPublicKey.h)
// on OpenSSL 3's libcrypto, built where UltraCrypt has it (ULTRACRYPT_HAVE_X509);
// without it every call returns BackendUnavailable.
//
// An RSA key is built from its parameters (EVP_PKEY_fromdata); a P-256 key
// is read as a SubjectPublicKeyInfo assembled from the point, because
// OSSL_PARAM_BLD's EC import fails on OpenSSL 3.0.2 (Ubuntu 22.04) while
// d2i_PUBKEY reads alike everywhere - and checks that the point is on the
// curve.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptPublicKey.h"

#if defined(ULTRACRYPT_HAVE_X509)

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/x509.h>

#include <memory>

namespace {

struct KeyFree { void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); } };
struct BnFree { void operator()(BIGNUM* b) const { BN_free(b); } };
using Key = std::unique_ptr<EVP_PKEY, KeyFree>;
using Bn = std::unique_ptr<BIGNUM, BnFree>;

constexpr int kMinRsaBits = 2048;

UltraCryptResult Fail(UltraCryptResultCode code, const char* message) {
    return UltraCryptResult::Error(code, message);
}

Bn BigNumber(const std::vector<uint8_t>& bytes) {
    return Bn(BN_bin2bn(bytes.data(), static_cast<int>(bytes.size()), nullptr));
}

// The RSA key, or the reason there is none.
UltraCryptResult RsaKey(const std::vector<uint8_t>& modulus, const std::vector<uint8_t>& exponent, Key& out) {
    out.reset();
    Bn n = BigNumber(modulus), e = BigNumber(exponent);
    if (!n || !e) return Fail(UltraCryptResultCode::InvalidArgument, "the RSA key cannot be read");
    if (BN_num_bits(n.get()) < kMinRsaBits)
        return Fail(UltraCryptResultCode::InvalidKeySize, "an RSA key below 2048 bits is not accepted");
    if (!BN_is_odd(e.get()) || BN_num_bits(e.get()) < 2 || BN_num_bits(e.get()) > 64)
        return Fail(UltraCryptResultCode::InvalidArgument, "the RSA public exponent is not a usable one");
    OSSL_PARAM_BLD* build = OSSL_PARAM_BLD_new();
    EVP_PKEY* key = nullptr;
    if (build && OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_N, n.get()) == 1 &&
        OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_E, e.get()) == 1) {
        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(build);
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
        if (params && ctx && EVP_PKEY_fromdata_init(ctx) == 1)
            EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params);
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
    }
    OSSL_PARAM_BLD_free(build);
    ERR_clear_error();
    if (!key) return Fail(UltraCryptResultCode::InvalidArgument, "OpenSSL refuses the RSA key");
    out.reset(key);
    return UltraCryptResult::Ok();
}

UltraCryptResult P256Key(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y, Key& out) {
    out.reset();
    if (x.size() != 32 || y.size() != 32)
        return Fail(UltraCryptResultCode::InvalidKeySize, "a P-256 coordinate is 32 bytes");
    // SubjectPublicKeyInfo (RFC 5480): the fixed DER prefix for an
    // id-ecPublicKey on prime256v1, then the uncompressed point 04 || x || y.
    static const uint8_t prefix[] = { 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                      0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01,
                                      0x07, 0x03, 0x42, 0x00, 0x04 };
    std::vector<uint8_t> der(prefix, prefix + sizeof prefix);
    der.insert(der.end(), x.begin(), x.end());
    der.insert(der.end(), y.begin(), y.end());
    const unsigned char* p = der.data();
    out.reset(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
    ERR_clear_error();
    if (!out) return Fail(UltraCryptResultCode::InvalidArgument, "the point is not on P-256");
    return UltraCryptResult::Ok();
}

// SHA-256 and the key's own scheme over `data`.
bool VerifyWith(EVP_PKEY* key, const void* data, size_t size, const uint8_t* signature, size_t signatureSize) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
                    EVP_DigestVerify(ctx, signature, signatureSize, static_cast<const unsigned char*>(data), size) == 1;
    EVP_MD_CTX_free(ctx);
    ERR_clear_error();
    return ok;
}

// r || s as the DER ECDSA-Sig-Value OpenSSL verifies.
std::vector<uint8_t> EcdsaDer(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> der;
    ECDSA_SIG* sig = ECDSA_SIG_new();
    BIGNUM* r = BN_bin2bn(raw.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(raw.data() + 32, 32, nullptr);
    if (sig && r && s && ECDSA_SIG_set0(sig, r, s) == 1) {
        r = s = nullptr;                    // the signature owns them now
        unsigned char* out = nullptr;
        const int size = i2d_ECDSA_SIG(sig, &out);
        if (size > 0) der.assign(out, out + size);
        OPENSSL_free(out);
    }
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
    return der;
}

} // namespace

bool UltraCrypt_IsRsaEcdsaAvailable() { return true; }

UltraCryptResult UltraCrypt_CheckRsaPublicKey(const std::vector<uint8_t>& modulus,
                                              const std::vector<uint8_t>& exponent) {
    Key key;
    return RsaKey(modulus, exponent, key);
}

UltraCryptResult UltraCrypt_VerifyRsaPkcs1Sha256(const std::vector<uint8_t>& modulus,
                                                 const std::vector<uint8_t>& exponent,
                                                 const void* data, size_t size,
                                                 const std::vector<uint8_t>& signature) {
    Key key;
    const UltraCryptResult made = RsaKey(modulus, exponent, key);
    if (!made) return made;
    if (signature.empty()) return Fail(UltraCryptResultCode::InvalidArgument, "no signature");
    if (!VerifyWith(key.get(), data, size, signature.data(), signature.size()))
        return Fail(UltraCryptResultCode::SignatureInvalid, "the signature is not the key's over these bytes");
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CheckP256PublicKey(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) {
    Key key;
    return P256Key(x, y, key);
}

UltraCryptResult UltraCrypt_VerifyEcdsaP256Sha256(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y,
                                                  const void* data, size_t size,
                                                  const std::vector<uint8_t>& signature) {
    Key key;
    const UltraCryptResult made = P256Key(x, y, key);
    if (!made) return made;
    if (signature.size() != 64) return Fail(UltraCryptResultCode::InvalidArgument, "an ES256 signature is 64 bytes (r || s)");
    const std::vector<uint8_t> der = EcdsaDer(signature);
    if (der.empty() || !VerifyWith(key.get(), data, size, der.data(), der.size()))
        return Fail(UltraCryptResultCode::SignatureInvalid, "the signature is not the key's over these bytes");
    return UltraCryptResult::Ok();
}

#else // !ULTRACRYPT_HAVE_X509

namespace {
UltraCryptResult NotBuilt() {
    return UltraCryptResult::Error(UltraCryptResultCode::BackendUnavailable,
                                   "UltraCrypt is built without OpenSSL 3: no RSA or ECDSA");
}
} // namespace

bool UltraCrypt_IsRsaEcdsaAvailable() { return false; }
UltraCryptResult UltraCrypt_CheckRsaPublicKey(const std::vector<uint8_t>&, const std::vector<uint8_t>&) { return NotBuilt(); }
UltraCryptResult UltraCrypt_VerifyRsaPkcs1Sha256(const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                                 const void*, size_t, const std::vector<uint8_t>&) { return NotBuilt(); }
UltraCryptResult UltraCrypt_CheckP256PublicKey(const std::vector<uint8_t>&, const std::vector<uint8_t>&) { return NotBuilt(); }
UltraCryptResult UltraCrypt_VerifyEcdsaP256Sha256(const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                                  const void*, size_t, const std::vector<uint8_t>&) { return NotBuilt(); }

#endif
