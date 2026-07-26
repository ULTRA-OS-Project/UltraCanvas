// VirtualFS/providers/VirtualFSLibArchiveProvider.cpp
// libarchive-based provider implementation
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework

#include "VirtualFSLibArchiveProvider.h"
#include "VirtualFSManager.h"

#ifdef VIRTUALFS_HAS_LIBARCHIVE

#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef VIRTUALFS_HAS_MINIZ
#include "miniz.h"
#endif

namespace VirtualFS {

// ============================================================================
// IMPLEMENTATION DETAILS
// ============================================================================

struct VirtualFSLibArchiveProvider::Impl {
    struct archive* readArchive = nullptr;
    struct archive* writeArchive = nullptr;
    
    std::string archivePath;
    std::string lastError;
    bool isOpen = false;
    bool isWriteMode = false;
    
    VirtualFSOpenOptions openOptions;
    VirtualFSArchiveInfo archiveInfo;
    
    // Entry cache
    std::map<std::string, VirtualFSEntry> entryCache;
    std::map<std::string, std::vector<std::string>> directoryContents;
    bool cacheValid = false;
    
    ~Impl() {
        CloseArchives();
    }
    
    void CloseArchives() {
        if (readArchive) {
            archive_read_free(readArchive);
            readArchive = nullptr;
        }
        if (writeArchive) {
            archive_write_free(writeArchive);
            writeArchive = nullptr;
        }
        isOpen = false;
        cacheValid = false;
    }
};

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

VirtualFSLibArchiveProvider::VirtualFSLibArchiveProvider()
    : pImpl(std::make_unique<Impl>()) {
}

VirtualFSLibArchiveProvider::~VirtualFSLibArchiveProvider() {
    Close();
}

// ============================================================================
// IDENTIFICATION
// ============================================================================

std::string VirtualFSLibArchiveProvider::GetLibraryInfo() const {
    return std::string("libarchive ") + archive_version_string();
}

// ============================================================================
// DETECTION
// ============================================================================

bool VirtualFSLibArchiveProvider::CanHandle(const std::string& path) const {
    std::string ext = path;
    
    size_t dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos + 1);
    }
    
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    auto extensions = GetSupportedExtensions();
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

bool VirtualFSLibArchiveProvider::CanHandleByMagic(const uint8_t* data, size_t size) const {
    if (!data || size < 4) return false;
    
    // ZIP: PK\x03\x04
    if (size >= 4 && data[0] == 0x50 && data[1] == 0x4B &&
        (data[2] == 0x03 || data[2] == 0x05 || data[2] == 0x07)) {
        return true;
    }
    
    // 7z: 7z\xBC\xAF\x27\x1C
    if (size >= 6 && data[0] == 0x37 && data[1] == 0x7A &&
        data[2] == 0xBC && data[3] == 0xAF && data[4] == 0x27 && data[5] == 0x1C) {
        return true;
    }
    
    // RAR: Rar!\x1A\x07
    if (size >= 6 && data[0] == 0x52 && data[1] == 0x61 && data[2] == 0x72 &&
        data[3] == 0x21 && data[4] == 0x1A && data[5] == 0x07) {
        return true;
    }
    
    // GZIP: \x1F\x8B
    if (size >= 2 && data[0] == 0x1F && data[1] == 0x8B) {
        return true;
    }
    
    // BZIP2: BZ
    if (size >= 2 && data[0] == 0x42 && data[1] == 0x5A) {
        return true;
    }
    
    // XZ: \xFD7zXZ\x00
    if (size >= 6 && data[0] == 0xFD && data[1] == 0x37 && data[2] == 0x7A &&
        data[3] == 0x58 && data[4] == 0x5A && data[5] == 0x00) {
        return true;
    }
    
    // Zstandard: \x28\xB5\x2F\xFD
    if (size >= 4 && data[0] == 0x28 && data[1] == 0xB5 &&
        data[2] == 0x2F && data[3] == 0xFD) {
        return true;
    }
    
    // LZ4: \x04\x22\x4D\x18
    if (size >= 4 && data[0] == 0x04 && data[1] == 0x22 &&
        data[2] == 0x4D && data[3] == 0x18) {
        return true;
    }
    
    // TAR: ustar at offset 257
    if (size >= 263 && std::memcmp(data + 257, "ustar", 5) == 0) {
        return true;
    }
    
    // CAB: MSCF
    if (size >= 4 && data[0] == 0x4D && data[1] == 0x53 &&
        data[2] == 0x43 && data[3] == 0x46) {
        return true;
    }
    
    return false;
}

// ============================================================================
// ARCHIVE LIFECYCLE
// ============================================================================

VirtualFSResult VirtualFSLibArchiveProvider::Open(
    const std::string& archivePath,
    const VirtualFSOpenOptions& options) {
    
    Close();
    
    pImpl->archivePath = archivePath;
    pImpl->openOptions = options;
    
    if (!std::filesystem::exists(archivePath)) {
        pImpl->lastError = "Archive file not found: " + archivePath;
        return VirtualFSResult::NotFound;
    }
    
    pImpl->readArchive = archive_read_new();
    if (!pImpl->readArchive) {
        pImpl->lastError = "Failed to create archive reader";
        return VirtualFSResult::OutOfMemory;
    }
    
    archive_read_support_filter_all(pImpl->readArchive);
    archive_read_support_format_all(pImpl->readArchive);
    
    if (!options.password.empty()) {
        archive_read_add_passphrase(pImpl->readArchive, options.password.c_str());
    }
    
    int result = archive_read_open_filename(pImpl->readArchive, archivePath.c_str(), 10240);
    if (result != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(pImpl->readArchive);
        archive_read_free(pImpl->readArchive);
        pImpl->readArchive = nullptr;
        
        if (result == ARCHIVE_FATAL) {
            return VirtualFSResult::ArchiveCorrupt;
        }
        return VirtualFSResult::Error;
    }
    
    pImpl->isOpen = true;
    pImpl->isWriteMode = (options.mode == VirtualFSOpenMode::Write || 
                          options.mode == VirtualFSOpenMode::Create);
    
    BuildEntryCache();
    
    pImpl->archiveInfo.path = archivePath;
    pImpl->archiveInfo.archiveSize = std::filesystem::file_size(archivePath);
    pImpl->archiveInfo.entryCount = pImpl->entryCache.size();
    
    for (const auto& pair : pImpl->entryCache) {
        if (pair.second.IsDirectory()) {
            pImpl->archiveInfo.directoryCount++;
        } else {
            pImpl->archiveInfo.fileCount++;
            pImpl->archiveInfo.uncompressedSize += pair.second.size;
        }
    }
    
    if (pImpl->archiveInfo.uncompressedSize > 0) {
        pImpl->archiveInfo.compressionRatio = 100.0f * 
            (1.0f - static_cast<float>(pImpl->archiveInfo.archiveSize) / 
                    static_cast<float>(pImpl->archiveInfo.uncompressedSize));
    }
    
    return VirtualFSResult::Success;
}

void VirtualFSLibArchiveProvider::Close() {
    pImpl->CloseArchives();
    ClearEntryCache();
    pImpl->archivePath.clear();
    pImpl->lastError.clear();
}

bool VirtualFSLibArchiveProvider::IsOpen() const {
    return pImpl->isOpen;
}

std::string VirtualFSLibArchiveProvider::GetOpenPath() const {
    return pImpl->archivePath;
}

VirtualFSArchiveInfo VirtualFSLibArchiveProvider::GetArchiveInfo() const {
    return pImpl->archiveInfo;
}

// ============================================================================
// ENTRY CACHE
// ============================================================================

void VirtualFSLibArchiveProvider::BuildEntryCache() {
    ClearEntryCache();
    
    if (!pImpl->readArchive) return;
    
    archive_read_free(pImpl->readArchive);
    pImpl->readArchive = archive_read_new();
    archive_read_support_filter_all(pImpl->readArchive);
    archive_read_support_format_all(pImpl->readArchive);
    
    if (!pImpl->openOptions.password.empty()) {
        archive_read_add_passphrase(pImpl->readArchive, pImpl->openOptions.password.c_str());
    }
    
    if (archive_read_open_filename(pImpl->readArchive, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        return;
    }
    
    struct archive_entry* entry;
    while (archive_read_next_header(pImpl->readArchive, &entry) == ARCHIVE_OK) {
        VirtualFSEntry vfsEntry = ConvertArchiveEntry(entry);
        
        std::string normalizedPath = NormalizeInternalPath(vfsEntry.path);
        vfsEntry.path = normalizedPath;
        
        pImpl->entryCache[normalizedPath] = vfsEntry;
        
        std::string parentPath = "";
        size_t lastSlash = normalizedPath.rfind('/');
        if (lastSlash != std::string::npos) {
            parentPath = normalizedPath.substr(0, lastSlash);
        }
        
        pImpl->directoryContents[parentPath].push_back(vfsEntry.name);
        
        archive_read_data_skip(pImpl->readArchive);
    }
    
    pImpl->cacheValid = true;
}

void VirtualFSLibArchiveProvider::ClearEntryCache() {
    pImpl->entryCache.clear();
    pImpl->directoryContents.clear();
    pImpl->cacheValid = false;
}

VirtualFSEntry VirtualFSLibArchiveProvider::ConvertArchiveEntry(void* archiveEntryPtr) {
    struct archive_entry* entry = static_cast<struct archive_entry*>(archiveEntryPtr);
    VirtualFSEntry vfsEntry;
    
    const char* pathname = archive_entry_pathname_utf8(entry);
    if (!pathname) pathname = archive_entry_pathname(entry);
    
    if (pathname) {
        vfsEntry.path = pathname;
        
        size_t lastSlash = vfsEntry.path.rfind('/');
        if (lastSlash != std::string::npos && lastSlash < vfsEntry.path.length() - 1) {
            vfsEntry.name = vfsEntry.path.substr(lastSlash + 1);
        } else if (lastSlash == std::string::npos) {
            vfsEntry.name = vfsEntry.path;
        } else {
            std::string temp = vfsEntry.path.substr(0, lastSlash);
            lastSlash = temp.rfind('/');
            vfsEntry.name = (lastSlash != std::string::npos) ? temp.substr(lastSlash + 1) : temp;
        }
    }
    
    mode_t fileType = archive_entry_filetype(entry);
    switch (fileType) {
        case AE_IFREG: vfsEntry.type = VirtualFSEntryType::File; break;
        case AE_IFDIR: vfsEntry.type = VirtualFSEntryType::Directory; break;
        case AE_IFLNK:
            vfsEntry.type = VirtualFSEntryType::Symlink;
            vfsEntry.linkTarget = archive_entry_symlink_utf8(entry) ?
                archive_entry_symlink_utf8(entry) : 
                (archive_entry_symlink(entry) ? archive_entry_symlink(entry) : "");
            break;
        default: vfsEntry.type = VirtualFSEntryType::Unknown; break;
    }
    
    if (archive_entry_size_is_set(entry)) {
        vfsEntry.size = static_cast<uint64_t>(archive_entry_size(entry));
    }
    
    vfsEntry.permissions = archive_entry_perm(entry);
    
    if (archive_entry_mtime_is_set(entry)) {
        time_t mtime = archive_entry_mtime(entry);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&mtime));
        vfsEntry.modifiedTime = buf;
    }
    
    vfsEntry.uid = archive_entry_uid(entry);
    vfsEntry.gid = archive_entry_gid(entry);
    vfsEntry.isEncrypted = archive_entry_is_encrypted(entry) != 0;
    vfsEntry.providerName = "LibArchive";
    
    return vfsEntry;
}

std::string VirtualFSLibArchiveProvider::NormalizeInternalPath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    
    while (!normalized.empty() && normalized[0] == '/') {
        normalized = normalized.substr(1);
    }
    
    while (normalized.length() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    
    return normalized;
}

// ============================================================================
// DIRECTORY OPERATIONS
// ============================================================================

std::vector<VirtualFSEntry> VirtualFSLibArchiveProvider::ListDirectory(const std::string& virtualPath) {
    std::vector<VirtualFSEntry> entries;
    
    if (!pImpl->isOpen || !pImpl->cacheValid) return entries;
    
    std::string normalizedPath = NormalizeInternalPath(virtualPath);
    
    auto it = pImpl->directoryContents.find(normalizedPath);
    if (it == pImpl->directoryContents.end()) return entries;
    
    for (const auto& name : it->second) {
        std::string fullPath = normalizedPath.empty() ? name : normalizedPath + "/" + name;
        
        auto entryIt = pImpl->entryCache.find(fullPath);
        if (entryIt != pImpl->entryCache.end()) {
            entries.push_back(entryIt->second);
        }
    }
    
    return entries;
}

std::vector<VirtualFSEntry> VirtualFSLibArchiveProvider::ListAll() {
    std::vector<VirtualFSEntry> entries;
    entries.reserve(pImpl->entryCache.size());
    
    for (const auto& pair : pImpl->entryCache) {
        entries.push_back(pair.second);
    }
    
    return entries;
}

bool VirtualFSLibArchiveProvider::Exists(const std::string& virtualPath) {
    if (!pImpl->isOpen || !pImpl->cacheValid) return false;
    
    std::string normalizedPath = NormalizeInternalPath(virtualPath);
    return pImpl->entryCache.find(normalizedPath) != pImpl->entryCache.end();
}

VirtualFSEntry VirtualFSLibArchiveProvider::GetInfo(const std::string& virtualPath) {
    if (!pImpl->isOpen || !pImpl->cacheValid) return VirtualFSEntry();
    
    std::string normalizedPath = NormalizeInternalPath(virtualPath);
    
    auto it = pImpl->entryCache.find(normalizedPath);
    return (it != pImpl->entryCache.end()) ? it->second : VirtualFSEntry();
}

// ============================================================================
// FILE READING
// ============================================================================

VirtualFSResult VirtualFSLibArchiveProvider::ReadFile(
    const std::string& virtualPath,
    std::vector<uint8_t>& outData) {
    
    if (!pImpl->isOpen) return VirtualFSResult::ArchiveNotOpen;
    
    std::string normalizedPath = NormalizeInternalPath(virtualPath);
    
    auto entryIt = pImpl->entryCache.find(normalizedPath);
    if (entryIt == pImpl->entryCache.end()) {
        pImpl->lastError = "Entry not found: " + virtualPath;
        return VirtualFSResult::NotFound;
    }
    
    if (entryIt->second.IsDirectory()) {
        pImpl->lastError = "Cannot read directory as file";
        return VirtualFSResult::InvalidArgument;
    }
    
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    
    if (!pImpl->openOptions.password.empty()) {
        archive_read_add_passphrase(a, pImpl->openOptions.password.c_str());
    }
    
    if (archive_read_open_filename(a, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(a);
        archive_read_free(a);
        return VirtualFSResult::ReadError;
    }
    
    struct archive_entry* entry;
    VirtualFSResult result = VirtualFSResult::NotFound;
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname) pathname = archive_entry_pathname(entry);
        
        std::string entryPath = NormalizeInternalPath(pathname ? pathname : "");
        
        if (entryPath == normalizedPath) {
            int64_t entrySize = archive_entry_size(entry);
            outData.resize(static_cast<size_t>(entrySize));
            
            if (entrySize > 0) {
                la_ssize_t bytesRead = archive_read_data(a, outData.data(), outData.size());
                
                if (bytesRead < 0) {
                    pImpl->lastError = archive_error_string(a);
                    result = VirtualFSResult::ReadError;
                } else {
                    outData.resize(static_cast<size_t>(bytesRead));
                    result = VirtualFSResult::Success;
                }
            } else {
                outData.clear();
                result = VirtualFSResult::Success;
            }
            break;
        }
        
        archive_read_data_skip(a);
    }
    
    archive_read_free(a);
    return result;
}

VirtualFSResult VirtualFSLibArchiveProvider::ReadFilePartial(
    const std::string& virtualPath,
    uint64_t offset,
    uint64_t length,
    std::vector<uint8_t>& outData) {
    
    std::vector<uint8_t> fullData;
    auto result = ReadFile(virtualPath, fullData);
    
    if (result != VirtualFSResult::Success) return result;
    
    if (offset >= fullData.size()) {
        outData.clear();
        return VirtualFSResult::Success;
    }
    
    uint64_t actualLength = std::min(length, fullData.size() - offset);
    outData.assign(
        fullData.begin() + static_cast<ptrdiff_t>(offset),
        fullData.begin() + static_cast<ptrdiff_t>(offset + actualLength)
    );
    
    return VirtualFSResult::Success;
}

// ============================================================================
// EXTRACTION
// ============================================================================

VirtualFSResult VirtualFSLibArchiveProvider::ExtractFile(
    const std::string& virtualPath,
    const std::string& destPath,
    const VirtualFSExtractOptions& options) {
    
    std::vector<uint8_t> data;
    auto result = ReadFile(virtualPath, data);
    
    if (result != VirtualFSResult::Success) return result;
    
    if (options.createDirectories) {
        std::filesystem::path dest(destPath);
        if (dest.has_parent_path()) {
            std::filesystem::create_directories(dest.parent_path());
        }
    }
    
    if (std::filesystem::exists(destPath) && !options.overwriteExisting) {
        pImpl->lastError = "File already exists: " + destPath;
        return VirtualFSResult::AlreadyExists;
    }
    
    std::ofstream file(destPath, std::ios::binary);
    if (!file) {
        pImpl->lastError = "Failed to create file: " + destPath;
        return VirtualFSResult::WriteError;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), 
               static_cast<std::streamsize>(data.size()));
    
    if (!file) {
        pImpl->lastError = "Failed to write file: " + destPath;
        return VirtualFSResult::WriteError;
    }
    
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFSLibArchiveProvider::ExtractAll(
    const std::string& destDirectory,
    const VirtualFSExtractOptions& options,
    VirtualFSProgressCallback progressCallback) {
    
    if (!pImpl->isOpen) return VirtualFSResult::ArchiveNotOpen;
    
    std::filesystem::create_directories(destDirectory);
    
    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    
    if (!pImpl->openOptions.password.empty()) {
        archive_read_add_passphrase(a, pImpl->openOptions.password.c_str());
    }
    
    int flags = ARCHIVE_EXTRACT_TIME;
    if (options.preservePermissions) flags |= ARCHIVE_EXTRACT_PERM;
    if (options.overwriteExisting) flags |= ARCHIVE_EXTRACT_UNLINK;
    
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);
    
    if (archive_read_open_filename(a, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(a);
        archive_read_free(a);
        archive_write_free(ext);
        return VirtualFSResult::ReadError;
    }
    
    VirtualFSProgress progress;
    progress.totalFiles = pImpl->entryCache.size();
    progress.grandTotalBytes = pImpl->archiveInfo.uncompressedSize;
    
    struct archive_entry* entry;
    VirtualFSResult result = VirtualFSResult::Success;
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* currentPath = archive_entry_pathname(entry);
        std::string newPath = destDirectory + "/" + currentPath;
        archive_entry_set_pathname(entry, newPath.c_str());
        
        if (progressCallback) {
            progress.currentFile = currentPath;
            progress.filesProcessed++;
            progress.UpdatePercent();
            
            if (!progressCallback(progress)) {
                result = VirtualFSResult::Cancelled;
                break;
            }
        }
        
        int r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            pImpl->lastError = archive_error_string(ext);
            result = VirtualFSResult::WriteError;
            break;
        }
        
        if (archive_entry_size(entry) > 0) {
            const void* buff;
            size_t size;
            la_int64_t offset;
            
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                    pImpl->lastError = archive_error_string(ext);
                    result = VirtualFSResult::WriteError;
                    break;
                }
                progress.totalBytes += size;
            }
        }
        
        archive_write_finish_entry(ext);
        if (result != VirtualFSResult::Success) break;
    }
    
    archive_read_free(a);
    archive_write_free(ext);
    
    return result;
}

// ============================================================================
// WRITE OPERATIONS
// ============================================================================

VirtualFSResult VirtualFSLibArchiveProvider::CreateArchive(
    const std::string& archivePath,
    const VirtualFSOpenOptions& options) {
    
    Close();
    
    pImpl->archivePath = archivePath;
    pImpl->openOptions = options;
    pImpl->isWriteMode = true;
    
    pImpl->writeArchive = archive_write_new();
    if (!pImpl->writeArchive) {
        pImpl->lastError = "Failed to create archive writer";
        return VirtualFSResult::OutOfMemory;
    }
    
    ConfigureWriteFormat(pImpl->writeArchive, archivePath);

    std::string ext = archivePath;
    size_t dotPos = ext.rfind('.');
    if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (!options.password.empty() && ext == "zip") {
        archive_write_set_passphrase(pImpl->writeArchive, options.password.c_str());
        archive_write_set_options(pImpl->writeArchive, "zip:encryption=aes256");
    }
    
    if (archive_write_open_filename(pImpl->writeArchive, archivePath.c_str()) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(pImpl->writeArchive);
        archive_write_free(pImpl->writeArchive);
        pImpl->writeArchive = nullptr;
        return VirtualFSResult::WriteError;
    }
    
    pImpl->isOpen = true;
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFSLibArchiveProvider::AddFile(
    const std::string& sourcePath,
    const std::string& virtualPath,
    const VirtualFSOpenOptions& options) {
    
    if (!pImpl->writeArchive) return VirtualFSResult::ArchiveNotOpen;
    
    if (!std::filesystem::exists(sourcePath)) {
        pImpl->lastError = "Source file not found: " + sourcePath;
        return VirtualFSResult::NotFound;
    }
    
    std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
    if (!file) {
        pImpl->lastError = "Failed to open source file";
        return VirtualFSResult::ReadError;
    }
    
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        pImpl->lastError = "Failed to read source file";
        return VirtualFSResult::ReadError;
    }
    
    return AddFromMemory(virtualPath, data, options);
}

VirtualFSResult VirtualFSLibArchiveProvider::AddFromMemory(
    const std::string& virtualPath,
    const std::vector<uint8_t>& data,
    const VirtualFSOpenOptions& options) {
    
    if (!pImpl->writeArchive) return VirtualFSResult::ArchiveNotOpen;
    
    struct archive_entry* entry = archive_entry_new();
    
    archive_entry_set_pathname(entry, virtualPath.c_str());
    archive_entry_set_size(entry, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_mtime(entry, time(nullptr), 0);
    
    if (archive_write_header(pImpl->writeArchive, entry) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(pImpl->writeArchive);
        archive_entry_free(entry);
        return VirtualFSResult::WriteError;
    }
    
    if (!data.empty()) {
        if (archive_write_data(pImpl->writeArchive, data.data(), data.size()) < 0) {
            pImpl->lastError = archive_error_string(pImpl->writeArchive);
            archive_entry_free(entry);
            return VirtualFSResult::WriteError;
        }
    }
    
    archive_entry_free(entry);
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFSLibArchiveProvider::AddDirectory(
    const std::string& sourcePath,
    const std::string& virtualPath,
    bool recursive,
    const VirtualFSOpenOptions& options,
    VirtualFSProgressCallback progressCallback) {
    
    if (!pImpl->writeArchive) return VirtualFSResult::ArchiveNotOpen;
    
    if (!std::filesystem::exists(sourcePath)) {
        pImpl->lastError = "Source directory not found";
        return VirtualFSResult::NotFound;
    }
    
    std::filesystem::path srcPath(sourcePath);
    
    auto addEntry = [&](const std::filesystem::directory_entry& dirEntry) -> VirtualFSResult {
        std::string relativePath = std::filesystem::relative(dirEntry.path(), srcPath).string();
        std::string destPath = virtualPath.empty() ? relativePath : virtualPath + "/" + relativePath;
        std::replace(destPath.begin(), destPath.end(), '\\', '/');
        
        if (dirEntry.is_directory()) {
            struct archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, (destPath + "/").c_str());
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
            archive_entry_set_mtime(entry, time(nullptr), 0);
            
            if (archive_write_header(pImpl->writeArchive, entry) != ARCHIVE_OK) {
                pImpl->lastError = archive_error_string(pImpl->writeArchive);
                archive_entry_free(entry);
                return VirtualFSResult::WriteError;
            }
            archive_entry_free(entry);
        } else if (dirEntry.is_regular_file()) {
            return AddFile(dirEntry.path().string(), destPath, options);
        }
        
        return VirtualFSResult::Success;
    };
    
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(srcPath)) {
                auto result = addEntry(entry);
                if (result != VirtualFSResult::Success) return result;
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(srcPath)) {
                auto result = addEntry(entry);
                if (result != VirtualFSResult::Success) return result;
            }
        }
    } catch (const std::exception& e) {
        pImpl->lastError = e.what();
        return VirtualFSResult::ReadError;
    }
    
    return VirtualFSResult::Success;
}

VirtualFSResult VirtualFSLibArchiveProvider::Finalize() {
    if (pImpl->writeArchive) {
        archive_write_close(pImpl->writeArchive);
        archive_write_free(pImpl->writeArchive);
        pImpl->writeArchive = nullptr;
    }

    pImpl->isOpen = false;
    pImpl->isWriteMode = false;

    return VirtualFSResult::Success;
}

// ============================================================================
// DELETE OPERATIONS
// ============================================================================

namespace {

// True when `entryPath` (normalized, no trailing slash) is `target` itself or
// lies anywhere below it, so directory targets delete their whole subtree.
bool PathMatchesTarget(const std::string& entryPath, const std::string& target) {
    if (entryPath.size() < target.size()) return false;
    if (entryPath.compare(0, target.size(), target) != 0) return false;
    return entryPath.size() == target.size() || entryPath[target.size()] == '/';
}

bool PathMatchesAnyTarget(const std::string& entryPath,
                          const std::vector<std::string>& targets) {
    for (const auto& target : targets) {
        if (PathMatchesTarget(entryPath, target)) return true;
    }
    return false;
}

} // namespace

VirtualFSResult VirtualFSLibArchiveProvider::Delete(const std::string& virtualPath) {
    return DeleteEntries({virtualPath}, nullptr);
}

VirtualFSResult VirtualFSLibArchiveProvider::DeleteEntries(
    const std::vector<std::string>& virtualPaths,
    VirtualFSProgressCallback progressCallback) {

    if (!pImpl->isOpen) return VirtualFSResult::ArchiveNotOpen;
    if (pImpl->writeArchive) {
        pImpl->lastError = "Archive is open for writing";
        return VirtualFSResult::InvalidArgument;
    }

    // Delete works by rewriting the archive, so it is only offered for
    // container formats libarchive (or miniz) can also write. Read-only
    // formats (RAR, CAB, ...) and single-stream compressed files (.gz,
    // .bz2, ...) are rejected up front instead of producing a broken file.
    {
        std::string lowerPath = pImpl->archivePath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        auto hasSuffix = [&lowerPath](const std::string& suffix) {
            return lowerPath.size() >= suffix.size() &&
                   lowerPath.compare(lowerPath.size() - suffix.size(),
                                     suffix.size(), suffix) == 0;
        };
        static const char* rewritableExts[] = {
            "zip", "cbz", "jar", "war", "ear", "apk", "ipa", "xpi", "crx",
            "epub", "docx", "xlsx", "pptx", "odt", "ods", "odp",
            "7z", "tar", "tgz", "tbz", "tbz2", "txz", "tzst", "cpio",
            "ar", "a", "iso"
        };
        static const char* rewritableSuffixes[] = {
            ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4"
        };
        std::string ext = lowerPath;
        size_t dotPos = ext.rfind('.');
        if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);

        bool rewritable = false;
        for (const char* e : rewritableExts) {
            if (ext == e) { rewritable = true; break; }
        }
        for (const char* s : rewritableSuffixes) {
            if (!rewritable && hasSuffix(s)) { rewritable = true; break; }
        }
        if (!rewritable) {
            pImpl->lastError = "Format does not support deleting entries: " +
                               pImpl->archivePath;
            return VirtualFSResult::NotSupported;
        }
    }

    std::vector<std::string> targets;
    targets.reserve(virtualPaths.size());
    for (const auto& path : virtualPaths) {
        std::string normalized = NormalizeInternalPath(path);
        if (normalized.empty()) {
            pImpl->lastError = "Cannot delete archive root";
            return VirtualFSResult::InvalidArgument;
        }
        targets.push_back(std::move(normalized));
    }
    if (targets.empty()) return VirtualFSResult::Success;

    // Every requested path must match at least one cached entry. The cache is
    // sorted, so a subtree check is a lower_bound + prefix test.
    for (const auto& target : targets) {
        auto it = pImpl->entryCache.lower_bound(target);
        if (it == pImpl->entryCache.end() || !PathMatchesTarget(it->first, target)) {
            pImpl->lastError = "Entry not found: " + target;
            return VirtualFSResult::NotFound;
        }
    }

    std::string tempPath = pImpl->archivePath + ".vfs-rewrite.tmp";
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);

    VirtualFSResult result;
    bool handledByFastPath = false;
    result = DeleteEntriesZipFast(targets, tempPath, progressCallback, handledByFastPath);
    if (!handledByFastPath) {
        result = DeleteEntriesGeneric(targets, tempPath, progressCallback);
    }

    if (result != VirtualFSResult::Success) {
        std::filesystem::remove(tempPath, ec);
        return result;
    }

    std::filesystem::rename(tempPath, pImpl->archivePath, ec);
    if (ec) {
        pImpl->lastError = "Failed to replace archive: " + ec.message();
        std::filesystem::remove(tempPath, ec);
        return VirtualFSResult::WriteError;
    }

    // Reopen so the entry cache and archive info reflect the new file.
    std::string archivePath = pImpl->archivePath;
    VirtualFSOpenOptions options = pImpl->openOptions;
    return Open(archivePath, options);
}

VirtualFSResult VirtualFSLibArchiveProvider::DeleteEntriesZipFast(
    const std::vector<std::string>& targets,
    const std::string& tempPath,
    VirtualFSProgressCallback progressCallback,
    bool& handled) {

    handled = false;
#ifdef VIRTUALFS_HAS_MINIZ
    // Raw-copy fast path: surviving entries move to the new archive as their
    // original compressed streams — no decompression, no recompression. Only
    // plain (non-password) ZIP-family archives qualify; anything else falls
    // back to the generic libarchive rewrite.
    static const char* zipExts[] = {"zip", "cbz", "jar", "war", "ear", "apk",
                                    "ipa", "xpi", "crx", "epub", "docx",
                                    "xlsx", "pptx", "odt", "ods", "odp"};
    std::string lowerPath = pImpl->archivePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    std::string ext = lowerPath;
    size_t dotPos = ext.rfind('.');
    if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);

    bool isZip = false;
    for (const char* z : zipExts) {
        if (ext == z) { isZip = true; break; }
    }
    if (!isZip || !pImpl->openOptions.password.empty()) {
        return VirtualFSResult::NotSupported;
    }

    mz_zip_archive src;
    mz_zip_zero_struct(&src);
    if (!mz_zip_reader_init_file(&src, pImpl->archivePath.c_str(), 0)) {
        return VirtualFSResult::NotSupported; // generic path retries
    }

    mz_zip_archive dst;
    mz_zip_zero_struct(&dst);
    if (!mz_zip_writer_init_file_v2(&dst, tempPath.c_str(), 0, 0)) {
        mz_zip_reader_end(&src);
        return VirtualFSResult::NotSupported;
    }

    handled = true;
    VirtualFSResult result = VirtualFSResult::Success;

    VirtualFSProgress progress;
    mz_uint fileCount = mz_zip_reader_get_num_files(&src);
    progress.totalFiles = fileCount;

    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&src, i, &stat)) {
            pImpl->lastError = mz_zip_get_error_string(mz_zip_get_last_error(&src));
            result = VirtualFSResult::ReadError;
            break;
        }

        std::string entryPath = NormalizeInternalPath(stat.m_filename);

        if (progressCallback) {
            progress.currentFile = entryPath;
            progress.filesProcessed++;
            progress.UpdatePercent();
            if (!progressCallback(progress)) {
                result = VirtualFSResult::Cancelled;
                break;
            }
        }

        if (PathMatchesAnyTarget(entryPath, targets)) {
            continue;
        }

        if (!mz_zip_writer_add_from_zip_reader(&dst, &src, i)) {
            pImpl->lastError = mz_zip_get_error_string(mz_zip_get_last_error(&dst));
            result = VirtualFSResult::WriteError;
            break;
        }
    }

    if (result == VirtualFSResult::Success &&
        !mz_zip_writer_finalize_archive(&dst)) {
        pImpl->lastError = mz_zip_get_error_string(mz_zip_get_last_error(&dst));
        result = VirtualFSResult::WriteError;
    }

    mz_zip_writer_end(&dst);
    mz_zip_reader_end(&src);
    return result;
#else
    (void)targets; (void)tempPath; (void)progressCallback;
    return VirtualFSResult::NotSupported;
#endif
}

VirtualFSResult VirtualFSLibArchiveProvider::DeleteEntriesGeneric(
    const std::vector<std::string>& targets,
    const std::string& tempPath,
    VirtualFSProgressCallback progressCallback) {

    struct archive* in = archive_read_new();
    archive_read_support_filter_all(in);
    archive_read_support_format_all(in);

    if (!pImpl->openOptions.password.empty()) {
        archive_read_add_passphrase(in, pImpl->openOptions.password.c_str());
    }

    if (archive_read_open_filename(in, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(in);
        archive_read_free(in);
        return VirtualFSResult::ReadError;
    }

    struct archive* out = archive_write_new();
    ConfigureWriteFormat(out, pImpl->archivePath);

    if (!pImpl->openOptions.password.empty()) {
        std::string ext = pImpl->archivePath;
        size_t dotPos = ext.rfind('.');
        if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == "zip") {
            archive_write_set_passphrase(out, pImpl->openOptions.password.c_str());
            archive_write_set_options(out, "zip:encryption=aes256");
        }
    }

    if (archive_write_open_filename(out, tempPath.c_str()) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(out);
        archive_read_free(in);
        archive_write_free(out);
        return VirtualFSResult::WriteError;
    }

    VirtualFSProgress progress;
    progress.totalFiles = pImpl->entryCache.size();
    progress.grandTotalBytes = pImpl->archiveInfo.uncompressedSize;

    struct archive_entry* entry;
    VirtualFSResult result = VirtualFSResult::Success;

    while (archive_read_next_header(in, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname) pathname = archive_entry_pathname(entry);
        std::string entryPath = NormalizeInternalPath(pathname ? pathname : "");

        if (progressCallback) {
            progress.currentFile = entryPath;
            progress.filesProcessed++;
            progress.UpdatePercent();
            if (!progressCallback(progress)) {
                result = VirtualFSResult::Cancelled;
                break;
            }
        }

        if (PathMatchesAnyTarget(entryPath, targets)) {
            archive_read_data_skip(in);
            continue;
        }

        if (archive_write_header(out, entry) != ARCHIVE_OK) {
            pImpl->lastError = archive_error_string(out);
            result = VirtualFSResult::WriteError;
            break;
        }

        const void* buff;
        size_t size;
        la_int64_t offset;
        la_int64_t written = 0;
        int r;

        while ((r = archive_read_data_block(in, &buff, &size, &offset)) == ARCHIVE_OK) {
            // Sparse entries report holes as offset jumps; fill them with
            // zeros since a plain write archive has no seek support.
            if (offset > written) {
                static const char zeros[8192] = {0};
                la_int64_t gap = offset - written;
                while (gap > 0) {
                    size_t chunk = static_cast<size_t>(
                        std::min<la_int64_t>(gap, sizeof(zeros)));
                    if (archive_write_data(out, zeros, chunk) < 0) {
                        pImpl->lastError = archive_error_string(out);
                        result = VirtualFSResult::WriteError;
                        break;
                    }
                    gap -= static_cast<la_int64_t>(chunk);
                }
                if (result != VirtualFSResult::Success) break;
                written = offset;
            }
            if (size > 0 && archive_write_data(out, buff, size) < 0) {
                pImpl->lastError = archive_error_string(out);
                result = VirtualFSResult::WriteError;
                break;
            }
            written += static_cast<la_int64_t>(size);
            progress.totalBytes += size;
        }

        if (result == VirtualFSResult::Success && r != ARCHIVE_EOF && r != ARCHIVE_OK) {
            pImpl->lastError = archive_error_string(in);
            result = VirtualFSResult::ReadError;
        }
        if (result != VirtualFSResult::Success) break;
    }

    if (archive_write_close(out) != ARCHIVE_OK && result == VirtualFSResult::Success) {
        pImpl->lastError = archive_error_string(out);
        result = VirtualFSResult::WriteError;
    }
    archive_write_free(out);
    archive_read_free(in);

    return result;
}

// ============================================================================
// UTILITY
// ============================================================================

bool VirtualFSLibArchiveProvider::Validate() {
    if (!pImpl->isOpen) return false;
    
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    
    if (archive_read_open_filename(a, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }
    
    struct archive_entry* entry;
    bool valid = true;
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_read_data_skip(a) != ARCHIVE_OK) {
            valid = false;
            break;
        }
    }
    
    archive_read_free(a);
    return valid;
}

VirtualFSResult VirtualFSLibArchiveProvider::Test(VirtualFSProgressCallback progressCallback) {
    if (!pImpl->isOpen) return VirtualFSResult::ArchiveNotOpen;
    
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    
    if (!pImpl->openOptions.password.empty()) {
        archive_read_add_passphrase(a, pImpl->openOptions.password.c_str());
    }
    
    if (archive_read_open_filename(a, pImpl->archivePath.c_str(), 10240) != ARCHIVE_OK) {
        pImpl->lastError = archive_error_string(a);
        archive_read_free(a);
        return VirtualFSResult::ReadError;
    }
    
    VirtualFSProgress progress;
    progress.totalFiles = pImpl->entryCache.size();
    
    struct archive_entry* entry;
    VirtualFSResult result = VirtualFSResult::Success;
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (progressCallback) {
            progress.currentFile = archive_entry_pathname(entry);
            progress.filesProcessed++;
            progress.UpdatePercent();
            
            if (!progressCallback(progress)) {
                result = VirtualFSResult::Cancelled;
                break;
            }
        }
        
        const void* buff;
        size_t size;
        la_int64_t offset;
        
        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            progress.totalBytes += size;
        }
    }
    
    archive_read_free(a);
    return result;
}

std::string VirtualFSLibArchiveProvider::GetLastError() const {
    return pImpl->lastError;
}

void VirtualFSLibArchiveProvider::ConfigureWriteFormat(
    void* writeArchivePtr, const std::string& archivePath) {

    struct archive* writeArchive = static_cast<struct archive*>(writeArchivePtr);

    // Determine the archive format and compression filter from the file name.
    // Compound extensions such as ".tar.gz" / ".tar.bz2" must be recognised in
    // full: inspecting only the final token ("gz") would select the ZIP format
    // and then bolt a gzip filter onto it, producing a corrupt archive. Match
    // the longest known suffix first, then fall back to the single-token map.
    std::string lowerPath = archivePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    auto hasSuffix = [&lowerPath](const std::string& suffix) {
        return lowerPath.size() >= suffix.size() &&
               lowerPath.compare(lowerPath.size() - suffix.size(),
                                 suffix.size(), suffix) == 0;
    };

    std::string ext = lowerPath;
    size_t dotPos = ext.rfind('.');
    if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);

    enum class WriteFilter { None, Gzip, BZip2, XZ, Zstd, LZ4 };
    int format;
    WriteFilter filter;

    if (hasSuffix(".tar.gz") || hasSuffix(".tgz")) {
        format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED; filter = WriteFilter::Gzip;
    } else if (hasSuffix(".tar.bz2") || hasSuffix(".tbz2") || hasSuffix(".tbz")) {
        format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED; filter = WriteFilter::BZip2;
    } else if (hasSuffix(".tar.xz") || hasSuffix(".txz")) {
        format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED; filter = WriteFilter::XZ;
    } else if (hasSuffix(".tar.zst") || hasSuffix(".tzst")) {
        format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED; filter = WriteFilter::Zstd;
    } else if (hasSuffix(".tar.lz4")) {
        format = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED; filter = WriteFilter::LZ4;
    } else {
        format = GetLibArchiveFormat(ext);
        // Standalone compressed streams keep their zip-family/default format but
        // still receive the matching filter.
        if      (ext == "gz")                   filter = WriteFilter::Gzip;
        else if (ext == "bz2")                  filter = WriteFilter::BZip2;
        else if (ext == "xz")                   filter = WriteFilter::XZ;
        else if (ext == "zst" || ext == "zstd") filter = WriteFilter::Zstd;
        else if (ext == "lz4")                  filter = WriteFilter::LZ4;
        else                                    filter = WriteFilter::None;
    }

    archive_write_set_format(writeArchive, format);
    switch (filter) {
        case WriteFilter::Gzip:  archive_write_add_filter_gzip(writeArchive);  break;
        case WriteFilter::BZip2: archive_write_add_filter_bzip2(writeArchive); break;
        case WriteFilter::XZ:    archive_write_add_filter_xz(writeArchive);    break;
        case WriteFilter::Zstd:  archive_write_add_filter_zstd(writeArchive);  break;
        case WriteFilter::LZ4:   archive_write_add_filter_lz4(writeArchive);   break;
        case WriteFilter::None:  break;
    }
}

int VirtualFSLibArchiveProvider::GetLibArchiveFormat(const std::string& ext) {
    if (ext == "zip" || ext == "cbz" || ext == "jar" || ext == "apk" ||
        ext == "docx" || ext == "xlsx" || ext == "pptx" || ext == "epub") {
        return ARCHIVE_FORMAT_ZIP;
    } else if (ext == "7z") {
        return ARCHIVE_FORMAT_7ZIP;
    } else if (ext == "tar" || ext == "tgz" || ext == "tbz" || ext == "txz") {
        return ARCHIVE_FORMAT_TAR_PAX_RESTRICTED;
    } else if (ext == "cpio") {
        return ARCHIVE_FORMAT_CPIO;
    } else if (ext == "ar" || ext == "a" || ext == "deb") {
        return ARCHIVE_FORMAT_AR_BSD;
    } else if (ext == "iso") {
        return ARCHIVE_FORMAT_ISO9660;
    }
    
    return ARCHIVE_FORMAT_ZIP;
}

int VirtualFSLibArchiveProvider::GetLibArchiveFilter(VirtualFSCompressionMethod method) {
    switch (method) {
        case VirtualFSCompressionMethod::Store: return ARCHIVE_FILTER_NONE;
        case VirtualFSCompressionMethod::Deflate: return ARCHIVE_FILTER_GZIP;
        case VirtualFSCompressionMethod::BZip2: return ARCHIVE_FILTER_BZIP2;
        case VirtualFSCompressionMethod::LZMA:
        case VirtualFSCompressionMethod::LZMA2: return ARCHIVE_FILTER_LZMA;
        case VirtualFSCompressionMethod::XZ: return ARCHIVE_FILTER_XZ;
        case VirtualFSCompressionMethod::Zstd: return ARCHIVE_FILTER_ZSTD;
        case VirtualFSCompressionMethod::LZ4: return ARCHIVE_FILTER_LZ4;
        default: return ARCHIVE_FILTER_NONE;
    }
}

int VirtualFSLibArchiveProvider::GetLibArchiveCompressionLevel(VirtualFSCompressionLevel level) {
    switch (level) {
        case VirtualFSCompressionLevel::Store: return 0;
        case VirtualFSCompressionLevel::Fastest: return 1;
        case VirtualFSCompressionLevel::Fast: return 3;
        case VirtualFSCompressionLevel::Normal: return 5;
        case VirtualFSCompressionLevel::Good: return 7;
        case VirtualFSCompressionLevel::Best:
        case VirtualFSCompressionLevel::Ultra: return 9;
        default: return 5;
    }
}

VirtualFSResult RegisterLibArchiveProvider() {
    return VirtualFSManager::Instance().RegisterProvider(CreateLibArchiveProvider());
}

} // namespace VirtualFS

#endif // VIRTUALFS_HAS_LIBARCHIVE
