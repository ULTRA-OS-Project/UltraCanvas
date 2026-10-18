// UltraCloud/include/UltraCloud/UltraCloudUploads.h
// Uploads to a server that writes a file as it arrives - FTP, FTPS and SFTP
// (ProviderCapabilities::partialUploadsRemain). Sent straight to its name,
// an upload that replaces a file destroys the old one with its first byte,
// and one stopped part way - the application quit, the connection broke -
// leaves the part it sent looking like the file: the protocol has no way to
// take it back. CloudService::Upload and UploadAndShare therefore, for every
// application on UltraCloud:
//
//   - write the upload to a hidden part name beside the file
//     (UploadPartName: ".report.pdf.part") and rename it into place once the
//     last byte is in (PutUploadInPlace). Until then the old file is whole.
//     A server that will not rename onto an existing file (SFTP, some
//     Windows servers) has the old one set aside for that moment
//     (UploadSetAsideName) and put back if the rename fails.
//   - go straight to the name instead for an anonymous FTP login (an
//     "incoming" folder takes uploads but seldom renames or deletes them,
//     IsAnonymousFtpLogin), for a name too long to mark, when the server
//     refuses the part name, and for an account whose server refused to
//     rename an upload into place - remembered in the account store
//     (AccountStore::SetUploadsDirect) for kPendingUploadMaxAgeSeconds.
//   - keep a PendingUpload in the account store for as long as anything the
//     upload wrote could be left behind: from before the first byte at a
//     part name, from the moment the server has created the file at the
//     file's own name. A process killed during the upload leaves the record
//     too. The next call on that account - in this run or a later one -
//     removes what is still recognisably that upload's (IsLeftByPendingUpload)
//     before it does anything else.
//
// The pure parts are here, header-only, so Tests/UltraCloud checks them on
// their own; the steps against a server are in UltraCloudService.cpp.
// Version: 0.1.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace UltraCloud {

// An FTP login without an account of its own: no user name (libcurl then
// signs in as "anonymous"), "anonymous" or "ftp". Never SFTP, which always
// signs in as someone.
inline bool IsAnonymousFtpLogin(const std::string& serverUrl, const std::string& username) {
    std::string scheme = serverUrl.substr(0, serverUrl.find("://"));
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme == "sftp") return false;
    std::string user = username;
    for (char& c : user) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return user.empty() || user == "anonymous" || user == "ftp";
}

// The longest name the servers UltraCloud meets will store (NAME_MAX).
inline constexpr std::size_t kMaxRemoteNameBytes = 255;

// The hidden name an upload of `name` is written to: ".name.part", the
// convention other clients know. Empty when that would be too long.
inline std::string UploadPartName(const std::string& name) {
    if (name.empty() || name.size() + 6 > kMaxRemoteNameBytes) return std::string();
    return "." + name + ".part";
}

// The name a part name is for: "report.pdf" for ".report.pdf.part"; empty
// for a name that is not one.
inline std::string UploadNameOfPart(const std::string& partName) {
    if (partName.size() <= 6 || partName.front() != '.' ||
        partName.compare(partName.size() - 5, 5, ".part") != 0)
        return std::string();
    return partName.substr(1, partName.size() - 6);
}

// The hidden name the file being replaced is moved to for a moment, on a
// server that will not rename onto a file that exists. Empty when that
// would be too long.
inline std::string UploadSetAsideName(const std::string& name) {
    if (name.empty() || name.size() + 10 > kMaxRemoteNameBytes) return std::string();
    return "." + name + ".replaced";
}

// What PutUploadInPlace needs of the server: rename an entry within its
// folder, and delete a file. Each answers whether the server did it, and
// sets `unreachable` when it did not answer at all - as opposed to answering
// no. `say` takes a line for the calling thread's log.
struct InPlaceOps {
    std::function<bool(const std::string& path, const std::string& newName,
                       bool& unreachable)> rename;
    std::function<bool(const std::string& path, bool& unreachable)> remove;
    std::function<void(const std::string& line)> say;
};

enum class InPlace {
    Done,          // the upload is under its own name, the part name gone
    Refused,       // the server would not rename it; everything as it was
    Unreachable    // the server stopped answering; the part may still be there
};

// Moves the complete upload at `folder`/UploadPartName(name) to
// `folder`/name, replacing what is there. `folder` is the server path ("" at
// the root). Most servers rename onto an existing file in one step; one that
// will not has the old file set aside first and deleted once the new one is
// in place, or put back if the rename still fails - so the folder never ends
// up with neither.
inline InPlace PutUploadInPlace(const std::string& folder, const std::string& name,
                                const InPlaceOps& ops) {
    const std::string part = UploadPartName(name);
    const std::string aside = UploadSetAsideName(name);
    auto say = [&ops](const std::string& line) { if (ops.say) ops.say(line); };
    bool unreachable = false;
    if (ops.rename(folder + "/" + part, name, unreachable)) return InPlace::Done;
    if (unreachable) return InPlace::Unreachable;
    if (aside.empty()) return InPlace::Refused;

    // Refused: perhaps only because "name" is there already. Set it aside -
    // which fails too when it is not there, or renames are refused outright.
    if (!ops.rename(folder + "/" + name, aside, unreachable))
        return unreachable ? InPlace::Unreachable : InPlace::Refused;
    say("\"" + name + "\" is set aside as \"" + aside + "\" while the new one takes its name");
    if (ops.rename(folder + "/" + part, name, unreachable)) {
        bool gone = false;
        if (!ops.remove(folder + "/" + aside, gone))
            say("The old \"" + name + "\" could not be removed; it is still there as \"" +
                aside + "\"");
        return InPlace::Done;
    }
    const bool lost = unreachable;
    // Put back as it was, the old file under its name.
    unreachable = false;
    if (!ops.rename(folder + "/" + aside, name, unreachable))
        say("The old \"" + name + "\" could not be put back; it is still there as \"" +
            aside + "\"");
    return lost ? InPlace::Unreachable : InPlace::Refused;
}

// An upload whose leftovers, if it does not finish, are to be removed.
struct PendingUpload {
    std::string accountId;
    std::string remotePath;    // what it writes: the part name, or the file's own
    bool partName = false;     // `remotePath` is a part name (UploadPartName)
    uint64_t fileSize = 0;     // the whole local file
    int64_t startedAt = 0;     // seconds since the epoch
};

// How long a pending upload is kept for an account that is not used again,
// and how long an account's uploads go straight to their names after its
// server refused a rename. Past that, the file at that path is as likely to
// be something else, and the server may have changed.
inline constexpr int64_t kPendingUploadMaxAgeSeconds = 30ll * 24 * 3600;

// How often one run asks again about a pending upload whose server did not
// answer, each time a later call on the account gets through. Past that,
// the next run.
inline constexpr int kPendingCleanupTriesPerRun = 3;

// Whether a file of `sizeOnServer` bytes, found where `upload` wrote, is
// what it left. At a part name always: nothing else writes there, and a
// complete copy is an upload that stopped before it was renamed into place.
// At the file's own name only when shorter than the whole file: one of the
// full size or more was finished since, or is another file of that name,
// and is kept.
inline bool IsLeftByPendingUpload(const PendingUpload& upload, uint64_t sizeOnServer) {
    return upload.partName || sizeOnServer < upload.fileSize;
}

} // namespace UltraCloud
