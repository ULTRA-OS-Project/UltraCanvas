// core/UltraNet/UltraNetTls.cpp
// Public TLS-wrap entry points. Delegates the actual TLS work to the
// per-platform backend in ultranet_tls_platform:: (declared in
// UltraNetTlsImpl.h; implemented in OS/<Platform>/UltraNetTlsImpl.*).
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetTls.h"
#include "UltraNetSocketInternal.h"
#include "UltraNetTlsImpl.h"
#include "UltraNet/UltraNetMime.h"
#include "UltraNetSha256.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The TLS context belongs to the socket entry, which hands it to `Close`
// when the socket is closed, so Handshake and GetInfo below ask the socket
// for it (ultranet_internal::GetTlsCtx) rather than keeping a table of their
// own. One kept here would never learn that a connection had closed: it
// would grow by an entry per connection for the life of the process and
// answer later calls with a pointer that had already been freed.

UltraNetHandle UltraNet_TlsWrap(UltraNetHandle tcpHandle,
                                const std::string& serverName,
                                const UltraNetTlsOptions& options) {
    const std::intptr_t fd = ultranet_internal::GetTcpFd(tcpHandle);
    if (fd < 0) return UltraNetInvalidHandle;

    void* ctx = nullptr;
    UltraNetResult r = ultranet_tls_platform::Wrap(fd, serverName, options, &ctx);
    if (!r || !ctx) return UltraNetInvalidHandle;

    if (!ultranet_internal::AttachTlsToSocket(
            tcpHandle, ctx,
            &ultranet_tls_platform::Read,
            &ultranet_tls_platform::Write,
            &ultranet_tls_platform::Close)) {
        ultranet_tls_platform::Close(ctx);
        return UltraNetInvalidHandle;
    }
    return tcpHandle;
}

UltraNetResult UltraNet_TlsHandshake(UltraNetHandle handle) {
    void* ctx = ultranet_internal::GetTlsCtx(handle);
    if (!ctx) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                     "no TLS context attached to handle");
    }
    return ultranet_tls_platform::Handshake(ctx);
}

UltraNetTlsInfo UltraNet_TlsGetInfo(UltraNetHandle handle) {
    void* ctx = ultranet_internal::GetTlsCtx(handle);
    if (!ctx) return {};
    return ultranet_tls_platform::GetInfo(ctx);
}

UltraNetResult UltraNet_TlsSetCABundle(const std::string& caBundlePath) {
    ultranet_tls_platform::SetGlobalCABundle(caBundlePath);
    return UltraNetResult::Ok();
}

UltraNetResult UltraNet_TlsAddTrustedCert(const std::string& certPemData) {
    ultranet_tls_platform::AddGlobalTrustedCertPem(certPemData);
    return UltraNetResult::Ok();
}

// ============================================================================
// Public-key pins
// ============================================================================

namespace {

// One DER element at `pos`, inside [pos, end): its tag, and where its content
// starts and ends. Definite lengths only, of at most four bytes - all a
// certificate uses - and every length is checked against `end`, since the
// bytes come from whoever answered the connection.
struct DerElement {
    uint8_t tag = 0;
    size_t start = 0;          // first byte of the tag
    size_t contentStart = 0;
    size_t end = 0;            // one past the last content byte
};

bool ReadDer(const std::vector<uint8_t>& der, size_t pos, size_t end, DerElement& out) {
    if (end > der.size() || pos >= end || end - pos < 2) return false;
    out.tag = der[pos];
    out.start = pos;
    size_t at = pos + 1;
    size_t length = der[at++];
    if (length & 0x80) {
        const size_t bytes = length & 0x7F;
        if (bytes == 0 || bytes > 4 || end - at < bytes) return false;
        length = 0;
        for (size_t i = 0; i < bytes; ++i) length = (length << 8) | der[at++];
    }
    if (end - at < length) return false;
    out.contentStart = at;
    out.end = at + length;
    return true;
}

// The SubjectPublicKeyInfo of an X.509 certificate (RFC 5280 s4.1), tag and
// length included - which is what the pin hashes:
//   Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signature }
//   TBSCertificate ::= SEQUENCE { [0] version OPTIONAL, serialNumber,
//       signature, issuer, validity, subject, subjectPublicKeyInfo, ... }
bool SubjectPublicKeyInfo(const std::vector<uint8_t>& der, size_t& from, size_t& to) {
    constexpr uint8_t kSequence = 0x30;
    DerElement certificate, tbs, field;
    if (!ReadDer(der, 0, der.size(), certificate) || certificate.tag != kSequence) return false;
    if (!ReadDer(der, certificate.contentStart, certificate.end, tbs) || tbs.tag != kSequence)
        return false;

    size_t pos = tbs.contentStart;
    if (!ReadDer(der, pos, tbs.end, field)) return false;
    if (field.tag == 0xA0) {                        // explicit [0] version
        pos = field.end;
        if (!ReadDer(der, pos, tbs.end, field)) return false;
    }
    // serialNumber, signature, issuer, validity, subject - then the key.
    for (int skip = 0; skip < 5; ++skip) {
        pos = field.end;
        if (!ReadDer(der, pos, tbs.end, field)) return false;
    }
    if (field.tag != kSequence) return false;
    from = field.start;
    to = field.end;
    return true;
}

// The DER of the first certificate in `data`: `data` itself when it is not
// PEM, the decoded body of its first CERTIFICATE block when it is.
std::vector<uint8_t> CertificateDer(const std::vector<uint8_t>& data) {
    static const std::string kBegin = "-----BEGIN CERTIFICATE-----";
    static const std::string kEnd = "-----END CERTIFICATE-----";
    const std::string text(data.begin(), data.end());
    const size_t begin = text.find(kBegin);
    if (begin == std::string::npos) return data;
    const size_t bodyStart = begin + kBegin.size();
    const size_t end = text.find(kEnd, bodyStart);
    if (end == std::string::npos) return {};
    std::vector<uint8_t> der;
    if (!UltraNet_Base64Decode(text.substr(bodyStart, end - bodyStart), der)) return {};
    return der;
}

}  // namespace

std::string UltraNet_PublicKeyPinOf(const std::vector<uint8_t>& certificate) {
    const std::vector<uint8_t> der = CertificateDer(certificate);
    size_t from = 0, to = 0;
    if (der.empty() || !SubjectPublicKeyInfo(der, from, to)) return std::string();

    ultranet_internal::Sha256 sha;
    sha.Update(der.data() + from, to - from);
    std::vector<uint8_t> digest(32);
    sha.Final(digest.data());
    return "sha256//" + UltraNet_Base64Encode(digest, false);
}
