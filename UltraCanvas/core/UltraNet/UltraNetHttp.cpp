// core/UltraNet/UltraNetHttp.cpp
// Synchronous HTTP verbs + UltraNetHttpHeaders. Uses ultranet_internal helpers
// from UltraNetHttpEasy.h to keep option-setting logic in one place.
// Async lives in UltraNetHttpAsync.cpp.
// Version: 0.2.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetHttp.h"
#include "UltraNetHttpEasy.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
    bool IEquals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }
}

// ============================================================================
// UltraNetHttpHeaders — case-insensitive header bag
// ============================================================================
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
    for (const auto& kv : entries_) if (IEquals(kv.first, name)) return kv.second;
    return {};
}

std::vector<std::string>
UltraNetHttpHeaders::GetAll(const std::string& name) const {
    std::vector<std::string> out;
    for (const auto& kv : entries_) if (IEquals(kv.first, name)) out.push_back(kv.second);
    return out;
}

bool UltraNetHttpHeaders::Has(const std::string& name) const {
    for (const auto& kv : entries_) if (IEquals(kv.first, name)) return true;
    return false;
}

// ============================================================================
// Synchronous request — single source of truth for the sync path
// ============================================================================
namespace {

    UltraNetResult PerformSync(const UltraNetHttpRequest& request,
                               UltraNetResponse& outResponse,
                               ultranet_internal::WriteSink& sink,
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

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(
            curl_easy_init(), curl_easy_cleanup);
        if (!easy) {
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                         "curl_easy_init() failed");
        }

        curl_slist* slist = ultranet_internal::ConfigureEasyHandle(
            easy.get(), request, cfg, &sink, &outResponse);
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
            slistGuard(slist, curl_slist_free_all);

        if (readBody && !readBody->empty()) {
            curl_easy_setopt(easy.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(readBody->size()));
            curl_easy_setopt(easy.get(), CURLOPT_POSTFIELDS, readBody->data());
        }

        CURLcode rc = curl_easy_perform(easy.get());
        return ultranet_internal::FinalizeFromEasy(
            easy.get(), rc, request.url, outResponse, sink.exceededLimit);
    }
}

UltraNetResult UltraNet_HttpRequest(const UltraNetHttpRequest& request,
                                    UltraNetResponse& outResponse) {
    outResponse = {};
    ultranet_internal::WriteSink sink;
    sink.body = &outResponse.body;
    return PerformSync(request, outResponse, sink, &request.body);
}

UltraNetResult UltraNet_HttpGet(const std::string& url,
                                UltraNetResponse& outResponse,
                                const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Get; req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPost(const std::string& url,
                                 const std::vector<uint8_t>& body,
                                 UltraNetResponse& outResponse,
                                 const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Post;
    req.body = body; req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPut(const std::string& url,
                                const std::vector<uint8_t>& body,
                                UltraNetResponse& outResponse,
                                const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Put;
    req.body = body; req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpDelete(const std::string& url,
                                   UltraNetResponse& outResponse,
                                   const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Delete; req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpHead(const std::string& url,
                                 UltraNetResponse& outResponse,
                                 const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Head; req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}

UltraNetResult UltraNet_HttpPatch(const std::string& url,
                                  const std::vector<uint8_t>& body,
                                  UltraNetResponse& outResponse,
                                  const UltraNetHttpOptions& options) {
    UltraNetHttpRequest req;
    req.url = url; req.method = UltraNetHttpMethod::Patch;
    req.body = body; req.options = options;
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
    req.url = url; req.method = UltraNetHttpMethod::Get; req.options = options;

    UltraNetResponse resp;
    ultranet_internal::WriteSink sink;
    sink.file = fp;

    UltraNetResult result = PerformSync(req, resp, sink, nullptr);
    std::fclose(fp);
    return result;
}

UltraNetResult UltraNet_HttpUploadFile(const std::string& url,
                                       const std::string& localPath,
                                       UltraNetResponse& outResponse,
                                       const UltraNetHttpOptions& options) {
    std::FILE* fp = std::fopen(localPath.c_str(), "rb");
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                     "cannot open local file for read");
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> body(static_cast<std::size_t>(size > 0 ? size : 0));
    if (size > 0) std::fread(body.data(), 1, body.size(), fp);
    std::fclose(fp);

    UltraNetHttpRequest req;
    req.url     = url;
    req.method  = UltraNetHttpMethod::Put;
    req.body    = std::move(body);
    req.options = options;
    return UltraNet_HttpRequest(req, outResponse);
}
