// include/UltraNet/UltraNetHttp.h
// HTTP / HTTPS surface for UltraNet: synchronous variants plus async via a
// curl_multi worker thread (see core/UltraNet/UltraNetHttpAsync.cpp).
// Version: 0.3.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class UltraNetHttpMethod {
    Get,
    Post,
    Put,
    Delete,
    Head,
    Patch,
    Options,
    Connect,
    Trace,
    Custom
};

// ============================================================================
// Case-insensitive header bag. Stored as an ordered vector of pairs so we
// preserve the wire order set by callers — important for some servers (e.g.
// AWS sigv4 signing-with-canonical-order use cases).
// ============================================================================
class UltraNetHttpHeaders {
public:
    void Add(const std::string& name, const std::string& value);
    void Set(const std::string& name, const std::string& value); // replaces existing
    void Remove(const std::string& name);
    std::string Get(const std::string& name) const;              // empty if absent
    std::vector<std::string> GetAll(const std::string& name) const;
    bool Has(const std::string& name) const;
    void Clear() { entries_.clear(); }
    bool Empty() const { return entries_.empty(); }
    const std::vector<std::pair<std::string, std::string>>& Entries() const { return entries_; }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

struct UltraNetHttpOptions {
    UltraNetHttpHeaders headers;
    // Whole-transfer limit. 0 = the config default (30 s), except for a
    // transfer that writes a file (outputFilePath, UltraNet_HttpDownloadFile),
    // which has no whole-transfer limit by default and a stall limit instead:
    // a download is as long as the file is large. -1 = no limit at all.
    int timeoutMs = 0;
    // Fail with Timeout when no byte has moved for this long. 0 = off, except
    // for a transfer that writes a file, where 0 = the config default (30 s).
    // The check is suspended while the transfer is paused.
    int stallTimeoutMs = 0;
    int connectTimeoutMs = 0;
    bool followRedirects = true;
    int maxRedirects = 10;
    // Follow a redirect only to an https:// URL. A redirect to http:// (or
    // any other scheme) ends the transfer with RedirectRefused and the 3xx
    // status, so a download started over TLS cannot be bounced onto a plain
    // connection. Off, a redirect may go to http:// or https:// - never to
    // another scheme.
    bool httpsOnlyRedirects = false;
    bool verifyTls = true;
    UltraNetAuthType authType = UltraNetAuthType::None;
    UltraNetCredentials credentials;
    UltraNetProxyConfig proxy;      // type==None → use global
    std::vector<uint8_t> clientCertPem;
    std::vector<uint8_t> clientKeyPem;
    std::string clientKeyPassword;
    bool acceptInvalidCert = false; // dangerous; explicit opt-in
    int64_t maxReceiveSize = 0;     // 0 = use config default
    // When set, the body is streamed to this file (UTF-8 path) instead of
    // into UltraNetResponse::body, on the sync and the async path alike;
    // UltraNet_HttpDownloadFile is this with its localPath. The file is
    // truncated first unless resumeOutputFile is set.
    std::string outputFilePath;
    // Resume into outputFilePath: the file's current size is the offset, the
    // request asks for the bytes from there (Range: bytes=<size>-) and the
    // reply is appended. The response's statusCode says what happened: 206,
    // the rest arrived; 200, the server ignored the range and sent the whole
    // body, which replaced the file; 416 with a Content-Range naming the
    // file's size as the total, the file was already complete (reported as
    // success). To make sure the file on the server is still the one the
    // partial file came from, add an If-Range header with the ETag or
    // Last-Modified of the first reply.
    bool resumeOutputFile = false;
    // Ask for the body from this byte on (Range: bytes=<offset>-), for a
    // caller that keeps its own partial copy. 206 and the rest of the body,
    // or 200 and the whole body when the server ignores the range. Ignored
    // when resumeOutputFile sets the offset from the file.
    int64_t resumeFromOffset = 0;

    // Accept the server only when its certificate carries this public key:
    // "sha256//<base64>" (UltraNet_PublicKeyPinOf; several may be joined with
    // ';'). Checked on every handshake - with acceptInvalidCert too, which is
    // what lets a device with a self-signed certificate be trusted by its key
    // alone and by nothing else. A mismatch fails with TlsPublicKeyMismatch.
    // When the TLS library cannot check a pin, certificate verification is
    // switched back on for the request rather than the pin being dropped.
    std::string pinnedPublicKey;
    // Fill UltraNetResponse::tlsInfo from the server's certificate, its
    // public-key pin included - also when the request itself fails.
    bool capturePeerCertificate = false;

    static UltraNetHttpOptions Default() { return {}; }
};

struct UltraNetResponse;

struct UltraNetHttpRequest {
    std::string url;
    UltraNetHttpMethod method = UltraNetHttpMethod::Get;
    std::string customMethod;       // used when method == Custom
    UltraNetHttpHeaders headers;
    std::vector<uint8_t> body;
    UltraNetHttpOptions options;

    // Optional. When set, libcurl chunks are streamed to this callback in
    // arrival order instead of being accumulated in response.body. The
    // response.body field will be empty after the request completes. Fires
    // on the request thread — the calling thread for sync, the curl_multi
    // worker for async. Use this for Server-Sent Events, chunked LLM
    // responses, and any other "process tokens as they arrive" workload.
    std::function<void(const std::vector<uint8_t>&)> onDataChunk;

    // Optional. Per-request download / upload progress. Fires alongside any
    // global UltraNetTransferCallbacks set via UltraNet_SetTransferCallbacks
    // — apps with multiple concurrent transfers should set these per-request
    // so they can tell which transfer the progress relates to.
    // bytesNow is the running count; bytesTotal is what the server reported
    // (Content-Length) or -1 if unknown for downloads.
    std::function<void(int64_t bytesNow, int64_t bytesTotal)> onDownloadProgress;
    std::function<void(int64_t bytesNow, int64_t bytesTotal)> onUploadProgress;

    // Optional. Fires once, when the final response's headers are complete
    // and before any of its body: statusCode, headers, contentType and
    // contentLength are filled, body is empty. Redirects being followed and
    // 1xx interim responses do not fire it. Return false to end the transfer
    // there (a Content-Length too large, a content type that is not the one
    // expected): the result is Cancelled and the body is never read. Fires
    // on the request thread, like onDataChunk.
    std::function<bool(const UltraNetResponse&)> onHeaders;
};

struct UltraNetResponse {
    int statusCode = 0;
    std::string statusMessage;
    UltraNetHttpHeaders headers;
    std::vector<uint8_t> body;
    std::string finalUrl;
    std::string contentType;
    int64_t contentLength = -1;
    double elapsedTime = 0;
    UltraNetTlsInfo tlsInfo;
    // Why the transfer did not finish - timed out, connection lost, over
    // maxReceiveSize, cancelled - or empty when the whole response arrived.
    // A transfer can fail after the status line: statusCode is then the
    // server's (200, say) and body only what came before the failure, so an
    // async caller checks this before trusting body. An HTTP error status
    // (404, 500) is a finished transfer and leaves this empty.
    std::string transferError;
    // The body went over maxReceiveSize (transferError says so as well).
    bool exceededReceiveLimit = false;

    std::string GetBodyAsString() const {
        return std::string(body.begin(), body.end());
    }
    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
    bool IsComplete() const { return transferError.empty(); }
};

// ============================================================================
// Synchronous verb shortcuts. Each builds a UltraNetHttpRequest under the hood
// and dispatches via UltraNet_HttpRequest.
// ============================================================================
UltraNetResult UltraNet_HttpGet(
    const std::string& url,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpPost(
    const std::string& url,
    const std::vector<uint8_t>& body,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpPut(
    const std::string& url,
    const std::vector<uint8_t>& body,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpDelete(
    const std::string& url,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpHead(
    const std::string& url,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpPatch(
    const std::string& url,
    const std::vector<uint8_t>& body,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

// Full-control synchronous entry point.
UltraNetResult UltraNet_HttpRequest(
    const UltraNetHttpRequest& request,
    UltraNetResponse& outResponse);

// Async entry point. Enqueues the request on the curl_multi worker thread
// and returns a handle usable with UltraNet_CancelRequest /
// UltraNet_IsRequestActive / UltraNet_GetTransferStats. onComplete (and any
// onDataChunk / progress callbacks on the request) fire on the worker
// thread — marshal to your own loop and do not block in the callback.
UltraNetHandle UltraNet_HttpRequestAsync(
    const UltraNetHttpRequest& request,
    std::function<void(const UltraNetResponse&)> onComplete);

// File transfer helpers — both are streamed (constant memory regardless of
// payload size). Implemented via libcurl's write/read callbacks. The download
// is a GET with options.outputFilePath = localPath: no whole-transfer timeout
// unless options.timeoutMs says otherwise, a stall timeout instead, and
// options.resumeOutputFile continues a partial file.
UltraNetResult UltraNet_HttpDownloadFile(
    const std::string& url,
    const std::string& localPath,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpUploadFile(
    const std::string& url,
    const std::string& localPath,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());
