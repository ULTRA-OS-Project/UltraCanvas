// OS/Linux/UltraNetTlsImpl.cpp
// OpenSSL-backed TLS wrap for the UltraNet_Tls* surface on Linux.
// Implements ultranet_tls_platform:: declared in core/UltraNet/UltraNetTlsImpl.h.
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetTlsImpl.h"

#ifdef __linux__

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace ultranet_tls_platform {
namespace {

std::mutex   g_globalMutex;
std::string  g_globalCaBundle;
std::string  g_globalExtraPem;

struct Ctx {
    SSL_CTX* sslCtx = nullptr;
    SSL*     ssl    = nullptr;
    int      fd     = -1;
    bool     verifyHost = true;
    std::string sni;
};

int MapTlsVersion(UltraNetTlsVersion v) {
    switch (v) {
        case UltraNetTlsVersion::Tls10: return TLS1_VERSION;
        case UltraNetTlsVersion::Tls11: return TLS1_1_VERSION;
        case UltraNetTlsVersion::Tls12: return TLS1_2_VERSION;
        case UltraNetTlsVersion::Tls13: return TLS1_3_VERSION;
        case UltraNetTlsVersion::Auto:
        default:                        return 0;
    }
}

void AddPemBundleFromMemory(SSL_CTX* ctx, const std::string& pem) {
    if (pem.empty()) return;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return;
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    while (X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) {
        X509_STORE_add_cert(store, x);
        X509_free(x);
    }
    BIO_free(bio);
}

std::string OneLine(X509_NAME* n) {
    if (!n) return {};
    char buf[512]{};
    X509_NAME_oneline(n, buf, sizeof buf);
    return buf;
}

std::string Sha256Fingerprint(X509* x) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  mdlen = 0;
    if (!X509_digest(x, EVP_sha256(), md, &mdlen)) return {};
    std::string out;
    out.reserve(mdlen * 3);
    for (unsigned i = 0; i < mdlen; ++i) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02x%s", md[i], (i + 1 < mdlen ? ":" : ""));
        out += hex;
    }
    return out;
}

UltraNetTlsVersion VersionFromName(const char* s) {
    if (!s) return UltraNetTlsVersion::Auto;
    if (std::strcmp(s, "TLSv1.3") == 0) return UltraNetTlsVersion::Tls13;
    if (std::strcmp(s, "TLSv1.2") == 0) return UltraNetTlsVersion::Tls12;
    if (std::strcmp(s, "TLSv1.1") == 0) return UltraNetTlsVersion::Tls11;
    if (std::strcmp(s, "TLSv1")   == 0) return UltraNetTlsVersion::Tls10;
    return UltraNetTlsVersion::Auto;
}

std::string LastOpenSslError() {
    BIO* mem = BIO_new(BIO_s_mem());
    ERR_print_errors(mem);
    char* data = nullptr;
    long  len  = BIO_get_mem_data(mem, &data);
    std::string out(data, data + (len > 0 ? len : 0));
    BIO_free(mem);
    if (out.empty()) out = "OpenSSL error";
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
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
    SSL_CTX* sctx = SSL_CTX_new(TLS_client_method());
    if (!sctx) {
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     LastOpenSslError());
    }
    if (int v = MapTlsVersion(opt.minVersion)) SSL_CTX_set_min_proto_version(sctx, v);
    if (int v = MapTlsVersion(opt.maxVersion)) SSL_CTX_set_max_proto_version(sctx, v);

    // Trust roots: per-call CA bundle wins over the global ones; otherwise
    // fall through to the OS default store, augmented by any global PEM
    // blobs UltraNet_TlsAddTrustedCert added.
    std::string caBundle = opt.caBundlePath;
    std::string extraPem;
    {
        std::lock_guard<std::mutex> lk(g_globalMutex);
        if (caBundle.empty()) caBundle = g_globalCaBundle;
        extraPem = g_globalExtraPem;
    }
    if (!caBundle.empty()) {
        SSL_CTX_load_verify_locations(sctx, caBundle.c_str(), nullptr);
    } else {
        SSL_CTX_set_default_verify_paths(sctx);
    }
    if (!extraPem.empty()) AddPemBundleFromMemory(sctx, extraPem);

    SSL_CTX_set_verify(sctx,
                       opt.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
                       nullptr);

    if (!opt.clientCertPem.empty()) {
        BIO* bio = BIO_new_mem_buf(opt.clientCertPem.data(),
                                   static_cast<int>(opt.clientCertPem.size()));
        if (bio) {
            X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (cert) {
                SSL_CTX_use_certificate(sctx, cert);
                X509_free(cert);
            }
            BIO_free(bio);
        }
    }
    if (!opt.clientKeyPem.empty()) {
        BIO* bio = BIO_new_mem_buf(opt.clientKeyPem.data(),
                                   static_cast<int>(opt.clientKeyPem.size()));
        if (bio) {
            void* pw = opt.clientKeyPassword.empty()
                          ? nullptr
                          : const_cast<char*>(opt.clientKeyPassword.c_str());
            EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, pw);
            if (key) {
                SSL_CTX_use_PrivateKey(sctx, key);
                EVP_PKEY_free(key);
            }
            BIO_free(bio);
        }
    }

    if (!opt.alpnProtocols.empty()) {
        std::vector<unsigned char> wire;
        for (const auto& p : opt.alpnProtocols) {
            if (p.empty() || p.size() > 255) continue;
            wire.push_back(static_cast<unsigned char>(p.size()));
            wire.insert(wire.end(), p.begin(), p.end());
        }
        if (!wire.empty()) {
            SSL_CTX_set_alpn_protos(sctx, wire.data(),
                                    static_cast<unsigned int>(wire.size()));
        }
    }

    SSL* ssl = SSL_new(sctx);
    if (!ssl) {
        SSL_CTX_free(sctx);
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     LastOpenSslError());
    }
    if (SSL_set_fd(ssl, static_cast<int>(fd)) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(sctx);
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     "SSL_set_fd failed");
    }
    const std::string sniHost = opt.sniHostname.empty() ? sni : opt.sniHostname;
    if (!sniHost.empty()) {
        SSL_set_tlsext_host_name(ssl, sniHost.c_str());
        if (opt.verifyHostname && opt.verifyPeer) {
            SSL_set1_host(ssl, sniHost.c_str());
        }
    }

    auto* c = new Ctx{};
    c->sslCtx     = sctx;
    c->ssl        = ssl;
    c->fd         = static_cast<int>(fd);
    c->verifyHost = opt.verifyHostname;
    c->sni        = sniHost;
    *outCtx = c;
    return UltraNetResult::Ok();
}

UltraNetResult Handshake(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "no SSL context");
    }
    int rc = SSL_connect(c->ssl);
    if (rc == 1) return UltraNetResult::Ok();

    int err = SSL_get_error(c->ssl, rc);
    if (err == SSL_ERROR_SSL) {
        const long v = SSL_get_verify_result(c->ssl);
        if (v != X509_V_OK) {
            return UltraNetResult::Error(UltraNetResultCode::TlsCertificateInvalid,
                                         X509_verify_cert_error_string(v));
        }
    }
    return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                 LastOpenSslError());
}

UltraNetTlsInfo GetInfo(void* ctx) {
    UltraNetTlsInfo info;
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return info;

    info.version = VersionFromName(SSL_get_version(c->ssl));
    if (const char* cipher = SSL_get_cipher_name(c->ssl)) {
        info.cipherSuite = cipher;
    }
    if (X509* peer = SSL_get_peer_certificate(c->ssl)) {
        info.peerCertificateSubject     = OneLine(X509_get_subject_name(peer));
        info.peerCertificateIssuer      = OneLine(X509_get_issuer_name(peer));
        info.peerCertificateFingerprint = Sha256Fingerprint(peer);
        X509_free(peer);
    }
    const unsigned char* alpn = nullptr;
    unsigned int alpnLen = 0;
    SSL_get0_alpn_selected(c->ssl, &alpn, &alpnLen);
    if (alpn && alpnLen) {
        info.negotiatedAlpn.assign(reinterpret_cast<const char*>(alpn), alpnLen);
    }
    info.sessionResumed = SSL_session_reused(c->ssl);
    return info;
}

int Read(void* ctx, char* buf, std::size_t len, int /*timeoutMs*/) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return -1;
    int n = SSL_read(c->ssl, buf, static_cast<int>(len));
    if (n > 0) return n;
    int err = SSL_get_error(c->ssl, n);
    if (err == SSL_ERROR_ZERO_RETURN) return 0;          // clean shutdown
    return -1;
}

int Write(void* ctx, const char* buf, std::size_t len) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->ssl) return -1;
    int n = SSL_write(c->ssl, buf, static_cast<int>(len));
    if (n > 0) return n;
    return -1;
}

void Close(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c) return;
    if (c->ssl) {
        // Best-effort close-notify. Ignore failures — the fd may already be
        // half-closed by the peer.
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->sslCtx) SSL_CTX_free(c->sslCtx);
    delete c;
}

} // namespace ultranet_tls_platform

#endif // __linux__
