// UltraSite/include/UltraSite/UltraSiteServer.h
// A preview server for a built site: the output folder served on this
// computer alone (127.0.0.1), the way a host serves it - the site's path
// in front (Builder::BasePath), a folder's index.html for its address, a
// redirect for an address that lacks its trailing slash, the 404 page with
// status 404, the cache rules of .htaccess, and the .br or .gz copy of a
// file when the build wrote one and the browser takes it. For looking at a
// site before it is uploaded (`ultrapress serve`), not for the internet:
// it answers one request at a time, GET and HEAD only.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace UltraSite {

struct ServeResponse {
    int status = 200;                  // 200, 301, 304 never, 400, 403, 404, 405
    std::string contentType;
    std::string contentEncoding;       // "br", "gzip" or empty
    std::string cacheControl;
    std::string location;              // a 301's
    std::string body;                  // empty for HEAD
};

// The answer to one request, without a socket: `target` is the request's
// path and query ("/v1/docs/?q=x"), `acceptEncoding` its Accept-Encoding.
ServeResponse Respond(const std::string& folder, const std::string& basePath, const std::string& method,
                      const std::string& target, const std::string& acceptEncoding);

struct ServeOptions {
    int port = 3000;                                   // 0: any free port
    std::string basePath = "/";                        // the site's path (Builder::BasePath)
    std::function<void(int port)> onReady;             // listening, on this port
    std::function<void(const std::string& line)> onRequest;   // "GET /docs/ 200", one per request
    const std::atomic<bool>* stop = nullptr;           // set to stop; checked a few times a second
    // Called before each request is answered, with its target: a preview
    // rebuilds the site here when its files changed.
    std::function<void(const std::string& target)> beforeRequest;
};

// Serves `folder` until `stop` is set (or for ever without one). Returns
// an empty string when it stopped, else why it could not listen - always
// so in a build without UltraNet (WebAssembly).
std::string Serve(const std::string& folder, const ServeOptions& options);

} // namespace UltraSite
