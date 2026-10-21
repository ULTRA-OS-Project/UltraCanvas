// Plugins/UltraIM/matrix/MatrixHttp.cpp
// Matrix provider - the homeserver's client-server API over UltraNet HTTP
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixHttp.h"

#include "UltraNet/UltraNetHttp.h"

#include <atomic>
#include <chrono>
#include <cstdio>

namespace UltraIMMatrix {

// Outlives the client for as long as a callback holds it; `alive` says
// whether a callback may still reach its handler.
struct MatrixHttp::Shared {
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool alive = true;
    bool stopping = false;
    std::string baseUrl;
    std::string token;
    std::set<UltraNetHandle> requests;
    int inFlight = 0;           // callbacks running right now; Stop waits for them

    bool Enter() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!alive || stopping) return false;
        ++inFlight;
        return true;
    }
    void Leave() {
        std::lock_guard<std::mutex> lock(mutex);
        if (--inFlight == 0) changed.notify_all();
    }
};

namespace {

UltraNetHttpMethod MethodFor(const std::string& method) {
    if (method == "POST") return UltraNetHttpMethod::Post;
    if (method == "PUT") return UltraNetHttpMethod::Put;
    if (method == "DELETE") return UltraNetHttpMethod::Delete;
    return UltraNetHttpMethod::Get;
}

MatrixResponse ToResponse(const UltraNetResponse& raw) {
    MatrixResponse response;
    if (!raw.transferError.empty()) {
        response.error = raw.transferError;
        return response;
    }
    response.status = raw.statusCode;
    const std::string text = raw.GetBodyAsString();
    if (!text.empty()) {
        UltraCanvas::JSONParseResult parsed;
        response.body = UltraCanvas::JSON::Parse(text, &parsed);
    }
    if (!response.Ok()) {
        response.errcode = response.body["errcode"].GetString();
        response.error = response.body["error"].GetString("HTTP " + std::to_string(raw.statusCode));
    }
    return response;
}

} // namespace

std::string PathSegment(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

MatrixHttp::MatrixHttp() : shared_(std::make_shared<Shared>()) {}

MatrixHttp::~MatrixHttp() {
    Stop();
}

void MatrixHttp::SetBaseUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->baseUrl = url;
    while (!shared_->baseUrl.empty() && shared_->baseUrl.back() == '/') shared_->baseUrl.pop_back();
}

std::string MatrixHttp::GetBaseUrl() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->baseUrl;
}

void MatrixHttp::SetToken(const std::string& accessToken) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->token = accessToken;
}

bool MatrixHttp::IsStopping() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->stopping;
}

void MatrixHttp::Request(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                         int timeoutMs, MatrixCallback onDone) {
    UltraNetHttpRequest request;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (!shared_->alive || shared_->stopping) return;
        request.url = shared_->baseUrl + path;
        if (!shared_->token.empty()) request.headers.Set("Authorization", "Bearer " + shared_->token);
    }
    request.method = MethodFor(method);
    request.headers.Set("Accept", "application/json");
    request.options.timeoutMs = timeoutMs > 0 ? timeoutMs : 30000;
    if (body) {
        const std::string text = UltraCanvas::JSON::Serialize(*body);
        request.body.assign(text.begin(), text.end());
        request.headers.Set("Content-Type", "application/json");
    } else if (method == "POST" || method == "PUT") {
        request.body = {'{', '}'};
        request.headers.Set("Content-Type", "application/json");
    }

    std::weak_ptr<Shared> weak = shared_;
    auto handleBox = std::make_shared<std::atomic<UltraNetHandle>>(UltraNetInvalidHandle);
    auto done = [weak, onDone, handleBox](const UltraNetResponse& raw) {
        auto shared = weak.lock();
        if (!shared) return;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->requests.erase(handleBox->load());
        }
        if (!shared->Enter()) return;
        if (onDone) onDone(ToResponse(raw));
        shared->Leave();
    };
    const UltraNetHandle handle = UltraNet_HttpRequestAsync(request, done);
    handleBox->store(handle);
    if (handle != UltraNetInvalidHandle) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (UltraNet_IsRequestActive(handle)) shared_->requests.insert(handle);
    }
}

MatrixResponse MatrixHttp::Await(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                                 int timeoutMs) {
    struct Wait {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        MatrixResponse response;
    };
    auto wait = std::make_shared<Wait>();
    Request(method, path, body, timeoutMs, [wait](const MatrixResponse& r) {
        std::lock_guard<std::mutex> lock(wait->mutex);
        wait->response = r;
        wait->done = true;
        wait->cv.notify_all();
    });
    std::shared_ptr<Shared> shared = shared_;
    std::unique_lock<std::mutex> lock(wait->mutex);
    while (!wait->done) {
        wait->cv.wait_for(lock, std::chrono::milliseconds(200));
        if (!wait->done && IsStopping()) {
            MatrixResponse cancelled;
            cancelled.cancelled = true;
            cancelled.error = "Stopped";
            return cancelled;
        }
    }
    return wait->response;
}

void MatrixHttp::Stop() {
    std::set<UltraNetHandle> requests;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (!shared_->alive) return;
        shared_->stopping = true;
        requests = shared_->requests;
    }
    for (UltraNetHandle handle : requests) UltraNet_CancelRequest(handle);
    std::unique_lock<std::mutex> lock(shared_->mutex);
    shared_->changed.wait(lock, [this]() { return shared_->inFlight == 0; });
    shared_->alive = false;
}

} // namespace UltraIMMatrix
