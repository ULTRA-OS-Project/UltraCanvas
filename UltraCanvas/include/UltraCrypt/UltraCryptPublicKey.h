// include/UltraCrypt/UltraCryptPublicKey.h
// RSA and ECDSA signature checks against a public key given by its parts -
// what JOSE (JWS RS256 and ES256, RFC 7518), an OpenID Connect provider's key
// set (JWKS) and WebCrypto hand over: an RSA key as its modulus and public
// exponent, a P-256 key as its affine coordinates.
//
// Checks only. Everything UltraCanvas signs of its own is Ed25519
// (UltraCryptCore.h, libsodium); these are for signatures the world makes.
//
// The backend is OpenSSL 3's libcrypto, as for X.509 and CMS
// (UltraCryptX509.h), and as optional: without it
// UltraCrypt_IsRsaEcdsaAvailable() is false and every call returns
// BackendUnavailable. No OpenSSL type appears here. Nothing throws.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRACRYPTPUBLICKEY_H
#define ULTRACRYPTPUBLICKEY_H

#include "UltraCrypt/UltraCryptCore.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Whether this build has RSA and ECDSA (OpenSSL 3).
bool UltraCrypt_IsRsaEcdsaAvailable();

// ============================================================================
// RSA: RSASSA-PKCS1-v1_5 with SHA-256 (RFC 8017; JWS "RS256")
// ============================================================================
// The key as its modulus and public exponent, unsigned big-endian bytes - a
// JWK's "n" and "e", base64url-decoded.

// Whether the parts make a key these checks accept: InvalidKeySize for a
// modulus below 2048 bits, InvalidArgument for one OpenSSL refuses (an even
// or out-of-range exponent, ...).
UltraCryptResult UltraCrypt_CheckRsaPublicKey(const std::vector<uint8_t>& modulus,
                                              const std::vector<uint8_t>& exponent);

// Ok when `signature` is the key's over exactly `data`; SignatureInvalid when
// not (whichever byte is off). The key is checked as above first.
UltraCryptResult UltraCrypt_VerifyRsaPkcs1Sha256(const std::vector<uint8_t>& modulus,
                                                 const std::vector<uint8_t>& exponent,
                                                 const void* data, size_t size,
                                                 const std::vector<uint8_t>& signature);

// ============================================================================
// ECDSA on P-256 with SHA-256 (FIPS 186-4; JWS "ES256")
// ============================================================================
// The key as its affine coordinates x and y, 32 bytes each - a JWK's "x" and
// "y". The signature as r || s, 32 bytes each (64 in all), as JWS and
// WebCrypto give it - not DER.

// InvalidKeySize when x or y is not 32 bytes, InvalidArgument when the point
// is not on the curve.
UltraCryptResult UltraCrypt_CheckP256PublicKey(const std::vector<uint8_t>& x,
                                               const std::vector<uint8_t>& y);

// Ok when `signature` is the key's over exactly `data`; SignatureInvalid when
// not; InvalidArgument for a signature that is not 64 bytes.
UltraCryptResult UltraCrypt_VerifyEcdsaP256Sha256(const std::vector<uint8_t>& x,
                                                  const std::vector<uint8_t>& y,
                                                  const void* data, size_t size,
                                                  const std::vector<uint8_t>& signature);

#endif // ULTRACRYPTPUBLICKEY_H
