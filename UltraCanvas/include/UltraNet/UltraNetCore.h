// include/UltraNet/UltraNetCore.h
// Stage 1 subset of the UltraNet master registry: handles, result type,
// configuration, credentials, proxy descriptor, transfer stats, and the
// initialize/shutdown/version entrypoints. Full surface defined in the
// UltraNet Master Registry v1.0.0.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Defensive: X11/Xlib.h defines `Success` and `None` as macros, which would
// turn our enum-class members into literal integers. UltraCanvas's Linux
// platform glue pulls X11 in transitively, so any TU that includes both an
// UltraCanvas header and an UltraNet header would otherwise fail to compile.
// We can't put X11 headers below UltraNet ones reliably, so just undef the
// known conflict names here.
#ifdef Success
#undef Success
#endif
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif

// ============================================================================
// Opaque handle. Zero is reserved for "invalid handle". Returned by
// connection-oriented or async operations (sockets, sessions, websockets,
// async HTTP requests).
// ============================================================================
using UltraNetHandle = uint64_t;
constexpr UltraNetHandle UltraNetInvalidHandle = 0;

// ============================================================================
// Result codes — exhaustive list from the master registry. Stage 1 only
// produces a subset of these (HTTP/URL paths), but the full enum is defined
// up front so the surface is stable for downstream callers.
// ============================================================================
enum class UltraNetResultCode {
    Success,
    InvalidUrl,
    UnsupportedScheme,
    HostNotFound,
    ConnectionRefused,
    ConnectionReset,
    ConnectionTimeout,
    SendFailed,
    ReceiveFailed,
    TlsHandshakeFailed,
    TlsCertificateInvalid,
    TlsCertificateExpired,
    AuthenticationRequired,
    AuthenticationFailed,
    AccessDenied,
    NotFound,
    HttpError,
    Cancelled,
    Timeout,
    PluginNotFound,
    PluginError,
    InsufficientMemory,
    NotInitialized,
    InvalidHandle,
    InvalidState,
    Unknown
};

// ============================================================================
// Structured result for every blocking operation. operator bool() lets callers
// write `if (UltraNet_HttpGet(...)) { ... }` for the success path.
// ============================================================================
struct UltraNetResult {
    UltraNetResultCode code = UltraNetResultCode::Unknown;
    bool success = false;
    std::string message;
    std::string url;
    int httpStatus = 0;         // 0 if not HTTP
    double processingTime = 0;  // seconds

    operator bool() const { return success; }

    static UltraNetResult Ok() {
        UltraNetResult r;
        r.code = UltraNetResultCode::Success;
        r.success = true;
        return r;
    }

    static UltraNetResult Error(UltraNetResultCode c, const std::string& msg) {
        UltraNetResult r;
        r.code = c;
        r.success = false;
        r.message = msg;
        return r;
    }
};

// ============================================================================
// Auth + credentials — referenced by HTTP options and (later) the plugin
// surface. Stage 1 honours Basic / Bearer; the rest are reserved.
// ============================================================================
enum class UltraNetAuthType {
    None,
    Basic,
    Digest,
    Bearer,
    NTLM,
    Negotiate,
    OAuth2,
    ApiKey,
    Custom
};

struct UltraNetCredentials {
    UltraNetAuthType type = UltraNetAuthType::None;
    std::string username;
    std::string password;
    std::string token;
    std::string realm;
    std::map<std::string, std::string> custom;
};

// ============================================================================
// Proxy descriptor — required by UltraNetConfig and per-request options. The
// detection of system-level proxy values is implemented per platform in
// OS/<Platform>/UltraNetSupport.{cpp,mm}.
// ============================================================================
enum class UltraNetProxyType {
    None,
    Http,
    Https,
    Socks4,
    Socks5,
    System
};

struct UltraNetProxyConfig {
    UltraNetProxyType type = UltraNetProxyType::None;
    std::string host;
    int port = 0;
    UltraNetCredentials credentials;
    std::vector<std::string> noProxyHosts;

    bool IsEnabled() const { return type != UltraNetProxyType::None; }
};

// ============================================================================
// TLS version baseline — included here because UltraNetConfig pins it. Full
// TLS API (handshake, certificate callbacks, ALPN, …) is Stage 3.
// ============================================================================
enum class UltraNetTlsVersion {
    Auto,
    Tls10,
    Tls11,
    Tls12,
    Tls13
};

struct UltraNetTlsInfo {
    UltraNetTlsVersion version = UltraNetTlsVersion::Auto;
    std::string cipherSuite;
    std::string peerCertificateSubject;
    std::string peerCertificateIssuer;
    std::string peerCertificateFingerprint;
    std::string negotiatedAlpn;
    bool sessionResumed = false;
};

// ============================================================================
// Module-wide configuration. Defaults match the master registry. Mutating
// fields via UltraNet_SetConfig only affects subsequent requests.
// ============================================================================
struct UltraNetConfig {
    int defaultTimeoutMs = 30000;
    int connectTimeoutMs = 10000;
    int maxConcurrentRequests = 64;
    int maxRedirects = 10;
    bool followRedirects = true;
    bool verifyTlsPeer = true;
    bool verifyTlsHostname = true;
    std::string caBundlePath;       // empty = libcurl/system default
    std::string userAgent = "UltraNet/0.1";
    UltraNetProxyConfig proxy;
    UltraNetTlsVersion minTlsVersion = UltraNetTlsVersion::Tls12;
    int64_t maxReceiveSize = 0;     // 0 = unlimited
    bool enableHttp2 = true;
    bool enableCompression = true;
    std::string pluginDirectory = "Plugins/UltraNet";

    static UltraNetConfig Default() { return {}; }
};

// ============================================================================
// Transfer stats — populated by UltraNet_HttpRequest and friends. Stage 1
// fills bytesReceived / durationSeconds; the rest is Stage 2 (async/multi).
// ============================================================================
struct UltraNetTransferStats {
    int64_t bytesSent = 0;
    int64_t bytesReceived = 0;
    double durationSeconds = 0;
    double averageSpeedBps = 0;
    double currentSpeedBps = 0;
    int redirectCount = 0;
};

// ============================================================================
// Lifecycle / introspection.
// ============================================================================
UltraNetResult UltraNet_Initialize(const UltraNetConfig& config = UltraNetConfig::Default());
void           UltraNet_Shutdown();
bool           UltraNet_IsInitialized();

UltraNetConfig UltraNet_GetConfig();
void           UltraNet_SetConfig(const UltraNetConfig& config);

std::string    UltraNet_GetVersion();
std::string    UltraNet_GetBackendInfo();

// ============================================================================
// Proxy helpers. UltraNet_DetectSystemProxy is implemented in the per-platform
// UltraNetSupport translation unit; the other two are pure config setters.
// ============================================================================
void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy);
void UltraNet_SetGlobalProxy(const UltraNetProxyConfig& proxy);
UltraNetProxyConfig UltraNet_GetGlobalProxy();
void UltraNet_DisableProxy();

// ============================================================================
// Cancellation & progress. Operate on a UltraNetHandle returned by an async
// entry point (HTTP today; sessions / websockets later).
// ============================================================================
UltraNetResult         UltraNet_CancelRequest(UltraNetHandle handle);
bool                   UltraNet_IsRequestActive(UltraNetHandle handle);
UltraNetTransferStats  UltraNet_GetTransferStats(UltraNetHandle handle);

// ============================================================================
// Global transfer progress callback bag. Set once on the module, every HTTP
// request (sync and async) reports its progress through these callbacks.
// All callbacks fire on the worker / request thread; do not block.
// ============================================================================
struct UltraNetTransferCallbacks {
    // bytesReceivedSoFar, totalBytesExpected (-1 if server didn't send length)
    std::function<void(int64_t, int64_t)>          onDownloadProgress;
    // bytesSentSoFar, totalBytesToSend
    std::function<void(int64_t, int64_t)>          onUploadProgress;
    // bytesTransferred, totalBytesExpected, speedBytesPerSec
    std::function<void(int64_t, int64_t, double)>  onTransferStats;
};

// Replaces the global callbacks. Returns the previous bag.
UltraNetTransferCallbacks UltraNet_SetTransferCallbacks(
    const UltraNetTransferCallbacks& cb);
UltraNetTransferCallbacks UltraNet_GetTransferCallbacks();
