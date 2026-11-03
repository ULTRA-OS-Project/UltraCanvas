// Apps/UltraWeb/host/UltraWebNetworkFetch.cpp
// An app's fetch sent through UltraNet (MakeNetworkFetch in
// UltraWebFetch.h). Apart from the rules in UltraWebFetch.cpp so that a
// build without UltraNet - the browser runtime - can take the rules alone.
// Version: 0.2.1 - moved out of UltraWebFetch.cpp
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebFetch.h"

#include "UltraNet/UltraNetHttp.h"

#include <atomic>
#include <memory>

namespace UltraWeb {

FetchService MakeNetworkFetch(std::function<void(std::function<void()>)> post) {
    return [post](const FetchRequest& request, std::function<void(FetchResponse)> done) -> std::function<void()> {
        UltraNetHttpRequest http;
        http.url = request.url;
        if (request.method == "GET") http.method = UltraNetHttpMethod::Get;
        else if (request.method == "HEAD") http.method = UltraNetHttpMethod::Head;
        else if (request.method == "POST") http.method = UltraNetHttpMethod::Post;
        else if (request.method == "PUT") http.method = UltraNetHttpMethod::Put;
        else if (request.method == "DELETE") http.method = UltraNetHttpMethod::Delete;
        else http.method = UltraNetHttpMethod::Patch;
        http.body = request.body;
        if (!request.contentType.empty()) http.headers.Set("Content-Type", request.contentType);
        if (request.sendOrigin) http.headers.Set("Origin", request.origin);
        http.options.timeoutMs = FetchRules::kTimeoutMs;
        http.options.maxReceiveSize = int64_t(FetchRules::kMaxResponseBytes);
        http.options.followRedirects = true;
        http.options.maxRedirects = FetchRules::kMaxRedirects;

        // A cancelled request still calls back (as "Cancelled"); the flag,
        // read on the UI thread where the cancel also runs, drops it.
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        const UltraNetHandle handle = UltraNet_HttpRequestAsync(http, [post, done, cancelled](const UltraNetResponse& r) {
            FetchResponse out;
            out.status = r.statusCode;
            out.error = r.transferError;
            if (out.status == 0 && out.error.empty()) out.error = r.statusMessage.empty() ? "no answer" : r.statusMessage;
            out.tooLarge = r.exceededReceiveLimit;
            out.finalUrl = r.finalUrl;
            out.headers = r.headers.Entries();
            out.body = r.body;
            post([done, cancelled, out = std::move(out)]() mutable {
                if (!cancelled->load()) done(std::move(out));
            });
        });
        return [handle, cancelled]() {
            cancelled->store(true);
            if (handle != UltraNetInvalidHandle) UltraNet_CancelRequest(handle);
        };
    };
}

} // namespace UltraWeb
