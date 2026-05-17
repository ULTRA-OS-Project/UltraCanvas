// include/UltraNet/UltraNetHttp.h
// HTTP / HTTPS surface for UltraNet, Stage 1 (synchronous variants and the
// async signature). Async is declared but unimplemented in Stage 1.
// Version: 0.1.0 (Stage 1)
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
    int timeoutMs = 0;              // 0 = use config default
    int connectTimeoutMs = 0;
    bool followRedirects = true;
    int maxRedirects = 10;
    bool verifyTls = true;
    UltraNetAuthType authType = UltraNetAuthType::None;
    UltraNetCredentials credentials;
    UltraNetProxyConfig proxy;      // type==None → use global
    std::vector<uint8_t> clientCertPem;
    std::vector<uint8_t> clientKeyPem;
    std::string clientKeyPassword;
    bool acceptInvalidCert = false; // dangerous; explicit opt-in
    int64_t maxReceiveSize = 0;     // 0 = use config default
    std::string outputFilePath;     // if set, body streamed to disk

    static UltraNetHttpOptions Default() { return {}; }
};

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

    std::string GetBodyAsString() const {
        return std::string(body.begin(), body.end());
    }
    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
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

// Async entry point — Stage 2. Declared here so callers can link-check the
// signature; the Stage 1 implementation returns UltraNetInvalidHandle and an
// onComplete callback carrying a NotInitialized-style failure response.
UltraNetHandle UltraNet_HttpRequestAsync(
    const UltraNetHttpRequest& request,
    std::function<void(const UltraNetResponse&)> onComplete);

// File transfer helpers — both are streamed (constant memory regardless of
// payload size). Implemented via libcurl's write/read callbacks.
UltraNetResult UltraNet_HttpDownloadFile(
    const std::string& url,
    const std::string& localPath,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());

UltraNetResult UltraNet_HttpUploadFile(
    const std::string& url,
    const std::string& localPath,
    UltraNetResponse& outResponse,
    const UltraNetHttpOptions& options = UltraNetHttpOptions::Default());
