// core/UltraCrypt/UltraCryptX509.cpp
// X.509 and CMS (UltraCryptX509.h) on OpenSSL 3's libcrypto. Built with it
// where CMake finds OpenSSL 3 (ULTRACRYPT_HAVE_X509); without it every call
// returns BackendUnavailable.
//
// Key material - a private key's PEM, a PKCS #12 password, decrypted content -
// goes through OpenSSL's secure-memory BIOs and is copied out into
// UltraCryptSecureBuffer, so no plain heap copy of it outlives the call.
//
// The system's roots are OpenSSL's default store and, on Windows and macOS,
// the operating system's own (UltraCryptSystemRoots.cpp) - OpenSSL's default
// store there is only what came with the OpenSSL build. SSL_CERT_FILE or
// SSL_CERT_DIR, when set, is taken as the whole answer instead.
//
// Version: 0.2.0 - the system's roots include Windows' ROOT store and macOS's
//                  anchors and trust settings
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCrypt/UltraCryptX509.h"

#if defined(ULTRACRYPT_HAVE_X509)

#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/provider.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace ultracrypt_detail {
// UltraCryptSystemRoots.cpp: the operating system's trusted roots, DER.
std::vector<std::vector<uint8_t>> PlatformRootsDer();
}

namespace {

struct BioFree { void operator()(BIO* b) const { BIO_free_all(b); } };
struct CmsFree { void operator()(CMS_ContentInfo* c) const { CMS_ContentInfo_free(c); } };
struct X509Free { void operator()(X509* x) const { X509_free(x); } };
struct KeyFree { void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); } };
struct StoreFree { void operator()(X509_STORE* s) const { X509_STORE_free(s); } };
struct StoreCtxFree { void operator()(X509_STORE_CTX* c) const { X509_STORE_CTX_free(c); } };
struct StackFree { void operator()(STACK_OF(X509)* s) const { sk_X509_pop_free(s, X509_free); } };
struct Pkcs12Free { void operator()(PKCS12* p) const { PKCS12_free(p); } };
using Bio = std::unique_ptr<BIO, BioFree>;
using Cms = std::unique_ptr<CMS_ContentInfo, CmsFree>;
using Cert = std::unique_ptr<X509, X509Free>;
using Key = std::unique_ptr<EVP_PKEY, KeyFree>;
using Store = std::unique_ptr<X509_STORE, StoreFree>;
using StoreCtx = std::unique_ptr<X509_STORE_CTX, StoreCtxFree>;
using Certs = std::unique_ptr<STACK_OF(X509), StackFree>;
using Pkcs12 = std::unique_ptr<PKCS12, Pkcs12Free>;

UltraCryptResult Fail(UltraCryptResultCode code, const std::string& message) {
    return UltraCryptResult::Error(code, message);
}

// OpenSSL's reason for the last failure (never data), and its queue emptied
// so the next call starts clean.
std::string OpenSslError() {
    const unsigned long code = ERR_peek_last_error();
    ERR_clear_error();
    if (!code) return "unknown error";
    char text[256];
    ERR_error_string_n(code, text, sizeof text);
    return text;
}

Bio MemoryOf(const void* data, size_t size) {
    return Bio(BIO_new_mem_buf(data, static_cast<int>(size)));
}

std::vector<uint8_t> Drain(BIO* bio) {
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    return n > 0 && data ? std::vector<uint8_t>(data, data + n) : std::vector<uint8_t>();
}

std::string DrainText(BIO* bio) {
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    return n > 0 && data ? std::string(data, static_cast<size_t>(n)) : std::string();
}

// A secure-memory BIO's contents into a secure buffer.
UltraCryptSecureBuffer DrainSecure(BIO* bio) {
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    return n > 0 && data ? UltraCryptSecureBuffer(data, static_cast<size_t>(n)) : UltraCryptSecureBuffer();
}

Cms ReadCms(const std::vector<uint8_t>& der) {
    Bio in = MemoryOf(der.data(), der.size());
    Cms cms(in ? d2i_CMS_bio(in.get(), nullptr) : nullptr);
    ERR_clear_error();
    return cms;
}

// Every PEM certificate in `text`, in order.
std::vector<Cert> ReadCertificates(const std::string& text) {
    std::vector<Cert> out;
    Bio in = MemoryOf(text.data(), text.size());
    while (in) {
        X509* x = PEM_read_bio_X509(in.get(), nullptr, nullptr, nullptr);
        if (!x) break;
        out.emplace_back(x);
    }
    ERR_clear_error();   // the end of the text reads as an error
    return out;
}

// The first certificate: PEM, else one DER certificate.
Cert ReadFirstCertificate(const std::string& text) {
    std::vector<Cert> certs = ReadCertificates(text);
    if (!certs.empty()) return std::move(certs.front());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
    Cert der(d2i_X509(nullptr, &p, static_cast<long>(text.size())));
    ERR_clear_error();
    return der;
}

std::string PemOf(X509* cert) {
    Bio out(BIO_new(BIO_s_mem()));
    if (!out || PEM_write_bio_X509(out.get(), cert) != 1) return std::string();
    return DrainText(out.get());
}

Key ReadPrivateKey(const UltraCryptSecureBuffer& pem) {
    if (pem.IsEmpty()) return Key();
    Bio in = MemoryOf(pem.Data(), pem.GetSize());
    Key key(in ? PEM_read_bio_PrivateKey(in.get(), nullptr, nullptr, nullptr) : nullptr);
    ERR_clear_error();
    return key;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string NameEntry(const X509_NAME* name, int nid) {
    if (!name) return std::string();
    const int at = X509_NAME_get_index_by_NID(name, nid, -1);
    if (at < 0) return std::string();
    const ASN1_STRING* data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, at));
    unsigned char* utf8 = nullptr;
    const int n = ASN1_STRING_to_UTF8(&utf8, data);
    if (n < 0) return std::string();
    std::string out(reinterpret_cast<char*>(utf8), static_cast<size_t>(n));
    OPENSSL_free(utf8);
    return out;
}

// Seconds since 1970 of an ASN.1 time, without timegm (which Windows lacks).
int64_t Seconds(const ASN1_TIME* t) {
    if (!t) return 0;
    ASN1_TIME* epoch = ASN1_TIME_set(nullptr, 0);
    int days = 0, seconds = 0;
    const bool ok = epoch && ASN1_TIME_diff(&days, &seconds, epoch, t) == 1;
    ASN1_TIME_free(epoch);
    return ok ? static_cast<int64_t>(days) * 86400 + seconds : 0;
}

void Describe(X509* cert, UltraCryptCertificateInfo& info) {
    info = UltraCryptCertificateInfo{};
    info.commonName = NameEntry(X509_get_subject_name(cert), NID_commonName);
    if (STACK_OF(OPENSSL_STRING)* emails = X509_get1_email(cert)) {
        for (int i = 0; i < sk_OPENSSL_STRING_num(emails); ++i) {
            const std::string e = Lower(sk_OPENSSL_STRING_value(emails, i));
            if (std::find(info.emails.begin(), info.emails.end(), e) == info.emails.end()) info.emails.push_back(e);
        }
        X509_email_free(emails);
    }
    if (info.commonName.empty() && !info.emails.empty()) info.commonName = info.emails.front();
    const X509_NAME* issuer = X509_get_issuer_name(cert);
    info.issuer = NameEntry(issuer, NID_organizationName);
    const std::string issuerName = NameEntry(issuer, NID_commonName);
    if (info.issuer.empty()) info.issuer = issuerName;
    else if (!issuerName.empty() && issuerName != info.issuer) info.issuer += " (" + issuerName + ")";
    info.notBefore = Seconds(X509_get0_notBefore(cert));
    info.notAfter = Seconds(X509_get0_notAfter(cert));
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &length) == 1) {
        static const char* hex = "0123456789ABCDEF";
        for (unsigned int i = 0; i < length; ++i) {
            if (i) info.fingerprintSha256 += ':';
            info.fingerprintSha256 += hex[digest[i] >> 4];
            info.fingerprintSha256 += hex[digest[i] & 15];
        }
    }
    ERR_clear_error();
}

// The operating system's roots, read from it once (Windows' store and
// macOS's keychains are slow to walk), and none when SSL_CERT_FILE or
// SSL_CERT_DIR says where the roots are.
const std::vector<std::vector<uint8_t>>& PlatformRoots() {
    static const std::vector<std::vector<uint8_t>> roots = [] {
        if (std::getenv("SSL_CERT_FILE") || std::getenv("SSL_CERT_DIR")) return std::vector<std::vector<uint8_t>>();
        return ultracrypt_detail::PlatformRootsDer();
    }();
    return roots;
}

// `store` given the system's roots: OpenSSL's default store, then the
// operating system's (a root already there is skipped, not an error).
void AddSystemRoots(X509_STORE* store) {
    X509_STORE_set_default_paths(store);
    for (const std::vector<uint8_t>& der : PlatformRoots()) {
        const unsigned char* p = der.data();
        if (X509* root = d2i_X509(nullptr, &p, static_cast<long>(der.size()))) {
            X509_STORE_add_cert(store, root);
            X509_free(root);
        }
    }
    ERR_clear_error();
}

// The system's roots, read once: X509_STORE is reference-counted and safe to
// verify against from several threads.
X509_STORE* SystemRoots() {
    static std::once_flag once;
    static X509_STORE* store = nullptr;
    std::call_once(once, [] {
        store = X509_STORE_new();
        if (store) AddSystemRoots(store);
    });
    return store;
}

void FillSignature(CMS_ContentInfo* cms, UltraCryptCmsSignature& out) {
    out = UltraCryptCmsSignature{};
    if (STACK_OF(X509)* signers = CMS_get0_signers(cms)) {
        if (sk_X509_num(signers) > 0) out.signerCertificatePem = PemOf(sk_X509_value(signers, 0));
        sk_X509_free(signers);   // the stack only: the certificates are the structure's
    }
    Certs carried(CMS_get1_certs(cms));
    for (int i = 0; carried && i < sk_X509_num(carried.get()); ++i)
        out.certificatesPem.push_back(PemOf(sk_X509_value(carried.get(), i)));
    ERR_clear_error();
}

std::once_flag g_legacyProviders;

} // namespace

bool UltraCrypt_IsX509Available() { return true; }

UltraCryptResult UltraCrypt_ReadCertificate(const std::string& certificate, UltraCryptCertificateInfo& out) {
    out = UltraCryptCertificateInfo{};
    Cert cert = ReadFirstCertificate(certificate);
    if (!cert) return Fail(UltraCryptResultCode::InvalidArgument, "no certificate");
    Describe(cert.get(), out);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CheckCertificate(const std::string& certificatePem,
                                             const std::vector<std::string>& intermediatesPem,
                                             const UltraCryptTrustOptions& options,
                                             UltraCryptTrustCheck& out) {
    out = UltraCryptTrustCheck{};
    Cert cert = ReadFirstCertificate(certificatePem);
    if (!cert) return Fail(UltraCryptResultCode::InvalidArgument, "no certificate");

    // The system's store as it is, or one of this call's own when it adds roots.
    Store own;
    X509_STORE* roots = nullptr;
    if (options.useSystemRoots && options.extraRootsPem.empty()) {
        roots = SystemRoots();
    } else {
        own.reset(X509_STORE_new());
        if (own && options.useSystemRoots) AddSystemRoots(own.get());
        for (const std::string& pem : options.extraRootsPem)
            for (Cert& root : ReadCertificates(pem))
                if (own) X509_STORE_add_cert(own.get(), root.get());
        roots = own.get();
    }
    if (!roots) return Fail(UltraCryptResultCode::InternalError, "no certificate store: " + OpenSslError());

    Certs chain(sk_X509_new_null());
    for (const std::string& pem : intermediatesPem)
        for (Cert& c : ReadCertificates(pem))
            if (chain && sk_X509_push(chain.get(), c.get()) > 0) c.release();

    StoreCtx ctx(X509_STORE_CTX_new());
    if (!ctx || X509_STORE_CTX_init(ctx.get(), roots, cert.get(), chain.get()) != 1)
        return Fail(UltraCryptResultCode::InternalError, "cannot check the certificate: " + OpenSslError());
    switch (options.purpose) {
        case UltraCryptCertificatePurpose::EmailSigning:
            X509_STORE_CTX_set_purpose(ctx.get(), X509_PURPOSE_SMIME_SIGN); break;
        case UltraCryptCertificatePurpose::EmailEncryption:
            X509_STORE_CTX_set_purpose(ctx.get(), X509_PURPOSE_SMIME_ENCRYPT); break;
        case UltraCryptCertificatePurpose::Any: break;
    }
    if (options.at > 0) X509_VERIFY_PARAM_set_time(X509_STORE_CTX_get0_param(ctx.get()), static_cast<time_t>(options.at));

    const bool trusted = X509_verify_cert(ctx.get()) == 1;
    const int error = X509_STORE_CTX_get_error(ctx.get());
    ERR_clear_error();
    if (trusted) {
        out.trust = UltraCryptTrust::Trusted;
        return UltraCryptResult::Ok();
    }
    switch (error) {
        case X509_V_ERR_CERT_HAS_EXPIRED:     out.trust = UltraCryptTrust::Expired; break;
        case X509_V_ERR_CERT_NOT_YET_VALID:   out.trust = UltraCryptTrust::NotYetValid; break;
        case X509_V_ERR_CERT_REVOKED:         out.trust = UltraCryptTrust::Revoked; break;
        case X509_V_ERR_INVALID_PURPOSE:      out.trust = UltraCryptTrust::WrongPurpose; break;
        default:                              out.trust = UltraCryptTrust::Untrusted; break;
    }
    out.reason = X509_verify_cert_error_string(error);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_ImportPkcs12(const std::vector<uint8_t>& pkcs12,
                                         const UltraCryptSecureBuffer& password,
                                         UltraCryptX509Identity& out) {
    out = UltraCryptX509Identity{};
    // PKCS12_parse wants a NUL-terminated password.
    UltraCryptSecureBuffer pass(password.GetSize() + 1);
    if (!password.IsEmpty()) std::copy(password.Data(), password.Data() + password.GetSize(), pass.Data());
    const char* passText = reinterpret_cast<const char*>(pass.Data());

    auto parse = [&](EVP_PKEY** key, X509** cert, STACK_OF(X509)** ca) {
        Bio in = MemoryOf(pkcs12.data(), pkcs12.size());
        Pkcs12 p12(in ? d2i_PKCS12_bio(in.get(), nullptr) : nullptr);
        return p12 && PKCS12_parse(p12.get(), passText, key, cert, ca) == 1;
    };
    EVP_PKEY* rawKey = nullptr;
    X509* rawCert = nullptr;
    STACK_OF(X509)* rawCa = nullptr;
    bool ok = parse(&rawKey, &rawCert, &rawCa);
    if (!ok) {
        // Older files are encrypted with RC2 or 3DES, which OpenSSL 3 keeps in
        // its legacy provider; loading one provider ends the implicit default,
        // so the default is loaded alongside.
        ERR_clear_error();
        std::call_once(g_legacyProviders, [] {
            OSSL_PROVIDER_load(nullptr, "legacy");
            OSSL_PROVIDER_load(nullptr, "default");
            ERR_clear_error();
        });
        ok = parse(&rawKey, &rawCert, &rawCa);
    }
    Key key(rawKey);
    Cert cert(rawCert);
    Certs ca(rawCa);
    if (!ok) {
        const bool wrongPassword = ERR_GET_REASON(ERR_peek_last_error()) == PKCS12_R_MAC_VERIFY_FAILURE;
        const std::string why = OpenSslError();
        if (wrongPassword) return Fail(UltraCryptResultCode::AuthenticationFailed, "the password does not open this file");
        return Fail(UltraCryptResultCode::InvalidArgument, "not a PKCS #12 file it can read: " + why);
    }
    if (!key || !cert) return Fail(UltraCryptResultCode::InvalidArgument, "the file holds no certificate with its private key");

    Bio keyOut(BIO_new(BIO_s_secmem()));
    if (!keyOut || PEM_write_bio_PrivateKey(keyOut.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
        return Fail(UltraCryptResultCode::InternalError, "the private key cannot be written: " + OpenSslError());
    out.privateKeyPem = DrainSecure(keyOut.get());
    out.certificatePem = PemOf(cert.get());
    for (int i = 0; ca && i < sk_X509_num(ca.get()); ++i) out.certificatePem += PemOf(sk_X509_value(ca.get(), i));
    return UltraCryptResult::Ok();
}

UltraCryptCmsType UltraCrypt_CmsTypeOf(const std::vector<uint8_t>& der) {
    Cms cms = ReadCms(der);
    if (!cms) return UltraCryptCmsType::NotCms;
    switch (OBJ_obj2nid(CMS_get0_type(cms.get()))) {
        case NID_pkcs7_signed:    return UltraCryptCmsType::SignedData;
        case NID_pkcs7_enveloped: return UltraCryptCmsType::EnvelopedData;
        default:                  return UltraCryptCmsType::Other;
    }
}

UltraCryptResult UltraCrypt_CmsVerifyDetached(const std::vector<uint8_t>& content,
                                              const std::vector<uint8_t>& signatureDer,
                                              UltraCryptCmsSignature& out) {
    out = UltraCryptCmsSignature{};
    Cms cms = ReadCms(signatureDer);
    if (!cms || OBJ_obj2nid(CMS_get0_type(cms.get())) != NID_pkcs7_signed)
        return Fail(UltraCryptResultCode::InvalidArgument, "not a CMS signature");
    Bio data = MemoryOf(content.data(), content.size());
    // Whether it holds for these bytes, whoever made it: the signer's
    // certificate is judged by UltraCrypt_CheckCertificate.
    const bool holds = CMS_verify(cms.get(), nullptr, nullptr, data.get(), nullptr,
                                  CMS_NO_SIGNER_CERT_VERIFY | CMS_BINARY) == 1;
    const std::string why = holds ? std::string() : OpenSslError();
    FillSignature(cms.get(), out);
    if (!holds) return Fail(UltraCryptResultCode::SignatureInvalid, "the signature does not hold: " + why);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CmsVerifyAttached(const std::vector<uint8_t>& signedDer,
                                              std::vector<uint8_t>& outContent,
                                              UltraCryptCmsSignature& out) {
    out = UltraCryptCmsSignature{};
    outContent.clear();
    Cms cms = ReadCms(signedDer);
    if (!cms || OBJ_obj2nid(CMS_get0_type(cms.get())) != NID_pkcs7_signed)
        return Fail(UltraCryptResultCode::InvalidArgument, "not CMS signed data");
    Bio content(BIO_new(BIO_s_mem()));
    const bool holds = CMS_verify(cms.get(), nullptr, nullptr, nullptr, content.get(),
                                  CMS_NO_SIGNER_CERT_VERIFY | CMS_BINARY) == 1;
    const std::string why = holds ? std::string() : OpenSslError();
    if (holds) {
        outContent = Drain(content.get());
    } else {
        // The content as it is, unchecked, so it can be shown for what it is.
        Bio again(BIO_new(BIO_s_mem()));
        if (CMS_verify(cms.get(), nullptr, nullptr, nullptr, again.get(),
                       CMS_NO_SIGNER_CERT_VERIFY | CMS_NO_CONTENT_VERIFY | CMS_BINARY) == 1)
            outContent = Drain(again.get());
        ERR_clear_error();
    }
    FillSignature(cms.get(), out);
    if (!holds) return Fail(UltraCryptResultCode::SignatureInvalid, "the signature does not hold: " + why);
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CmsSignDetached(const std::vector<uint8_t>& content,
                                            const UltraCryptX509Identity& signer,
                                            std::vector<uint8_t>& outSignatureDer) {
    outSignatureDer.clear();
    std::vector<Cert> certs = ReadCertificates(signer.certificatePem);
    Key key = ReadPrivateKey(signer.privateKeyPem);
    if (certs.empty() || !key)
        return Fail(UltraCryptResultCode::InvalidArgument, "the certificate or its private key cannot be read");
    if (X509_check_private_key(certs.front().get(), key.get()) != 1) {
        ERR_clear_error();
        return Fail(UltraCryptResultCode::InvalidArgument, "the private key is not the certificate's");
    }
    // The chain goes with the signature, so a reader can get from the
    // certificate to a root it trusts.
    Certs chain(sk_X509_new_null());
    for (size_t i = 1; chain && i < certs.size(); ++i)
        if (sk_X509_push(chain.get(), certs[i].get()) > 0) certs[i].release();
    Bio data = MemoryOf(content.data(), content.size());
    Cms cms(CMS_sign(certs.front().get(), key.get(), chain.get(), data.get(), CMS_DETACHED | CMS_BINARY));
    if (!cms) return Fail(UltraCryptResultCode::InternalError, "signing failed: " + OpenSslError());
    Bio out(BIO_new(BIO_s_mem()));
    if (!out || i2d_CMS_bio(out.get(), cms.get()) != 1)
        return Fail(UltraCryptResultCode::InternalError, "signing failed: " + OpenSslError());
    outSignatureDer = Drain(out.get());
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CmsEncrypt(const std::vector<uint8_t>& content,
                                       const std::vector<std::string>& recipientsPem,
                                       std::vector<uint8_t>& outEnvelopedDer) {
    outEnvelopedDer.clear();
    if (recipientsPem.empty()) return Fail(UltraCryptResultCode::InvalidArgument, "no recipient");
    Certs recipients(sk_X509_new_null());
    if (!recipients) return Fail(UltraCryptResultCode::InternalError, OpenSslError());
    for (const std::string& pem : recipientsPem) {
        Cert cert = ReadFirstCertificate(pem);
        if (!cert) return Fail(UltraCryptResultCode::InvalidArgument, "a recipient's certificate cannot be read");
        if (sk_X509_push(recipients.get(), cert.get()) > 0) cert.release();
    }
    Bio data = MemoryOf(content.data(), content.size());
    Cms cms(CMS_encrypt(recipients.get(), data.get(), EVP_aes_256_cbc(), CMS_BINARY));
    if (!cms) return Fail(UltraCryptResultCode::InternalError, "encrypting failed: " + OpenSslError());
    Bio out(BIO_new(BIO_s_mem()));
    if (!out || i2d_CMS_bio(out.get(), cms.get()) != 1)
        return Fail(UltraCryptResultCode::InternalError, "encrypting failed: " + OpenSslError());
    outEnvelopedDer = Drain(out.get());
    return UltraCryptResult::Ok();
}

UltraCryptResult UltraCrypt_CmsDecrypt(const std::vector<uint8_t>& envelopedDer,
                                       const UltraCryptX509Identity& recipient,
                                       UltraCryptSecureBuffer& outContent) {
    outContent.Clear();
    Cms cms = ReadCms(envelopedDer);
    if (!cms || OBJ_obj2nid(CMS_get0_type(cms.get())) != NID_pkcs7_enveloped)
        return Fail(UltraCryptResultCode::InvalidArgument, "not CMS enveloped data");
    Cert cert = ReadFirstCertificate(recipient.certificatePem);
    Key key = ReadPrivateKey(recipient.privateKeyPem);
    if (!cert || !key)
        return Fail(UltraCryptResultCode::InvalidArgument, "the certificate or its private key cannot be read");
    Bio out(BIO_new(BIO_s_secmem()));
    if (!out || CMS_decrypt(cms.get(), key.get(), cert.get(), nullptr, out.get(), CMS_BINARY) != 1) {
        ERR_clear_error();
        return Fail(UltraCryptResultCode::DecryptionFailed, "not encrypted to this certificate, or it does not open with its key");
    }
    outContent = DrainSecure(out.get());
    return UltraCryptResult::Ok();
}

#else // !ULTRACRYPT_HAVE_X509

namespace {
UltraCryptResult NotBuilt() {
    return UltraCryptResult::Error(UltraCryptResultCode::BackendUnavailable,
                                   "UltraCrypt is built without OpenSSL 3: no X.509 or CMS");
}
} // namespace

bool UltraCrypt_IsX509Available() { return false; }
UltraCryptResult UltraCrypt_ReadCertificate(const std::string&, UltraCryptCertificateInfo& out) {
    out = UltraCryptCertificateInfo{};
    return NotBuilt();
}
UltraCryptResult UltraCrypt_CheckCertificate(const std::string&, const std::vector<std::string>&,
                                             const UltraCryptTrustOptions&, UltraCryptTrustCheck& out) {
    out = UltraCryptTrustCheck{};
    return NotBuilt();
}
UltraCryptResult UltraCrypt_ImportPkcs12(const std::vector<uint8_t>&, const UltraCryptSecureBuffer&,
                                         UltraCryptX509Identity& out) {
    out = UltraCryptX509Identity{};
    return NotBuilt();
}
UltraCryptCmsType UltraCrypt_CmsTypeOf(const std::vector<uint8_t>&) { return UltraCryptCmsType::NotCms; }
UltraCryptResult UltraCrypt_CmsVerifyDetached(const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                              UltraCryptCmsSignature& out) {
    out = UltraCryptCmsSignature{};
    return NotBuilt();
}
UltraCryptResult UltraCrypt_CmsVerifyAttached(const std::vector<uint8_t>&, std::vector<uint8_t>& outContent,
                                              UltraCryptCmsSignature& out) {
    outContent.clear();
    out = UltraCryptCmsSignature{};
    return NotBuilt();
}
UltraCryptResult UltraCrypt_CmsSignDetached(const std::vector<uint8_t>&, const UltraCryptX509Identity&,
                                            std::vector<uint8_t>& outSignatureDer) {
    outSignatureDer.clear();
    return NotBuilt();
}
UltraCryptResult UltraCrypt_CmsEncrypt(const std::vector<uint8_t>&, const std::vector<std::string>&,
                                       std::vector<uint8_t>& outEnvelopedDer) {
    outEnvelopedDer.clear();
    return NotBuilt();
}
UltraCryptResult UltraCrypt_CmsDecrypt(const std::vector<uint8_t>&, const UltraCryptX509Identity&,
                                       UltraCryptSecureBuffer& outContent) {
    outContent.Clear();
    return NotBuilt();
}

#endif
