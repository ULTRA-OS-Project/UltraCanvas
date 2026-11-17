// UltraSite/core/UltraSiteServer.cpp
// The preview server (UltraSiteServer.h): requests answered from the
// output folder over UltraNet's sockets, on the loopback interface only.
// A build without UltraNet (WebAssembly, -DULTRACANVAS_ENABLE_NET=OFF) keeps
// Respond; its Serve only says it cannot listen.
// Version: 0.1.1 - builds without UltraNet: Serve reports it has no network
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSiteServer.h"
#include "UltraSiteInternal.h"

#include "UltraCanvasPathUtf8.h"
#if defined(ULTRACANVAS_HAS_NET)
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetSocket.h"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace UltraSite {

namespace {

const std::map<std::string, std::string>& Types() {
    static const std::map<std::string, std::string> types = {
        {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"}, {".mjs", "text/javascript; charset=utf-8"},
        {".json", "application/json"}, {".xml", "application/xml"}, {".txt", "text/plain; charset=utf-8"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"}, {".webp", "image/webp"}, {".avif", "image/avif"}, {".ico", "image/x-icon"},
        {".woff2", "font/woff2"}, {".woff", "font/woff"}, {".ttf", "font/ttf"}, {".pdf", "application/pdf"},
        {".zip", "application/zip"}, {".wasm", "application/wasm"}, {".mp4", "video/mp4"}, {".webm", "video/webm"},
    };
    return types;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// "%20" -> " "; false for a broken escape.
bool Unescape(const std::string& text, std::string& out) {
    out.clear();
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out += text[i];
            continue;
        }
        if (i + 2 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            return false;
        }
        out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
        i += 2;
    }
    return true;
}

bool Accepts(const std::string& acceptEncoding, const std::string& coding) {
    const std::string list = Lower(acceptEncoding);
    size_t at = 0;
    while ((at = list.find(coding, at)) != std::string::npos) {
        const bool startOk = at == 0 || list[at - 1] == ' ' || list[at - 1] == ',';
        const size_t end = at + coding.size();
        const bool endOk = end == list.size() || list[end] == ',' || list[end] == ';' || list[end] == ' ';
        if (startOk && endOk) return list.compare(end, 4, ";q=0") != 0 || list.compare(end, 6, ";q=0.") == 0;
        at = end;
    }
    return false;
}

// A name the build gave its content hash: "site.3f9a1c2b7e.css".
bool Hashed(const std::string& name) {
    const size_t last = name.rfind('.');
    if (last == std::string::npos || last < 11) return false;
    const size_t dot = name.rfind('.', last - 1);
    if (dot == std::string::npos || last - dot != 11) return false;
    for (size_t i = dot + 1; i < last; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
    }
    return true;
}

} // namespace

ServeResponse Respond(const std::string& folder, const std::string& basePath, const std::string& method,
                      const std::string& target, const std::string& acceptEncoding) {
    ServeResponse response;
    const bool head = method == "HEAD";
    if (method != "GET" && !head) {
        response.status = 405;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "GET and HEAD only\n";
        return response;
    }
    std::string base = basePath.empty() ? "/" : basePath;
    if (base.back() != '/') base += '/';
    std::string path = target.substr(0, target.find_first_of("?#"));
    std::string decoded;
    if (path.empty() || path[0] != '/' || !Unescape(path, decoded) || decoded.find('\0') != std::string::npos) {
        response.status = 400;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "bad request\n";
        return response;
    }
    const fs::path root = UltraCanvas::PathFromUtf8(folder);
    auto notFound = [&]() {
        response = ServeResponse();
        response.status = 404;
        response.contentType = "text/html; charset=utf-8";
        response.cacheControl = "no-cache";
        if (!ReadFile(UltraCanvas::PathToUtf8(root / "404.html"), response.body)) response.body = "<!doctype html><title>404</title><h1>404</h1>\n";
        if (head) response.body.clear();
        return response;
    };
    if (decoded + "/" == base) {
        response.status = 301;
        response.location = base;
        return response;
    }
    if (decoded.compare(0, base.size(), base) != 0) return notFound();
    std::string relative = decoded.substr(base.size());
    // Nothing outside the folder, nothing hidden (.htaccess, a deploy manifest).
    for (size_t start = 0; start <= relative.size();) {
        size_t end = relative.find('/', start);
        if (end == std::string::npos) end = relative.size();
        const std::string part = relative.substr(start, end - start);
        if (part == ".." || (!part.empty() && part[0] == '.')) {
            response.status = 403;
            response.contentType = "text/plain; charset=utf-8";
            response.body = head ? "" : "forbidden\n";
            return response;
        }
        start = end + 1;
    }
    std::error_code ec;
    fs::path file = root / UltraCanvas::PathFromUtf8(relative);
    if (fs::is_directory(file, ec)) {
        if (!relative.empty() && relative.back() != '/') {
            response.status = 301;
            response.location = decoded + "/";
            return response;
        }
        file /= "index.html";
    }
    if (!fs::is_regular_file(file, ec)) return notFound();

    const std::string name = UltraCanvas::PathToUtf8(file.filename());
    const std::string extension = Lower(UltraCanvas::PathToUtf8(file.extension()));
    auto type = Types().find(extension);
    response.contentType = type == Types().end() ? "application/octet-stream" : type->second;
    response.cacheControl = Hashed(name) ? "public, max-age=31536000, immutable" : "no-cache";
    fs::path chosen = file;
    for (const auto& [suffix, coding] : {std::pair<const char*, const char*>{".br", "br"}, {".gz", "gzip"}}) {
        fs::path compressed = file;
        compressed += suffix;
        if (Accepts(acceptEncoding, coding) && fs::is_regular_file(compressed, ec)) {
            chosen = compressed;
            response.contentEncoding = coding;
            break;
        }
    }
    if (!ReadFile(UltraCanvas::PathToUtf8(chosen), response.body)) return notFound();
    if (head) response.body.clear();
    return response;
}

#if !defined(ULTRACANVAS_HAS_NET)
std::string Serve(const std::string&, const ServeOptions& options) {
    return "cannot listen on 127.0.0.1:" + std::to_string(options.port) + " - this build has no UltraNet";
}
#else
std::string Serve(const std::string& folder, const ServeOptions& options) {
    UltraNet_Initialize();
    UltraNetSocketOptions socket;
    socket.bindAddress = "127.0.0.1";
    socket.reuseAddress = true;
    const UltraNetHandle listener = UltraNet_TcpListen(options.port, socket);
    if (listener == UltraNetInvalidHandle) return "cannot listen on 127.0.0.1:" + std::to_string(options.port);
    UltraNetEndpoint local;
    int port = options.port;
    if (UltraNet_SocketLocalEndpoint(listener, local).success) port = local.port;
    if (options.onReady) options.onReady(port);
    while (!(options.stop && options.stop->load())) {
        UltraNetEndpoint remote;
        const UltraNetHandle client = UltraNet_TcpAccept(listener, remote, 250);
        if (client == UltraNetInvalidHandle) continue;
        // The request's head, up to its blank line.
        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536) {
            std::vector<uint8_t> chunk;
            if (!UltraNet_TcpReceive(client, chunk, 8192, 5000).success || chunk.empty()) break;
            request.append(chunk.begin(), chunk.end());
        }
        const size_t lineEnd = request.find("\r\n");
        const std::string line = request.substr(0, lineEnd);
        const size_t firstSpace = line.find(' ');
        const size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : line.find(' ', firstSpace + 1);
        std::string acceptEncoding;
        for (size_t at = lineEnd; at != std::string::npos && at < request.size();) {
            const size_t next = request.find("\r\n", at + 2);
            const std::string header = request.substr(at + 2, next == std::string::npos ? std::string::npos : next - at - 2);
            if (Lower(header).rfind("accept-encoding:", 0) == 0) acceptEncoding = header.substr(16);
            at = next;
        }
        ServeResponse response;
        std::string method, target;
        if (secondSpace == std::string::npos) {
            response.status = 400;
            response.contentType = "text/plain; charset=utf-8";
            response.body = "bad request\n";
        } else {
            method = line.substr(0, firstSpace);
            target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            if (options.beforeRequest) options.beforeRequest(target);
            response = Respond(folder, options.basePath, method, target, acceptEncoding);
        }
        const char* reason = response.status == 200 ? "OK" : response.status == 301 ? "Moved Permanently" : response.status == 400 ? "Bad Request"
                           : response.status == 403 ? "Forbidden" : response.status == 405 ? "Method Not Allowed" : "Not Found";
        std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " + reason + "\r\n";
        if (!response.contentType.empty()) head += "Content-Type: " + response.contentType + "\r\n";
        if (!response.contentEncoding.empty()) head += "Content-Encoding: " + response.contentEncoding + "\r\nVary: Accept-Encoding\r\n";
        if (!response.cacheControl.empty()) head += "Cache-Control: " + response.cacheControl + "\r\n";
        if (!response.location.empty()) head += "Location: " + response.location + "\r\n";
        std::string length = std::to_string(response.body.size());
        if (method == "HEAD") {
            // HEAD names the length a GET would send.
            const ServeResponse full = Respond(folder, options.basePath, "GET", target, acceptEncoding);
            length = std::to_string(full.body.size());
        }
        head += "Content-Length: " + length + "\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
        std::vector<uint8_t> bytes(head.begin(), head.end());
        bytes.insert(bytes.end(), response.body.begin(), response.body.end());
        size_t sent = 0;
        while (sent < bytes.size()) {
            std::vector<uint8_t> part(bytes.begin() + static_cast<std::ptrdiff_t>(sent),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), sent + 65536)));
            int count = 0;
            if (!UltraNet_TcpSend(client, part, count).success || count <= 0) break;
            sent += static_cast<size_t>(count);
        }
        UltraNet_SocketClose(client);
        if (options.onRequest) options.onRequest(method + " " + target + " " + std::to_string(response.status));
    }
    UltraNet_SocketClose(listener);
    return std::string();
}
#endif

} // namespace UltraSite
