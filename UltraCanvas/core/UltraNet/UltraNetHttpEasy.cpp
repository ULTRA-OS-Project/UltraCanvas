// core/UltraNet/UltraNetHttpEasy.cpp
// Shared libcurl easy-handle plumbing: callbacks, option-setting,
// finalisation. Used by both the sync and the async HTTP code paths.
// Version: 0.2.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNetHttpEasy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ultranet_internal {

namespace {

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

std::size_t HeaderCallback(char* data, std::size_t size,
                           std::size_t nmemb, void* userdata) {
    UltraNetResponse* resp = static_cast<UltraNetResponse*>(userdata);
    const std::size_t total = size * nmemb;
    std::string line(data, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    if (line.empty()) return total;
    if (line.rfind("HTTP/", 0) == 0) {
        std::size_t firstSpace  = line.find(' ');
        std::size_t secondSpace = line.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos) {
            resp->statusMessage = line.substr(secondSpace + 1);
        }
        resp->headers.Clear();
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
    int ProgressCallback(void* /*userdata*/,
                         curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t ultotal, curl_off_t ulnow) {
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
    curl_slist* slist = BuildSlist(headers);
    if (slist) curl_easy_setopt(easy, CURLOPT_HTTPHEADER, slist);

    const long authMask = CurlAuthMask(opt.authType);
    if (authMask != 0) {
        curl_easy_setopt(easy, CURLOPT_HTTPAUTH, authMask);
        curl_easy_setopt(easy, CURLOPT_USERNAME, opt.credentials.username.c_str());
        curl_easy_setopt(easy, CURLOPT_PASSWORD, opt.credentials.password.c_str());
    }

    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION,
                     (opt.followRedirects && cfg.followRedirects) ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS,
                     static_cast<long>(opt.maxRedirects > 0 ? opt.maxRedirects
                                                            : cfg.maxRedirects));

    const int connectTo = opt.connectTimeoutMs > 0 ? opt.connectTimeoutMs : cfg.connectTimeoutMs;
    const int overallTo = opt.timeoutMs > 0       ? opt.timeoutMs       : cfg.defaultTimeoutMs;
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connectTo));
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,        static_cast<long>(overallTo));

    const bool verify = opt.verifyTls && cfg.verifyTlsPeer && !opt.acceptInvalidCert;
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST,
                     (verify && cfg.verifyTlsHostname) ? 2L : 0L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, CurlTlsVersionMask(cfg.minTlsVersion));
    if (!cfg.caBundlePath.empty()) {
        curl_easy_setopt(easy, CURLOPT_CAINFO, cfg.caBundlePath.c_str());
    }

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
    if (cfg.enableHttp2) {
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
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, sink);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &HeaderCallback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, responseForHeaders);

    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 0L);

    // Progress reporting — libcurl invokes ProgressCallback frequently during
    // the transfer; the callback fans out to the global UltraNet callbacks.
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &ProgressCallback);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, nullptr);

    return slist;
}

UltraNetResult FinalizeFromEasy(CURL* easy,
                                CURLcode rc,
                                const std::string& url,
                                UltraNetResponse& response,
                                bool exceededLimit) {
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

    if (exceededLimit) {
        return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                     "response exceeded maxReceiveSize");
    }
    UltraNetResult result;
    result.url            = url;
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
