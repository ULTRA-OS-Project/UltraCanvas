// core/UltraNet/UltraNetHttp.cpp
// libcurl-easy backed synchronous HTTP/HTTPS implementation.
// Streamed-to-disk variants use the same code path with a write callback that
// targets a FILE* instead of std::vector.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetHttp.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// UltraNetHttpHeaders — case-insensitive header bag
// ============================================================================
namespace {
    bool IEquals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
}

void UltraNetHttpHeaders::Add(const std::string& name, const std::string& value) {
    entries_.emplace_back(name, value);
}

void UltraNetHttpHeaders::Set(const std::string& name, const std::string& value) {
    Remove(name);
    entries_.emplace_back(name, value);
}

void UltraNetHttpHeaders::Remove(const std::string& name) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const auto& kv) { return IEquals(kv.first, name); }),
        entries_.end());
}

std::string UltraNetHttpHeaders::Get(const std::string& name) const {
    for (const auto& kv : entries_) {
        if (IEquals(kv.first, name)) return kv.second;
    }
    return {};
}

std::vector<std::string>
UltraNetHttpHeaders::GetAll(const std::string& name) const {
    std::vector<std::string> out;
    for (const auto& kv : entries_) {
        if (IEquals(kv.first, name)) out.push_back(kv.second);
    }
    return out;
}

bool UltraNetHttpHeaders::Has(const std::string& name) const {
    for (const auto& kv : entries_) {
        if (IEquals(kv.first, name)) return true;
    }
    return false;
}

// ============================================================================
// libcurl plumbing — write callbacks, header parsing, easy-handle setup
// ============================================================================
namespace {

    // Sink: either accumulates body in a std::vector or streams to a FILE*.
    struct WriteSink {
        std::vector<uint8_t>* body = nullptr;
        std::FILE* file = nullptr;
        int64_t maxBytes = 0;       // 0 = unlimited
        int64_t received = 0;
        bool exceededLimit = false;
    };

    std::size_t WriteCallback(char* data, std::size_t size, std::size_t nmemb,
                              void* userdata) {
        WriteSink* sink = static_cast<WriteSink*>(userdata);
        const std::size_t total = size * nmemb;
        if (sink->maxBytes > 0 &&
            sink->received + static_cast<int64_t>(total) > sink->maxBytes) {
            sink->exceededLimit = true;
            return 0; // abort transfer
        }
        if (sink->body) {
            sink->body->insert(sink->body->end(), data, data + total);
        }
        if (sink->file) {
            std::size_t written = std::fwrite(data, 1, total, sink->file);
            if (written != total) return written;
        }
        sink->received += static_cast<int64_t>(total);
        return total;
    }

    std::size_t HeaderCallback(char* data, std::size_t size, std::size_t nmemb,
                               void* userdata) {
        UltraNetResponse* resp = static_cast<UltraNetResponse*>(userdata);
        const std::size_t total = size * nmemb;
        std::string line(data, total);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty()) return total;             // end of a header block
        if (line.rfind("HTTP/", 0) == 0) {
            // Status line — keep the message (status code comes from CURLINFO_RESPONSE_CODE)
            std::size_t firstSpace = line.find(' ');
            std::size_t secondSpace = line.find(' ', firstSpace + 1);
            if (secondSpace != std::string::npos) {
                resp->statusMessage = line.substr(secondSpace + 1);
            }
            resp->headers.Clear();                  // drop headers from previous redirect hop
            return total;
        }
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) return total;
        std::string name  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // Trim leading whitespace from value
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        resp->headers.Add(name, value);
        return total;
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

    UltraNetResultCode MapCurlError(CURLcode rc, long httpStatus) {
        switch (rc) {
            case CURLE_OK:
                if (httpStatus >= 400) return UltraNetResultCode::HttpError;
                return UltraNetResultCode::Success;
            case CURLE_URL_MALFORMAT:
                return UltraNetResultCode::InvalidUrl;
            case CURLE_UNSUPPORTED_PROTOCOL:
                return UltraNetResultCode::UnsupportedScheme;
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_RESOLVE_PROXY:
                return UltraNetResultCode::HostNotFound;
            case CURLE_COULDNT_CONNECT:
                return UltraNetResultCode::ConnectionRefused;
            case CURLE_OPERATION_TIMEDOUT:
                return UltraNetResultCode::Timeout;
            case CURLE_SEND_ERROR:
                return UltraNetResultCode::SendFailed;
            case CURLE_RECV_ERROR:
                return UltraNetResultCode::ReceiveFailed;
            case CURLE_SSL_CONNECT_ERROR:
                return UltraNetResultCode::TlsHandshakeFailed;
            case CURLE_PEER_FAILED_VERIFICATION:
                return UltraNetResultCode::TlsCertificateInvalid;
            case CURLE_LOGIN_DENIED:
                return UltraNetResultCode::AuthenticationFailed;
            case CURLE_ABORTED_BY_CALLBACK:
                return UltraNetResultCode::Cancelled;
            case CURLE_OUT_OF_MEMORY:
                return UltraNetResultCode::InsufficientMemory;
            default:
                return UltraNetResultCode::Unknown;
        }
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

    // Build a libcurl slist from a UltraNetHttpHeaders; caller owns the result.
    curl_slist* BuildSlist(const UltraNetHttpHeaders& headers) {
        curl_slist* head = nullptr;
        for (const auto& [name, value] : headers.Entries()) {
            std::string line = name + ": " + value;
            head = curl_slist_append(head, line.c_str());
        }
        return head;
    }

    // Core implementation shared by all synchronous variants.
    UltraNetResult PerformRequest(const UltraNetHttpRequest& request,
                                  UltraNetResponse& outResponse,
                                  WriteSink& sink,
                                  const std::vector<uint8_t>* readBody) {
        if (!UltraNet_IsInitialized()) {
            UltraNetResult r = UltraNet_Initialize();
            if (!r) return r;
        }
        if (request.url.empty()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                         "URL is empty");
        }

        const UltraNetConfig cfg = UltraNet_GetConfig();
        const UltraNetHttpOptions& opt = request.options;

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(
            curl_easy_init(), curl_easy_cleanup);
        if (!easy) {
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                         "curl_easy_init() failed");
        }
        CURL* h = easy.get();
        curl_easy_setopt(h, CURLOPT_URL, request.url.c_str());

        // Method
        const std::string method = MethodString(request.method, request.customMethod);
        if (request.method == UltraNetHttpMethod::Head) {
            curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
        } else if (request.method == UltraNetHttpMethod::Get) {
            curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
        } else {
            curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
        }

        // Request body
        if (readBody && !readBody->empty()) {
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(readBody->size()));
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, readBody->data());
        }

        // Headers
        UltraNetHttpHeaders headers = request.headers;
        if (!opt.headers.Entries().empty()) {
            for (const auto& kv : opt.headers.Entries()) headers.Set(kv.first, kv.second);
        }
        if (!headers.Has("User-Agent") && !cfg.userAgent.empty()) {
            headers.Set("User-Agent", cfg.userAgent);
        }
        // Auth type Bearer / ApiKey expressed as Authorization header
        if (opt.authType == UltraNetAuthType::Bearer && !opt.credentials.token.empty()) {
            headers.Set("Authorization", "Bearer " + opt.credentials.token);
        } else if (opt.authType == UltraNetAuthType::ApiKey && !opt.credentials.token.empty()) {
            headers.Set("Authorization", opt.credentials.token);
        }
        curl_slist* slist = BuildSlist(headers);
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            slistGuard(slist, curl_slist_free_all);
        if (slist) curl_easy_setopt(h, CURLOPT_HTTPHEADER, slist);

        // Auth (Basic/Digest/NTLM/Negotiate)
        long authMask = CurlAuthMask(opt.authType);
        if (authMask != 0) {
            curl_easy_setopt(h, CURLOPT_HTTPAUTH, authMask);
            curl_easy_setopt(h, CURLOPT_USERNAME, opt.credentials.username.c_str());
            curl_easy_setopt(h, CURLOPT_PASSWORD, opt.credentials.password.c_str());
        }

        // Redirects
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION,
                         (opt.followRedirects && cfg.followRedirects) ? 1L : 0L);
        curl_easy_setopt(h, CURLOPT_MAXREDIRS,
                         static_cast<long>(opt.maxRedirects > 0 ? opt.maxRedirects
                                                                : cfg.maxRedirects));

        // Timeouts
        const int connectTo = opt.connectTimeoutMs > 0 ? opt.connectTimeoutMs : cfg.connectTimeoutMs;
        const int overallTo = opt.timeoutMs > 0       ? opt.timeoutMs       : cfg.defaultTimeoutMs;
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connectTo));
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,        static_cast<long>(overallTo));

        // TLS
        const bool verify = opt.verifyTls && cfg.verifyTlsPeer && !opt.acceptInvalidCert;
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST,
                         (verify && cfg.verifyTlsHostname) ? 2L : 0L);
        curl_easy_setopt(h, CURLOPT_SSLVERSION, CurlTlsVersionMask(cfg.minTlsVersion));
        if (!cfg.caBundlePath.empty()) {
            curl_easy_setopt(h, CURLOPT_CAINFO, cfg.caBundlePath.c_str());
        }

        // Proxy: per-request overrides global; global wins over none.
        const UltraNetProxyConfig& proxy =
            opt.proxy.IsEnabled() ? opt.proxy : cfg.proxy;
        const std::string proxyUrl = ProxyUrl(proxy);
        if (!proxyUrl.empty()) {
            curl_easy_setopt(h, CURLOPT_PROXY, proxyUrl.c_str());
            if (!proxy.credentials.username.empty()) {
                curl_easy_setopt(h, CURLOPT_PROXYUSERNAME,
                                 proxy.credentials.username.c_str());
                curl_easy_setopt(h, CURLOPT_PROXYPASSWORD,
                                 proxy.credentials.password.c_str());
            }
            if (!proxy.noProxyHosts.empty()) {
                std::ostringstream os;
                for (std::size_t i = 0; i < proxy.noProxyHosts.size(); ++i) {
                    if (i) os << ',';
                    os << proxy.noProxyHosts[i];
                }
                std::string list = os.str();
                curl_easy_setopt(h, CURLOPT_NOPROXY, list.c_str());
            }
        }

        // Compression
        if (cfg.enableCompression) {
            curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");   // "" = all curl supports
        }
        // HTTP/2 — best effort; libcurl falls back to 1.1 if unavailable.
        if (cfg.enableHttp2) {
            curl_easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        }

        // Client certificate (PEM bytes)
        struct curl_blob certBlob{}, keyBlob{};
        if (!opt.clientCertPem.empty()) {
            certBlob.data  = const_cast<uint8_t*>(opt.clientCertPem.data());
            certBlob.len   = opt.clientCertPem.size();
            certBlob.flags = CURL_BLOB_COPY;
            curl_easy_setopt(h, CURLOPT_SSLCERT_BLOB, &certBlob);
            curl_easy_setopt(h, CURLOPT_SSLCERTTYPE, "PEM");
        }
        if (!opt.clientKeyPem.empty()) {
            keyBlob.data  = const_cast<uint8_t*>(opt.clientKeyPem.data());
            keyBlob.len   = opt.clientKeyPem.size();
            keyBlob.flags = CURL_BLOB_COPY;
            curl_easy_setopt(h, CURLOPT_SSLKEY_BLOB, &keyBlob);
            curl_easy_setopt(h, CURLOPT_SSLKEYTYPE, "PEM");
        }
        if (!opt.clientKeyPassword.empty()) {
            curl_easy_setopt(h, CURLOPT_KEYPASSWD, opt.clientKeyPassword.c_str());
        }

        // Limits
        sink.maxBytes = opt.maxReceiveSize > 0 ? opt.maxReceiveSize : cfg.maxReceiveSize;

        // Write callbacks
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, &WriteCallback);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, &HeaderCallback);
        curl_easy_setopt(h, CURLOPT_HEADERDATA, &outResponse);

        // Misc
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_FAILONERROR, 0L);   // we want bodies for 4xx/5xx

        // Perform
        CURLcode rc = curl_easy_perform(h);

        long status = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
        outResponse.statusCode = static_cast<int>(status);

        char* finalUrl = nullptr;
        if (curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &finalUrl) == CURLE_OK && finalUrl) {
            outResponse.finalUrl = finalUrl;
        }
        outResponse.contentType  = outResponse.headers.Get("Content-Type");
        const std::string len    = outResponse.headers.Get("Content-Length");
        outResponse.contentLength = len.empty() ? -1 : std::atoll(len.c_str());
        double elapsed = 0;
        curl_easy_getinfo(h, CURLINFO_TOTAL_TIME, &elapsed);
        outResponse.elapsedTime = elapsed;

        if (sink.exceededLimit) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         "response exceeded maxReceiveSize");
        }

        UltraNetResult result;
        result.url            = request.url;
        result.httpStatus     = static_cast<int>(status);
        result.processingTime = elapsed;
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
        }
        return result;
    }
}

// ============================================================================
// Public entry points
// ============================================================================
UltraNetResult UltraNet_HttpRequest(const UltraNetHttpRequest& request,
                                    UltraNetResponse& outResponse) {
    outResponse = {};
    WriteSink sink;
    sink.body = &outResponse.body;
    return PerformRequest(request, outResponse, sink, &request.body);
}

UltraNetResult UltraNet_HttpGet(const std::string& url,
                                UltraNetResponse& outResponse,
                                const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Get;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPost(const std::string& url,
                                 const std::vector<uint8_t>& body,
                                 UltraNetResponse& outResponse,
                                 const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Post;
    req.body = body;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPut(const std::string& url,
                                const std::vector<uint8_t>& body,
                                UltraNetResponse& outResponse,
                                const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Put;
    req.body = body;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpDelete(const std::string& url,
                                   UltraNetResponse& outResponse,
                                   const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Delete;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpHead(const std::string& url,
                                 UltraNetResponse& outResponse,
                                 const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Head;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPatch(const std::string& url,
                                  const std::vector<uint8_t>& body,
                                  UltraNetResponse& outResponse,
                                  const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Patch;
    req.body = body;
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpDownloadFile(const std::string& url,
                                         const std::string& localPath,
                                         const UltraNetHttpOptions& options) {
    if (localPath.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "localPath is empty");
    }
    std::FILE* fp = std::fopen(localPath.c_str(), "wb");
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::AccessDenied,
                                     "cannot open local file for write");
    }
    UltraNetHttpRequest req;
    req.url = url;
    req.method = UltraNetHttpMethod::Get;
    req.options = options;

    UltraNetResponse resp;
    WriteSink sink;
    sink.file = fp;

    UltraNetResult result = PerformRequest(req, resp, sink, nullptr);
    std::fclose(fp);
    return result;
}

UltraNetResult UltraNet_HttpUploadFile(const std::string& url,
                                       const std::string& localPath,
                                       UltraNetResponse& outResponse,
                                       const UltraNetHttpOptions& options) {
    // Read the file into memory. The streaming-from-disk variant (READFUNCTION
    // + INFILESIZE_LARGE) is a small refinement that will land alongside the
    // multi-threaded async path in Stage 2.
    std::FILE* fp = std::fopen(localPath.c_str(), "rb");
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                     "cannot open local file for read");
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> body(static_cast<std::size_t>(size > 0 ? size : 0));
    if (size > 0) {
        std::fread(body.data(), 1, body.size(), fp);
    }
    std::fclose(fp);

    UltraNetHttpRequest req;
    req.url     = url;
    req.method  = UltraNetHttpMethod::Put;
    req.body    = std::move(body);
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

// ============================================================================
// Async — Stage 2. Stage 1 makes the symbol resolvable and reports clearly
// that the feature is not yet implemented; switch to multi-handle worker in
// Stage 2 without changing the signature.
// ============================================================================
UltraNetHandle UltraNet_HttpRequestAsync(
    const UltraNetHttpRequest& /*request*/,
    std::function<void(const UltraNetResponse&)> onComplete) {
    if (onComplete) {
        UltraNetResponse resp;
        resp.statusCode = 0;
        resp.statusMessage = "UltraNet_HttpRequestAsync is not yet implemented (Stage 2)";
        onComplete(resp);
    }
    return UltraNetInvalidHandle;
}
