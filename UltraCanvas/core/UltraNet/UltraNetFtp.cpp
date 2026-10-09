// core/UltraNet/UltraNetFtp.cpp
// FTP / FTPS / SFTP via libcurl. All operations are synchronous in Stage 3;
// streaming download/upload uses constant memory (libcurl write/read callbacks
// to/from FILE*). Mutating operations (DELE / RNFR-RNTO / MKD / RMD) ride on
// CURLOPT_QUOTE so libcurl handles connection setup and authentication for us.
//
// Every handle runs with libcurl's debug stream switched on and read by an
// ftplog::Transcript (UltraNetFtpLog.h): it is what feeds the caller's session
// log, and what lets a failure quote the server's last reply even when nobody
// is listening to the log.
//
// A connection outlives the call that made it (ThreadConnections below): the
// next call on the same thread to the same server, as the same user, takes it
// up without connecting or logging in again. And a server that refused MLSD
// is not asked for it again (ListingFormats). Before both, every folder an
// FTP drive showed cost a login - two on a server without MLSD, such as
// vsftpd, which logged in once to be refused and again to ask with LIST.
// Version: 0.5.0 - connections kept open between calls, MLSD refusal remembered
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetFtp.h"
#include "UltraNet/UltraNetCurlError.h"   // error buffer detail + diagnostics chain
#include "UltraNetHttpEasy.h"   // MapCurlError (private helpers)
#include "UltraNetFtpQuote.h"   // the text of DELE / RNFR-RNTO / MKD / RMD
#include "UltraNetFtpLog.h"     // the session log
#include "UltraNetFtpInternal.h"

#include <curl/curl.h>

#include <cctype>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "../../include/UltraCanvasPathUtf8.h"

namespace {

using ultranet_internal::ftplog::Channel;
using ultranet_internal::ftplog::Transcript;

// What the diagnostics chain names as the component that failed.
constexpr const char* kComponent = "UltraNet FTP 0.5.0";

// The log sink of calls made on this thread without an onLog of their own
// (UltraNet_SetThreadFtpLog).
thread_local UltraNetFtpLogCallback t_threadLog;

// ===== Connections kept open between calls =====
// libcurl keeps a finished transfer's connection in a pool for the next
// transfer to the same server and user - but the pool belongs to the easy
// handle, and every call here makes its own and frees it at the end, so the
// connection went with it. A share object holding the pool
// (CURL_LOCK_DATA_CONNECT) outlives the handles that attach it.
//
// One per thread, because libcurl does not support sharing connections
// between threads that run transfers at the same time - and one thread
// making call after call is the case worth speeding up: a file manager's
// drive worker listing one folder after another. libcurl itself decides
// whether a kept connection fits (same scheme, host, port, user, password
// and TLS settings), notices one the server has closed in the meantime, and
// drops one idle for longer than two minutes; the server's own idle timeout
// closes it from the other side. Closing the pool sends QUIT, which libcurl
// waits on for two seconds at most.

// Every thread's pool, so that UltraNet_Shutdown can close them before
// curl_global_cleanup. Never destroyed: a thread can end - and its
// ThreadConnections with it - after static destruction has begun.
struct KeptPools {
    std::mutex mutex;
    std::unordered_set<CURLSH**> slots;   // each thread's ThreadConnections::share_
};

KeptPools& Kept() {
    static KeptPools* pools = new KeptPools;
    return *pools;
}

class ThreadConnections {
public:
    ThreadConnections() = default;
    ThreadConnections(const ThreadConnections&) = delete;
    ThreadConnections& operator=(const ThreadConnections&) = delete;
    ~ThreadConnections() { Close(); }

    // The share this thread's calls attach, made on first use; null when
    // libcurl cannot make one, which costs the reuse and nothing else.
    CURLSH* Share() {
        KeptPools& kept = Kept();
        std::lock_guard<std::mutex> lk(kept.mutex);
        if (share_) return share_;
        CURLSH* share = curl_share_init();
        if (!share) return nullptr;
        // No lock callbacks: only this thread ever attaches it.
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        share_ = share;
        kept.slots.insert(&share_);
        return share_;
    }

    // Closes this thread's kept connections. The QUIT is said outside the
    // lock: every thread's next call takes it, and one server slow to answer
    // must not hold up the calls to the others.
    void Close() {
        CURLSH* share = nullptr;
        {
            KeptPools& kept = Kept();
            std::lock_guard<std::mutex> lk(kept.mutex);
            kept.slots.erase(&share_);
            share = share_;
            share_ = nullptr;
        }
        if (share) curl_share_cleanup(share);
    }

private:
    CURLSH* share_ = nullptr;   // guarded by Kept().mutex
};

thread_local ThreadConnections t_connections;

// ===== What each server has said it cannot do =====
// A server that answers MLSD with "500 Unknown command" says the same to
// every folder, so after the first refusal its listings go straight to LIST.
// Remembered for the life of the process, per server and user.
struct ListingFormats {
    std::mutex mutex;
    std::unordered_set<std::string> withoutMlsd;
};

ListingFormats& Formats() {
    static ListingFormats* formats = new ListingFormats;
    return *formats;
}

// "ftp://erika@files.example.org:2121" for "ftp://files.example.org:2121/pub/"
// signed in as erika: the scheme and the authority, in lower case, without a
// password the URL may carry, and with the user the options sign in as.
std::string ServerKey(const std::string& url, const UltraNetFtpOptions& opt) {
    std::size_t start = url.find("://");
    std::string scheme = start == std::string::npos ? std::string() : url.substr(0, start);
    start = start == std::string::npos ? 0 : start + 3;
    std::size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    std::string authority = url.substr(start, end - start);
    std::string user = opt.credentials.username;
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        if (user.empty()) user = authority.substr(0, authority.find(':'));
        authority.erase(0, at + 1);
    }
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : authority) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return scheme + "://" + user + "@" + authority;
}

bool ServerRefusesMlsd(const std::string& key) {
    ListingFormats& f = Formats();
    std::lock_guard<std::mutex> lk(f.mutex);
    return f.withoutMlsd.count(key) > 0;
}

void RememberServerRefusesMlsd(const std::string& key) {
    ListingFormats& f = Formats();
    std::lock_guard<std::mutex> lk(f.mutex);
    f.withoutMlsd.insert(key);
}

// The replies that say the server does not have the command at all, rather
// than that it will not list this folder: 500 unknown command, 502 not
// implemented, 504 not implemented for that parameter. A 550 is a refusal of
// the folder, and LIST will hear the same.
bool IsUnknownCommandReply(int code) {
    return code == 500 || code == 502 || code == 504;
}

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

bool IsSftpUrl(const std::string& url) {
    return ultranet_internal::ftplog::Upper(url.substr(0, 7)) == "SFTP://";
}

// The inactivity limit in the whole seconds libcurl counts in; 0 for none.
int InactivitySeconds(const UltraNetFtpOptions& opt) {
    if (opt.inactivityTimeoutMs <= 0) return 0;
    return opt.inactivityTimeoutMs < 1000 ? 1 : (opt.inactivityTimeoutMs + 999) / 1000;
}

// The log of one public call: the caller's onLog, else this thread's sink.
Transcript MakeTranscript(const UltraNetFtpOptions& opt, const std::string& url) {
    return Transcript(opt.onLog ? opt.onLog : t_threadLog, IsSftpUrl(url));
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
    // A server that goes quiet. Two limits, because libcurl has two places to
    // wait: for the reply to a command (the server response timeout; libcurl
    // alone waits two minutes) and for the bytes of a listing or a file (the
    // low-speed limit: under one byte a second for that long). Without the
    // second a data connection that opened and then sent nothing - a firewall
    // that lets PASV through and drops the data - held the call for ever.
    if (const int seconds = InactivitySeconds(opt); seconds > 0) {
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, static_cast<long>(seconds));
#if LIBCURL_VERSION_NUM >= 0x080600
        curl_easy_setopt(h, CURLOPT_SERVER_RESPONSE_TIMEOUT_MS,
                         static_cast<long>(seconds) * 1000L);
#else
        curl_easy_setopt(h, CURLOPT_SERVER_RESPONSE_TIMEOUT, static_cast<long>(seconds));
#endif
    }
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    // The connection the last call on this thread left open, if it fits.
    if (CURLSH* share = t_connections.Share())
        curl_easy_setopt(h, CURLOPT_SHARE, share);
}

// Transfer progress for a file moving to or from a server.
//
// libcurl calls this as the bytes go by; it feeds the module's global
// transfer callbacks - the same bag every HTTP request already reports
// through (UltraNet_SetTransferCallbacks), so a caller that wants to show a
// progress bar sets it once and hears about every transfer, whatever the
// protocol. Without this an FTP upload was silent from first byte to last.
//
// Fires on the transfer thread and must not block; UltraNet's contract for
// these callbacks says so, and libcurl stalls the transfer for as long as
// this takes.
int ReportTransferProgress(void* /*userdata*/, curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t ultotal, curl_off_t ulnow) {
    const UltraNetTransferCallbacks cb = UltraNet_GetTransferCallbacks();
    if (cb.onDownloadProgress && dlnow > 0) {
        cb.onDownloadProgress(static_cast<int64_t>(dlnow),
                              static_cast<int64_t>(dltotal));
    }
    if (cb.onUploadProgress && ulnow > 0) {
        cb.onUploadProgress(static_cast<int64_t>(ulnow),
                            static_cast<int64_t>(ultotal));
    }
    return 0;   // non-zero would abort the transfer
}

// Turns the progress callback on for one handle. Only the two file transfers
// use it: a listing or a DELE moves too little for anyone to watch.
void ApplyProgressCallback(CURL* h) {
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, &ReportTransferProgress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
}

// libcurl's debug stream into the call's transcript. The payload (DATA_IN /
// DATA_OUT: the listing, the file) and raw TLS records are not log lines.
int FeedTranscript(CURL* /*h*/, curl_infotype type, char* data, std::size_t size,
                   void* userp) {
    Transcript* log = static_cast<Transcript*>(userp);
    switch (type) {
        case CURLINFO_TEXT:       log->Feed(Channel::Text, std::string(data, size)); break;
        case CURLINFO_HEADER_OUT: log->Feed(Channel::Sent, std::string(data, size)); break;
        case CURLINFO_HEADER_IN:  log->Feed(Channel::Received, std::string(data, size)); break;
        default: break;
    }
    return 0;
}

// What the diagnostics chain says about this call besides what libcurl
// reports: which component, how TLS starts, how it signs in. Set for the
// length of one transfer and put back after it, because the context is per
// thread and shared with the mail plug-ins.
class DiagnosticsContext {
public:
    DiagnosticsContext(const UltraNetFtpOptions& opt, const std::string& url)
        : saved_(ultranet_curlerror::CurrentContext()) {
        ultranet_curlerror::Context& c = ultranet_curlerror::CurrentContext();
        c.component = kComponent;
        c.tls = IsSftpUrl(url)  ? "SSH (SFTP)"
              : !opt.useTls     ? "none - plain FTP, password and data unencrypted"
              : opt.implicitTls ? "implicit (TLS from connect)"
                                : "explicit (AUTH TLS; continues without it when refused)";
        c.signIn = opt.credentials.username.empty() ? "anonymous" : "user name and password";
    }
    ~DiagnosticsContext() { ultranet_curlerror::CurrentContext() = saved_; }
    DiagnosticsContext(const DiagnosticsContext&) = delete;
    DiagnosticsContext& operator=(const DiagnosticsContext&) = delete;

private:
    ultranet_curlerror::Context saved_;
};

// libcurl's error classes that mean something more specific for FTP than the
// shared HTTP mapping gives them.
UltraNetResultCode MapFtpError(CURLcode rc) {
    switch (rc) {
        case CURLE_REMOTE_FILE_NOT_FOUND: return UltraNetResultCode::NotFound;
        case CURLE_FTP_ACCEPT_TIMEOUT:    return UltraNetResultCode::Timeout;
        case CURLE_USE_SSL_FAILED:        return UltraNetResultCode::TlsHandshakeFailed;
        default:                          return ultranet_internal::MapCurlError(rc, 0);
    }
}

// Runs the transfer on `h`, logging it into `log`. A failure carries
// libcurl's specific reason (its error buffer, not just the error class)
// with the server's refusal added, and the diagnostics chain.
UltraNetResult Perform(CURL* h, const std::string& url, const UltraNetFtpOptions& opt,
                       Transcript& log, CURLcode* rcOut = nullptr) {
    curl_easy_setopt(h, CURLOPT_DEBUGFUNCTION, &FeedTranscript);
    curl_easy_setopt(h, CURLOPT_DEBUGDATA, &log);
    curl_easy_setopt(h, CURLOPT_VERBOSE, 1L);
    log.Begin(url);

    std::string detail, diagnostics;
    CURLcode rc = CURLE_OK;
    {
        DiagnosticsContext context(opt, url);
        rc = ultranet_curlerror::Perform(h, detail, &diagnostics);
    }
    if (rcOut) *rcOut = rc;

    UltraNetResult r;
    r.url = ultranet_curlerror::RedactUrl(url);
    if (rc == CURLE_OK) {
        r.code    = UltraNetResultCode::Success;
        r.success = true;
        return r;
    }
    r.code    = MapFtpError(rc);
    r.success = false;
    r.message = ultranet_internal::ftplog::FailureMessage(
            detail, InactivitySeconds(opt), log.LastReplyCode(), log.LastReply());
    r.diagnostics = diagnostics;
    if (log.LastReplyCode() > 0) r.diagnostics += "Last server reply: " + log.LastReply() + "\n";
    return r;
}

// Ends a public call: a failure becomes the log's last line. `rc` is the
// libcurl code when the failure came from a transfer, CURLE_OK when the call
// was refused before one (an empty URL, a local file that cannot be opened).
UltraNetResult Finish(Transcript& log, UltraNetResult r, CURLcode rc = CURLE_OK) {
    if (!r.success) log.Fail(r.message, r.code, static_cast<int>(rc));
    return r;
}

// Whether a listing that failed is worth asking for again in another format
// (MLSD -> LIST -> NLST). Only when the server refused the listing command
// itself; a failure to reach the server, sign in, set up TLS or the data
// connection, or a server gone quiet, fails every format alike - and asking
// three times turned one 30-second timeout into a minute and a half, and the
// message into the third attempt's rather than the first's.
bool WorthAnotherListing(CURLcode rc) {
    switch (rc) {
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_FTP_WEIRD_SERVER_REPLY:
        case CURLE_REMOTE_ACCESS_DENIED:
        case CURLE_FTP_ACCEPT_FAILED:
        case CURLE_FTP_WEIRD_PASS_REPLY:
        case CURLE_FTP_ACCEPT_TIMEOUT:
        case CURLE_FTP_WEIRD_PASV_REPLY:
        case CURLE_FTP_WEIRD_227_FORMAT:
        case CURLE_FTP_CANT_GET_HOST:
        case CURLE_OUT_OF_MEMORY:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_ABORTED_BY_CALLBACK:
        case CURLE_GOT_NOTHING:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_USE_SSL_FAILED:
        case CURLE_LOGIN_DENIED:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSH:
            return false;
        default:
            return true;
    }
}

} // anonymous namespace

UltraNetFtpLogCallback UltraNet_SetThreadFtpLog(UltraNetFtpLogCallback sink) {
    UltraNetFtpLogCallback previous = std::move(t_threadLog);
    t_threadLog = std::move(sink);
    return previous;
}

void UltraNet_FtpCloseIdleConnections() {
    t_connections.Close();
}

namespace ultranet_internal {

// Called by UltraNet_Shutdown before curl_global_cleanup: every thread's kept
// connections are closed, since libcurl may not be called once it is gone. A
// thread that calls again afterwards starts a pool of its own.
void CloseFtpConnections() {
    std::vector<CURLSH*> shares;
    {
        KeptPools& kept = Kept();
        std::lock_guard<std::mutex> lk(kept.mutex);
        for (CURLSH** slot : kept.slots) {
            if (*slot) shares.push_back(*slot);
            *slot = nullptr;
        }
        kept.slots.clear();
    }
    for (CURLSH* share : shares) curl_share_cleanup(share);
}

namespace ftp {

void ForgetServerListingFormats() {
    ListingFormats& f = Formats();
    std::lock_guard<std::mutex> lk(f.mutex);
    f.withoutMlsd.clear();
}

} // namespace ftp
} // namespace ultranet_internal

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

UltraNetResult UltraNet_FtpDownload(const std::string& url,
                                    const std::string& localPath,
                                    const UltraNetFtpOptions& opt) {
    Transcript log = MakeTranscript(opt, url);
    if (url.empty() || localPath.empty()) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                                 "url or localPath is empty"));
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    const char* mode = opt.resumeTransfer ? "ab" : "wb";
    std::FILE* fp = UltraCanvas::OpenFileUtf8(localPath, mode);
    if (!fp) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::AccessDenied,
                                                 "cannot open local file for write: " +
                                                 localPath));
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        std::fclose(fp);
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                                 "curl_easy_init() failed"));
    }
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteToFile);
    curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, fp);
    if (opt.resumeTransfer && opt.resumeOffset > 0) {
        curl_easy_setopt(h.get(), CURLOPT_RESUME_FROM_LARGE,
                         static_cast<curl_off_t>(opt.resumeOffset));
    }
    ApplyCommonOptions(h.get(), opt);
    ApplyProgressCallback(h.get());

    CURLcode rc = CURLE_OK;
    UltraNetResult r = Perform(h.get(), url, opt, log, &rc);
    std::fclose(fp);
    return Finish(log, r, rc);
}

UltraNetResult UltraNet_FtpUpload(const std::string& localPath,
                                  const std::string& url,
                                  const UltraNetFtpOptions& opt) {
    Transcript log = MakeTranscript(opt, url);
    if (url.empty() || localPath.empty()) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                                 "url or localPath is empty"));
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();

    std::FILE* fp = UltraCanvas::OpenFileUtf8(localPath, "rb");
    if (!fp) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::NotFound,
                                                 "cannot open local file for read: " +
                                                 localPath));
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
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                                 "curl_easy_init() failed"));
    }
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h.get(), CURLOPT_READFUNCTION, &ReadFromFile);
    curl_easy_setopt(h.get(), CURLOPT_READDATA, fp);
    curl_easy_setopt(h.get(), CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(size > 0 ? size : 0));
    ApplyCommonOptions(h.get(), opt);
    ApplyProgressCallback(h.get());

    CURLcode rc = CURLE_OK;
    UltraNetResult r = Perform(h.get(), url, opt, log, &rc);
    std::fclose(fp);
    return Finish(log, r, rc);
}

UltraNetResult UltraNet_FtpListDirectory(const std::string& url,
                                         std::vector<UltraNetFtpEntry>& out,
                                         const UltraNetFtpOptions& opt) {
    out.clear();
    Transcript log = MakeTranscript(opt, url);
    if (url.empty()) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InvalidUrl,
                                                 "url is empty"));
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
    //   2. If the server refuses MLSD or the response doesn't look like
    //      MLSD, fall back to libcurl's default LIST. The body is usually
    //      UNIX `ls -l` output (also what libcurl's SFTP emits).
    //   3. If neither yields valid entries, last-resort DIRLISTONLY —
    //      names only, which is what the previous implementation always
    //      did.
    //
    // A format that came back EMPTY is an empty folder, not a format the
    // parser could not read: it is the answer, and asking again in another
    // format only logged in twice more to hear the same. And a failure that
    // is not the server refusing the command (WorthAnotherListing) is
    // returned as it is rather than repeated.
    //
    // A server that does not know MLSD (500 / 502 / 504) is remembered as
    // such (ListingFormats) and its later listings start at LIST. The second pass of one listing runs on the connection
    // the first left open (ThreadConnections), so even the first refusal no
    // longer costs a second login.
    //
    // SFTP is handled by libcurl issuing its own listing; we just parse
    // whatever it returns (always UNIX ls -l style for libcurl/SFTP).
    auto runListing = [&](const char* customReq, bool dirListOnly,
                          std::string& body, CURLcode& rc) -> UltraNetResult {
        body.clear();
        rc = CURLE_OK;
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
        return Perform(h.get(), listUrl, opt, log, &rc);
    };

    auto isFtp = listUrl.rfind("ftp://", 0) == 0 ||
                 listUrl.rfind("ftps://", 0) == 0;

    // Pass 1: MLSD (FTP/FTPS only, and not to a server known to refuse it)
    const std::string server = ServerKey(listUrl, opt);
    if (isFtp && !ServerRefusesMlsd(server)) {
        std::string body;
        CURLcode rc = CURLE_OK;
        UltraNetResult r = runListing("MLSD", false, body, rc);
        if (r) {
            const std::vector<std::string> lines = SplitLines(body);
            if (lines.empty()) return UltraNetResult::Ok();   // an empty folder
            for (const auto& line : lines) {
                UltraNetFtpEntry e;
                if (ParseMlsdLine(line, e)) {
                    e.fullPath = listUrl + e.name;
                    out.push_back(std::move(e));
                }
            }
            if (!out.empty()) return UltraNetResult::Ok();
            // Not remembered: a folder holding nothing but its own "." and
            // ".." entries reads this way too, on a server whose MLSD is fine.
            log.Step("The MLSD listing could not be read - asking again with LIST");
        } else if (!WorthAnotherListing(rc)) {
            return Finish(log, r, rc);
        } else {
            // The reply to MLSD itself: a 500 to an earlier command (TYPE,
            // say) says nothing about MLSD.
            if (log.LastCommand() == "MLSD" && IsUnknownCommandReply(log.LastReplyCode())) {
                RememberServerRefusesMlsd(server);
                log.Step("The server refused MLSD (" + r.message +
                           ") - asking again with LIST, and with LIST from now on");
            } else {
                log.Step("The server refused MLSD (" + r.message +
                           ") - asking again with LIST");
            }
        }
    }

    // Pass 2: LIST / SFTP default (UNIX ls -l format)
    {
        std::string body;
        CURLcode rc = CURLE_OK;
        UltraNetResult r = runListing(nullptr, false, body, rc);
        if (r) {
            bool anyLine = false;
            for (const auto& line : SplitLines(body)) {
                if (line.rfind("total ", 0) == 0) continue;  // ls -l preamble
                anyLine = true;
                UltraNetFtpEntry e;
                if (ParseUnixLine(line, e)) {
                    e.fullPath = listUrl + e.name;
                    out.push_back(std::move(e));
                }
            }
            if (!out.empty() || !anyLine) return r;   // entries, or an empty folder
            log.Step("The LIST listing could not be read - asking for the names only");
        } else if (!isFtp || !WorthAnotherListing(rc)) {
            // SFTP / other: surface the error rather than try DIRLISTONLY,
            // which usually fails on non-FTP transports too.
            return Finish(log, r, rc);
        } else {
            log.Step("The server refused LIST (" + r.message +
                       ") - asking for the names only");
        }
    }

    // Pass 3: DIRLISTONLY — names only (previous-version behaviour).
    {
        std::string body;
        CURLcode rc = CURLE_OK;
        UltraNetResult r = runListing(nullptr, true, body, rc);
        if (!r) return Finish(log, r, rc);
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

// Runs the quote commands that carry out `verb` on the entry `url` names.
// UltraNetFtpQuote.h makes the command text: the decoded name for FTP, the
// SFTP backend's own commands for an sftp:// URL.
UltraNetResult RunQuote(const std::string& url, ultranet_internal::ftpquote::Verb verb,
                        const std::string& newName, const UltraNetFtpOptions& opt) {
    Transcript log = MakeTranscript(opt, url);
    ultranet_internal::ftpquote::Plan plan;
    std::string error;
    if (!ultranet_internal::ftpquote::Build(url, verb, newName, plan, error)) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InvalidUrl, error));
    }
    if (!UltraNet_IsInitialized()) UltraNet_Initialize();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(curl_easy_init(),
                                                          curl_easy_cleanup);
    if (!h) {
        return Finish(log, UltraNetResult::Error(UltraNetResultCode::InsufficientMemory,
                                                 "curl_easy_init() failed"));
    }
    curl_slist* cmds = nullptr;
    for (const std::string& c : plan.commands)
        cmds = curl_slist_append(cmds, c.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>
        guard(cmds, curl_slist_free_all);

    curl_easy_setopt(h.get(), CURLOPT_URL, plan.parentUrl.c_str());
    curl_easy_setopt(h.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(h.get(), CURLOPT_QUOTE, cmds);
    ApplyCommonOptions(h.get(), opt);

    CURLcode rc = CURLE_OK;
    UltraNetResult r = Perform(h.get(), plan.parentUrl, opt, log, &rc);
    return Finish(log, r, rc);
}

} // namespace

UltraNetResult UltraNet_FtpDelete(const std::string& url,
                                  const UltraNetFtpOptions& opt) {
    return RunQuote(url, ultranet_internal::ftpquote::Verb::Delete, {}, opt);
}

UltraNetResult UltraNet_FtpRename(const std::string& url,
                                  const std::string& newName,
                                  const UltraNetFtpOptions& opt) {
    return RunQuote(url, ultranet_internal::ftpquote::Verb::Rename, newName, opt);
}

UltraNetResult UltraNet_FtpCreateDirectory(const std::string& url,
                                           const UltraNetFtpOptions& opt) {
    return RunQuote(url, ultranet_internal::ftpquote::Verb::MakeDirectory, {}, opt);
}

UltraNetResult UltraNet_FtpRemoveDirectory(const std::string& url,
                                           const UltraNetFtpOptions& opt) {
    return RunQuote(url, ultranet_internal::ftpquote::Verb::RemoveDirectory, {}, opt);
}
