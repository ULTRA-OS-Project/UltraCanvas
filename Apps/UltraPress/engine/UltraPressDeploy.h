// Apps/UltraPress/engine/UltraPressDeploy.h
// The upload half of publishing: the folder PublishStatic wrote goes onto a
// host, file by file, through whoever can put a file there - an UltraCloud
// account (FTP, SFTP, WebDAV, Nextcloud; hosting/UltraPressHosting.h), or a
// test's recorder. Only what changed since the last upload to that host is
// sent: a manifest beside the output remembers each file's fingerprint per
// host, so the second publish of a site with a hundred pictures uploads the
// page that changed and nothing else.
//
// The engine stays headless and free of UltraCloud: the seam is two
// functions, and the host code that fills them lives beside the program.
// Version: 0.5.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace UltraPress {

// Whoever can put a file on the host. `remotePath` is absolute on the host
// ("/public_html/the-plan/index.html"); `error` says why not.
struct Uploader {
    // Creating a folder that is already there may fail (WebDAV answers
    // MKCOL with 405): the deploy treats a failure here as advice and lets
    // the upload decide. FTP creates the missing folders of an upload itself.
    std::function<bool(const std::string& remotePath, std::string& error)> makeDirectory;
    std::function<bool(const std::string& localPath, const std::string& remotePath, std::string& error)> upload;
};

struct DeployOptions {
    std::string remoteRoot = "/";    // the folder on the host that is the site's root
    bool force = false;              // upload everything, whatever the manifest remembers
    // After this many failures in a row the rest is left for the next
    // publish: a host that is down is one failure, not one per file. 0 never
    // stops.
    int stopAfterFailures = 3;
    // Called before each upload: the file, its number and how many there are.
    std::function<void(const std::string& relative, size_t index, size_t total)> onProgress;
};

struct DeployResult {
    bool ok = false;
    std::vector<std::string> uploaded;     // relative paths, '/'-separated
    std::vector<std::string> unchanged;    // skipped: the manifest had the same fingerprint
    std::vector<std::string> failed;
    std::vector<std::string> notTried;     // left for next time after the failures in a row
    std::vector<std::string> errors;       // one line per failure, and the manifest's
    int directoriesCreated = 0;

    std::string Summary() const;           // "12 uploaded, 30 unchanged, 0 failed", "... 3 failed, 40 not tried"
};

// A file's fingerprint: its size and FNV-1a 64 over its content, as hex.
// Change detection between two publishes, not integrity or security. Empty
// when the file cannot be read.
std::string Fingerprint(const std::string& path);

// The file beside the output that remembers what each host holds.
extern const char* const kDeployManifestName;    // ".ultrapress-deploy.json"

// Uploads `outputDirectory` (what PublishStatic wrote) onto the host behind
// `uploader`, under `options.remoteRoot`. `hostKey` names the host in the
// manifest - the account id and the root - so a second host gets everything.
// Files are visited in path order; the folders a file needs are created
// first, each once. The manifest is written at the end, failures left out of
// it so they are tried again next time; a file that fails does not stop the
// others. `ok` is true when every file went up.
DeployResult DeployFolder(const std::string& outputDirectory, const Uploader& uploader,
                          const std::string& hostKey, const DeployOptions& options = {});

// The remote path of a relative file under a root: JoinRemote("/public_html/",
// "the-plan/index.html") is "/public_html/the-plan/index.html"; the root
// always begins with '/', and "/" alone is the host's root.
std::string JoinRemote(const std::string& root, const std::string& relative);

} // namespace UltraPress
