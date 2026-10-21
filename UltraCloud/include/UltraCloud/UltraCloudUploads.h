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
//   - finish an upload that was complete when the connection went, before it
//     could be renamed into place, instead of removing it: the next call
//     gives it its name (PlaceCompletePart) - replacing the file there when
//     it finished moments ago, beside that file when it did not
//     (CompletePartReplaces).
//
// The pure parts are here, header-only, so Tests/UltraCloud checks them on
// their own; the steps against a server are in UltraCloudService.cpp.
// Version: 0.2.0 - a complete upload whose rename the connection cut off is
//                  given its name by the cleanup, not removed (completedAt,
//                  PlaceCompletePart, CompletePartReplaces, UploadKeptName)
// Version: 0.1.0
// Last Modified: 2026-10-19
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

// An upload whose leftovers, if it does not finish, are to be removed - or,
// at a part name, put in place if it did.
struct PendingUpload {
    std::string accountId;
    std::string remotePath;    // what it writes: the part name, or the file's own
    bool partName = false;     // `remotePath` is a part name (UploadPartName)
    uint64_t fileSize = 0;     // the whole local file
    int64_t startedAt = 0;     // seconds since the epoch
    // When the last byte was in at the part name, if the upload got as far as
    // knowing it: the server stopped answering before the rename. 0 when not.
    int64_t completedAt = 0;
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
// what it left. At a part name always: nothing else writes there (a
// complete one is put in place rather than removed - IsCompletePart). At the
// file's own name only when shorter than the whole file: one of the full
// size or more was finished since, or is another file of that name, and is
// kept.
inline bool IsLeftByPendingUpload(const PendingUpload& upload, uint64_t sizeOnServer) {
    return upload.partName || sizeOnServer < upload.fileSize;
}

// Whether the part `upload` wrote holds the whole file: the upload said so
// (completedAt), or the folder's listing has it at the file's full size - a
// process that ended between the last byte and the rename. A part the
// listing leaves out (a server that hides dot names) is complete only when
// the upload said so.
inline bool IsCompletePart(const PendingUpload& upload, bool listed, uint64_t sizeOnServer) {
    return upload.partName &&
           (upload.completedAt != 0 || (listed && sizeOnServer == upload.fileSize));
}

// How long after it finished a complete part still replaces the file at its
// name - what the upload was doing when the connection went. Later, that file
// may be one written there since, and the part is kept beside it instead.
inline constexpr int64_t kCompleteUploadReplacesSeconds = 3600;

// Whether the complete part `upload` wrote replaces a file at its name at
// `now`. A part whose upload did not see the end finished after it started,
// so the start stands in for the end: a recent start is a recent end.
inline bool CompletePartReplaces(const PendingUpload& upload, int64_t now) {
    const int64_t finished = upload.completedAt != 0 ? upload.completedAt : upload.startedAt;
    return finished != 0 && now - finished < kCompleteUploadReplacesSeconds;
}

// The name a complete upload is kept under beside a file that has its name:
// "name (2).ext", then "name (3).ext" - the rule UltraFiler's "Keep both"
// follows. `taken` answers whether a name is in use in the folder.
inline std::string UploadKeptName(const std::string& name,
                                  const std::function<bool(const std::string&)>& taken) {
    // The extension the way std::filesystem splits one: from the last dot,
    // unless that dot starts the name (".bashrc" has none).
    const std::string::size_type dot = name.find_last_of('.');
    const bool hasExt = dot != std::string::npos && dot > 0;
    const std::string stem = hasExt ? name.substr(0, dot) : name;
    const std::string ext = hasExt ? name.substr(dot) : std::string();
    std::string candidate;
    for (int n = 2; candidate.empty() || taken(candidate); ++n)
        candidate = stem + " (" + std::to_string(n) + ")" + ext;
    return candidate;
}

// What the folder holds at the name a complete part is for.
enum class AtName { Nothing, File, Folder };

enum class CompletePart {
    Named,         // under its own name, which was free
    Replaced,      // under its own name, in place of the file there
    KeptBeside,    // under `keptName`, beside what has its name
    Refused,       // the server would not rename it; everything as it was
    Unreachable    // the server stopped answering; the part may still be there
};

// The cleanup's counterpart of PutUploadInPlace, for a complete upload at
// `folder`/UploadPartName(name) whose connection went before the rename.
// `atName` is what the folder's listing has at `name`; `setAside` whether it
// has UploadSetAsideName(name) - the old file, set aside by an upload cut
// off while it replaced it, which goes once the new one has the name (or
// back to the name when the part cannot be renamed). A file at `name` is
// replaced when `replace` (CompletePartReplaces), and otherwise kept, the
// part put beside it as `keptName` (UploadKeptName).
inline CompletePart PlaceCompletePart(const std::string& folder, const std::string& name,
                                      AtName atName, bool setAside, bool replace,
                                      const std::string& keptName, const InPlaceOps& ops) {
    const std::string part = UploadPartName(name);
    const std::string aside = UploadSetAsideName(name);
    auto say = [&ops](const std::string& line) { if (ops.say) ops.say(line); };
    bool unreachable = false;
    if (atName == AtName::Nothing) {
        if (ops.rename(folder + "/" + part, name, unreachable)) {
            if (setAside) {
                bool gone = false;
                if (ops.remove(folder + "/" + aside, gone))
                    say("The old \"" + name + "\", set aside as \"" + aside + "\", is removed");
                else
                    say("The old \"" + name + "\" could not be removed; it is still there as \"" +
                        aside + "\"");
            }
            return CompletePart::Named;
        }
        if (unreachable) return CompletePart::Unreachable;
        if (setAside) {
            if (ops.rename(folder + "/" + aside, name, unreachable))
                say("\"" + name + "\" was set aside as \"" + aside + "\": it has its name back");
            else
                say("\"" + name + "\" is still set aside as \"" + aside + "\"");
        }
        return unreachable ? CompletePart::Unreachable : CompletePart::Refused;
    }
    if (atName == AtName::File && replace) {
        switch (PutUploadInPlace(folder, name, ops)) {
            case InPlace::Done:        return CompletePart::Replaced;
            case InPlace::Unreachable: return CompletePart::Unreachable;
            case InPlace::Refused:     return CompletePart::Refused;
        }
    }
    if (keptName.empty()) return CompletePart::Refused;
    if (ops.rename(folder + "/" + part, keptName, unreachable)) return CompletePart::KeptBeside;
    return unreachable ? CompletePart::Unreachable : CompletePart::Refused;
}

} // namespace UltraCloud
