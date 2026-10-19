// Apps/UltraWeb/host/UltraWebDownloads.h
// Saving what a link leads to when it is no page: the bytes the page loader
// already has, or - over its size limit - the file fetched again and
// streamed to disk (http(s) through UltraNet, a local file copied). A file
// is written to "<name>.part" and renamed when it is complete, so a broken
// download never leaves a file that looks whole. A download is as long as
// the file is large: it has no time limit, only a stall limit.
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraWeb {

// Writes `bytes` to the UTF-8 `path` (through "<path>.part"); false with
// `error` when it cannot.
bool SaveBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error);

// A download in progress; Cancel() stops it and removes the part file.
class Download {
public:
    virtual ~Download() = default;
    virtual void Cancel() = 0;
};

// Fetch `url` to the UTF-8 `path`. `onProgress` (bytes so far, the total or
// -1) and `onDone` (true, or false with why) arrive through `post`, on the
// UI thread; onDone once, also after Cancel.
std::shared_ptr<Download> StartDownload(const std::string& url, const std::string& path,
                                        std::function<void(std::function<void()>)> post,
                                        std::function<void(int64_t received, int64_t total)> onProgress,
                                        std::function<void(bool ok, const std::string& error)> onDone);

// A download fails when no byte has arrived for this long (30 s by
// default; tests shorten it). A whole-transfer limit there is none.
void SetDownloadStallTimeout(int milliseconds);

// "1.4 MB", "830 KB", "12 bytes".
std::string ByteSize(int64_t bytes);

// The folder downloads are offered in: the user's Downloads folder, else
// the home folder, else "".
std::string DefaultDownloadFolder();

} // namespace UltraWeb
