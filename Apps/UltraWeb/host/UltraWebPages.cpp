// Apps/UltraWeb/host/UltraWebPages.cpp
// The page services and the app-or-page decision (UltraWebPages.h).
// Version: 0.5.0 - downloads: Content-Disposition and the size limit reported
// Version: 0.4.1 - AcceptExtensions for file uploads
// Version: 0.4.0 - forms: POST through UltraNet
// Version: 0.3.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebPages.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetMime.h"
#include "UltraNet/UltraNetUrl.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <system_error>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool StartsWithLower(const std::string& s, const std::string& prefix) {
    return Lower(s.substr(0, prefix.size())) == prefix;
}

// A file:// URL's path, percent-escapes undone.
std::string FilePath(const std::string& url) {
    std::string path = UltraNet_UrlDecode(url.substr(7));
    const size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos) path = path.substr(0, cut);
#ifdef _WIN32
    // file:///C:/dir -> C:/dir
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path = path.substr(1);
#endif
    return path;
}

HTML::FetchResult ReadFile(const std::string& url) {
    HTML::FetchResult result;
    result.finalUrl = url;
    const std::string path = FilePath(url);
    std::error_code ec;
    if (std::filesystem::is_directory(PathFromUtf8(path), ec)) {
        // A folder: a page listing it.
        std::string html = "<!DOCTYPE html><html><head><title>" + HTML::EscapeText(path) +
                           "</title></head><body><h1>" + HTML::EscapeText(path) + "</h1><ul>";
        std::vector<std::pair<bool, std::string>> entries;
        for (const auto& entry : std::filesystem::directory_iterator(PathFromUtf8(path), ec)) {
            entries.emplace_back(!entry.is_directory(ec), PathToUtf8(entry.path().filename()));
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& [isFile, name] : entries) {
            const std::string shown = name + (isFile ? "" : "/");
            html += "<li><a href=\"" + HTML::EscapeText(UltraNet_UrlEncode(name) + (isFile ? "" : "/")) + "\">" +
                    HTML::EscapeText(shown) + "</a></li>";
        }
        html += "</ul></body></html>";
        result.ok = true;
        result.status = 200;
        result.contentType = "text/html; charset=utf-8";
        result.body.assign(html.begin(), html.end());
        if (!url.empty() && url.back() != '/') result.finalUrl = url + "/";
        return result;
    }
    std::FILE* file = OpenFileUtf8(path, "rb");
    if (!file) {
        result.error = "Cannot open " + path + ".";
        return result;
    }
    uint8_t chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        result.body.insert(result.body.end(), chunk, chunk + got);
        if (static_cast<int64_t>(result.body.size()) > kMaxPageBytes) break;
    }
    std::fclose(file);
    if (static_cast<int64_t>(result.body.size()) > kMaxPageBytes) {
        result.body.clear();
        result.tooLarge = true;
        result.error = path + " is larger than " + std::to_string(kMaxPageBytes / (1024 * 1024)) + " MB.";
        return result;
    }
    result.ok = true;
    result.status = 200;
    return result;   // no Content-Type: the loader goes by the name and the bytes
}

} // namespace

HTML::PageLoaderServices MakePageServices(std::function<void(std::function<void()>)> post) {
    HTML::PageLoaderServices services;
    services.fetch = [post](const std::string& url, std::function<void(HTML::FetchResult)> done) {
        auto answer = [post, done](HTML::FetchResult result) {
            post([done, result = std::move(result)]() mutable { done(std::move(result)); });
        };
        if (StartsWithLower(url, "file://")) {
            answer(ReadFile(url));
            return;
        }
        if (!StartsWithLower(url, "http://") && !StartsWithLower(url, "https://")) {
            HTML::FetchResult refused;
            refused.error = "UltraWeb does not open " + url.substr(0, url.find(':')) + ": addresses.";
            answer(std::move(refused));
            return;
        }
        UltraNetHttpRequest request;
        request.url = url;
        request.options.maxReceiveSize = kMaxPageBytes;
        request.options.timeoutMs = 30000;
        request.headers.Set("Accept", "text/html,application/xhtml+xml,text/plain;q=0.9,image/*;q=0.8,*/*;q=0.5");
        UltraNet_HttpRequestAsync(request, [answer, url](const UltraNetResponse& response) {
            HTML::FetchResult result;
            result.status = response.statusCode;
            result.finalUrl = response.finalUrl;
            result.contentType = response.contentType;
            result.contentDisposition = response.headers.Get("Content-Disposition");
            if (response.statusCode == 0) {
                result.error = "Could not load " + url +
                               (response.statusMessage.empty() ? std::string(" (no answer).") : ": " + response.statusMessage);
            } else if (response.exceededReceiveLimit) {
                // A file to download, perhaps: the loader decides, and the
                // window fetches it again, streaming, to disk.
                result.tooLarge = true;
                result.error = url + " is larger than " + std::to_string(kMaxPageBytes / (1024 * 1024)) + " MB.";
            } else if (!response.IsComplete()) {
                result.error = "Could not load " + url + ": " + response.transferError;
            } else {
                result.ok = true;
                result.body = response.body;
            }
            answer(std::move(result));
        });
    };
    // A form's POST: http(s) only, the answer read as a page.
    services.post = [post](const std::string& url, const std::string& contentType, const std::string& body,
                           std::function<void(HTML::FetchResult)> done) {
        auto answer = [post, done](HTML::FetchResult result) {
            post([done, result = std::move(result)]() mutable { done(std::move(result)); });
        };
        if (!StartsWithLower(url, "http://") && !StartsWithLower(url, "https://")) {
            HTML::FetchResult refused;
            refused.error = "UltraWeb sends forms only to http and https addresses.";
            answer(std::move(refused));
            return;
        }
        UltraNetHttpRequest request;
        request.url = url;
        request.method = UltraNetHttpMethod::Post;
        request.body.assign(body.begin(), body.end());
        request.options.maxReceiveSize = kMaxPageBytes;
        request.options.timeoutMs = 30000;
        request.headers.Set("Content-Type", contentType);
        request.headers.Set("Accept", "text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5");
        UltraNet_HttpRequestAsync(request, [answer, url](const UltraNetResponse& response) {
            HTML::FetchResult result;
            result.status = response.statusCode;
            result.finalUrl = response.finalUrl;
            result.contentType = response.contentType;
            if (response.statusCode == 0) {
                result.error = "Could not send the form to " + url +
                               (response.statusMessage.empty() ? std::string(" (no answer).") : ": " + response.statusMessage);
            } else if (response.exceededReceiveLimit) {
                result.error = url + " answered with more than " + std::to_string(kMaxPageBytes / (1024 * 1024)) + " MB.";
            } else if (!response.IsComplete()) {
                result.error = "Could not send the form to " + url + ": " + response.transferError;
            } else {
                result.ok = true;
                result.body = response.body;
            }
            answer(std::move(result));
        });
    };
    services.resolve = [](const std::string& base, const std::string& reference, std::string& absolute) {
        return static_cast<bool>(UltraNet_ResolveUrl(base, reference, absolute));
    };
    services.toUtf8 = [](const std::string& bytes, const std::string& charset) {
        return UltraNet_ConvertToUtf8(bytes, charset);
    };
    return services;
}

bool IsAppAddress(const std::string& address) {
    const std::string lower = Lower(address);
    if (lower.rfind("about:", 0) == 0) return true;
    std::string path = lower.substr(0, lower.find_first_of("?#"));
    auto endsWith = [&](const char* ext) {
        const std::string e(ext);
        return path.size() >= e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0;
    };
    return endsWith(".wasm") || endsWith(".wat");
}

std::vector<std::string> AcceptExtensions(const std::string& accept) {
    std::vector<std::string> out;
    auto add = [&](const std::string& ext) {
        if (!ext.empty() && std::find(out.begin(), out.end(), ext) == out.end()) out.push_back(ext);
    };
    size_t start = 0;
    while (start <= accept.size()) {
        size_t comma = accept.find(',', start);
        if (comma == std::string::npos) comma = accept.size();
        std::string item = Lower(accept.substr(start, comma - start));
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty() && item[0] == '.') add(item.substr(1));
        else if (item == "image/*") for (const char* e : { "png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "avif" }) add(e);
        else if (item == "audio/*") for (const char* e : { "mp3", "ogg", "wav", "flac", "m4a" }) add(e);
        else if (item == "video/*") for (const char* e : { "mp4", "webm", "mkv", "mov" }) add(e);
        else if (item == "application/pdf") add("pdf");
        else if (item == "text/plain") add("txt");
        else if (!item.empty()) return {};           // a type we cannot map: any file
        start = comma + 1;
    }
    return out;
}

std::string ToPageUrl(const std::string& address) {
    const std::string lower = Lower(address);
    if (lower.find("://") != std::string::npos || lower.rfind("about:", 0) == 0) return address;
    std::error_code ec;
    std::filesystem::path path = std::filesystem::absolute(PathFromUtf8(address), ec);
    if (ec) path = PathFromUtf8(address);
    std::string text = PathToUtf8(path.lexically_normal());
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.empty() || text[0] != '/') text = "/" + text;        // C:/x -> /C:/x
    std::string url = "file://";
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            url += static_cast<char>(c);
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            url += escaped;
        }
    }
    return url;
}

} // namespace UltraWeb
