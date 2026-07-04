// VirtualFS/providers/VirtualFSLibArchiveProvider.h
// libarchive-based provider for multi-format archive support
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSProvider.h"
#include <map>
#include <memory>

#ifdef VIRTUALFS_HAS_LIBARCHIVE

namespace VirtualFS {

/**
 * @class VirtualFSLibArchiveProvider
 * @brief Multi-format archive provider using libarchive
 * 
 * Supports reading and writing many archive formats including:
 * - ZIP, 7-Zip, RAR (read-only), TAR, GZIP, BZIP2, XZ, LZMA
 * - ISO 9660, UDF, CAB, CPIO, AR, and more
 * 
 * This is the primary provider for VirtualFS and should be registered
 * by default during initialization.
 */
class VirtualFSLibArchiveProvider : public IVirtualFSProvider {
public:
    VirtualFSLibArchiveProvider();
    ~VirtualFSLibArchiveProvider() override;
    
    // =========================================================================
    // IDENTIFICATION
    // =========================================================================
    
    std::string GetName() const override { return "LibArchive"; }
    std::string GetVersion() const override { return "1.0.0"; }
    std::string GetDescription() const override {
        return "Multi-format archive provider using libarchive";
    }
    std::string GetLibraryInfo() const override;
    
    std::vector<std::string> GetSupportedExtensions() const override {
        return {
            // ZIP family
            "zip", "cbz", "jar", "war", "ear", "apk", "ipa", "xpi", "crx", "epub",
            "docx", "xlsx", "pptx", "odt", "ods", "odp",
            // 7-Zip
            "7z",
            // RAR (read-only)
            "rar", "cbr",
            // TAR family
            "tar", "tgz", "tbz", "tbz2", "txz", "tlz", "tzst",
            // Compressed
            "gz", "gzip", "bz2", "bzip2", "xz", "lzma", "lz", "zst", "zstd", "lz4",
            // Other
            "cab", "iso", "udf", "cpio", "ar", "a", "deb", "rpm", "pax", "shar"
        };
    }
    
    std::vector<std::string> GetSupportedMimeTypes() const override {
        return {
            "application/zip",
            "application/x-7z-compressed",
            "application/vnd.rar",
            "application/x-rar-compressed",
            "application/x-tar",
            "application/gzip",
            "application/x-bzip2",
            "application/x-xz",
            "application/x-lzma",
            "application/zstd",
            "application/x-lz4",
            "application/vnd.ms-cab-compressed",
            "application/x-iso9660-image",
            "application/x-cpio",
            "application/x-debian-package",
            "application/x-rpm"
        };
    }
    
    VirtualFSCapability GetCapabilities() const override {
        return VirtualFSCapability::Read |
               VirtualFSCapability::Write |
               VirtualFSCapability::Create |
               VirtualFSCapability::ListDirectory |
               VirtualFSCapability::Password |
               VirtualFSCapability::Streaming |
               VirtualFSCapability::LargeFiles |
               VirtualFSCapability::Unicode |
               VirtualFSCapability::Timestamps |
               VirtualFSCapability::Permissions;
    }
    
    int GetPriority() const override { return 100; } // High priority (default provider)

    std::shared_ptr<IVirtualFSProvider> CreateInstance() const override {
        return std::make_shared<VirtualFSLibArchiveProvider>();
    }
    
    // =========================================================================
    // DETECTION
    // =========================================================================
    
    bool CanHandle(const std::string& path) const override;
    bool CanHandleByMagic(const uint8_t* data, size_t size) const override;
    
    // =========================================================================
    // ARCHIVE LIFECYCLE
    // =========================================================================
    
    VirtualFSResult Open(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) override;
    
    void Close() override;
    bool IsOpen() const override;
    std::string GetOpenPath() const override;
    VirtualFSArchiveInfo GetArchiveInfo() const override;
    
    // =========================================================================
    // DIRECTORY OPERATIONS
    // =========================================================================
    
    std::vector<VirtualFSEntry> ListDirectory(const std::string& virtualPath = "") override;
    std::vector<VirtualFSEntry> ListAll() override;
    bool Exists(const std::string& virtualPath) override;
    VirtualFSEntry GetInfo(const std::string& virtualPath) override;
    
    // =========================================================================
    // FILE READING
    // =========================================================================
    
    VirtualFSResult ReadFile(
        const std::string& virtualPath,
        std::vector<uint8_t>& outData) override;
    
    VirtualFSResult ReadFilePartial(
        const std::string& virtualPath,
        uint64_t offset,
        uint64_t length,
        std::vector<uint8_t>& outData) override;
    
    // =========================================================================
    // EXTRACTION
    // =========================================================================
    
    VirtualFSResult ExtractFile(
        const std::string& virtualPath,
        const std::string& destPath,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default()) override;
    
    VirtualFSResult ExtractAll(
        const std::string& destDirectory,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) override;
    
    // =========================================================================
    // WRITE OPERATIONS
    // =========================================================================
    
    VirtualFSResult CreateArchive(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) override;
    
    VirtualFSResult AddFile(
        const std::string& sourcePath,
        const std::string& virtualPath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) override;
    
    VirtualFSResult AddFromMemory(
        const std::string& virtualPath,
        const std::vector<uint8_t>& data,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) override;
    
    VirtualFSResult AddDirectory(
        const std::string& sourcePath,
        const std::string& virtualPath,
        bool recursive = true,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) override;
    
    VirtualFSResult Finalize() override;
    
    // =========================================================================
    // UTILITY
    // =========================================================================
    
    bool Validate() override;
    VirtualFSResult Test(VirtualFSProgressCallback progressCallback = nullptr) override;
    std::string GetLastError() const override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
    
    // Internal helpers
    void BuildEntryCache();
    void ClearEntryCache();
    VirtualFSEntry ConvertArchiveEntry(void* archiveEntry);
    std::string NormalizeInternalPath(const std::string& path);
    int GetLibArchiveFormat(const std::string& extension);
    int GetLibArchiveFilter(VirtualFSCompressionMethod method);
    int GetLibArchiveCompressionLevel(VirtualFSCompressionLevel level);
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

/**
 * @brief Creates a new LibArchive provider instance
 * @return Shared pointer to provider
 */
inline std::shared_ptr<IVirtualFSProvider> CreateLibArchiveProvider() {
    return std::make_shared<VirtualFSLibArchiveProvider>();
}

/**
 * @brief Registers the LibArchive provider with VirtualFSManager
 * @return Success or error
 */
VirtualFSResult RegisterLibArchiveProvider();

} // namespace VirtualFS

#endif // VIRTUALFS_HAS_LIBARCHIVE
