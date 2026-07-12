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

} // anonymous namespace

// Listing parsers live in ultranet_internal::ftp:: rather than the anonymous
// namespace so the test suite can call them directly with synthetic input.
// See UltraNetFtpInternal.h for the public-to-test declarations.
namespace ultranet_internal::ftp {

// =====================================================================
// Listing parsers
// =====================================================================

// Parses an MLSD line (RFC 3659): semicolon-separated key=value facts
// followed by a single space then the filename. Example:
//   "type=file;size=12345;modify=20231215120000;perm=adfr;UNIX.mode=0644;
//    UNIX.owner=alice;UNIX.group=users; readme.txt"
// Returns true if the line is a valid MLSD entry; false otherwise (caller
// then tries other formats).
bool ParseMlsdLine(const std::string& line, UltraNetFtpEntry& out) {
    const std::size_t spaceAfterFacts = line.find(' ');
    if (spaceAfterFacts == std::string::npos) return false;
    const std::string facts = line.substr(0, spaceAfterFacts);
    if (facts.empty() || facts.find('=') == std::string::npos) return false;

    out.name = line.substr(spaceAfterFacts + 1);
    if (out.name.empty() || out.name == "." || out.name == "..") return false;

    std::size_t i = 0;
    while (i < facts.size()) {
        std::size_t semi = facts.find(';', i);
        if (semi == std::string::npos) semi = facts.size();
        const std::string fact = facts.substr(i, semi - i);
        i = semi + 1;
        if (fact.empty()) continue;

        const std::size_t eq = fact.find('=');
        if (eq == std::string::npos) continue;
        std::string key = fact.substr(0, eq);
        std::string val = fact.substr(eq + 1);
        // MLSD fact keys are case-insensitive.
        for (char& c : key) c = static_cast<char>(std::tolower(c));

        if (key == "type") {
            for (char& c : val) c = static_cast<char>(std::tolower(c));
            // Symlinks come through with OS-prefixed values like
            // "OS.unix=slink" or "OS.unix=symlink:target" — the value
            // itself contains an '=' that the field split above leaves
            // intact. Match on the well-known substrings.
            const bool linkLike =
                val.find("slink")   != std::string::npos ||
                val.find("symlink") != std::string::npos;
            if      (val == "file")    out.type = UltraNetFtpEntryType::File;
            else if (val == "dir" ||
                     val == "cdir" ||
                     val == "pdir")    out.type = UltraNetFtpEntryType::Directory;
            else if (linkLike)         out.type = UltraNetFtpEntryType::Symlink;
            else                       out.type = UltraNetFtpEntryType::Unknown;
        } else if (key == "size") {
            out.size = std::atoll(val.c_str());
        } else if (key == "modify") {
            // Convert YYYYMMDDHHMMSS to ISO 8601 for the public field.
            if (val.size() >= 14) {
                out.modificationTime =
                    val.substr(0, 4) + "-" + val.substr(4, 2) + "-" +
                    val.substr(6, 2) + "T" + val.substr(8, 2) + ":" +
                    val.substr(10, 2) + ":" + val.substr(12, 2) + "Z";
            } else {
                out.modificationTime = val;
            }
        } else if (key == "perm") {
            out.permissions = val;
        } else if (key == "unix.mode") {
            // POSIX permission bits; convert to "rwxr-xr-x"-style string only
            // when "perm" wasn't already set by the server.
            if (out.permissions.empty()) {
                const long mode = std::strtol(val.c_str(), nullptr, 8);
                std::string s(9, '-');
                auto bit = [&](int shift, int idx, char ch) {
                    if (mode & (1 << shift)) s[idx] = ch;
                };
                bit(8, 0, 'r'); bit(7, 1, 'w'); bit(6, 2, 'x');   // user
                bit(5, 3, 'r'); bit(4, 4, 'w'); bit(3, 5, 'x');   // group
                bit(2, 6, 'r'); bit(1, 7, 'w'); bit(0, 8, 'x');   // other
                out.permissions = s;
            }
        } else if (key == "unix.owner") {
            out.owner = val;
        } else if (key == "unix.group") {
            out.group = val;
        }
    }
    return true;
}

// Parses a UNIX ls -l-style line (common LIST output from FTP servers,
// also the format libcurl's SFTP emits). Example:
//   "drwxr-xr-x 2 alice users 4096 Dec 15 12:00 myfolder"
//   "-rw-r--r-- 1 alice users 1234 Jan  3  2023 file.txt"
// Returns true on a valid parse; false for non-matching lines (e.g.
// the "total 42" header line many ftpds emit at the start).
bool ParseUnixLine(const std::string& line, UltraNetFtpEntry& out) {
    if (line.size() < 10) return false;
    // First char must be 'd', '-', 'l', etc. (file type)
    const char typeChar = line[0];
    if (typeChar != 'd' && typeChar != '-' && typeChar != 'l' &&
        typeChar != 'c' && typeChar != 'b' && typeChar != 's' &&
        typeChar != 'p') {
        return false;
    }

    std::istringstream is(line);
    std::string perms, links, owner, group, size;
    if (!(is >> perms >> links >> owner >> group >> size)) return false;

    // Date is 3 tokens: "Dec 15 12:00" or "Jan  3  2023". Tolerate the
    // double-space variant by reading three tokens.
    std::string mon, day, timeOrYear;
    if (!(is >> mon >> day >> timeOrYear)) return false;

    // Filename = everything that's left in the original line after the
    // date token. Locate the date in the original line and take the rest.
    const std::size_t dateMarker = line.rfind(timeOrYear);
    if (dateMarker == std::string::npos) return false;
    std::string name = line.substr(dateMarker + timeOrYear.size());
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
        name.erase(0, 1);
    }
    // Symlinks: "link -> target"; keep just the link name.
    if (typeChar == 'l') {
        const std::size_t arrow = name.find(" -> ");
        if (arrow != std::string::npos) name = name.substr(0, arrow);
    }
    if (name.empty() || name == "." || name == "..") return false;

    out.name        = name;
    out.permissions = perms.size() == 10 ? perms.substr(1) : perms;
    out.owner       = owner;
    out.group       = group;
    out.size        = std::atoll(size.c_str());
    out.modificationTime = mon + " " + day + " " + timeOrYear;
    if      (typeChar == 'd') out.type = UltraNetFtpEntryType::Directory;
    else if (typeChar == 'l') out.type = UltraNetFtpEntryType::Symlink;
    else if (typeChar == '-') out.type = UltraNetFtpEntryType::File;
    else                       out.type = UltraNetFtpEntryType::Unknown;
    return true;
}

// Splits the raw listing body into lines (handling both \n and \r\n).
std::vector<std::string> SplitLines(const std::string& body) {
    std::vector<std::string> out;
    std::istringstream is(body);
    std::string line;
    while (std::getline(is, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) out.push_back(std::move(line));
    }
    return out;
}

} // namespace ultranet_internal::ftp

namespace {
// Bring the parsers into the anonymous namespace's usage scope so the call
// sites in UltraNet_FtpListDirectory remain unqualified.
using ultranet_internal::ftp::ParseMlsdLine;
using ultranet_internal::ftp::ParseUnixLine;
using ultranet_internal::ftp::SplitLines;
} // namespace

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

    // Ensure trailing slash so libcurl treats this as a directory listing.
    std::string listUrl = url;
    if (listUrl.back() != '/') listUrl.push_back('/');

    // Three-tier strategy:
    //   1. FTP / FTPS — try MLSD via CUSTOMREQUEST. Modern servers
    //      (vsftpd, ProFTPD, pureftpd) support it; the response is
    //      structured (RFC 3659) and gives us size / mtime / perm /
    //      owner / group reliably.
    //   2. If MLSD fails or the response doesn't look like MLSD,
    //      fall back to libcurl's default LIST. The body is usually
    //      UNIX `ls -l` output (also what libcurl's SFTP emits).
    //   3. If neither yields valid entries, last-resort DIRLISTONLY —
    //      names only, which is what the previous implementation always
    //      did.
    //
    // SFTP is handled by libcurl issuing its own listing; we just parse
    // whatever it returns (always UNIX ls -l style for libcurl/SFTP).
    auto runListing = [&](const char* customReq, bool dirListOnly,
                          std::string& body) -> UltraNetResult {
        body.clear();
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                              curl_easy_cleanup);
        if (!h) {
            return UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                         "curl_easy_init() failed");
        }
        curl_easy_setopt(h.get(), CURLOPT_URL, listUrl.c_str());
        if (customReq) curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, customReq);
        curl_easy_setopt(h.get(), CURLOPT_DIRLISTONLY, dirListOnly ? 1L : 0L);
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);
        ApplyCommonOptions(h.get(), opt);
        return Perform(h.get(), listUrl);
    };

    auto isFtp = listUrl.rfind("ftp://", 0) == 0 ||
                 listUrl.rfind("ftps://", 0) == 0;

    // Pass 1: MLSD (FTP/FTPS only)
    if (isFtp) {
        std::string body;
        if (runListing("MLSD", false, body)) {
            for (const auto& line : SplitLines(body)) {
                UltraNetFtpEntry e;
                if (ParseMlsdLine(line, e)) {
                    e.fullPath = listUrl + e.name;
                    out.push_back(std::move(e));
                }
            }
            if (!out.empty()) return UltraNetResult::Ok();
        }
    }

    // Pass 2: LIST / SFTP default (UNIX ls -l format)
    {
        std::string body;
        UltraNetResult r = runListing(nullptr, false, body);
        if (r) {
            for (const auto& line : SplitLines(body)) {
                if (line.rfind("total ", 0) == 0) continue;  // ls -l preamble
                UltraNetFtpEntry e;
                if (ParseUnixLine(line, e)) {
                    e.fullPath = listUrl + e.name;
                    out.push_back(std::move(e));
                }
            }
            if (!out.empty()) return r;
        } else if (!isFtp) {
            // SFTP / other: surface the error rather than try DIRLISTONLY,
            // which usually fails on non-FTP transports too.
            return r;
        }
    }

    // Pass 3: DIRLISTONLY — names only (previous-version behaviour).
    {
        std::string body;
        UltraNetResult r = runListing(nullptr, true, body);
        if (!r) return r;
        for (const auto& line : SplitLines(body)) {
            if (line == "." || line == "..") continue;
            UltraNetFtpEntry e;
            e.name     = line;
            e.fullPath = listUrl + line;
            out.push_back(std::move(e));
        }
        return r;
    }
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
