// Apps/UltraMail/engine/UltraMailSmime.cpp
// S/MIME's byte work on OpenSSL 3's CMS (UltraMailSecureBackends.h): checking
// a signature - first whether it holds for the bytes, then whether the
// signer's certificate chains to a root the system trusts - opening enveloped
// data with the user's identity, signing and encrypting, and reading a
// PKCS #12 file. Without OpenSSL (ULTRAMAIL_HAVE_SMIME unset) every function
// says it is not built.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecureBackends.h"

#if defined(ULTRAMAIL_HAVE_SMIME)

#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/provider.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>

namespace UltraMail::secure {

namespace {

struct BioFree { void operator()(BIO* b) const { BIO_free_all(b); } };
struct CmsFree { void operator()(CMS_ContentInfo* c) const { CMS_ContentInfo_free(c); } };
struct X509Free { void operator()(X509* x) const { X509_free(x); } };
struct KeyFree { void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); } };
struct StoreFree { void operator()(X509_STORE* s) const { X509_STORE_free(s); } };
struct StackFree { void operator()(STACK_OF(X509)* s) const { sk_X509_pop_free(s, X509_free); } };
using Bio = std::unique_ptr<BIO, BioFree>;
using Cms = std::unique_ptr<CMS_ContentInfo, CmsFree>;
using Cert = std::unique_ptr<X509, X509Free>;
using Key = std::unique_ptr<EVP_PKEY, KeyFree>;
using Certs = std::unique_ptr<STACK_OF(X509), StackFree>;

Bio MemoryOf(const void* data, size_t size) {
    return Bio(BIO_new_mem_buf(data, static_cast<int>(size)));
}

std::string Drain(BIO* bio) {
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    return n > 0 && data ? std::string(data, static_cast<size_t>(n)) : std::string();
}

std::string OpenSslError() {
    const unsigned long code = ERR_get_error();
    ERR_clear_error();
    if (!code) return "unknown error";
    char text[256];
    ERR_error_string_n(code, text, sizeof text);
    return text;
}

Cms ReadCms(const std::vector<uint8_t>& der) {
    Bio in = MemoryOf(der.data(), der.size());
    return Cms(in ? d2i_CMS_bio(in.get(), nullptr) : nullptr);
}

std::vector<Cert> ReadCertificates(const std::string& pem) {
    std::vector<Cert> out;
    Bio in = MemoryOf(pem.data(), pem.size());
    while (in) {
        X509* x = PEM_read_bio_X509(in.get(), nullptr, nullptr, nullptr);
        if (!x) break;
        out.emplace_back(x);
    }
    ERR_clear_error();   // the end of the PEM reads as an error
    return out;
}

std::string PemOf(X509* cert) {
    Bio out(BIO_new(BIO_s_mem()));
    if (!out || PEM_write_bio_X509(out.get(), cert) != 1) return std::string();
    return Drain(out.get());
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string NameEntry(X509_NAME* name, int nid) {
    if (!name) return std::string();
    const int at = X509_NAME_get_index_by_NID(name, nid, -1);
    if (at < 0) return std::string();
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, at));
    unsigned char* utf8 = nullptr;
    const int n = ASN1_STRING_to_UTF8(&utf8, data);
    if (n < 0) return std::string();
    std::string out(reinterpret_cast<char*>(utf8), static_cast<size_t>(n));
    OPENSSL_free(utf8);
    return out;
}

void Describe(X509* cert, CertificateInfo& info) {
    info = CertificateInfo{};
    X509_NAME* subject = X509_get_subject_name(cert);
    info.name = NameEntry(subject, NID_commonName);
    if (STACK_OF(OPENSSL_STRING)* emails = X509_get1_email(cert)) {
        for (int i = 0; i < sk_OPENSSL_STRING_num(emails); ++i) {
            const std::string e = Lower(sk_OPENSSL_STRING_value(emails, i));
            if (std::find(info.emails.begin(), info.emails.end(), e) == info.emails.end()) info.emails.push_back(e);
        }
        X509_email_free(emails);
    }
    if (info.name.empty() && !info.emails.empty()) info.name = info.emails.front();
    X509_NAME* issuer = X509_get_issuer_name(cert);
    info.issuer = NameEntry(issuer, NID_organizationName);
    const std::string issuerName = NameEntry(issuer, NID_commonName);
    if (info.issuer.empty()) info.issuer = issuerName;
    else if (!issuerName.empty() && issuerName != info.issuer) info.issuer += " (" + issuerName + ")";
    std::tm until{};
    if (ASN1_TIME_to_tm(X509_get0_notAfter(cert), &until) == 1) {
#if defined(_WIN32)
        info.validUntil = static_cast<int64_t>(_mkgmtime(&until));
#else
        info.validUntil = static_cast<int64_t>(timegm(&until));
#endif
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &length) == 1) {
        static const char* hex = "0123456789ABCDEF";
        for (unsigned int i = 0; i < length; ++i) {
            if (i) info.fingerprint += ':';
            info.fingerprint += hex[digest[i] >> 4];
            info.fingerprint += hex[digest[i] & 15];
        }
    }
}

// The system's trusted roots, read once.
X509_STORE* TrustedRoots() {
    static std::once_flag once;
    static X509_STORE* store = nullptr;
    std::call_once(once, [] {
        store = X509_STORE_new();
        if (store) X509_STORE_set_default_paths(store);
        ERR_clear_error();
    });
    return store;
}

// The signer's certificate, already shown to have made a signature that
// holds: is it vouched for by a trusted root, for signing mail, today?
void JudgeSigner(CMS_ContentInfo* cms, X509* signer, SignatureCheck& check) {
    CertificateInfo info;
    Describe(signer, info);
    check.signerName = info.name;
    check.signerEmail = info.emails.empty() ? std::string() : info.emails.front();
    check.fingerprint = info.fingerprint;
    check.issuer = info.issuer;
    check.validUntil = info.validUntil;
    check.certificatePem = PemOf(signer);

    X509_STORE* roots = TrustedRoots();
    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    Certs intermediates(CMS_get1_certs(cms));
    int error = X509_V_ERR_UNSPECIFIED;
    bool trusted = false;
    if (roots && ctx && X509_STORE_CTX_init(ctx, roots, signer, intermediates.get()) == 1) {
        X509_STORE_CTX_set_purpose(ctx, X509_PURPOSE_SMIME_SIGN);
        trusted = X509_verify_cert(ctx) == 1;
        error = X509_STORE_CTX_get_error(ctx);
    }
    if (ctx) X509_STORE_CTX_free(ctx);
    ERR_clear_error();
    if (trusted) {
        check.verdict = SignatureVerdict::Valid;
        check.detail = "The signature holds, and the certificate is vouched for by an authority "
                       "this computer trusts.";
    } else if (error == X509_V_ERR_CERT_HAS_EXPIRED) {
        check.verdict = SignatureVerdict::Expired;
        check.detail = "The signature holds, but the certificate has expired.";
    } else if (error == X509_V_ERR_CERT_REVOKED) {
        check.verdict = SignatureVerdict::Revoked;
        check.detail = "The certificate was revoked.";
    } else {
        check.verdict = SignatureVerdict::Untrusted;
        check.detail = std::string("The signature holds, but the certificate is not vouched for here (") +
                       X509_verify_cert_error_string(error) + ").";
    }
}

std::once_flag g_legacy;

} // namespace

bool SmimeBuilt() { return true; }

SignatureCheck SmimeVerifyDetached(const std::string& content, const std::vector<uint8_t>& signature) {
    SignatureCheck check;
    check.method = SecureMethod::Smime;
    Cms cms = ReadCms(signature);
    if (!cms) {
        check.detail = "The signature cannot be read: " + OpenSslError();
        return check;
    }
    Bio data = MemoryOf(content.data(), content.size());
    // First whether the signature holds for these bytes, whoever made it.
    const bool holds = CMS_verify(cms.get(), nullptr, nullptr, data.get(), nullptr,
                                  CMS_NO_SIGNER_CERT_VERIFY | CMS_BINARY) == 1;
    STACK_OF(X509)* signers = CMS_get0_signers(cms.get());
    X509* signer = signers && sk_X509_num(signers) > 0 ? sk_X509_value(signers, 0) : nullptr;
    if (!holds) {
        check.verdict = SignatureVerdict::Invalid;
        check.detail = "The message was changed after it was signed (" + OpenSslError() + ").";
        if (signer) {
            CertificateInfo info;
            Describe(signer, info);
            check.signerName = info.name;
            check.signerEmail = info.emails.empty() ? std::string() : info.emails.front();
        }
        if (signers) sk_X509_free(signers);
        return check;
    }
    if (!signer) {
        check.detail = "The signature names no certificate.";
        if (signers) sk_X509_free(signers);
        return check;
    }
    JudgeSigner(cms.get(), signer, check);
    sk_X509_free(signers);
    return check;
}

bool SmimeIsSignedData(const std::vector<uint8_t>& der) {
    Cms cms = ReadCms(der);
    return cms && OBJ_obj2nid(CMS_get0_type(cms.get())) == NID_pkcs7_signed;
}

bool SmimeVerifyOpaque(const std::vector<uint8_t>& der, std::string& content, SignatureCheck& check) {
    check = SignatureCheck{};
    check.method = SecureMethod::Smime;
    Cms cms = ReadCms(der);
    if (!cms) {
        check.detail = "The signed message cannot be read: " + OpenSslError();
        return false;
    }
    Bio out(BIO_new(BIO_s_mem()));
    const bool holds = CMS_verify(cms.get(), nullptr, nullptr, nullptr, out.get(),
                                  CMS_NO_SIGNER_CERT_VERIFY | CMS_BINARY) == 1;
    content = Drain(out.get());
    STACK_OF(X509)* signers = CMS_get0_signers(cms.get());
    X509* signer = signers && sk_X509_num(signers) > 0 ? sk_X509_value(signers, 0) : nullptr;
    if (!holds) {
        check.verdict = SignatureVerdict::Invalid;
        check.detail = "The message was changed after it was signed (" + OpenSslError() + ").";
        // The content itself, unchecked, is still the message.
        Bio again(BIO_new(BIO_s_mem()));
        if (CMS_verify(cms.get(), nullptr, nullptr, nullptr, again.get(),
                       CMS_NO_SIGNER_CERT_VERIFY | CMS_NO_CONTENT_VERIFY | CMS_BINARY) == 1)
            content = Drain(again.get());
        ERR_clear_error();
    } else if (signer) {
        JudgeSigner(cms.get(), signer, check);
    } else {
        check.detail = "The signature names no certificate.";
    }
    if (signers) sk_X509_free(signers);
    return !content.empty();
}

bool SmimeDecrypt(const std::vector<uint8_t>& der, const std::vector<SmimeIdentity>& identities,
                  std::string& content, std::string& error) {
    Cms cms = ReadCms(der);
    if (!cms) {
        error = "the encrypted message cannot be read: " + OpenSslError();
        return false;
    }
    if (identities.empty()) {
        error = "no S/MIME certificate of yours is set up in UltraMail (Account Settings)";
        return false;
    }
    for (const SmimeIdentity& identity : identities) {
        std::vector<Cert> certs = ReadCertificates(identity.certificatePem);
        Bio keyIn = MemoryOf(identity.privateKeyPem.data(), identity.privateKeyPem.size());
        Key key(keyIn ? PEM_read_bio_PrivateKey(keyIn.get(), nullptr, nullptr, nullptr) : nullptr);
        if (certs.empty() || !key) continue;
        Bio out(BIO_new(BIO_s_mem()));
        if (CMS_decrypt(cms.get(), key.get(), certs.front().get(), nullptr, out.get(), CMS_BINARY) == 1) {
            content = Drain(out.get());
            ERR_clear_error();
            return true;
        }
        ERR_clear_error();
    }
    error = "it was encrypted to a certificate that is not set up in UltraMail";
    return false;
}

bool SmimeSignDetached(const std::string& content, const SmimeIdentity& identity,
                       std::vector<uint8_t>& signature, std::string& error) {
    std::vector<Cert> certs = ReadCertificates(identity.certificatePem);
    Bio keyIn = MemoryOf(identity.privateKeyPem.data(), identity.privateKeyPem.size());
    Key key(keyIn ? PEM_read_bio_PrivateKey(keyIn.get(), nullptr, nullptr, nullptr) : nullptr);
    if (certs.empty() || !key) {
        error = "the S/MIME certificate or its key cannot be read";
        ERR_clear_error();
        return false;
    }
    // The chain goes with the signature, so a reader can get from the
    // certificate to a root it trusts.
    Certs chain(sk_X509_new_null());
    for (size_t i = 1; i < certs.size(); ++i) {
        X509_up_ref(certs[i].get());
        sk_X509_push(chain.get(), certs[i].get());
    }
    Bio data = MemoryOf(content.data(), content.size());
    Cms cms(CMS_sign(certs.front().get(), key.get(), chain.get(), data.get(), CMS_DETACHED | CMS_BINARY));
    if (!cms) {
        error = "signing failed: " + OpenSslError();
        return false;
    }
    Bio out(BIO_new(BIO_s_mem()));
    if (i2d_CMS_bio(out.get(), cms.get()) != 1) {
        error = "signing failed: " + OpenSslError();
        return false;
    }
    const std::string der = Drain(out.get());
    signature.assign(der.begin(), der.end());
    return true;
}

bool SmimeEncrypt(const std::string& content, const std::vector<std::string>& certificatesPem,
                  std::vector<uint8_t>& der, std::string& error) {
    Certs recipients(sk_X509_new_null());
    for (const std::string& pem : certificatesPem) {
        std::vector<Cert> certs = ReadCertificates(pem);
        if (certs.empty()) {
            error = "a recipient's certificate cannot be read";
            return false;
        }
        sk_X509_push(recipients.get(), certs.front().release());
    }
    Bio data = MemoryOf(content.data(), content.size());
    // AES-256-CBC: what every S/MIME mail program reads (RFC 8551 asks for
    // AES-256-GCM in AuthEnvelopedData, which most cannot open yet).
    Cms cms(CMS_encrypt(recipients.get(), data.get(), EVP_aes_256_cbc(), CMS_BINARY));
    if (!cms) {
        error = "encrypting failed: " + OpenSslError();
        return false;
    }
    Bio out(BIO_new(BIO_s_mem()));
    if (i2d_CMS_bio(out.get(), cms.get()) != 1) {
        error = "encrypting failed: " + OpenSslError();
        return false;
    }
    const std::string bytes = Drain(out.get());
    der.assign(bytes.begin(), bytes.end());
    return true;
}

bool SmimeImportPkcs12(const std::vector<uint8_t>& p12bytes, const std::string& password,
                       SmimeIdentity& out, std::string& error) {
    auto parse = [&](EVP_PKEY** key, X509** cert, STACK_OF(X509)** ca) {
        Bio in = MemoryOf(p12bytes.data(), p12bytes.size());
        PKCS12* p12 = in ? d2i_PKCS12_bio(in.get(), nullptr) : nullptr;
        if (!p12) return false;
        const bool ok = PKCS12_parse(p12, password.c_str(), key, cert, ca) == 1;
        PKCS12_free(p12);
        return ok;
    };
    EVP_PKEY* rawKey = nullptr;
    X509* rawCert = nullptr;
    STACK_OF(X509)* rawCa = nullptr;
    bool ok = parse(&rawKey, &rawCert, &rawCa);
    if (!ok) {
        // Older files are encrypted with RC2 or 3DES, which OpenSSL 3 keeps in
        // its legacy provider.
        ERR_clear_error();
        std::call_once(g_legacy, [] {
            OSSL_PROVIDER_load(nullptr, "legacy");
            OSSL_PROVIDER_load(nullptr, "default");
        });
        ok = parse(&rawKey, &rawCert, &rawCa);
    }
    Key key(rawKey);
    Cert cert(rawCert);
    Certs ca(rawCa);
    if (!ok) {
        const unsigned long code = ERR_peek_last_error();
        error = ERR_GET_REASON(code) == PKCS12_R_MAC_VERIFY_FAILURE
                    ? std::string("the password does not open this file")
                    : "the file cannot be read as PKCS #12: " + OpenSslError();
        ERR_clear_error();
        return false;
    }
    if (!key || !cert) {
        error = "the file holds no certificate with its private key";
        return false;
    }
    Bio keyOut(BIO_new(BIO_s_mem()));
    if (PEM_write_bio_PrivateKey(keyOut.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        error = "the private key cannot be read: " + OpenSslError();
        return false;
    }
    out.privateKeyPem = Drain(keyOut.get());
    out.certificatePem = PemOf(cert.get());
    for (int i = 0; ca && i < sk_X509_num(ca.get()); ++i) out.certificatePem += PemOf(sk_X509_value(ca.get(), i));
    return true;
}

bool SmimeReadCertificate(const std::string& pem, CertificateInfo& out) {
    std::vector<Cert> certs = ReadCertificates(pem);
    if (certs.empty()) return false;
    Describe(certs.front().get(), out);
    return true;
}

} // namespace UltraMail::secure

#else // !ULTRAMAIL_HAVE_SMIME

namespace UltraMail::secure {

namespace {
SignatureCheck NotBuilt() {
    SignatureCheck check;
    check.method = SecureMethod::Smime;
    check.detail = "S/MIME is not built into this UltraMail (no OpenSSL).";
    return check;
}
} // namespace

bool SmimeBuilt() { return false; }
SignatureCheck SmimeVerifyDetached(const std::string&, const std::vector<uint8_t>&) { return NotBuilt(); }
bool SmimeVerifyOpaque(const std::vector<uint8_t>&, std::string&, SignatureCheck& check) {
    check = NotBuilt();
    return false;
}
bool SmimeIsSignedData(const std::vector<uint8_t>&) { return false; }
bool SmimeDecrypt(const std::vector<uint8_t>&, const std::vector<SmimeIdentity>&, std::string&, std::string& error) {
    error = "S/MIME is not built into this UltraMail (no OpenSSL)";
    return false;
}
bool SmimeSignDetached(const std::string&, const SmimeIdentity&, std::vector<uint8_t>&, std::string& error) {
    error = "S/MIME is not built into this UltraMail (no OpenSSL)";
    return false;
}
bool SmimeEncrypt(const std::string&, const std::vector<std::string>&, std::vector<uint8_t>&, std::string& error) {
    error = "S/MIME is not built into this UltraMail (no OpenSSL)";
    return false;
}
bool SmimeImportPkcs12(const std::vector<uint8_t>&, const std::string&, SmimeIdentity&, std::string& error) {
    error = "S/MIME is not built into this UltraMail (no OpenSSL)";
    return false;
}
bool SmimeReadCertificate(const std::string&, CertificateInfo&) { return false; }

} // namespace UltraMail::secure

#endif
