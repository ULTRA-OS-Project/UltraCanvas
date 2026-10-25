// Apps/UltraStore/core/Downloader.cpp
// Package downloads on UltraNet (Downloader.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Downloader.h"
#include "Verifier.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace UltraStore {

namespace fs = std::filesystem;

Downloader::Downloader(std::string downloadsDirUtf8) : dir_(std::move(downloadsDirUtf8)) {}

std::string Downloader::FileNameFor(const Package& package) {
    std::string format = package.format.empty() ? std::string("bin") : package.format;
    for (char& c : format) if (c == '/' || c == '\\' || c == ':') c = '_';
    return package.sha256 + "." + format;
}

void Downloader::Pause() {
    paused_ = true;
    if (const uint64_t h = handle_.load()) UltraNet_PauseRequest(h);
}

void Downloader::Resume() {
    paused_ = false;
    if (const uint64_t h = handle_.load()) UltraNet_ResumeRequest(h);
}

void Downloader::Cancel() {
    cancelled_ = true;
    if (const uint64_t h = handle_.load()) UltraNet_CancelRequest(h);
}

void Downloader::Clear() {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(UltraCanvas::PathFromUtf8(dir_), ec)) {
        fs::remove(entry.path(), ec);
    }
}

Result<std::string> Downloader::Fetch(const Package& package, const DownloadProgress& progress) {
    using R = Result<std::string>;
    if (package.url.empty() || package.sha256.size() != 64) {
        return R::Failure(ErrorCode::InvalidArgument, "The package has no url or no SHA-256");
    }
    std::error_code ec;
    const fs::path dir = UltraCanvas::PathFromUtf8(dir_);
    fs::create_directories(dir, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "The downloads folder could not be created", dir_);
    const fs::path finalPath = dir / UltraCanvas::PathFromUtf8(FileNameFor(package));
    const fs::path partPath = dir / UltraCanvas::PathFromUtf8(FileNameFor(package) + ".part");
    const std::string finalUtf8 = UltraCanvas::PathToUtf8(finalPath);
    const std::string partUtf8 = UltraCanvas::PathToUtf8(partPath);

    // Already here from an earlier run, and still the catalog's bytes.
    if (fs::exists(finalPath, ec)) {
        if (PackageHashMatches(Sha256HexOfFile(finalUtf8), package.sha256)) {
            if (progress) progress(static_cast<int64_t>(package.size), static_cast<int64_t>(package.size));
            return R::Succeed(finalUtf8);
        }
        fs::remove(finalPath, ec);
    }

    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    cancelled_ = false;
    handle_ = 0;

    UltraNetHttpRequest request;
    request.url = package.url;
    request.method = UltraNetHttpMethod::Get;
    request.options.outputFilePath = partUtf8;
    const bool resume = fs::exists(partPath, ec) && fs::file_size(partPath, ec) > 0;
    request.options.resumeOutputFile = resume;
    request.options.httpsOnlyRedirects = true;
    request.options.timeoutMs = -1;          // as long as the file is large; the stall timeout guards it
    // maxReceiveSize stays at the config default (unlimited): a package is
    // as large as the catalog says, and the hash check below refuses
    // anything that is not exactly that file.
    if (progress) {
        request.onDownloadProgress = [this, &progress](int64_t now, int64_t total) {
            if (!progress(now, total)) Cancel();
        };
    }

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    UltraNetResponse response;
    const uint64_t handle = UltraNet_HttpRequestAsync(request, [&](const UltraNetResponse& r) {
        std::lock_guard<std::mutex> lock(m);
        response = r;
        done = true;
        cv.notify_all();
    });
    handle_ = handle;
    if (paused_ && handle) UltraNet_PauseRequest(handle);
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return done; });
    }
    handle_ = 0;

    if (cancelled_) return R::Failure(ErrorCode::NetworkError, "The download was cancelled", package.url);
    if (!response.transferError.empty()) {
        return R::Failure(ErrorCode::NetworkError, "The download did not finish", response.transferError + " (" + package.url + ")");
    }
    // 200 the whole file, 206 the rest of it, 416 the partial file was already complete.
    if (!(response.statusCode == 200 || response.statusCode == 206 || response.statusCode == 416)) {
        fs::remove(partPath, ec);
        return R::Failure(ErrorCode::NetworkError, "The server answered " + std::to_string(response.statusCode), package.url);
    }
    const std::string got = Sha256HexOfFile(partUtf8);
    if (got.empty()) return R::Failure(ErrorCode::IoError, "The downloaded file could not be read", partUtf8);
    if (!PackageHashMatches(got, package.sha256)) {
        fs::remove(partPath, ec);
        return R::Failure(ErrorCode::SignatureInvalid, "The downloaded file is not the one the catalog names",
                          "sha256 " + got + " != " + package.sha256);
    }
    fs::rename(partPath, finalPath, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "The downloaded file could not be moved into place", ec.message());
    return R::Succeed(finalUtf8);
}

} // namespace UltraStore
