// include/UltraCrypt/UltraCryptX509.h
// X.509 certificates and CMS (RFC 5652): what S/MIME (RFC 8551) is made of,
// and what any other use of the public-key infrastructure needs.
//
//  - Certificates: what one says (UltraCrypt_ReadCertificate), and whether it
//    is vouched for - it chains to a root this computer trusts, through the
//    intermediates given, for the purpose asked, today
//    (UltraCrypt_CheckCertificate).
//  - CMS: signed data, detached (multipart/signed) and with its content
//    (opaque S/MIME), checked and made; enveloped data encrypted to
//    certificates and opened with an identity.
//  - An identity - a certificate, its chain and its private key - read from a
//    PKCS #12 file (.p12 / .pfx).
//
// The mail around these (MIME, which part is signed, whose certificate to
// learn) is the caller's: UltraCrypt knows bytes, certificates and keys.
//
// The backend is OpenSSL 3's libcrypto, wrapped completely like libsodium is
// in UltraCryptCore.h: no OpenSSL type appears here. It is optional - a build
// without OpenSSL 3 has UltraCrypt_IsX509Available() false, and every call
// returns BackendUnavailable. libsodium is not involved: everything
// UltraCanvas encrypts of its own stays XChaCha20-Poly1305 (UltraCryptCore.h);
// these are for formats the world defined.
//
// Certificates travel as PEM text (one or more "-----BEGIN CERTIFICATE-----"
// blocks; DER is read too where a single certificate is expected), CMS
// structures as DER bytes, a private key as PEM in an UltraCryptSecureBuffer.
// Nothing throws.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRACRYPTX509_H
#define ULTRACRYPTX509_H

#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <string>
#include <vector>

// Whether this build has the X.509 and CMS backend (OpenSSL 3).
bool UltraCrypt_IsX509Available();

// ============================================================================
// Certificates
// ============================================================================
struct UltraCryptCertificateInfo {
    std::string              commonName;   // the subject's CN; the first address when it has none
    std::vector<std::string> emails;       // subject emailAddress and rfc822Name alternative names, lowercased
    std::string              issuer;       // the issuer's organisation (and CN, when it differs)
    int64_t                  notBefore = 0;    // seconds since 1970, UTC
    int64_t                  notAfter = 0;
    std::string              fingerprintSha256;   // "AB:CD:...": SHA-256 of the DER
};

// The first certificate in `certificate` (PEM, or one DER certificate).
// InvalidArgument when there is none.
UltraCryptResult UltraCrypt_ReadCertificate(const std::string& certificate,
                                            UltraCryptCertificateInfo& out);

enum class UltraCryptCertificatePurpose : uint8_t {
    Any,
    EmailSigning,      // S/MIME signing (key usage, extended key usage emailProtection)
    EmailEncryption    // S/MIME encryption
};

enum class UltraCryptTrust : uint8_t {
    Trusted,           // chains to a trusted root, for the purpose, and is in its validity period
    Untrusted,         // no chain to a trusted root (self-signed, unknown issuer, ...)
    Expired,
    NotYetValid,
    Revoked,           // only where the trust store carries revocation lists
    WrongPurpose       // the certificate is not for what it was used for
};

struct UltraCryptTrustOptions {
    UltraCryptCertificatePurpose purpose = UltraCryptCertificatePurpose::Any;
    // The system's roots: OpenSSL's default store, which SSL_CERT_FILE and
    // SSL_CERT_DIR override.
    bool useSystemRoots = true;
    // More roots, PEM - a private authority, or a test's.
    std::vector<std::string> extraRootsPem;
    // When to judge it, seconds since 1970; 0 is now.
    int64_t at = 0;
};

struct UltraCryptTrustCheck {
    UltraCryptTrust trust = UltraCryptTrust::Untrusted;
    std::string     reason;   // why not, in OpenSSL's words; "" when Trusted
};

// Whether `certificatePem` (the first certificate in it) is vouched for.
// `intermediatesPem`: certificates that may complete the chain (each string
// may hold several) - those a signed message carries, the rest of an
// identity's chain. The call itself succeeds whenever the certificate can be
// read; `out` says what was found.
UltraCryptResult UltraCrypt_CheckCertificate(const std::string& certificatePem,
                                             const std::vector<std::string>& intermediatesPem,
                                             const UltraCryptTrustOptions& options,
                                             UltraCryptTrustCheck& out);

// ============================================================================
// Identities
// ============================================================================
// A certificate with its private key: the user's own, to sign with and to
// open what was encrypted to them.
struct UltraCryptX509Identity {
    std::string            certificatePem;   // the certificate first, then its chain
    UltraCryptSecureBuffer privateKeyPem;    // PKCS #8 PEM, not encrypted
};

// An identity from a PKCS #12 file and its password, older files (RC2,
// 3DES) included. AuthenticationFailed for a password that does not open the
// file, InvalidArgument for a file that is not PKCS #12 or holds no
// certificate with its key.
UltraCryptResult UltraCrypt_ImportPkcs12(const std::vector<uint8_t>& pkcs12,
                                         const UltraCryptSecureBuffer& password,
                                         UltraCryptX509Identity& out);

// ============================================================================
// CMS
// ============================================================================
enum class UltraCryptCmsType : uint8_t {
    NotCms,            // not a CMS structure at all
    SignedData,
    EnvelopedData,
    Other              // CMS, of a type this module does not handle
};
UltraCryptCmsType UltraCrypt_CmsTypeOf(const std::vector<uint8_t>& der);

// Who signed, as the signature says.
struct UltraCryptCmsSignature {
    std::string              signerCertificatePem;   // "" when the signature names none it carries
    std::vector<std::string> certificatesPem;        // every certificate it carries (the chain), PEM each
};

// Signature checks: whether the signature holds for exactly these bytes,
// whoever made it - Ok, or SignatureInvalid when a byte of the content or
// the signature differs. Whether the signer is vouched for is a separate
// question: UltraCrypt_CheckCertificate with the signer's certificate and
// the ones the signature carries. `out` is filled as far as the signature
// can be read, also when it does not hold. InvalidArgument when the bytes
// are not signed data.

// A detached signature (DER signed data, as in application/pkcs7-signature)
// over `content`, which is compared as bytes (S/MIME canonicalises to CRLF
// first; that is the caller's).
UltraCryptResult UltraCrypt_CmsVerifyDetached(const std::vector<uint8_t>& content,
                                              const std::vector<uint8_t>& signatureDer,
                                              UltraCryptCmsSignature& out);

// Signed data that carries its content (opaque S/MIME). `outContent` is the
// content also when the signature does not hold, so it can still be shown
// as what it is.
UltraCryptResult UltraCrypt_CmsVerifyAttached(const std::vector<uint8_t>& signedDer,
                                              std::vector<uint8_t>& outContent,
                                              UltraCryptCmsSignature& out);

// A detached signature over `content` by `signer`, carrying its certificate
// and chain so a reader can get from it to a root it trusts. SHA-256.
UltraCryptResult UltraCrypt_CmsSignDetached(const std::vector<uint8_t>& content,
                                            const UltraCryptX509Identity& signer,
                                            std::vector<uint8_t>& outSignatureDer);

// `content` encrypted to every certificate in `recipientsPem` (the first
// certificate of each string), as enveloped data. AES-256-CBC: what every
// S/MIME program reads (RFC 8551's AES-256-GCM, in authenticated-enveloped
// data, most cannot open yet). InvalidArgument for a certificate that
// cannot be read.
UltraCryptResult UltraCrypt_CmsEncrypt(const std::vector<uint8_t>& content,
                                       const std::vector<std::string>& recipientsPem,
                                       std::vector<uint8_t>& outEnvelopedDer);

// Enveloped data opened with `recipient`. DecryptionFailed when it was not
// encrypted to this identity, or does not open with its key.
UltraCryptResult UltraCrypt_CmsDecrypt(const std::vector<uint8_t>& envelopedDer,
                                       const UltraCryptX509Identity& recipient,
                                       UltraCryptSecureBuffer& outContent);

#endif // ULTRACRYPTX509_H
