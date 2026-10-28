// Apps/UltraStoreAdmin/core/Uploader.h
// The publish folder onto the host: what changed since the last upload to
// that server, the packages and the media first, the signature, and
// catalog.json last - so a store fetching in between sees the old catalog
// whole or the new one whole, never a new catalog whose packages are not
// there yet. The transport is an interface: UltraNet's SFTP / FTPS / FTP
// (UltraNetFtp.h) and HTTPS PUT (WebDAV, UltraNetHttp.h) here, a recorder
// in the tests. What each server already has is kept per target URL in the
// store folder (upload-state.json: file -> SHA-256), written after every
// file, so an upload that broke off goes on from where it stopped.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraStoreAdmin {

using UltraStore::Result;

// Where the folder goes: sftp://user@host/srv/store/v1, ftps://host/path,
// ftp://host/path, https://host/path (one PUT per file, folders made with
// MKCOL: a WebDAV server). Plain http:// is refused: the password would go in
// clear.
struct UploadTarget {
    std::string url;
    std::string username;             // when the URL carries none
    std::string password;
    std::string privateKeyFileUtf8;   // SFTP with a key, beside or instead of the password
    std::string keyPassphrase;
};

enum class UploadScheme { Sftp, Ftps, Ftp, Https, Unknown };
UploadScheme SchemeOf(const std::string& url);
std::string  SchemeName(UploadScheme scheme);

// What moves one file. Paths are relative to the target, with '/'.
class IUploadTransport {
public:
    virtual ~IUploadTransport() = default;
    // The folder exists afterwards ("packages", "media/org.ultraos.hello").
    virtual Result<void> EnsureFolder(const std::string& relativeDir) = 0;
    // The file is on the server afterwards, whole, under that name.
    virtual Result<void> Put(const std::string& relativePath, const std::string& localPathUtf8, uint64_t size) = 0;
};

// UltraNet's transport for the target's scheme; InvalidArgument for a URL
// the uploader does not speak.
Result<std::unique_ptr<IUploadTransport>> CreateUltraNetTransport(const UploadTarget& target);

struct UploadProgress {
    size_t filesDone = 0, filesTotal = 0;     // of the files to send
    uint64_t bytesDone = 0, bytesTotal = 0;
    std::string currentFile;                  // relative; what is being sent now
};

struct UploadOptions {
    std::string publishDirUtf8;      // the folder publish wrote
    std::string stateFileUtf8;       // <store>/upload-state.json; empty: nothing remembered, everything sent
    bool everything = false;         // send every file, whatever the state says
    std::function<void(const UploadProgress&)> onProgress;   // on the calling thread, before each file
};

struct UploadReport {
    size_t filesSent = 0, filesSkipped = 0;   // skipped: unchanged since the last upload to this target
    uint64_t bytesSent = 0;
    std::vector<std::string> warnings;        // files the server still has that the folder no longer does
};

// The files of the publish folder, in the order they are sent: everything
// but the catalog and its signature, sorted; then catalog.json.sig; then
// catalog.json. Relative paths with '/'.
Result<std::vector<std::string>> FilesToSend(const std::string& publishDirUtf8);

// Sends what the target lacks. Stops at the first failure - what was sent
// before it is remembered, so the next run goes on from there.
Result<UploadReport> UploadPublishFolder(IUploadTransport& transport, const std::string& targetUrl, const UploadOptions& options);

// <store>/upload-state.json.
std::string UploadStatePath(const std::string& storeDirUtf8);

} // namespace UltraStoreAdmin
