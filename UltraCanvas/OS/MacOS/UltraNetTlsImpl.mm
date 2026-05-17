// OS/MacOS/UltraNetTlsImpl.mm
// SecureTransport-backed TLS wrap for the UltraNet_Tls* surface on macOS.
// Implements ultranet_tls_platform:: declared in core/UltraNet/UltraNetTlsImpl.h.
//
// Note: SecureTransport is deprecated since macOS 10.15 in favour of
// Network.framework, but it remains functional through macOS 14/15 and
// matches our "zero extra dependencies" rule. A Network.framework backend
// can replace this file later without API changes.
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetTlsImpl.h"

#if defined(__APPLE__)

#import <CommonCrypto/CommonDigest.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Security/SecCertificate.h>
#import <Security/SecImportExport.h>
#import <Security/SecPolicy.h>
#import <Security/SecTrust.h>
#import <Security/SecureTransport.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// Silence the deprecation warnings for SecureTransport — see file header.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace ultranet_tls_platform {
namespace {

std::mutex   g_globalMutex;
std::string  g_globalCaBundle;
std::string  g_globalExtraPem;

struct Ctx {
    SSLContextRef ssl = nullptr;
    int           fd  = -1;
    bool          handshakeComplete = false;
    bool          verifyPeer        = true;
    bool          verifyHost        = true;
    std::string   sni;
};

OSStatus SocketRead(SSLConnectionRef connection, void* data, size_t* dataLen) {
    int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(connection));
    size_t want = *dataLen;
    size_t got  = 0;
    while (got < want) {
        ssize_t n = ::recv(fd, static_cast<char*>(data) + got, want - got, 0);
        if (n == 0) { *dataLen = got; return errSSLClosedGraceful; }
        if (n < 0) {
            *dataLen = got;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
            return errSecIO;
        }
        got += static_cast<size_t>(n);
    }
    *dataLen = got;
    return errSecSuccess;
}

OSStatus SocketWrite(SSLConnectionRef connection, const void* data, size_t* dataLen) {
    int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(connection));
    size_t want = *dataLen;
    size_t put  = 0;
    while (put < want) {
        ssize_t n = ::send(fd, static_cast<const char*>(data) + put, want - put, 0);
        if (n < 0) {
            *dataLen = put;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return errSSLWouldBlock;
            return errSecIO;
        }
        put += static_cast<size_t>(n);
    }
    *dataLen = put;
    return errSecSuccess;
}

SSLProtocol MapMinVersion(UltraNetTlsVersion v) {
    switch (v) {
        case UltraNetTlsVersion::Tls10: return kTLSProtocol1;
        case UltraNetTlsVersion::Tls11: return kTLSProtocol11;
        case UltraNetTlsVersion::Tls12: return kTLSProtocol12;
        case UltraNetTlsVersion::Tls13: return kTLSProtocol13;
        case UltraNetTlsVersion::Auto:
        default:                        return kTLSProtocol12;
    }
}

SSLProtocol MapMaxVersion(UltraNetTlsVersion v) {
    switch (v) {
        case UltraNetTlsVersion::Tls10: return kTLSProtocol1;
        case UltraNetTlsVersion::Tls11: return kTLSProtocol11;
        case UltraNetTlsVersion::Tls12: return kTLSProtocol12;
        case UltraNetTlsVersion::Tls13: return kTLSProtocol13;
        case UltraNetTlsVersion::Auto:
        default:                        return kTLSProtocol13;
    }
}

std::string CFStringToUtf8(CFStringRef s) {
    if (!s) return {};
    CFIndex len = CFStringGetLength(s);
    CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<size_t>(maxBytes), '\0');
    if (!CFStringGetCString(s, out.data(), maxBytes, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string CertSubject(SecCertificateRef cert) {
    CFStringRef s = SecCertificateCopySubjectSummary(cert);
    std::string out = CFStringToUtf8(s);
    if (s) CFRelease(s);
    return out;
}

std::string Sha256Fingerprint(SecCertificateRef cert) {
    CFDataRef der = SecCertificateCopyData(cert);
    if (!der) return {};
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(CFDataGetBytePtr(der),
              static_cast<CC_LONG>(CFDataGetLength(der)),
              digest);
    CFRelease(der);
    std::string out;
    out.reserve(CC_SHA256_DIGEST_LENGTH * 3);
    for (std::size_t i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02x%s", digest[i],
                      i + 1 < CC_SHA256_DIGEST_LENGTH ? ":" : "");
        out += hex;
    }
    return out;
}

OSStatus VerifyPeer(Ctx* c) {
    SecTrustRef trust = nullptr;
    OSStatus s = SSLCopyPeerTrust(c->ssl, &trust);
    if (s != errSecSuccess || !trust) return s ? s : errSecBadReq;

    if (c->verifyHost && !c->sni.empty()) {
        CFStringRef host = CFStringCreateWithCString(nullptr, c->sni.c_str(),
                                                     kCFStringEncodingUTF8);
        if (host) {
            SecPolicyRef pol = SecPolicyCreateSSL(true, host);
            if (pol) {
                const void* values[] = { pol };
                CFArrayRef  policies = CFArrayCreate(
                    nullptr, values, 1, &kCFTypeArrayCallBacks);
                SecTrustSetPolicies(trust, policies);
                CFRelease(policies);
                CFRelease(pol);
            }
            CFRelease(host);
        }
    }
    CFErrorRef err = nullptr;
    bool ok = SecTrustEvaluateWithError(trust, &err);
    if (err) CFRelease(err);
    CFRelease(trust);
    return ok ? errSecSuccess : errSSLPeerBadCert;
}

} // namespace

void SetGlobalCABundle(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_globalMutex);
    g_globalCaBundle = path;
}

void AddGlobalTrustedCertPem(const std::string& pem) {
    std::lock_guard<std::mutex> lk(g_globalMutex);
    g_globalExtraPem += pem;
    if (!g_globalExtraPem.empty() && g_globalExtraPem.back() != '\n') {
        g_globalExtraPem.push_back('\n');
    }
}

UltraNetResult Wrap(std::intptr_t fd,
                    const std::string& sni,
                    const UltraNetTlsOptions& opt,
                    void** outCtx) {
    if (fd < 0) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidHandle, "bad fd");
    }
    SSLContextRef ssl = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
    if (!ssl) {
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     "SSLCreateContext failed");
    }
    SSLSetIOFuncs(ssl, SocketRead, SocketWrite);
    SSLSetConnection(ssl, reinterpret_cast<SSLConnectionRef>(
                              static_cast<std::intptr_t>(fd)));

    const std::string sniHost = opt.sniHostname.empty() ? sni : opt.sniHostname;
    if (!sniHost.empty()) {
        SSLSetPeerDomainName(ssl, sniHost.c_str(), sniHost.size());
    }
    SSLSetProtocolVersionMin(ssl, MapMinVersion(opt.minVersion));
    SSLSetProtocolVersionMax(ssl, MapMaxVersion(opt.maxVersion));

    // We do our own verification post-handshake so we can swap in the global
    // CA bundle / extra trust roots if needed.
    SSLSetSessionOption(ssl, kSSLSessionOptionBreakOnServerAuth, true);

    auto* c = new Ctx{};
    c->ssl        = ssl;
    c->fd         = static_cast<int>(fd);
    c->verifyPeer = opt.verifyPeer;
    c->verifyHost = opt.verifyHostname;
    c->sni        = sniHost;
    *outCtx = c;
    return UltraNetResult::Ok();
}

UltraNetResult Handshake(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState, "no ssl");
    }
    for (;;) {
        OSStatus s = SSLHandshake(c->ssl);
        if (s == errSecSuccess) {
            c->handshakeComplete = true;
            return UltraNetResult::Ok();
        }
        if (s == errSSLWouldBlock) continue;
        if (s == errSSLPeerAuthCompleted) {
            // BreakOnServerAuth fires here. Run our verification, then continue.
            if (c->verifyPeer) {
                OSStatus v = VerifyPeer(c);
                if (v != errSecSuccess) {
                    return UltraNetResult::Error(
                        UltraNetResultCode::TlsCertificateInvalid,
                        "peer certificate verification failed");
                }
            }
            continue;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "SecureTransport status %d", (int)s);
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed, buf);
    }
}

UltraNetTlsInfo GetInfo(void* ctx) {
    UltraNetTlsInfo info;
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return info;

    SSLProtocol p = kSSLProtocolUnknown;
    SSLGetNegotiatedProtocolVersion(c->ssl, &p);
    switch (p) {
        case kTLSProtocol1:  info.version = UltraNetTlsVersion::Tls10; break;
        case kTLSProtocol11: info.version = UltraNetTlsVersion::Tls11; break;
        case kTLSProtocol12: info.version = UltraNetTlsVersion::Tls12; break;
        case kTLSProtocol13: info.version = UltraNetTlsVersion::Tls13; break;
        default:             info.version = UltraNetTlsVersion::Auto;  break;
    }
    SSLCipherSuite cs = SSL_NO_SUCH_CIPHERSUITE;
    if (SSLGetNegotiatedCipher(c->ssl, &cs) == errSecSuccess) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "0x%04x", (unsigned)cs);
        info.cipherSuite = buf;
    }
    SecTrustRef trust = nullptr;
    if (SSLCopyPeerTrust(c->ssl, &trust) == errSecSuccess && trust) {
        CFIndex count = SecTrustGetCertificateCount(trust);
        if (count > 0) {
            SecCertificateRef leaf = SecTrustGetCertificateAtIndex(trust, 0);
            if (leaf) {
                info.peerCertificateSubject     = CertSubject(leaf);
                info.peerCertificateFingerprint = Sha256Fingerprint(leaf);
            }
        }
        CFRelease(trust);
    }
    return info;
}

int Read(void* ctx, char* buf, std::size_t len, int /*timeoutMs*/) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return -1;
    size_t processed = 0;
    OSStatus s = SSLRead(c->ssl, buf, len, &processed);
    if (s == errSecSuccess || s == errSSLWouldBlock) {
        return static_cast<int>(processed);
    }
    if (s == errSSLClosedGraceful) return 0;
    return -1;
}

int Write(void* ctx, const char* buf, std::size_t len) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return -1;
    size_t processed = 0;
    OSStatus s = SSLWrite(c->ssl, buf, len, &processed);
    if (s == errSecSuccess) return static_cast<int>(processed);
    return -1;
}

void Close(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c) return;
    if (c->ssl) {
        if (c->handshakeComplete) SSLClose(c->ssl);
        CFRelease(c->ssl);
    }
    delete c;
}

} // namespace ultranet_tls_platform

#pragma clang diagnostic pop

#endif // __APPLE__
