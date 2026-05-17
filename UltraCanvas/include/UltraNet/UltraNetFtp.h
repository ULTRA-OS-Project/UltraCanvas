// include/UltraNet/UltraNetFtp.h
// FTP / FTPS / SFTP file transfer surface. Backed by libcurl (FTP/FTPS
// natively, SFTP via libssh2 when libcurl is built with SSH support).
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraNetCore.h"

#include <cstdint>
#include <string>
#include <vector>

enum class UltraNetFtpEntryType {
    File,
    Directory,
    Symlink,
    Unknown
};

struct UltraNetFtpOptions {
    UltraNetCredentials credentials;
    bool passiveMode = true;
    bool useTls = false;
    bool implicitTls = false;
    int connectTimeoutMs = 10000;
    int transferTimeoutMs = 0;          // 0 = use global default
    bool createMissingDirs = false;
    bool resumeTransfer = false;
    int64_t resumeOffset = 0;

    static UltraNetFtpOptions Default() { return {}; }
};

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
