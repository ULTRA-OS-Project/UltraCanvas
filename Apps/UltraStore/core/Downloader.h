// Apps/UltraStore/core/Downloader.h
// Getting a package onto the disk: UltraNet streams it into
// <downloads>/<sha256>.<format>.part, a partial file is resumed with a Range
// request, the finished file is checked against the catalog's SHA-256 and
// renamed into place, and a wrong hash deletes it. One transfer at a time
// per Downloader; Pause / Resume / Cancel act on it from any thread.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace UltraStore {

// bytes so far of the total (-1 when the server did not say); return false
// to cancel. Fires on UltraNet's worker thread.
using DownloadProgress = std::function<bool(int64_t done, int64_t total)>;

class Downloader {
public:
    explicit Downloader(std::string downloadsDirUtf8);

    // The package's complete, verified file in the downloads folder: fetched
    // now, or found there from an earlier run. A partial file from an
    // interrupted run is continued. Fails with NetworkError (nothing or not
    // all of it came), IoError (the disk) or SignatureInvalid (the bytes are
    // not the catalog's; the file is deleted).
    Result<std::string> Fetch(const Package& package, const DownloadProgress& progress = {});

    void Pause();
    void Resume();
    void Cancel();
    bool IsPaused() const { return paused_; }

    const std::string& Dir() const { return dir_; }
    // "<sha256>.<format>" - the name a package has in the downloads folder.
    static std::string FileNameFor(const Package& package);
    // Deletes every complete and partial download.
    void Clear();

private:
    std::string dir_;
    std::atomic<uint64_t> handle_{0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> cancelled_{false};
};

} // namespace UltraStore
