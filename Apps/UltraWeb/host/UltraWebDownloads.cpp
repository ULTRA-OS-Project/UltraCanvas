// Apps/UltraWeb/host/UltraWebDownloads.cpp
// Saving downloads (UltraWebDownloads.h).
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebDownloads.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

std::atomic<int> g_stallTimeoutMs{ 30000 };

bool StartsWithLower(const std::string& s, const char* prefix) {
    for (size_t i = 0; prefix[i]; ++i) {
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    }
    return true;
}

// The part file becomes the download: replacing a file already there.
bool Finish(const std::string& part, const std::string& path, std::string& error) {
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(path), ec);   // Windows will not rename over a file
    ec.clear();
    std::filesystem::rename(PathFromUtf8(part), PathFromUtf8(path), ec);
    if (ec) {
        error = "cannot save " + path + ": " + ec.message();
        std::filesystem::remove(PathFromUtf8(part), ec);
        return false;
    }
    return true;
}

class HttpDownload : public Download, public std::enable_shared_from_this<HttpDownload> {
public:
    std::string url, path, part;
    std::function<void(std::function<void()>)> post;
    std::function<void(int64_t, int64_t)> onProgress;
    std::function<void(bool, const std::string&)> onDone;
    std::atomic<bool> cancelled{ false };
    std::atomic<int64_t> lastReport{ 0 };     // ms since epoch of the last progress post
    UltraNetHandle handle = 0;

    void Cancel() override {
        if (cancelled.exchange(true)) return;
        if (handle) UltraNet_CancelRequest(handle);
    }

    void Start() {
        auto self = shared_from_this();
        UltraNetHttpRequest request;
        request.url = url;
        request.options.maxReceiveSize = int64_t(1) << 42;   // 4 TB: no limit worth the name
        // UltraNet streams the body to the part file and closes it before
        // onComplete. A download is as long as the file is large, so it has
        // no whole-transfer limit; one that stops moving ends as a stall.
        request.options.outputFilePath = part;
        request.options.timeoutMs = -1;
        request.options.stallTimeoutMs = g_stallTimeoutMs.load();
        request.onDownloadProgress = [self](int64_t now, int64_t total) {
            // A few reports a second are enough for a status line.
            const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (ms - self->lastReport < 250) return;
            self->lastReport = ms;
            if (total <= 0) total = -1;    // the list reads -1 as "no total"
            if (self->onProgress) self->post([self, now, total]() { self->onProgress(now, total); });
        };
        handle = UltraNet_HttpRequestAsync(request, [self](const UltraNetResponse& response) {
            std::string error;
            if (self->cancelled) error = "cancelled";
            else if (response.statusCode == 0) error = response.statusMessage.empty() ? "no answer" : response.statusMessage;
            else if (response.statusCode >= 400) error = "the server answered " + std::to_string(response.statusCode);
            else if (!response.IsComplete()) error = response.transferError;
            if (!error.empty()) {
                std::error_code ec;
                std::filesystem::remove(PathFromUtf8(self->part), ec);
                self->Done(false, error);
                return;
            }
            self->Done(Finish(self->part, self->path, error), error);
        });
    }

    void Done(bool ok, const std::string& error) {
        auto self = shared_from_this();
        post([self, ok, error]() { if (self->onDone) self->onDone(ok, error); });
    }
};

// A local file is copied, on the UI thread: a disk-to-disk copy.
class CopyDownload : public Download {
public:
    void Cancel() override {}
};

} // namespace

bool SaveBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error) {
    const std::string part = path + ".part";
    std::FILE* file = OpenFileUtf8(part, "wb");
    if (!file) {
        error = "cannot write " + part;
        return false;
    }
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(part), ec);
        error = "cannot write " + path + " (disk full?)";
        return false;
    }
    return Finish(part, path, error);
}

std::shared_ptr<Download> StartDownload(const std::string& url, const std::string& path,
                                        std::function<void(std::function<void()>)> post,
                                        std::function<void(int64_t, int64_t)> onProgress,
                                        std::function<void(bool, const std::string&)> onDone) {
    if (StartsWithLower(url, "file://")) {
        std::string source = url.substr(7);
        source = source.substr(0, source.find_first_of("?#"));
        std::string decoded;
        for (size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '%' && i + 2 < source.size()) {
                decoded += static_cast<char>(std::strtol(source.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                decoded += source[i];
            }
        }
#ifdef _WIN32
        if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':') decoded.erase(0, 1);
#endif
        std::error_code ec;
        const std::string part = path + ".part";
        std::filesystem::copy_file(PathFromUtf8(decoded), PathFromUtf8(part),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::string error = ec ? "cannot copy " + decoded + ": " + ec.message() : std::string();
        const bool ok = !ec && Finish(part, path, error);
        post([onDone, ok, error]() { if (onDone) onDone(ok, error); });
        return std::make_shared<CopyDownload>();
    }
    auto download = std::make_shared<HttpDownload>();
    download->url = url;
    download->path = path;
    download->part = path + ".part";
    download->post = std::move(post);
    download->onProgress = std::move(onProgress);
    download->onDone = std::move(onDone);
    if (!StartsWithLower(url, "http://") && !StartsWithLower(url, "https://")) {
        download->Done(false, "UltraWeb downloads only http, https and local files");
        return download;
    }
    download->Start();
    return download;
}

void SetDownloadStallTimeout(int milliseconds) {
    g_stallTimeoutMs = milliseconds > 0 ? milliseconds : 30000;
}

std::string ByteSize(int64_t bytes) {
    // Dot-decimal whatever the locale: tenths counted, not printed with %f.
    auto tenths = [](int64_t value, int64_t unit, const char* name) {
        const int64_t t = (value * 10 + unit / 2) / unit;
        return std::to_string(t / 10) + "." + std::to_string(t % 10) + " " + name;
    };
    if (bytes >= 1024LL * 1024 * 1024) return tenths(bytes, 1024LL * 1024 * 1024, "GB");
    if (bytes >= 1024 * 1024) return tenths(bytes, 1024 * 1024, "MB");
    if (bytes >= 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
}

std::string DefaultDownloadFolder() {
    for (const UserFolderInfo& folder : GetWellKnownUserFolders())
        if (folder.kind == UserFolderKind::Downloads) return folder.path;
    if (const char* home = std::getenv("HOME")) return home;
    return std::string();
}

} // namespace UltraWeb
