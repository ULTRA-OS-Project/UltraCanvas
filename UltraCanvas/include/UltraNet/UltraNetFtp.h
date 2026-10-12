// include/UltraNet/UltraNetFtp.h
// FTP / FTPS / SFTP file transfer surface. Backed by libcurl (FTP/FTPS
// natively, SFTP via libssh2 when libcurl is built with SSH support).
//
// Every call can report its session as it happens - the status steps, the
// commands sent and the server's replies, the way an FTP client's message
// log shows them - through UltraNetFtpOptions::onLog, or, for a caller that
// reaches these functions through another layer (an UltraCloud FTP account),
// through a per-thread sink (UltraNet_SetThreadFtpLog). A failed call's
// message is libcurl's specific reason plus the server's refusal ("RETR
// response: 550 - the server said \"550 Permission denied\""), and
// UltraNetResult::diagnostics carries the connection chain.
//
// Connections are kept open between calls on one thread
// (UltraNet_FtpCloseIdleConnections), and a server that refused MLSD is
// listed with LIST from then on.
// Version: 0.5.0 - connections kept open between calls, MLSD refusal remembered
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class UltraNetFtpEntryType {
    File,
    Directory,
    Symlink,
    Unknown
};

// What one line of an FTP / SFTP session log is.
// (Not "Status": Xlib #defines that name, and this header is read beside it.)
enum class UltraNetFtpLogKind {
    Step,       // what the client is doing: resolving, connecting, TLS, logged in,
                // listing - an FTP client's "Status:" line
    Command,    // a command sent to the server ("PASV", "MLSD"); a password never
                // appears - "PASS ********"
    Response,   // a reply from the server ("227 Entering Passive Mode (...)")
    Error       // why the call failed; the last line of a failed call
};

// One line of the session log.
struct UltraNetFtpLogLine {
    UltraNetFtpLogKind kind = UltraNetFtpLogKind::Step;
    std::string text;
    // Response lines: the three-digit reply code (220, 227, 530, ...); 0 for
    // a line inside a multi-line reply, and for every other kind.
    int replyCode = 0;
    // Error lines: UltraNet's classification of the failure, and the
    // transport library's own error number (libcurl's CURLcode, e.g. 28 for a
    // timeout); 0 when not known.
    UltraNetResultCode resultCode = UltraNetResultCode::Unknown;
    int transportCode = 0;
};

// Receives the log, one line at a time, on the thread that made the call,
// while the call is running. Must not block: the transfer waits for it.
using UltraNetFtpLogCallback = std::function<void(const UltraNetFtpLogLine&)>;

struct UltraNetFtpOptions {
    UltraNetCredentials credentials;
    bool passiveMode = true;
    bool useTls = false;
    bool implicitTls = false;
    int connectTimeoutMs = 10000;
    int transferTimeoutMs = 0;          // 0 = no limit on the whole call
    // Gives up when the server has sent nothing for this long - no reply to
    // a command, no bytes of a listing or a file - and reports "Connection
    // timed out after N seconds of inactivity". Whole seconds; 0 waits for
    // ever (libcurl's own reply timeout, two minutes, still applies).
    int inactivityTimeoutMs = 30000;
    bool createMissingDirs = false;
    bool resumeTransfer = false;
    int64_t resumeOffset = 0;
    // The session log of this call. When empty, the calling thread's sink
    // (UltraNet_SetThreadFtpLog) gets it, if there is one.
    UltraNetFtpLogCallback onLog;

    static UltraNetFtpOptions Default() { return {}; }
};

// Sets the log sink for FTP calls made on the CALLING THREAD whose options
// carry no onLog of their own, and returns the previous one so a caller can
// put it back. For code that reaches these functions through a layer that
// builds the options itself - UltraFiler's drive worker calls UltraCloud,
// which calls UltraNet - and so cannot hand an onLog down. Per thread, so
// two workers never read each other's sessions. An empty callback clears it.
UltraNetFtpLogCallback UltraNet_SetThreadFtpLog(UltraNetFtpLogCallback sink);

// Every call leaves its connection open, and the next call ON THE SAME THREAD
// to the same server, as the same user with the same TLS settings, takes it
// up without connecting or logging in again - so a worker listing one folder
// after another logs in once, not once per folder. libcurl drops a connection
// left idle for two minutes, and one the server has closed.
//
// This closes the calling thread's open connections now (each is sent QUIT).
// A thread's connections are also closed when it ends, and every thread's by
// UltraNet_Shutdown. A caller whose work comes in bursts calls it when it
// falls idle: an open connection whose network has gone away since can hold
// the QUIT for as long as libcurl waits for the reply - up to two minutes
// before libcurl 8.10 - and the thread's end, which a window closing may wait
// on, with it.
void UltraNet_FtpCloseIdleConnections();

struct UltraNetFtpEntry {
    std::string name;
    std::string fullPath;
    UltraNetFtpEntryType type = UltraNetFtpEntryType::Unknown;
    int64_t size = 0;
    std::string modificationTime;
    std::string permissions;
    std::string owner;
    std::string group;
};

UltraNetResult UltraNet_FtpDownload(
    const std::string& url,
    const std::string& localPath,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

UltraNetResult UltraNet_FtpUpload(
    const std::string& localPath,
    const std::string& url,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

// Lists names in the directory denoted by `url`. Returns one entry per name.
// Stage-3 metadata is populated from libcurl's CURLOPT_DIRLISTONLY listing —
// `size`, `modificationTime`, `permissions`, `owner`, `group` will be filled
// when the richer-parsing variant lands (FTP MLSD / SFTP attrs).
UltraNetResult UltraNet_FtpListDirectory(
    const std::string& url,
    std::vector<UltraNetFtpEntry>& outEntries,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

UltraNetResult UltraNet_FtpDelete(
    const std::string& url,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

UltraNetResult UltraNet_FtpRename(
    const std::string& url,
    const std::string& newName,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

UltraNetResult UltraNet_FtpCreateDirectory(
    const std::string& url,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());

UltraNetResult UltraNet_FtpRemoveDirectory(
    const std::string& url,
    const UltraNetFtpOptions& options = UltraNetFtpOptions::Default());
