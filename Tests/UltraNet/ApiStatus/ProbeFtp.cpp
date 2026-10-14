// Tests/UltraNet/ApiStatus/ProbeFtp.cpp
// Probes for UltraNet/UltraNetFtp.h.
//
// Unlike HTTP and WebSocket, FTP cannot be served by a small in-process origin
// (passive mode needs a second data connection and a full command grammar), so
// there is nothing to verify a transfer against offline. What the probes CAN
// establish without a server is that each entry point validates its input and
// that the call actually reaches the libcurl FTP backend — a closed loopback
// port must come back ConnectionRefused, not UnsupportedScheme.
//
// Point ULTRANET_PROBE_FTP_URL at a writable directory on a real FTP server
// (e.g. ftp://user:pass@host/probe/) to upgrade this whole area to verified
// WORKING entries.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ApiStatus.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetFtp.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ultranet_apistatus;

namespace {

constexpr const char* kArea = "FTP";

// Nothing listens here; a connection attempt must be refused straight away.
constexpr const char* kDeadUrl    = "ftp://127.0.0.1:18899/probe/file.txt";
// Listing wants a directory URL; MKD/RMD derive the parent from the last path
// segment, so they need a URL that still HAS a last segment (no trailing '/').
constexpr const char* kDeadDirUrl = "ftp://127.0.0.1:18899/probe/";
constexpr const char* kDeadSubDirUrl = "ftp://127.0.0.1:18899/probe/subdir";

std::filesystem::path ScratchDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ultranet_apistatus";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Base URL of a live server, or "" when the caller did not supply one.
std::string LiveBase() {
    std::string base = EnvOverride("ULTRANET_PROBE_FTP_URL");
    if (!base.empty() && base.back() != '/') base.push_back('/');
    return base;
}

bool ReachedBackend(const UltraNetResult& r) {
    // Anything except "libcurl has no idea what ftp:// is" means the request
    // was handed to the FTP backend and failed on the network, as intended.
    return !r && r.code != UltraNetResultCode::UnsupportedScheme;
}

Outcome BackendOnly(const char* what, const UltraNetResult& r) {
    if (r.code == UltraNetResultCode::UnsupportedScheme) {
        return NotImplemented(std::string(what) +
                              ": the linked libcurl has no ftp:// support");
    }
    return Implemented(std::string(what) +
                       " reached the libcurl FTP backend (closed port reported "
                       "\"" + r.message + "\"), but no FTP server is available "
                       "to verify the operation. Set ULTRANET_PROBE_FTP_URL to "
                       "check it against a real server.");
}

} // namespace

ULTRANET_PROBE_NAMED(kArea, "FTP scheme support (ftp/ftps/sftp)", FtpSchemes) {
    const std::filesystem::path sink = ScratchDir() / "ftp_scheme_probe.bin";
    std::error_code ec;

    UltraNetFtpOptions fast;
    fast.connectTimeoutMs = 1500;

    const UltraNetResult ftp = UltraNet_FtpDownload(kDeadUrl, sink.string(), fast);
    const UltraNetResult sftp = UltraNet_FtpDownload(
        "sftp://127.0.0.1:18899/probe/file.txt", sink.string(), fast);
    std::filesystem::remove(sink, ec);

    const bool ftpSupported  = ftp.code != UltraNetResultCode::UnsupportedScheme;
    const bool sftpSupported = sftp.code != UltraNetResultCode::UnsupportedScheme;

    if (!ftpSupported) {
        return NotImplemented("the linked libcurl was built without ftp:// "
                              "support, so the whole FTP surface is inert in "
                              "this build");
    }
    return Working(std::string("ftp:// is supported by the linked libcurl; "
                               "sftp:// is ") +
                   (sftpSupported ? "supported (libcurl has SSH support)"
                                  : "NOT supported (libcurl was built without "
                                    "libssh2, so sftp:// URLs will fail)"));
}

ULTRANET_PROBE(kArea, UltraNet_FtpDownload) {
    const UltraNetResult emptyUrl = UltraNet_FtpDownload("", "/tmp/x");
    PROBE_EXPECT(!emptyUrl && emptyUrl.code == UltraNetResultCode::InvalidUrl);
    const UltraNetResult emptyPath = UltraNet_FtpDownload(kDeadUrl, "");
    PROBE_EXPECT(!emptyPath && emptyPath.code == UltraNetResultCode::InvalidUrl);

    const std::filesystem::path out = ScratchDir() / "ftp_download.bin";
    std::error_code ec;

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpDownload(kDeadUrl, out.string(), fast);
        std::filesystem::remove(out, ec);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpDownload", dead);
    }

    // Live server: upload a known payload first so there is something to fetch.
    const std::filesystem::path src = ScratchDir() / "ftp_seed.txt";
    const std::string payload = "ultranet ftp probe payload\n";
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << payload; }
    const UltraNetResult seeded =
        UltraNet_FtpUpload(src.string(), base + "probe_download.txt");
    std::filesystem::remove(src, ec);
    if (!seeded) {
        return Implemented("could not seed a file on " + base + " (" +
                           seeded.message + "), so the download is unverified");
    }

    const UltraNetResult r =
        UltraNet_FtpDownload(base + "probe_download.txt", out.string());
    const bool exists = std::filesystem::exists(out);
    std::string got;
    if (exists) {
        std::ifstream in(out, std::ios::binary);
        got.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(out, ec);
    UltraNet_FtpDelete(base + "probe_download.txt");

    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);
    PROBE_EXPECT_MSG(got == payload, "downloaded \"" + got + "\"");
    return Working("file downloaded from " + base + " with byte-exact content");
}

ULTRANET_PROBE(kArea, UltraNet_FtpUpload) {
    const UltraNetResult emptyUrl = UltraNet_FtpUpload("/tmp/x", "");
    PROBE_EXPECT(!emptyUrl && emptyUrl.code == UltraNetResultCode::InvalidUrl);
    const UltraNetResult missingFile =
        UltraNet_FtpUpload("/nonexistent/ultranet/probe", kDeadUrl);
    PROBE_EXPECT(!missingFile && missingFile.code == UltraNetResultCode::NotFound);

    const std::filesystem::path src = ScratchDir() / "ftp_upload.txt";
    const std::string payload = "ultranet ftp upload probe\n";
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << payload; }
    std::error_code ec;

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpUpload(src.string(), kDeadUrl, fast);
        std::filesystem::remove(src, ec);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpUpload", dead);
    }

    const UltraNetResult r = UltraNet_FtpUpload(src.string(), base + "probe_upload.txt");
    std::filesystem::remove(src, ec);
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    const std::filesystem::path back = ScratchDir() / "ftp_upload_check.txt";
    const UltraNetResult verify =
        UltraNet_FtpDownload(base + "probe_upload.txt", back.string());
    std::string got;
    if (verify) {
        std::ifstream in(back, std::ios::binary);
        got.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(back, ec);
    UltraNet_FtpDelete(base + "probe_upload.txt");

    PROBE_EXPECT_MSG(got == payload, "read back \"" + got + "\"");
    return Working("file uploaded to " + base + " and read back byte-for-byte");
}

ULTRANET_PROBE(kArea, UltraNet_FtpListDirectory) {
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult emptyUrl = UltraNet_FtpListDirectory("", entries);
    PROBE_EXPECT(!emptyUrl && emptyUrl.code == UltraNetResultCode::InvalidUrl);

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpListDirectory(kDeadDirUrl, entries, fast);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpListDirectory", dead);
    }

    const std::filesystem::path src = ScratchDir() / "ftp_list.txt";
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << "listing probe\n"; }
    std::error_code ec;
    const UltraNetResult seeded =
        UltraNet_FtpUpload(src.string(), base + "probe_list.txt");
    std::filesystem::remove(src, ec);
    if (!seeded) {
        return Implemented("could not seed a file on " + base + " (" +
                           seeded.message + "), so the listing is unverified");
    }

    const UltraNetResult r = UltraNet_FtpListDirectory(base, entries);
    UltraNet_FtpDelete(base + "probe_list.txt");
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    bool found = false;
    bool anyMetadata = false;
    for (const UltraNetFtpEntry& e : entries) {
        if (e.name == "probe_list.txt") {
            found = true;
            anyMetadata = e.size > 0 || !e.modificationTime.empty();
        }
    }
    PROBE_EXPECT_MSG(found, "the uploaded file did not appear in the listing of " +
                            std::to_string(entries.size()) + " entries");
    if (!anyMetadata) {
        return Implemented("listing returned the entry name, but size and "
                           "modification time were empty — the server answered "
                           "with a names-only listing (documented Stage-3 "
                           "limitation)");
    }
    return Working("listing of " + base + " contained the seeded file with "
                   "size/modification metadata");
}

ULTRANET_PROBE(kArea, UltraNet_FtpDelete) {
    const UltraNetResult noParent = UltraNet_FtpDelete("nonsense");
    PROBE_EXPECT(!noParent && noParent.code == UltraNetResultCode::InvalidUrl);

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpDelete(kDeadUrl, fast);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpDelete", dead);
    }

    const std::filesystem::path src = ScratchDir() / "ftp_delete.txt";
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << "delete probe\n"; }
    std::error_code ec;
    const UltraNetResult seeded =
        UltraNet_FtpUpload(src.string(), base + "probe_delete.txt");
    std::filesystem::remove(src, ec);
    if (!seeded) {
        return Implemented("could not seed a file on " + base + " (" +
                           seeded.message + "), so the delete is unverified");
    }

    const UltraNetResult r = UltraNet_FtpDelete(base + "probe_delete.txt");
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    const std::filesystem::path back = ScratchDir() / "ftp_delete_check.txt";
    const UltraNetResult gone =
        UltraNet_FtpDownload(base + "probe_delete.txt", back.string());
    std::filesystem::remove(back, ec);
    PROBE_EXPECT_MSG(!gone, "the file was still downloadable after DELE");
    return Working("DELE removed the seeded file (a later fetch fails)");
}

ULTRANET_PROBE(kArea, UltraNet_FtpRename) {
    const UltraNetResult noParent = UltraNet_FtpRename("nonsense", "other");
    PROBE_EXPECT(!noParent && noParent.code == UltraNetResultCode::InvalidUrl);

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpRename(kDeadUrl, "renamed.txt", fast);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpRename", dead);
    }

    const std::filesystem::path src = ScratchDir() / "ftp_rename.txt";
    { std::ofstream f(src, std::ios::binary | std::ios::trunc); f << "rename probe\n"; }
    std::error_code ec;
    const UltraNetResult seeded =
        UltraNet_FtpUpload(src.string(), base + "probe_rename_a.txt");
    std::filesystem::remove(src, ec);
    if (!seeded) {
        return Implemented("could not seed a file on " + base + " (" +
                           seeded.message + "), so the rename is unverified");
    }

    const UltraNetResult r =
        UltraNet_FtpRename(base + "probe_rename_a.txt", "probe_rename_b.txt");
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    const std::filesystem::path back = ScratchDir() / "ftp_rename_check.txt";
    const UltraNetResult fetched =
        UltraNet_FtpDownload(base + "probe_rename_b.txt", back.string());
    std::filesystem::remove(back, ec);
    UltraNet_FtpDelete(base + "probe_rename_b.txt");
    PROBE_EXPECT_MSG(static_cast<bool>(fetched),
                     "the renamed file could not be fetched: " + fetched.message);
    return Working("RNFR/RNTO renamed the seeded file and the new name fetches");
}

ULTRANET_PROBE(kArea, UltraNet_FtpCreateDirectory) {
    // MKD names the new directory with the last path segment, so a URL that is
    // only a directory path has nothing to create.
    const UltraNetResult noName = UltraNet_FtpCreateDirectory(kDeadDirUrl);
    PROBE_EXPECT(!noName && noName.code == UltraNetResultCode::InvalidUrl);

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpCreateDirectory(kDeadSubDirUrl, fast);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpCreateDirectory", dead);
    }

    const UltraNetResult r = UltraNet_FtpCreateDirectory(base + "probe_dir");
    if (!r) {
        return Implemented("MKD on " + base + "probe_dir failed (" + r.message +
                           "); the account may not be allowed to create "
                           "directories");
    }
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult listed = UltraNet_FtpListDirectory(base, entries);
    UltraNet_FtpRemoveDirectory(base + "probe_dir");
    PROBE_EXPECT_MSG(static_cast<bool>(listed), listed.message);

    bool found = false;
    for (const UltraNetFtpEntry& e : entries) if (e.name == "probe_dir") found = true;
    PROBE_EXPECT_MSG(found, "the new directory did not appear in the listing");
    return Working("MKD created a directory that then appeared in the listing");
}

ULTRANET_PROBE(kArea, UltraNet_FtpRemoveDirectory) {
    const UltraNetResult noName = UltraNet_FtpRemoveDirectory(kDeadDirUrl);
    PROBE_EXPECT(!noName && noName.code == UltraNetResultCode::InvalidUrl);

    const std::string base = LiveBase();
    if (base.empty()) {
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        const UltraNetResult dead = UltraNet_FtpRemoveDirectory(kDeadSubDirUrl, fast);
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpRemoveDirectory", dead);
    }

    const UltraNetResult created = UltraNet_FtpCreateDirectory(base + "probe_rmdir");
    if (!created) {
        return Implemented("could not create a directory to remove on " + base +
                           " (" + created.message + ")");
    }
    const UltraNetResult r = UltraNet_FtpRemoveDirectory(base + "probe_rmdir");
    PROBE_EXPECT_MSG(static_cast<bool>(r), r.message);

    std::vector<UltraNetFtpEntry> entries;
    UltraNet_FtpListDirectory(base, entries);
    for (const UltraNetFtpEntry& e : entries) {
        if (e.name == "probe_rmdir") {
            return Broken("RMD reported success but the directory is still listed");
        }
    }
    return Working("RMD removed the directory (it is gone from the listing)");
}

ULTRANET_PROBE(kArea, UltraNet_SetThreadFtpLog) {
    // The session log needs no server: a closed port still resolves, tries to
    // connect and fails, and each of those is a line. The thread's sink hears
    // a call that brings no onLog; a call that does goes to its own instead.
    std::vector<UltraNetFtpLogLine> threadLines;
    UltraNetFtpLogCallback previous = UltraNet_SetThreadFtpLog(
            [&threadLines](const UltraNetFtpLogLine& l) { threadLines.push_back(l); });

    UltraNetFtpOptions fast;
    fast.connectTimeoutMs = 1500;
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult viaThread = UltraNet_FtpListDirectory(kDeadDirUrl, entries, fast);

    std::vector<UltraNetFtpLogLine> ownLines;
    UltraNetFtpOptions own = fast;
    own.onLog = [&ownLines](const UltraNetFtpLogLine& l) { ownLines.push_back(l); };
    const std::size_t threadBefore = threadLines.size();
    (void)UltraNet_FtpListDirectory(kDeadDirUrl, entries, own);
    UltraNet_SetThreadFtpLog(std::move(previous));

    if (viaThread.code == UltraNetResultCode::UnsupportedScheme)
        return NotImplemented("the linked libcurl has no ftp:// support");
    PROBE_EXPECT_MSG(!threadLines.empty(), "the thread's sink heard nothing");
    PROBE_EXPECT_MSG(threadLines.front().kind == UltraNetFtpLogKind::Step,
                     "the log does not start with a step");
    PROBE_EXPECT_MSG(threadLines.back().kind == UltraNetFtpLogKind::Error &&
                             threadLines.back().text == viaThread.message,
                     "the log does not end with the call's error");
    PROBE_EXPECT_MSG(threadLines.size() == threadBefore,
                     "a call with its own onLog also reached the thread's sink");
    PROBE_EXPECT_MSG(!ownLines.empty(), "onLog heard nothing");
    return Working("a call against a closed port logged " +
                   std::to_string(threadLines.size()) + " lines, ending \"" +
                   threadLines.back().text + "\"; onLog takes precedence");
}

ULTRANET_PROBE(kArea, UltraNet_FtpCloseIdleConnections) {
    // With nothing open it has nothing to do, and twice is the same as once.
    UltraNet_FtpCloseIdleConnections();
    UltraNet_FtpCloseIdleConnections();

    const std::string base = LiveBase();
    if (base.empty()) {
        // A call after it connects as any call does.
        UltraNetFtpOptions fast;
        fast.connectTimeoutMs = 1500;
        std::vector<UltraNetFtpEntry> entries;
        const UltraNetResult dead = UltraNet_FtpListDirectory(kDeadDirUrl, entries, fast);
        UltraNet_FtpCloseIdleConnections();
        PROBE_EXPECT_MSG(ReachedBackend(dead),
                         "a closed port did not produce a network error");
        return BackendOnly("UltraNet_FtpCloseIdleConnections", dead);
    }

    // Against a server: the second listing takes up the connection the first
    // left open, and the one after the close makes a new one.
    auto list = [&base](bool& reused) {
        reused = false;
        UltraNetFtpOptions opt;
        opt.onLog = [&reused](const UltraNetFtpLogLine& l) {
            if (l.kind == UltraNetFtpLogKind::Step &&
                l.text.rfind("Using the open connection", 0) == 0)
                reused = true;
        };
        std::vector<UltraNetFtpEntry> entries;
        return UltraNet_FtpListDirectory(base, entries, opt);
    };
    bool first = false, second = false, third = false;
    const UltraNetResult r1 = list(first);
    const UltraNetResult r2 = list(second);
    UltraNet_FtpCloseIdleConnections();
    const UltraNetResult r3 = list(third);
    UltraNet_FtpCloseIdleConnections();
    PROBE_EXPECT_MSG(static_cast<bool>(r1) && static_cast<bool>(r2) && static_cast<bool>(r3),
                     "a listing of " + base + " failed");
    PROBE_EXPECT_MSG(!first, "the first listing claimed an open connection");
    PROBE_EXPECT_MSG(second, "the second listing logged in again instead of "
                             "using the open connection");
    PROBE_EXPECT_MSG(!third, "a listing after the close still used the old connection");
    return Working("the second listing of " + base + " used the open connection; "
                   "after the close the third connected afresh");
}
