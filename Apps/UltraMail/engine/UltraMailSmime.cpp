// Apps/UltraMail/engine/UltraMailSmime.cpp
// S/MIME's byte work (UltraMailSecureBackends.h) on UltraCrypt's X.509 and
// CMS (UltraCrypt/UltraCryptX509.h): checking a signature - first whether it
// holds for the bytes, then whether the signer's certificate chains to a root
// the system trusts, for signing mail - opening enveloped data with the
// user's identity, signing and encrypting, and reading a PKCS #12 file. What
// is UltraMail's here is the wording: what each finding means to the reader.
// A build whose UltraCrypt has no X.509 (no OpenSSL 3) says S/MIME is not
// built.
// Version: 0.2.0 - on UltraCrypt's X.509 and CMS; UltraMail links no OpenSSL of its own
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSecureBackends.h"

#include "UltraCrypt/UltraCryptX509.h"

namespace UltraMail::secure {

namespace {

constexpr const char* kNotBuilt = "S/MIME is not built into this UltraMail (its UltraCrypt has no OpenSSL 3)";

std::vector<uint8_t> Bytes(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

UltraCryptX509Identity IdentityOf(const SmimeIdentity& identity) {
    UltraCryptX509Identity out;
    out.certificatePem = identity.certificatePem;
    out.privateKeyPem = UltraCryptSecureBuffer(identity.privateKeyPem.data(), identity.privateKeyPem.size());
    return out;
}

void NameSigner(const std::string& certificatePem, SignatureCheck& check) {
    UltraCryptCertificateInfo info;
    if (!UltraCrypt_ReadCertificate(certificatePem, info)) return;
    check.signerName = info.commonName;
    check.signerEmail = info.emails.empty() ? std::string() : info.emails.front();
    check.fingerprint = info.fingerprintSha256;
    check.issuer = info.issuer;
    check.validUntil = info.notAfter;
}

// The signer of a signature that holds: is the certificate vouched for by a
// trusted root, for signing mail, today?
void JudgeSigner(const UltraCryptCmsSignature& signature, SignatureCheck& check) {
    NameSigner(signature.signerCertificatePem, check);
    check.certificatePem = signature.signerCertificatePem;
    UltraCryptTrustOptions options;
    options.purpose = UltraCryptCertificatePurpose::EmailSigning;
    UltraCryptTrustCheck trust;
    if (!UltraCrypt_CheckCertificate(signature.signerCertificatePem, signature.certificatesPem, options, trust)) {
        check.verdict = SignatureVerdict::Unchecked;
        check.detail = "The signature holds, but its certificate cannot be read.";
        return;
    }
    switch (trust.trust) {
        case UltraCryptTrust::Trusted:
            check.verdict = SignatureVerdict::Valid;
            check.detail = "The signature holds, and the certificate is vouched for by an authority "
                           "this computer trusts.";
            break;
        case UltraCryptTrust::Expired:
            check.verdict = SignatureVerdict::Expired;
            check.detail = "The signature holds, but the certificate has expired.";
            break;
        case UltraCryptTrust::Revoked:
            check.verdict = SignatureVerdict::Revoked;
            check.detail = "The certificate was revoked.";
            break;
        case UltraCryptTrust::WrongPurpose:
            check.verdict = SignatureVerdict::Untrusted;
            check.detail = "The signature holds, but the certificate is not one for signing mail.";
            break;
        case UltraCryptTrust::NotYetValid:
        case UltraCryptTrust::Untrusted:
            check.verdict = SignatureVerdict::Untrusted;
            check.detail = "The signature holds, but the certificate is not vouched for here (" + trust.reason + ").";
            break;
    }
}

// A signature that did not hold, or named no certificate.
void Judge(const UltraCryptResult& verified, const UltraCryptCmsSignature& signature, SignatureCheck& check) {
    if (verified.code == UltraCryptResultCode::SignatureInvalid) {
        check.verdict = SignatureVerdict::Invalid;
        check.detail = "The message was changed after it was signed.";
        NameSigner(signature.signerCertificatePem, check);
    } else if (!verified) {
        check.detail = "The signature cannot be read: " + verified.message;
    } else if (signature.signerCertificatePem.empty()) {
        check.detail = "The signature names no certificate.";
    } else {
        JudgeSigner(signature, check);
    }
}

} // namespace

bool SmimeBuilt() { return UltraCrypt_IsX509Available(); }

SignatureCheck SmimeVerifyDetached(const std::string& content, const std::vector<uint8_t>& signature) {
    SignatureCheck check;
    check.method = SecureMethod::Smime;
    if (!SmimeBuilt()) {
        check.detail = std::string(kNotBuilt) + ".";
        return check;
    }
    UltraCryptCmsSignature found;
    const UltraCryptResult verified = UltraCrypt_CmsVerifyDetached(Bytes(content), signature, found);
    Judge(verified, found, check);
    return check;
}

bool SmimeIsSignedData(const std::vector<uint8_t>& der) {
    return UltraCrypt_CmsTypeOf(der) == UltraCryptCmsType::SignedData;
}

bool SmimeVerifyOpaque(const std::vector<uint8_t>& der, std::string& content, SignatureCheck& check) {
    check = SignatureCheck{};
    check.method = SecureMethod::Smime;
    if (!SmimeBuilt()) {
        check.detail = std::string(kNotBuilt) + ".";
        return false;
    }
    std::vector<uint8_t> bytes;
    UltraCryptCmsSignature found;
    const UltraCryptResult verified = UltraCrypt_CmsVerifyAttached(der, bytes, found);
    content.assign(bytes.begin(), bytes.end());
    Judge(verified, found, check);
    return !content.empty();
}

bool SmimeDecrypt(const std::vector<uint8_t>& der, const std::vector<SmimeIdentity>& identities,
                  std::string& content, std::string& error) {
    if (!SmimeBuilt()) {
        error = kNotBuilt;
        return false;
    }
    if (UltraCrypt_CmsTypeOf(der) != UltraCryptCmsType::EnvelopedData) {
        error = "the encrypted message cannot be read";
        return false;
    }
    if (identities.empty()) {
        error = "no S/MIME certificate of yours is set up in UltraMail (Account Settings)";
        return false;
    }
    for (const SmimeIdentity& identity : identities) {
        UltraCryptSecureBuffer plain;
        if (UltraCrypt_CmsDecrypt(der, IdentityOf(identity), plain)) {
            content.assign(reinterpret_cast<const char*>(plain.Data()), plain.GetSize());
            return true;
        }
    }
    error = "it was encrypted to a certificate that is not set up in UltraMail";
    return false;
}

bool SmimeSignDetached(const std::string& content, const SmimeIdentity& identity,
                       std::vector<uint8_t>& signature, std::string& error) {
    if (!SmimeBuilt()) {
        error = kNotBuilt;
        return false;
    }
    const UltraCryptResult signedIt = UltraCrypt_CmsSignDetached(Bytes(content), IdentityOf(identity), signature);
    if (!signedIt) {
        error = signedIt.code == UltraCryptResultCode::InvalidArgument
                    ? "the S/MIME certificate or its key cannot be read"
                    : "signing failed: " + signedIt.message;
        return false;
    }
    return true;
}

bool SmimeEncrypt(const std::string& content, const std::vector<std::string>& certificatesPem,
                  std::vector<uint8_t>& der, std::string& error) {
    if (!SmimeBuilt()) {
        error = kNotBuilt;
        return false;
    }
    const UltraCryptResult encrypted = UltraCrypt_CmsEncrypt(Bytes(content), certificatesPem, der);
    if (!encrypted) {
        error = encrypted.code == UltraCryptResultCode::InvalidArgument
                    ? "a recipient's certificate cannot be read"
                    : "encrypting failed: " + encrypted.message;
        return false;
    }
    return true;
}

bool SmimeImportPkcs12(const std::vector<uint8_t>& p12, const std::string& password,
                       SmimeIdentity& out, std::string& error) {
    if (!SmimeBuilt()) {
        error = kNotBuilt;
        return false;
    }
    UltraCryptX509Identity identity;
    const UltraCryptResult imported =
        UltraCrypt_ImportPkcs12(p12, UltraCryptSecureBuffer(password.data(), password.size()), identity);
    if (!imported) {
        error = imported.message;
        return false;
    }
    out.certificatePem = identity.certificatePem;
    out.privateKeyPem.assign(reinterpret_cast<const char*>(identity.privateKeyPem.Data()),
                             identity.privateKeyPem.GetSize());
    return true;
}

bool SmimeReadCertificate(const std::string& pem, CertificateInfo& out) {
    UltraCryptCertificateInfo info;
    if (!UltraCrypt_ReadCertificate(pem, info)) return false;
    out = CertificateInfo{};
    out.name = info.commonName;
    out.emails = info.emails;
    out.issuer = info.issuer;
    out.validUntil = info.notAfter;
    out.fingerprint = info.fingerprintSha256;
    return true;
}

} // namespace UltraMail::secure
