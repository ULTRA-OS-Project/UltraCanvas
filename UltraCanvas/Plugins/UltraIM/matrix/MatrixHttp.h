// Plugins/UltraIM/matrix/MatrixHttp.h
// Matrix provider - the homeserver's client-server API over UltraNet HTTP
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace UltraIMMatrix {

struct MatrixResponse {
    int                    status = 0;      // HTTP status; 0 = the request never completed
    UltraCanvas::JSONValue body;
    std::string            errcode;         // M_FORBIDDEN, M_UNKNOWN_TOKEN, ...
    std::string            error;           // the server's message, or the transfer error
    bool                   cancelled = false;

    bool Ok() const { return status >= 200 && status < 300; }
};

using MatrixCallback = std::function<void(const MatrixResponse& response)>;

// Callbacks run on UltraNet's HTTP worker. After Stop() returns none runs,
// and every request still open was cancelled.
class MatrixHttp {
public:
    MatrixHttp();
    ~MatrixHttp();

    void SetBaseUrl(const std::string& url);    // "https://matrix.example.org"
    std::string GetBaseUrl() const;
    void SetToken(const std::string& accessToken);

    // path: "/_matrix/client/v3/..." (already URL-encoded where it needs to be).
    void Request(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                 int timeoutMs, MatrixCallback onDone);
    // The same, waiting for the answer on the calling thread; Stop() ends the
    // wait (the answer then says cancelled). For the provider's own threads.
    MatrixResponse Await(const std::string& method, const std::string& path, const UltraCanvas::JSONValue* body,
                         int timeoutMs);

    void Stop();
    bool IsStopping() const;

    struct Shared;      // with UltraNet's callbacks (defined in the .cpp)

private:
    std::shared_ptr<Shared> shared_;
};

// Percent-encodes a path segment ("!room:server" -> "%21room%3Aserver").
std::string PathSegment(const std::string& text);

} // namespace UltraIMMatrix
