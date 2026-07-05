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
    
    std::string ext = archivePath;
    size_t dotPos = ext.rfind('.');
    if (dotPos != std::string::npos) ext = ext.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    int format = GetLibArchiveFormat(ext);
    archive_write_set_format(pImpl->writeArchive, format);
    
    if (ext == "gz" || ext == "tgz") {
        archive_write_add_filter_gzip(pImpl->writeArchive);
    } else if (ext == "bz2" || ext == "tbz2") {
        archive_write_add_filter_bzip2(pImpl->writeArchive);
    } else if (ext == "xz" || ext == "txz") {
        archive_write_add_filter_xz(pImpl->writeArchive);
    } else if (ext == "zst" || ext == "zstd") {
        archive_write_add_filter_zstd(pImpl->writeArchive);
    } else if (ext == "lz4") {
        archive_write_add_filter_lz4(pImpl->writeArchive);
    }
    
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
