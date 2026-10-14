// core/UltraNet/UltraNetHttpEasy.cpp
// Shared libcurl easy-handle plumbing: callbacks, option-setting,
// finalisation. Used by both the sync and the async HTTP code paths.
// Version: 0.3.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNetHttpEasy.h"
#include "UltraNet/UltraNetCurlTls.h"
#include "UltraNet/UltraNetTls.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../../include/UltraCanvasPathUtf8.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/utsname.h>
#else
#include <windows.h>
#endif

namespace ultranet_internal {

namespace {

    bool FileReadable(const char* path) {
        std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
        if (!f) return false;
        std::fclose(f);
        return true;
    }

#ifdef _WIN32
    // Directory of the running executable, UTF-8, with a trailing backslash, or
    // "" on failure. Self-contained (Win32 only) so the CA resolver has no
    // dependency on the UltraCanvas core path helpers.
    std::string WindowsExeDir() {
        wchar_t buf[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(nullptr, buf,
                                           static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
        std::wstring w(buf, n);
        const std::size_t slash = w.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        w.resize(slash + 1);
        const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                            nullptr, 0, nullptr, nullptr);
        if (len <= 1) return {};
        std::string out(static_cast<std::size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], len, nullptr, nullptr);
        return out;
    }
#endif

#ifdef _WIN32
    // True when libcurl's active TLS backend is Schannel. A multi-SSL build
    // lists every backend in build order and puts the inactive ones in
    // parentheses - "OpenSSL/3.3.1 (Schannel)" when OpenSSL is active,
    // "(OpenSSL/3.3.1) Schannel" when Schannel is - so "Schannel" counts
    // wherever it stands, unless an opening parenthesis comes right before it.
    bool ActiveTlsIsSchannel() {
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        if (!info || !info->ssl_version) return false;
        const std::string v = info->ssl_version;
        for (std::size_t pos = v.find("Schannel"); pos != std::string::npos;
             pos = v.find("Schannel", pos + 1)) {
            if (pos == 0 || v[pos - 1] != '(') return true;
        }
        return false;
    }
#endif

    // CA trust anchors used when UltraNetConfig::caBundlePath is empty.
    // libcurl bakes the CA bundle location of the *build* machine into the
    // library; when the binary then runs on a distro that stores its bundle
    // elsewhere (or the vendored libcurl was built on a different system),
    // every https:// request fails with CURLE_SSL_CACERT_BADFILE — "Problem
    // with the SSL CA cert (path? access rights?)". Probing the well-known
    // locations at runtime keeps TLS verification working (rather than off)
    // regardless of where the library was built.
    struct CaTrust {
        std::string bundle;   // CURLOPT_CAINFO
        std::string dir;      // CURLOPT_CAPATH
    };

    const CaTrust& DiscoverCaTrust() {
        static const CaTrust trust = [] {
            CaTrust t;

            // Explicit overrides first. libcurl itself does not read these —
            // only the curl tool does — so honour them here for parity.
            for (const char* name : {"CURL_CA_BUNDLE", "SSL_CERT_FILE"}) {
                const char* v = std::getenv(name);
                if (v && *v && FileReadable(v)) {
                    t.bundle = v;
                    return t;
                }
            }

#ifdef _WIN32
            // Schannel (the Windows TLS stack) verifies against the Windows
            // certificate store - kept current by Windows Update, and holding
            // the roots an organisation or a security suite installs. Handing
            // it a CA file instead makes curl verify against THAT file only,
            // following the chain exactly as the server sent it: a Let's
            // Encrypt server still sending the chain through the retired
            // "DST Root CA X3" then fails with "the certificate or certificate
            // chain is based on an untrusted root" although Windows itself
            // (Outlook, Edge) trusts it through ISRG Root X1. So with Schannel
            // no bundle at all; the cacert.pem below is for an OpenSSL build.
            if (ActiveTlsIsSchannel()) return t;

            // A cacert.pem shipped beside the app. The Windows system libcurl's
            // baked-in CA path points into the build machine's tree and does not
            // exist on an end user's box, so ship our own and find it relative to
            // the executable. Takes priority over that (broken) build default.
            {
                const std::string exeDir = WindowsExeDir();
                if (!exeDir.empty()) {
                    const std::string candidates[] = {
                        exeDir + "cacert.pem",
                        exeDir + "Resources\\certs\\cacert.pem",
                    };
                    for (const std::string& c : candidates) {
                        if (FileReadable(c.c_str())) {
                            t.bundle = c;
                            return t;
                        }
                    }
                }
            }
#endif

#if LIBCURL_VERSION_NUM >= 0x075400 /* 7.84.0: cainfo in version info */
            // If the build-time default actually exists on this machine,
            // leave libcurl alone.
            const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
            if (info && info->cainfo && FileReadable(info->cainfo)) {
                return t;
            }
#endif

            static const char* const kBundles[] = {
                "/etc/ssl/certs/ca-certificates.crt",                // Debian/Ubuntu/Arch/Gentoo
                "/etc/pki/tls/certs/ca-bundle.crt",                  // Fedora/RHEL
                "/etc/ssl/ca-bundle.pem",                            // openSUSE
                "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // RHEL/CentOS trust store
                "/etc/pki/tls/cacert.pem",                           // older Red Hat
                "/etc/ssl/cert.pem",                                 // Alpine/BSD/macOS
                "/usr/local/share/certs/ca-root-nss.crt",            // FreeBSD port
            };
            for (const char* p : kBundles) {
                if (FileReadable(p)) {
                    t.bundle = p;
                    break;
                }
            }

#ifndef _WIN32
            auto dirExists = [](const char* p) {
                struct stat st{};
                return ::stat(p, &st) == 0 && S_ISDIR(st.st_mode);
            };
            const char* certDir = std::getenv("SSL_CERT_DIR");
            if (certDir && *certDir && dirExists(certDir)) {
                t.dir = certDir;
            } else if (t.bundle.empty() && dirExists("/etc/ssl/certs")) {
                t.dir = "/etc/ssl/certs";     // hashed-certificate directory
            }
#endif
            return t;
        }();
        return trust;
    }

    const char* MethodString(UltraNetHttpMethod m,
                             const std::string& customMethod) {
        switch (m) {
            case UltraNetHttpMethod::Get:     return "GET";
            case UltraNetHttpMethod::Post:    return "POST";
            case UltraNetHttpMethod::Put:     return "PUT";
            case UltraNetHttpMethod::Delete:  return "DELETE";
            case UltraNetHttpMethod::Head:    return "HEAD";
            case UltraNetHttpMethod::Patch:   return "PATCH";
            case UltraNetHttpMethod::Options: return "OPTIONS";
            case UltraNetHttpMethod::Connect: return "CONNECT";
            case UltraNetHttpMethod::Trace:   return "TRACE";
            case UltraNetHttpMethod::Custom:  return customMethod.c_str();
        }
        return "GET";
    }

    long CurlAuthMask(UltraNetAuthType t) {
        switch (t) {
            case UltraNetAuthType::Basic:     return CURLAUTH_BASIC;
            case UltraNetAuthType::Digest:    return CURLAUTH_DIGEST;
            case UltraNetAuthType::NTLM:      return CURLAUTH_NTLM;
            case UltraNetAuthType::Negotiate: return CURLAUTH_NEGOTIATE;
            default:                          return 0;
        }
    }

    long CurlTlsVersionMask(UltraNetTlsVersion v) {
        switch (v) {
            case UltraNetTlsVersion::Tls10: return CURL_SSLVERSION_TLSv1_0;
            case UltraNetTlsVersion::Tls11: return CURL_SSLVERSION_TLSv1_1;
            case UltraNetTlsVersion::Tls12: return CURL_SSLVERSION_TLSv1_2;
            case UltraNetTlsVersion::Tls13: return CURL_SSLVERSION_TLSv1_3;
            case UltraNetTlsVersion::Auto:
            default:                        return CURL_SSLVERSION_DEFAULT;
        }
    }

    std::string ProxyUrl(const UltraNetProxyConfig& p) {
        if (!p.IsEnabled() || p.host.empty()) return {};
        std::string scheme;
        switch (p.type) {
            case UltraNetProxyType::Http:   scheme = "http://";    break;
            case UltraNetProxyType::Https:  scheme = "https://";   break;
            case UltraNetProxyType::Socks4: scheme = "socks4://";  break;
            case UltraNetProxyType::Socks5: scheme = "socks5://";  break;
            default: break;
        }
        std::ostringstream os;
        os << scheme << p.host;
        if (p.port > 0) os << ':' << p.port;
        return os.str();
    }

    curl_slist* BuildSlist(const UltraNetHttpHeaders& headers) {
        curl_slist* head = nullptr;
        for (const auto& [name, value] : headers.Entries()) {
            std::string line = name + ": " + value;
            head = curl_slist_append(head, line.c_str());
        }
        return head;
    }
}

std::size_t WriteCallback(char* data, std::size_t size,
                          std::size_t nmemb, void* userdata) {
    WriteSink* sink = static_cast<WriteSink*>(userdata);
    const std::size_t total = size * nmemb;
    if (sink->maxBytes > 0 &&
        sink->received + static_cast<int64_t>(total) > sink->maxBytes) {
        sink->exceededLimit = true;
        return 0;
    }
    if (sink->onChunk) {
        // Streaming mode: hand bytes to the caller as they arrive.
        sink->onChunk(std::vector<uint8_t>(
            reinterpret_cast<uint8_t*>(data),
            reinterpret_cast<uint8_t*>(data) + total));
    } else {
        if (sink->body) {
            sink->body->insert(sink->body->end(), data, data + total);
        }
        if (sink->file) {
            std::size_t written = std::fwrite(data, 1, total, sink->file);
            if (written != total) {
                sink->writeFailed = true;
                return written;
            }
        }
    }
    sink->received += static_cast<int64_t>(total);
    return total;
}

void WriteSink::CloseFile() {
    if (ownsFile && file) std::fclose(file);
    if (ownsFile) file = nullptr;
    ownsFile = false;
}

bool WriteSink::RestartFile() {
    if (!ownsFile || outputPath.empty()) return false;
    if (file) std::fclose(file);
    file = UltraCanvas::OpenFileUtf8(outputPath, "wb");
    resumeOffset = 0;
    received = 0;
    return file != nullptr;
}

UltraNetResult OpenOutputFile(const UltraNetHttpOptions& options, WriteSink& sink) {
    if (options.outputFilePath.empty()) return UltraNetResult::Ok();
    std::FILE* fp = UltraCanvas::OpenFileUtf8(options.outputFilePath,
                                              options.resumeOutputFile ? "ab" : "wb");
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::AccessDenied,
                                     "cannot open local file for write: " +
                                         options.outputFilePath);
    }
    sink.file       = fp;
    sink.ownsFile   = true;
    sink.outputPath = options.outputFilePath;
    if (options.resumeOutputFile) {
        // "ab" positions every write at the end; the size is the offset.
#if defined(_WIN32)
        _fseeki64(fp, 0, SEEK_END);
        const long long size = _ftelli64(fp);
#else
        std::fseek(fp, 0, SEEK_END);
        const long long size = static_cast<long long>(std::ftell(fp));
#endif
        if (size > 0) sink.resumeOffset = static_cast<int64_t>(size);
    }
    return UltraNetResult::Ok();
}

std::size_t HeaderCallback(char* data, std::size_t size,
                           std::size_t nmemb, void* userdata) {
    WriteSink* sink = static_cast<WriteSink*>(userdata);
    const std::size_t total = size * nmemb;
    UltraNetResponse* resp = sink ? sink->response : nullptr;
    if (!resp) return total;
    std::string line(data, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    if (line.rfind("HTTP/", 0) == 0) {
        // A new response: a redirect being followed, a 100 Continue, the
        // final one. "HTTP/1.1 206 Partial Content", "HTTP/2 200".
        std::size_t firstSpace  = line.find(' ');
        std::size_t secondSpace = line.find(' ', firstSpace + 1);
        if (firstSpace != std::string::npos) {
            resp->statusCode = std::atoi(line.c_str() + firstSpace + 1);
        }
        resp->statusMessage = secondSpace != std::string::npos
                                  ? line.substr(secondSpace + 1) : std::string();
        resp->headers.Clear();
        return total;
    }
    if (line.empty()) {
        // End of a header block. Only the final response counts: not an
        // interim 1xx, not a redirect libcurl is about to follow.
        const int status = resp->statusCode;
        if (status >= 100 && status < 200) return total;
        if (status >= 300 && status < 400 && sink->followRedirects &&
            resp->headers.Has("Location")) {
            return total;
        }
        resp->contentType = resp->headers.Get("Content-Type");
        const std::string len = resp->headers.Get("Content-Length");
        resp->contentLength = len.empty() ? -1 : std::atoll(len.c_str());

        // A resume answered with the whole body: the server ignored the
        // range. A partial file is replaced rather than appended to, and the
        // progress figures no longer count an offset the body starts before.
        if (sink->resumeOffset > 0 && status == 200) {
            if (sink->ownsFile && !sink->RestartFile()) {
                sink->writeFailed = true;
                return 0;
            }
            sink->resumeOffset = 0;
        }
        if (sink->onHeaders && !sink->headersReported) {
            sink->headersReported = true;
            if (!sink->onHeaders(*resp)) {
                sink->headersAborted = true;
                return 0;
            }
        }
        return total;
    }
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) return total;
    std::string name  = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    resp->headers.Add(name, value);
    return total;
}

namespace {
    int ProgressCallback(void* userdata,
                         curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t ultotal, curl_off_t ulnow) {
        // Per-request callbacks first — they're more specific. A resumed
        // download counts the bytes the caller already holds, so the figures
        // are of the whole file, as a progress bar wants them.
        auto* sink = static_cast<WriteSink*>(userdata);
        if (sink && sink->resumeOffset > 0) {
            dlnow += static_cast<curl_off_t>(sink->resumeOffset);
            if (dltotal > 0) dltotal += static_cast<curl_off_t>(sink->resumeOffset);
        }
        if (sink) {
            if (sink->perRequestDownload && dlnow > 0) {
                sink->perRequestDownload(static_cast<int64_t>(dlnow),
                                         static_cast<int64_t>(dltotal));
            }
            if (sink->perRequestUpload && ulnow > 0) {
                sink->perRequestUpload(static_cast<int64_t>(ulnow),
                                       static_cast<int64_t>(ultotal));
            }
        }
        // Then process-wide ones.
        const auto cb = UltraNet_GetTransferCallbacks();
        if (cb.onDownloadProgress && dlnow > 0) {
            cb.onDownloadProgress(static_cast<int64_t>(dlnow),
                                  static_cast<int64_t>(dltotal));
        }
        if (cb.onUploadProgress && ulnow > 0) {
            cb.onUploadProgress(static_cast<int64_t>(ulnow),
                                static_cast<int64_t>(ultotal));
        }
        if (cb.onTransferStats) {
            const curl_off_t now   = dlnow   + ulnow;
            const curl_off_t total = dltotal + ultotal;
            cb.onTransferStats(static_cast<int64_t>(now),
                               static_cast<int64_t>(total), 0.0);
        }
        return 0;
    }
}

UltraNetResultCode MapCurlError(CURLcode rc, long httpStatus) {
    switch (rc) {
        case CURLE_OK:
            if (httpStatus >= 400) return UltraNetResultCode::HttpError;
            return UltraNetResultCode::Success;
        case CURLE_URL_MALFORMAT:           return UltraNetResultCode::InvalidUrl;
        case CURLE_UNSUPPORTED_PROTOCOL:    return UltraNetResultCode::UnsupportedScheme;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:   return UltraNetResultCode::HostNotFound;
        case CURLE_COULDNT_CONNECT:         return UltraNetResultCode::ConnectionRefused;
        case CURLE_OPERATION_TIMEDOUT:      return UltraNetResultCode::Timeout;
        case CURLE_SEND_ERROR:              return UltraNetResultCode::SendFailed;
        case CURLE_RECV_ERROR:              return UltraNetResultCode::ReceiveFailed;
        case CURLE_SSL_CONNECT_ERROR:       return UltraNetResultCode::TlsHandshakeFailed;
        case CURLE_PEER_FAILED_VERIFICATION:return UltraNetResultCode::TlsCertificateInvalid;
        case CURLE_SSL_CACERT_BADFILE:      return UltraNetResultCode::TlsCertificateInvalid;
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:return UltraNetResultCode::TlsPublicKeyMismatch;
        case CURLE_LOGIN_DENIED:            return UltraNetResultCode::AuthenticationFailed;
        case CURLE_ABORTED_BY_CALLBACK:     return UltraNetResultCode::Cancelled;
        case CURLE_OUT_OF_MEMORY:           return UltraNetResultCode::InsufficientMemory;
        default:                            return UltraNetResultCode::Unknown;
    }
}

curl_slist* ConfigureEasyHandle(CURL* easy,
                                const UltraNetHttpRequest& request,
                                const UltraNetConfig& cfg,
                                WriteSink* sink,
                                UltraNetResponse* responseForHeaders) {
    const UltraNetHttpOptions& opt = request.options;

    curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());

    if (request.method == UltraNetHttpMethod::Head) {
        curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    } else if (request.method == UltraNetHttpMethod::Get) {
        curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST,
                         MethodString(request.method, request.customMethod));
    }

    // Merge headers: request.headers first, then per-call options override.
    UltraNetHttpHeaders headers = request.headers;
    for (const auto& kv : opt.headers.Entries()) headers.Set(kv.first, kv.second);
    if (!headers.Has("User-Agent") && !cfg.userAgent.empty()) {
        headers.Set("User-Agent", cfg.userAgent);
    }
    if (opt.authType == UltraNetAuthType::Bearer && !opt.credentials.token.empty()) {
        headers.Set("Authorization", "Bearer " + opt.credentials.token);
    } else if (opt.authType == UltraNetAuthType::ApiKey && !opt.credentials.token.empty()) {
        headers.Set("Authorization", opt.credentials.token);
    }
    // A partial copy the caller holds - a file being resumed, or an offset of
    // its own: ask for the rest. A server that ignores it answers 200 with
    // the whole body; HeaderCallback starts the file over in that case.
    if (sink->resumeOffset <= 0 && opt.resumeFromOffset > 0) {
        sink->resumeOffset = opt.resumeFromOffset;
    }
    if (sink->resumeOffset > 0) {
        headers.Set("Range", "bytes=" + std::to_string(sink->resumeOffset) + "-");
    }
    curl_slist* slist = BuildSlist(headers);
    if (slist) curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);

    const long authMask = CurlAuthMask(opt.authType);
    if (authMask != 0) {
        curl_easy_setopt(easy, CURLOPT_HTTPAUTH, authMask);
        curl_easy_setopt(easy, CURLOPT_USERNAME, opt.credentials.username.c_str());
        curl_easy_setopt(easy, CURLOPT_PASSWORD, opt.credentials.password.c_str());
    }

    sink->followRedirects    = opt.followRedirects && cfg.followRedirects;
    sink->httpsOnlyRedirects = opt.httpsOnlyRedirects;
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, sink->followRedirects ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS,
                     static_cast<long>(opt.maxRedirects > 0 ? opt.maxRedirects
                                                            : cfg.maxRedirects));
    // Where a redirect may go: https only when asked, otherwise http or
    // https - never ftp or another scheme, which libcurl's default allows.
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR,
                     opt.httpsOnlyRedirects ? "https" : "http,https");
#else
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS,
                     opt.httpsOnlyRedirects ? CURLPROTO_HTTPS
                                            : (CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif

    // Timeouts. A transfer into a file is as long as the file is large, so
    // by default it has no whole-transfer limit and a stall limit instead;
    // every other transfer keeps the config default unless the options say.
    const bool toFile   = sink->file != nullptr;
    const int connectTo = opt.connectTimeoutMs > 0 ? opt.connectTimeoutMs : cfg.connectTimeoutMs;
    int overallTo = opt.timeoutMs;
    if (overallTo == 0) overallTo = toFile ? -1 : cfg.defaultTimeoutMs;
    int stallTo = opt.stallTimeoutMs;
    if (stallTo == 0 && toFile) stallTo = cfg.defaultTimeoutMs;
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connectTo));
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(overallTo > 0 ? overallTo : 0));
    sink->stallTimeoutMs = stallTo > 0 ? stallTo : 0;
    if (stallTo > 0) {
        // libcurl counts whole seconds: under one byte a second for that long
        // is a stall.
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME,
                         static_cast<long>(std::max(1, (stallTo + 999) / 1000)));
    }

    const bool verify = opt.verifyTls && cfg.verifyTlsPeer && !opt.acceptInvalidCert;
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST,
                     (verify && cfg.verifyTlsHostname) ? 2L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, CurlTlsVersionMask(cfg.minTlsVersion));
    if (!opt.pinnedPublicKey.empty() &&
        curl_easy_setopt(easy, CURLOPT_PINNEDPUBLICKEY, opt.pinnedPublicKey.c_str()) != CURLE_OK) {
        // This TLS backend cannot check a pin. A pin usually comes with
        // acceptInvalidCert - it is the only thing vouching for the server -
        // so dropping it would accept any certificate at all. Verify the
        // ordinary way instead: a self-signed device then fails, loudly.
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    if (opt.capturePeerCertificate) curl_easy_setopt(easy, CURLOPT_CERTINFO, 1L);
    if (!cfg.caBundlePath.empty()) {
        curl_easy_setopt(easy, CURLOPT_CAINFO, cfg.caBundlePath.c_str());
    } else {
        // No application-supplied bundle: point libcurl at the trust anchors
        // this system actually has (see DiscoverCaTrust above). When nothing
        // is found, libcurl's own defaults stay in effect.
        const CaTrust& trust = DiscoverCaTrust();
        if (!trust.bundle.empty()) {
            curl_easy_setopt(easy, CURLOPT_CAINFO, trust.bundle.c_str());
        }
        if (!trust.dir.empty()) {
            curl_easy_setopt(easy, CURLOPT_CAPATH, trust.dir.c_str());
        }
    }
    // Windows: trust the system certificate store (auto-updated roots) on top
    // of any bundle above, and check revocation the way browsers do (see
    // UltraNetCurlTls.h). No-op elsewhere.
    ultranet_curltls::Apply(easy);

    const UltraNetProxyConfig& proxy =
        opt.proxy.IsEnabled() ? opt.proxy : cfg.proxy;
    const std::string proxyUrl = ProxyUrl(proxy);
    if (!proxyUrl.empty()) {
        curl_easy_setopt(easy, CURLOPT_PROXY, proxyUrl.c_str());
        if (!proxy.credentials.username.empty()) {
            curl_easy_setopt(easy, CURLOPT_PROXYUSERNAME,
                             proxy.credentials.username.c_str());
            curl_easy_setopt(easy, CURLOPT_PROXYPASSWORD,
                             proxy.credentials.password.c_str());
        }
        if (!proxy.noProxyHosts.empty()) {
            std::ostringstream os;
            for (std::size_t i = 0; i < proxy.noProxyHosts.size(); ++i) {
                if (i) os << ',';
                os << proxy.noProxyHosts[i];
            }
            std::string list = os.str();
            curl_easy_setopt(easy, CURLOPT_NOPROXY, list.c_str());
        }
    }

    if (cfg.enableCompression) {
        curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    }
    // HTTP version selection — HTTP/3 wins over HTTP/2 when requested.
    // CURL_HTTP_VERSION_3 negotiates HTTP/3 with HTTP/2 fallback;
    // CURL_HTTP_VERSION_3ONLY fails the request if HTTP/3 is unavailable.
    // When libcurl wasn't built with HTTP/3, the _3 path silently falls
    // back to HTTP/2 / HTTP/1.1; _3ONLY surfaces CURLE_UNSUPPORTED_PROTOCOL.
    if (cfg.enableHttp3) {
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION,
                         cfg.http3Only ? CURL_HTTP_VERSION_3ONLY
                                       : CURL_HTTP_VERSION_3);
    } else if (cfg.enableHttp2) {
        curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    }

    static thread_local struct curl_blob certBlob{}, keyBlob{};
    if (!opt.clientCertPem.empty()) {
        certBlob.data  = const_cast<uint8_t*>(opt.clientCertPem.data());
        certBlob.len   = opt.clientCertPem.size();
        certBlob.flags = CURL_BLOB_COPY;
        curl_easy_setopt(easy, CURLOPT_SSLCERT_BLOB, &certBlob);
        curl_easy_setopt(easy, CURLOPT_SSLCERTTYPE, "PEM");
    }
    if (!opt.clientKeyPem.empty()) {
        keyBlob.data  = const_cast<uint8_t*>(opt.clientKeyPem.data());
        keyBlob.len   = opt.clientKeyPem.size();
        keyBlob.flags = CURL_BLOB_COPY;
        curl_easy_setopt(easy, CURLOPT_SSLKEY_BLOB, &keyBlob);
        curl_easy_setopt(easy, CURLOPT_SSLKEYTYPE, "PEM");
    }
    if (!opt.clientKeyPassword.empty()) {
        curl_easy_setopt(easy, CURLOPT_KEYPASSWD, opt.clientKeyPassword.c_str());
    }

    sink->maxBytes = opt.maxReceiveSize > 0 ? opt.maxReceiveSize : cfg.maxReceiveSize;
    // Per-chunk streaming: when the caller provides request.onDataChunk we
    // bypass body accumulation entirely and fire the callback as bytes
    // arrive (this is what UltraNet_SseStream layers on top of).
    if (request.onDataChunk)         sink->onChunk            = request.onDataChunk;
    if (request.onDownloadProgress)  sink->perRequestDownload = request.onDownloadProgress;
    if (request.onUploadProgress)    sink->perRequestUpload   = request.onUploadProgress;
    if (request.onHeaders)           sink->onHeaders          = request.onHeaders;
    sink->response = responseForHeaders;
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, sink);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &HeaderCallback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, sink);

    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 0L);

    // Progress reporting — libcurl invokes ProgressCallback frequently during
    // the transfer; XFERINFODATA carries the WriteSink so per-request
    // callbacks (stashed there) fire alongside the global ones.
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &ProgressCallback);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, sink);

    return slist;
}

UltraNetResult FinalizeFromEasy(CURL* easy,
                                CURLcode rc,
                                const std::string& url,
                                UltraNetResponse& response,
                                const WriteSink& sink) {
    const bool exceededLimit = sink.exceededLimit;
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    response.statusCode = static_cast<int>(status);

    char* finalUrl = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &finalUrl) == CURLE_OK && finalUrl) {
        response.finalUrl = finalUrl;
    }
    response.contentType  = response.headers.Get("Content-Type");
    const std::string len = response.headers.Get("Content-Length");
    response.contentLength = len.empty() ? -1 : std::atoll(len.c_str());

    double elapsed = 0;
    curl_easy_getinfo(easy, CURLINFO_TOTAL_TIME, &elapsed);
    response.elapsedTime = elapsed;

    // The server's certificate, when the request asked for it (CERTINFO is
    // empty otherwise). The first certificate of the chain is the server's.
    struct curl_certinfo* certs = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_CERTINFO, &certs) == CURLE_OK && certs &&
        certs->num_of_certs > 0) {
        for (curl_slist* field = certs->certinfo[0]; field; field = field->next) {
            const std::string entry = field->data ? field->data : "";
            const size_t colon = entry.find(':');
            if (colon == std::string::npos) continue;
            const std::string name = entry.substr(0, colon);
            const std::string value = entry.substr(colon + 1);
            if (name == "Subject") {
                response.tlsInfo.peerCertificateSubject = value;
            } else if (name == "Issuer") {
                response.tlsInfo.peerCertificateIssuer = value;
            } else if (name == "Cert") {
                response.tlsInfo.peerPublicKeyPin = UltraNet_PublicKeyPinOf(
                    std::vector<uint8_t>(value.begin(), value.end()));
            }
        }
    }

    if (exceededLimit) {
        response.transferError = "response exceeded maxReceiveSize";
        response.exceededReceiveLimit = true;
        return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                     response.transferError);
    }
    UltraNetResult result;
    result.url            = url;
    result.httpStatus     = static_cast<int>(status);
    result.processingTime = elapsed;
    if (sink.headersAborted) {
        // onHeaders said no: libcurl reports the refused header write as
        // CURLE_WRITE_ERROR, which is not what happened.
        result.code    = UltraNetResultCode::Cancelled;
        result.success = false;
        result.message = "transfer ended by onHeaders";
        response.transferError = result.message;
        return result;
    }
    if (sink.writeFailed) {
        result.code    = UltraNetResultCode::AccessDenied;
        result.success = false;
        result.message = "could not write the output file (disk full?)";
        response.transferError = result.message;
        return result;
    }
    if (rc == CURLE_UNSUPPORTED_PROTOCOL && sink.httpsOnlyRedirects &&
        status >= 300 && status < 400) {
        result.code    = UltraNetResultCode::RedirectRefused;
        result.success = false;
        result.message = "redirect to a URL that is not https:// refused: " +
                         response.headers.Get("Location");
        response.transferError = result.message;
        return result;
    }
    if (rc == CURLE_OK && status == 416 && sink.resumeOffset > 0) {
        // Range Not Satisfiable on a resume: when the server's total is the
        // size already held, the file was complete before the request.
        const std::string range = response.headers.Get("Content-Range");
        const std::size_t slash = range.find("*/");
        const long long totalBytes = slash == std::string::npos
                                         ? -1 : std::atoll(range.c_str() + slash + 2);
        if (totalBytes == static_cast<long long>(sink.resumeOffset)) {
            result.code    = UltraNetResultCode::Success;
            result.success = true;
            result.message = "already complete";
            return result;
        }
    }
    if (rc == CURLE_OK) {
        if (status >= 400) {
            result.code    = UltraNetResultCode::HttpError;
            result.success = false;
            std::ostringstream os; os << "HTTP " << status;
            result.message = os.str();
        } else {
            result.code    = UltraNetResultCode::Success;
            result.success = true;
        }
    } else {
        result.code    = MapCurlError(rc, status);
        result.success = false;
        result.message = curl_easy_strerror(rc);
        response.transferError = result.message;
    }
    return result;
}

UltraNetTransferStats ReadTransferStats(CURL* easy) {
    UltraNetTransferStats s;
    curl_off_t bytesDl = 0, bytesUl = 0, speedDl = 0;
    double total = 0;
    long redirects = 0;
    curl_easy_getinfo(easy, CURLINFO_SIZE_DOWNLOAD_T,  &bytesDl);
    curl_easy_getinfo(easy, CURLINFO_SIZE_UPLOAD_T,    &bytesUl);
    curl_easy_getinfo(easy, CURLINFO_TOTAL_TIME,       &total);
    curl_easy_getinfo(easy, CURLINFO_SPEED_DOWNLOAD_T, &speedDl);
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_COUNT,   &redirects);
    s.bytesReceived   = static_cast<int64_t>(bytesDl);
    s.bytesSent       = static_cast<int64_t>(bytesUl);
    s.durationSeconds = total;
    s.currentSpeedBps = static_cast<double>(speedDl);
    s.averageSpeedBps = total > 0 ? static_cast<double>(bytesDl) / total : 0.0;
    s.redirectCount   = static_cast<int>(redirects);
    return s;
}

} // namespace ultranet_internal

// Public: the CA bundle both the HTTP client and the IMAP/SMTP plug-ins trust
// (declared in UltraNet/UltraNetCore.h). Resolved once by DiscoverCaTrust.
std::string UltraNet_ResolveCaBundlePath() {
    return ultranet_internal::DiscoverCaTrust().bundle;
}

std::string UltraNet_DescribeTrustRoots() {
    const ultranet_internal::CaTrust& trust = ultranet_internal::DiscoverCaTrust();
    std::string roots;
    if (!trust.bundle.empty()) roots = trust.bundle;
    if (!trust.dir.empty()) roots += (roots.empty() ? "" : " and ") + trust.dir;
#ifdef _WIN32
    // Every UltraNet handle sets CURLSSLOPT_NATIVE_CA (UltraNetCurlTls.h), and
    // Schannel is given no file at all (DiscoverCaTrust).
    roots += (roots.empty() ? "" : " and ") + std::string("the Windows certificate store");
#endif
    if (roots.empty()) roots = "libcurl's built-in default";
    return roots;
}

std::string UltraNet_DescribePlatform() {
#ifdef _WIN32
    // RtlGetVersion, not GetVersionEx: the latter reports 6.2 to any program
    // without a compatibility manifest, which is exactly the wrong answer here.
    std::string out = "Windows";
    using RtlGetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        if (fn && fn(&v) == 0) {
            out += " " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion)
                 + " build " + std::to_string(v.dwBuildNumber);
        }
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: out += ", x64";   break;
        case PROCESSOR_ARCHITECTURE_ARM64: out += ", ARM64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: out += ", x86";   break;
        default: break;
    }
    return out;
#else
    struct utsname u{};
    if (::uname(&u) != 0) return "unknown";
    return std::string(u.sysname) + " " + u.release + ", " + u.machine;
#endif
}
