// core/UltraNet/UltraNetFtp.cpp
// FTP / FTPS / SFTP via libcurl. All operations are synchronous in Stage 3;
// streaming download/upload uses constant memory (libcurl write/read callbacks
// to/from FILE*). Mutating operations (DELE / RNFR-RNTO / MKD / RMD) ride on
// CURLOPT_QUOTE so libcurl handles connection setup and authentication for us.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetFtp.h"
#include "UltraNetHttpEasy.h"   // MapCurlError (private helpers)

#include <curl/curl.h>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::size_t WriteToFile(char* data, std::size_t size, std::size_t nmemb, void* ud) {
    std::FILE* fp = static_cast<std::FILE*>(ud);
    return std::fwrite(data, 1, size * nmemb, fp);
}

std::size_t ReadFromFile(char* buffer, std::size_t size, std::size_t nmemb, void* ud) {
    std::FILE* fp = static_cast<std::FILE*>(ud);
    return std::fread(buffer, 1, size * nmemb, fp);
}

std::size_t WriteToString(char* data, std::size_t size, std::size_t nmemb, void* ud) {
    std::string* s = static_cast<std::string*>(ud);
    s->append(data, size * nmemb);
    return size * nmemb;
}

void ApplyCommonOptions(CURL* h, const UltraNetFtpOptions& opt) {
    if (!opt.credentials.username.empty()) {
        curl_easy_setopt(h, CURLOPT_USERNAME, opt.credentials.username.c_str());
        curl_easy_setopt(h, CURLOPT_PASSWORD, opt.credentials.password.c_str());
    }
    if (opt.useTls) {
        curl_easy_setopt(h, CURLOPT_USE_SSL,
                         opt.implicitTls ? CURLUSESSL_ALL : CURLUSESSL_TRY);
    }
    curl_easy_setopt(h, CURLOPT_FTP_USE_EPSV, opt.passiveMode ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_FTP_CREATE_MISSING_DIRS,
                     opt.createMissingDirs ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(opt.connectTimeoutMs));
    if (opt.transferTimeoutMs > 0) {
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,
                         static_cast<long>(opt.transferTimeoutMs));
    }
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
}

UltraNetResult Perform(CURL* h, const std::string& url) {
    CURLcode rc = curl_easy_perform(h);
    UltraNetResult r;
    r.url = url;
    if (rc == CURLE_OK) {
        r.code    = UltraNetResultCode::Success;
        r.success = true;
    } else {
        r.code    = ultranet_internal::MapCurlError(rc, 0);
        r.success = false;
        r.message = curl_easy_strerror(rc);
    }
    return r;
}

// Splits a URL into base (scheme://host[:port]/) and the last path segment.
// "ftp://host/dir/sub/file" -> {"ftp://host/dir/sub/", "file"}.
// Used by Delete/Rename/MakeDir/RemoveDir to issue post-quote commands at
// the parent directory.
bool SplitParentAndName(const std::string& url,
                        std::string& parentUrl,
                        std::string& name) {
    std::size_t lastSlash = url.find_last_of('/');
    if (lastSlash == std::string::npos) return false;
    // Avoid splitting on "ftp://" double-slash.
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos && lastSlash <= schemeEnd + 2) return false;
    parentUrl = url.substr(0, lastSlash + 1);
    name      = url.substr(lastSlash + 1);
    return !name.empty();
}

} // namespace

UltraNetResult UltraNet_FtpDownload(const std::string& url,
                                    const std::string& localPath,
                                    const UltraNetFtpOptions& opt) {
    if (url.empty() || localPath.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "url or localPath is empty");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    const char* mode = opt.resumeTransfer ? "ab" : "wb";
    std::FILE* fp = std::fopen(localPath.c_str(), mode);
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::AccessDenied,
                                     "cannot open local file for write");
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        std::fclose(fp);
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToFile);
    curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, fp);
    if (opt.resumeTransfer && opt.resumeOffset > 0) {
        curl_easy_setopt(h.get(), CURLOPT_RESUME_FROM_LARGE,
                         static_cast<curl_off_t>(opt.resumeOffset));
    }
    ApplyCommonOptions(h.get(), opt);

    UltraNetResult r = Perform(h.get(), url);
    std::fclose(fp);
    return r;
}

UltraNetResult UltraNet_FtpUpload(const std::string& localPath,
                                  const std::string& url,
                                  const UltraNetFtpOptions& opt) {
    if (url.empty() || localPath.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "url or localPath is empty");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    std::FILE* fp = std::fopen(localPath.c_str(), "rb");
    if (!fp) {
        return UltraNetResult::Error(UltraNetResultCode::NotFound,
                                     "cannot open local file for read");
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (opt.resumeTransfer && opt.resumeOffset > 0) {
        std::fseek(fp, static_cast<long>(opt.resumeOffset), SEEK_SET);
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        std::fclose(fp);
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h.get(), CURLOPT_READFUNCTION, &ReadFromFile);
    curl_easy_setopt(h.get(), CURLOPT_READDATA, fp);
    curl_easy_setopt(h.get(), CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(size > 0 ? size : 0));
    ApplyCommonOptions(h.get(), opt);

    UltraNetResult r = Perform(h.get(), url);
    std::fclose(fp);
    return r;
}

UltraNetResult UltraNet_FtpListDirectory(const std::string& url,
                                         std::vector<UltraNetFtpEntry>& out,
                                         const UltraNetFtpOptions& opt) {
    out.clear();
    if (url.empty()) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "url is empty");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }
    // Ensure trailing slash so libcurl treats this as a directory listing.
    std::string listUrl = url;
    if (listUrl.back() != '/') listUrl.push_back('/');

    std::string body;
    curl_easy_setopt(h.get(), CURLOPT_URL, listUrl.c_str());
    curl_easy_setopt(h.get(), CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
    curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);
    ApplyCommonOptions(h.get(), opt);

    UltraNetResult r = Perform(h.get(), listUrl);
    if (!r) return r;

    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "." || line == "..") continue;
        UltraNetFtpEntry e;
        e.name     = line;
        e.fullPath = listUrl + line;
        out.push_back(std::move(e));
    }
    return r;
}

namespace {

UltraNetResult RunCommand(const std::string& url,
                          const UltraNetFtpOptions& opt,
                          const std::string& command) {
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }
    curl_slist* cmds = nullptr;
    cmds = curl_slist_append(cmds, command.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        guard(cmds, curl_slist_free_all);

    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(h.get(), CURLOPT_QUOTE,  cmds);
    ApplyCommonOptions(h.get(), opt);

    return Perform(h.get(), url);
}

} // namespace

UltraNetResult UltraNet_FtpDelete(const std::string& url,
                                  const UltraNetFtpOptions& opt) {
    std::string parent, name;
    if (!SplitParentAndName(url, parent, name)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "cannot derive parent directory");
    }
    return RunCommand(parent, opt, "DELE " + name);
}

UltraNetResult UltraNet_FtpRename(const std::string& url,
                                  const std::string& newName,
                                  const UltraNetFtpOptions& opt) {
    std::string parent, name;
    if (!SplitParentAndName(url, parent, name)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "cannot derive parent directory");
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                     "curl_easy_init() failed");
    }
    curl_slist* cmds = nullptr;
    cmds = curl_slist_append(cmds, ("RNFR " + name).c_str());
    cmds = curl_slist_append(cmds, ("RNTO " + newName).c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        guard(cmds, curl_slist_free_all);

    curl_easy_setopt(h.get(), CURLOPT_URL, parent.c_str());
    curl_easy_setopt(h.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(h.get(), CURLOPT_QUOTE, cmds);
    ApplyCommonOptions(h.get(), opt);

    return Perform(h.get(), parent);
}

UltraNetResult UltraNet_FtpCreateDirectory(const std::string& url,
                                           const UltraNetFtpOptions& opt) {
    std::string parent, name;
    if (!SplitParentAndName(url, parent, name)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "cannot derive parent directory");
    }
    return RunCommand(parent, opt, "MKD " + name);
}

UltraNetResult UltraNet_FtpRemoveDirectory(const std::string& url,
                                           const UltraNetFtpOptions& opt) {
    std::string parent, name;
    if (!SplitParentAndName(url, parent, name)) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                     "cannot derive parent directory");
    }
    return RunCommand(parent, opt, "RMD " + name);
}
