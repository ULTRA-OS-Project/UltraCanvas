// OS/MSWindows/UltraNetTlsImpl.cpp
// Schannel-backed TLS wrap (Win32 SSPI) for the UltraNet_Tls* surface on
// Windows. Implements ultranet_tls_platform:: declared in
// core/UltraNet/UltraNetTlsImpl.h. No extra DLL dependencies beyond
// secur32 / crypt32 (linked from CMake).
// Version: 0.3.1 (Stage 3 hardening)
// Author: UltraCanvas Framework / ULTRA OS

#include "../../core/UltraNet/UltraNetTlsImpl.h"

#if defined(_WIN32) || defined(_WIN64)

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#include <subauth.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ws2_32.lib")

namespace ultranet_tls_platform {
namespace {

std::mutex   g_globalMutex;
std::string  g_globalCaBundle;
std::string  g_globalExtraPem;

struct Ctx {
    CredHandle cred = {};
    CtxtHandle ctx  = {};
    SOCKET     fd   = INVALID_SOCKET;
    bool       haveCred = false;
    bool       haveCtx  = false;
    bool       handshakeComplete = false;
    bool       verifyPeer = true;
    bool       verifyHost = true;
    std::string sni;
    SecPkgContext_StreamSizes sizes = {};

    std::vector<char> rxRaw;       // ciphertext from the socket, not yet decrypted
    std::vector<char> rxPlain;     // decrypted bytes not yet returned to caller
};

bool ReadFromSocket(SOCKET fd, std::vector<char>& out) {
    char tmp[8192];
    int n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) return false;
    out.insert(out.end(), tmp, tmp + n);
    return true;
}

bool WriteAll(SOCKET fd, const char* p, int len) {
    while (len > 0) {
        int n = ::send(fd, p, len, 0);
        if (n <= 0) return false;
        p   += n;
        len -= n;
    }
    return true;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()),
                                  nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::string LastSecStatusString(SECURITY_STATUS s) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "schannel status 0x%08lx",
                  static_cast<unsigned long>(s));
    return buf;
}

UltraNetTlsVersion VersionFromProtocol(DWORD p) {
    if (p & SP_PROT_TLS1_3_CLIENT) return UltraNetTlsVersion::Tls13;
    if (p & SP_PROT_TLS1_2_CLIENT) return UltraNetTlsVersion::Tls12;
    if (p & SP_PROT_TLS1_1_CLIENT) return UltraNetTlsVersion::Tls11;
    if (p & SP_PROT_TLS1_0_CLIENT) return UltraNetTlsVersion::Tls10;
    return UltraNetTlsVersion::Auto;
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

    // SCHANNEL_CRED is the classic Schannel credential struct; works on all
    // supported Windows versions and is universally available in MinGW-w64
    // headers. (The newer SCH_CREDENTIALS struct is cleaner for TLS 1.3 but
    // requires Windows 10 1809+ SDK headers, which several MinGW shipments
    // still lack.)
    SCHANNEL_CRED creds = {};
    creds.dwVersion = SCHANNEL_CRED_VERSION;
    creds.dwFlags   = SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_DEFAULT_CREDS;
    if (!opt.verifyPeer) {
        creds.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION |
                         SCH_CRED_NO_SERVERNAME_CHECK    |
                         SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                         SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    } else {
        creds.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION |
                         SCH_CRED_REVOCATION_CHECK_CHAIN;
    }

    // Build the enabled-protocols bitmap from minVersion / maxVersion.
    DWORD enabled = 0;
    auto addRange = [&](UltraNetTlsVersion lo, UltraNetTlsVersion hi) {
        const int loN = static_cast<int>(lo);
        const int hiN = static_cast<int>(hi);
        for (int n = loN; n <= hiN; ++n) {
            switch (static_cast<UltraNetTlsVersion>(n)) {
                case UltraNetTlsVersion::Tls10: enabled |= SP_PROT_TLS1_0_CLIENT; break;
                case UltraNetTlsVersion::Tls11: enabled |= SP_PROT_TLS1_1_CLIENT; break;
                case UltraNetTlsVersion::Tls12: enabled |= SP_PROT_TLS1_2_CLIENT; break;
                case UltraNetTlsVersion::Tls13: enabled |= SP_PROT_TLS1_3_CLIENT; break;
                default: break;
            }
        }
    };
    const UltraNetTlsVersion lo = (opt.minVersion == UltraNetTlsVersion::Auto)
                                      ? UltraNetTlsVersion::Tls12
                                      : opt.minVersion;
    const UltraNetTlsVersion hi = (opt.maxVersion == UltraNetTlsVersion::Auto)
                                      ? UltraNetTlsVersion::Tls13
                                      : opt.maxVersion;
    addRange(lo, hi);
    creds.grbitEnabledProtocols = enabled;

    auto* c = new Ctx{};
    c->fd         = static_cast<SOCKET>(fd);
    c->verifyPeer = opt.verifyPeer;
    c->verifyHost = opt.verifyHostname;
    c->sni        = opt.sniHostname.empty() ? sni : opt.sniHostname;

    SECURITY_STATUS s = AcquireCredentialsHandleW(
        nullptr, const_cast<LPWSTR>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND, nullptr, &creds,
        nullptr, nullptr, &c->cred, nullptr);
    if (s != SEC_E_OK) {
        delete c;
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     LastSecStatusString(s));
    }
    c->haveCred = true;
    *outCtx = c;
    return UltraNetResult::Ok();
}

UltraNetResult Handshake(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c) return UltraNetResult::Error(UltraNetResultCode::InvalidState, "no ctx");

    std::wstring wname = Utf8ToWide(c->sni);
    const DWORD requestFlags =
        ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_ALLOCATE_MEMORY |
        ISC_REQ_CONFIDENTIALITY    | ISC_REQ_REPLAY_DETECT  |
        ISC_REQ_SEQUENCE_DETECT    | ISC_REQ_STREAM         |
        ISC_REQ_EXTENDED_ERROR;

    bool first = true;
    for (;;) {
        SecBuffer inBuffers[2] = {};
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        if (!first) {
            inBuffers[0].pvBuffer = c->rxRaw.data();
            inBuffers[0].cbBuffer = static_cast<ULONG>(c->rxRaw.size());
        }
        inBuffers[1].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc inDesc  = {SECBUFFER_VERSION, 2, inBuffers};

        SecBuffer outBuffers[3] = {};
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[1].BufferType = SECBUFFER_ALERT;
        outBuffers[2].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc outDesc = {SECBUFFER_VERSION, 3, outBuffers};

        DWORD outFlags = 0;
        SECURITY_STATUS s = InitializeSecurityContextW(
            &c->cred,
            c->haveCtx ? &c->ctx : nullptr,
            wname.empty() ? nullptr : wname.data(),
            requestFlags, 0, 0,
            first ? nullptr : &inDesc, 0,
            c->haveCtx ? nullptr : &c->ctx,
            &outDesc, &outFlags, nullptr);
        c->haveCtx = true;
        first      = false;

        // Send any token Schannel produced for the server.
        if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer) {
            bool ok = WriteAll(c->fd,
                               static_cast<const char*>(outBuffers[0].pvBuffer),
                               static_cast<int>(outBuffers[0].cbBuffer));
            FreeContextBuffer(outBuffers[0].pvBuffer);
            if (!ok) {
                return UltraNetResult::Error(UltraNetResultCode::SendFailed,
                                             "socket send failed mid-handshake");
            }
        }

        // Handle SECBUFFER_EXTRA — bytes Schannel didn't consume.
        if (inBuffers[1].BufferType == SECBUFFER_EXTRA) {
            const size_t extra = inBuffers[1].cbBuffer;
            std::vector<char> tail(c->rxRaw.end() - extra, c->rxRaw.end());
            c->rxRaw = std::move(tail);
        } else if (s != SEC_E_INCOMPLETE_MESSAGE) {
            c->rxRaw.clear();
        }

        if (s == SEC_E_OK) {
            if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_STREAM_SIZES,
                                       &c->sizes) != SEC_E_OK) {
                return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                             "QueryContextAttributes(StreamSizes) failed");
            }
            c->handshakeComplete = true;
            return UltraNetResult::Ok();
        }
        if (s == SEC_I_CONTINUE_NEEDED || s == SEC_E_INCOMPLETE_MESSAGE) {
            if (!ReadFromSocket(c->fd, c->rxRaw)) {
                return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                             "socket closed mid-handshake");
            }
            continue;
        }
        return UltraNetResult::Error(UltraNetResultCode::TlsHandshakeFailed,
                                     LastSecStatusString(s));
    }
}

UltraNetTlsInfo GetInfo(void* ctx) {
    UltraNetTlsInfo info;
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->haveCtx) return info;

    SecPkgContext_ConnectionInfo conn{};
    if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_CONNECTION_INFO, &conn) == SEC_E_OK) {
        info.version = VersionFromProtocol(conn.dwProtocol);
        char buf[64];
        std::snprintf(buf, sizeof buf, "0x%04x/0x%04x",
                      conn.aiCipher, conn.aiHash);
        info.cipherSuite = buf;
    }
    PCCERT_CONTEXT cert = nullptr;
    if (QueryContextAttributes(&c->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                               &cert) == SEC_E_OK && cert) {
        char name[512]{};
        if (CertNameToStrA(X509_ASN_ENCODING,
                           &cert->pCertInfo->Subject,
                           CERT_X500_NAME_STR, name, sizeof name)) {
            info.peerCertificateSubject = name;
        }
        if (CertNameToStrA(X509_ASN_ENCODING,
                           &cert->pCertInfo->Issuer,
                           CERT_X500_NAME_STR, name, sizeof name)) {
            info.peerCertificateIssuer = name;
        }
        BYTE hash[32]{};
        DWORD hashLen = sizeof hash;
        if (CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID,
                                              hash, &hashLen)) {
            std::string fp;
            for (DWORD i = 0; i < hashLen; ++i) {
                char hex[4];
                std::snprintf(hex, sizeof hex, "%02x%s", hash[i],
                              i + 1 < hashLen ? ":" : "");
                fp += hex;
            }
            info.peerCertificateFingerprint = fp;
        }
        CertFreeCertificateContext(cert);
    }
    return info;
}

int Read(void* ctx, char* buf, std::size_t len, int /*timeoutMs*/) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->handshakeComplete) return -1;

    // Drain plaintext buffer first.
    if (!c->rxPlain.empty()) {
        std::size_t n = std::min(len, c->rxPlain.size());
        std::memcpy(buf, c->rxPlain.data(), n);
        c->rxPlain.erase(c->rxPlain.begin(),
                         c->rxPlain.begin() + static_cast<std::ptrdiff_t>(n));
        return static_cast<int>(n);
    }
    for (;;) {
        if (!c->rxRaw.empty()) {
            SecBuffer buffers[4] = {};
            buffers[0].pvBuffer   = c->rxRaw.data();
            buffers[0].cbBuffer   = static_cast<ULONG>(c->rxRaw.size());
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[1].BufferType = SECBUFFER_EMPTY;
            buffers[2].BufferType = SECBUFFER_EMPTY;
            buffers[3].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc desc    = {SECBUFFER_VERSION, 4, buffers};

            SECURITY_STATUS s = DecryptMessage(&c->ctx, &desc, 0, nullptr);
            if (s == SEC_E_OK) {
                SecBuffer* data  = nullptr;
                SecBuffer* extra = nullptr;
                for (int i = 0; i < 4; ++i) {
                    if (buffers[i].BufferType == SECBUFFER_DATA)  data  = &buffers[i];
                    if (buffers[i].BufferType == SECBUFFER_EXTRA) extra = &buffers[i];
                }
                std::size_t leftoverOffset = c->rxRaw.size();
                if (extra && extra->cbBuffer > 0) {
                    leftoverOffset = c->rxRaw.size() - extra->cbBuffer;
                }
                std::vector<char> leftover(c->rxRaw.begin() + leftoverOffset,
                                           c->rxRaw.end());
                c->rxRaw = std::move(leftover);
                if (!data || data->cbBuffer == 0) continue;

                std::size_t n = std::min<std::size_t>(len, data->cbBuffer);
                std::memcpy(buf, data->pvBuffer, n);
                if (n < data->cbBuffer) {
                    c->rxPlain.insert(
                        c->rxPlain.end(),
                        static_cast<const char*>(data->pvBuffer) + n,
                        static_cast<const char*>(data->pvBuffer) + data->cbBuffer);
                }
                return static_cast<int>(n);
            }
            if (s == SEC_I_CONTEXT_EXPIRED) return 0;
            if (s != SEC_E_INCOMPLETE_MESSAGE) return -1;
        }
        if (!ReadFromSocket(c->fd, c->rxRaw)) return 0;
    }
}

int Write(void* ctx, const char* buf, std::size_t len) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c || !c->handshakeComplete) return -1;

    std::size_t totalSent = 0;
    while (totalSent < len) {
        const std::size_t chunk =
            std::min<std::size_t>(len - totalSent, c->sizes.cbMaximumMessage);
        std::vector<char> message(c->sizes.cbHeader + chunk + c->sizes.cbTrailer);
        std::memcpy(message.data() + c->sizes.cbHeader,
                    buf + totalSent, chunk);

        SecBuffer buffers[4] = {};
        buffers[0].pvBuffer   = message.data();
        buffers[0].cbBuffer   = c->sizes.cbHeader;
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[1].pvBuffer   = message.data() + c->sizes.cbHeader;
        buffers[1].cbBuffer   = static_cast<ULONG>(chunk);
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[2].pvBuffer   = message.data() + c->sizes.cbHeader + chunk;
        buffers[2].cbBuffer   = c->sizes.cbTrailer;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc    = {SECBUFFER_VERSION, 4, buffers};

        SECURITY_STATUS s = EncryptMessage(&c->ctx, 0, &desc, 0);
        if (s != SEC_E_OK) return -1;

        const int wire = static_cast<int>(
            buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer);
        if (!WriteAll(c->fd, message.data(), wire)) return -1;
        totalSent += chunk;
    }
    return static_cast<int>(totalSent);
}

void Close(void* ctx) {
    auto* c = static_cast<Ctx*>(ctx);
    if (!c) return;
    if (c->handshakeComplete) {
        DWORD type = SCHANNEL_SHUTDOWN;
        SecBuffer sb = {sizeof type, SECBUFFER_TOKEN, &type};
        SecBufferDesc sd = {SECBUFFER_VERSION, 1, &sb};
        ApplyControlToken(&c->ctx, &sd);
        // Best-effort close_notify dispatch: one round of
        // InitializeSecurityContext to produce the alert, then send it.
        std::wstring wname = Utf8ToWide(c->sni);
        SecBuffer outBuffers[1] = {};
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        SecBufferDesc outDesc = {SECBUFFER_VERSION, 1, outBuffers};
        DWORD outFlags = 0;
        SECURITY_STATUS s = InitializeSecurityContextW(
            &c->cred, &c->ctx,
            wname.empty() ? nullptr : wname.data(),
            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
            0, 0, nullptr, 0, nullptr,
            &outDesc, &outFlags, nullptr);
        if (s == SEC_E_OK && outBuffers[0].cbBuffer > 0) {
            WriteAll(c->fd,
                     static_cast<const char*>(outBuffers[0].pvBuffer),
                     static_cast<int>(outBuffers[0].cbBuffer));
        }
        if (outBuffers[0].pvBuffer) FreeContextBuffer(outBuffers[0].pvBuffer);
    }
    if (c->haveCtx)  DeleteSecurityContext(&c->ctx);
    if (c->haveCred) FreeCredentialsHandle(&c->cred);
    delete c;
}

} // namespace ultranet_tls_platform

#endif // _WIN32 || _WIN64
