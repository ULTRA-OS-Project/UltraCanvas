// core/UltraNet/UltraNetHttpEasy.h
// Private helpers shared by UltraNetHttp.cpp (sync) and
// UltraNetHttpAsync.cpp (async). Not part of the public include surface.
// Version: 0.2.0 (Stage 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <curl/curl.h>

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <vector>

namespace ultranet_internal {

    // Body sink for libcurl WRITEFUNCTION. Either accumulates bytes into a
    // std::vector, streams to a FILE*, or fires a per-chunk callback for
    // streaming use cases (SSE, chunked LLM tokens). When onChunk is set,
    // the body / file accumulators are bypassed. maxBytes==0 means unlimited.
    //
    // The struct also carries the per-request progress callbacks. They live
    // here (rather than in a separate struct) so a single XFERINFODATA
    // pointer can carry everything libcurl needs from us.
    struct WriteSink {
        std::vector<uint8_t>* body = nullptr;
        std::FILE* file = nullptr;
        std::function<void(const std::vector<uint8_t>&)> onChunk;
        std::function<void(int64_t, int64_t)> perRequestDownload;
        std::function<void(int64_t, int64_t)> perRequestUpload;
        int64_t maxBytes = 0;
        int64_t received = 0;
        bool exceededLimit = false;
    };

    // libcurl callbacks. Public so other TUs can wire them on custom easy
    // handles, though ConfigureEasyHandle does this automatically.
    std::size_t WriteCallback(char* data, std::size_t size,
                              std::size_t nmemb, void* userdata);
    std::size_t HeaderCallback(char* data, std::size_t size,
                               std::size_t nmemb, void* userdata);

    // Maps a libcurl error code (plus the HTTP status, if any) to an
    // UltraNetResultCode. Used by both sync and async finalisation.
    UltraNetResultCode MapCurlError(CURLcode rc, long httpStatus);

    // Configures every option on `easy` from the request + global config
    // (method, headers, body, timeouts, TLS, proxy, auth, sink, callbacks).
    // Returns the curl_slist of HTTP headers; the caller owns it and must
    // free with curl_slist_free_all. The request body is NOT attached here
    // (callers do that with POSTFIELDS for sync; async stores a copy).
    curl_slist* ConfigureEasyHandle(CURL* easy,
                                    const UltraNetHttpRequest& request,
                                    const UltraNetConfig& config,
                                    WriteSink* sink,
                                    UltraNetResponse* responseForHeaders);

    // Populates `response` from completed easy handle and builds an
    // UltraNetResult from (rc, status). Does NOT free the easy handle.
    UltraNetResult FinalizeFromEasy(CURL* easy,
                                    CURLcode rc,
                                    const std::string& url,
                                    UltraNetResponse& response,
                                    bool exceededLimit);

    // Reads transfer stats (bytes / duration / speed / redirects) from
    // a libcurl easy handle. Safe to call mid-transfer.
    UltraNetTransferStats ReadTransferStats(CURL* easy);

} // namespace ultranet_internal
