// VirtualFS/include/VirtualFSManager.h
// Central manager for VirtualFS - singleton pattern
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSTypes.h"
#include "VirtualFSProvider.h"
#include "VirtualFSPath.h"
#include <map>
#include <mutex>
#include <memory>

namespace VirtualFS {

/**
 * @class VirtualFSManager
 * @brief Central manager for the Virtual File System (Singleton)
 * 
 * Manages provider registration, path resolution, and provides
 * a unified API for accessing files across real filesystem and archives.
 * 
 * Usage:
 * @code
 * auto& vfs = VirtualFSManager::Instance();
 * vfs.Initialize();
 * 
 * auto entries = vfs.ListDirectory("/path/to/archive.zip/folder");
 * @endcode
 */
class VirtualFSManager {
public:
    // =========================================================================
    // SINGLETON ACCESS
    // =========================================================================
    
    /**
     * @brief Gets the singleton instance
     * @return Reference to VirtualFSManager
     */
    static VirtualFSManager& Instance() {
        static VirtualFSManager instance;
        return instance;
    }
    
    // Delete copy/move constructors
    VirtualFSManager(const VirtualFSManager&) = delete;
    VirtualFSManager& operator=(const VirtualFSManager&) = delete;
    VirtualFSManager(VirtualFSManager&&) = delete;
    VirtualFSManager& operator=(VirtualFSManager&&) = delete;
    
    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    
    /**
     * @brief Initializes VirtualFS and loads built-in providers
     * @return Success or error code
     */
    VirtualFSResult Initialize();
    
    /**
     * @brief Shuts down VirtualFS and releases resources
     */
    void Shutdown();
    
    /**
     * @brief Checks if VirtualFS is initialized
     * @return true if initialized
     */
    bool IsInitialized() const { return initialized; }
    
    /**
     * @brief Gets VirtualFS version string
     * @return Version string
     */
    std::string GetVersion() const { return "1.0.0"; }
    
    // =========================================================================
    // PROVIDER MANAGEMENT
    // =========================================================================
    
    /**
     * @brief Registers a new format provider
     * @param provider Provider implementation
     * @return Success or error code
     */
    VirtualFSResult RegisterProvider(std::shared_ptr<IVirtualFSProvider> provider);
    
    /**
     * @brief Unregisters a provider by name
     * @param providerName Provider name
     */
    void UnregisterProvider(const std::string& providerName);
    
    /**
     * @brief Gets list of all registered providers
     * @return Vector of provider info
     */
    std::vector<VirtualFSProviderInfo> GetRegisteredProviders() const;
    
    /**
     * @brief Gets info for a specific provider
     * @param providerName Provider name
     * @return Provider info
     */
    VirtualFSProviderInfo GetProviderInfo(const std::string& providerName) const;
    
    /**
     * @brief Gets provider for a path (by extension)
     * @param path File path
     * @return Provider or nullptr
     */
    std::shared_ptr<IVirtualFSProvider> GetProviderForPath(const std::string& path) const;
    
    /**
     * @brief Gets provider for an extension
     * @param extension File extension (without dot)
     * @return Provider or nullptr
     */
    std::shared_ptr<IVirtualFSProvider> GetProviderForExtension(const std::string& extension) const;
    
    /**
     * @brief Gets provider by magic bytes
     * @param data File header bytes
     * @param size Size of data
     * @return Provider or nullptr
     */
    std::shared_ptr<IVirtualFSProvider> GetProviderByMagic(const uint8_t* data, size_t size) const;
    
    /**
     * @brief Gets all supported extensions
     * @return Vector of extensions
     */
    std::vector<std::string> GetSupportedExtensions() const;
    
    // =========================================================================
    // PATH OPERATIONS
    // =========================================================================
    
    /**
     * @brief Resolves a virtual path
     * @param virtualPath Path to resolve
     * @return Resolved path structure
     */
    VirtualFSResolvedPath ResolvePath(const std::string& virtualPath) const;
    
    /**
     * @brief Checks if path points to or inside an archive
     * @param path Path to check
     * @return true if archive path
     */
    bool IsArchivePath(const std::string& path) const;
    
    /**
     * @brief Checks if path is inside an archive
     * @param path Path to check
     * @return true if inside archive
     */
    bool IsInsideArchive(const std::string& path) const;
    
    /**
     * @brief Checks if path is a supported archive format
     * @param path Path to check
     * @return true if supported archive
     */
    bool IsArchive(const std::string& path) const;
    
    /**
     * @brief Normalizes a path
     * @param path Path to normalize
     * @return Normalized path
     */
    std::string NormalizePath(const std::string& path) const;
    
    /**
     * @brief Joins two path components
     * @param base Base path
     * @param relative Relative path
     * @return Combined path
     */
    std::string JoinPath(const std::string& base, const std::string& relative) const;
    
    /**
     * @brief Gets parent path
     * @param path Input path
     * @return Parent path
     */
    std::string GetParentPath(const std::string& path) const;
    
    /**
     * @brief Gets filename from path
     * @param path Input path
     * @return Filename
     */
    std::string GetFileName(const std::string& path) const;
    
    /**
     * @brief Gets file extension
     * @param path Input path
     * @return Extension (without dot)
     */
    std::string GetExtension(const std::string& path) const;
    
    // =========================================================================
    // DIRECTORY OPERATIONS
    // =========================================================================
    
    /**
     * @brief Lists directory contents (works with archives)
     * @param path Directory or archive path
     * @return Vector of entries
     */
    std::vector<VirtualFSEntry> ListDirectory(const std::string& path);
    
    /**
     * @brief Lists directory with filter
     * @param path Directory path
     * @param filter Filter callback
     * @return Filtered entries
     */
    std::vector<VirtualFSEntry> ListDirectoryFiltered(
        const std::string& path,
        VirtualFSFilterCallback filter);
    
    /**
     * @brief Lists directory recursively
     * @param path Starting path
     * @param maxDepth Maximum depth (-1 = unlimited)
     * @return All entries
     */
    std::vector<VirtualFSEntry> ListDirectoryRecursive(
        const std::string& path,
        int maxDepth = -1);
    
    /**
     * @brief Enumerates directory via callback
     * @param path Directory path
     * @param callback Entry callback
     * @param recursive Include subdirectories
     */
    void EnumerateDirectory(
        const std::string& path,
        VirtualFSEntryCallback callback,
        bool recursive = false);
    
    // =========================================================================
    // ENTRY INFORMATION
    // =========================================================================
    
    /**
     * @brief Checks if file or directory exists
     * @param path Path to check
     * @return true if exists
     */
    bool Exists(const std::string& path);
    
    /**
     * @brief Gets entry information
     * @param path Path to entry
     * @return Entry info
     */
    VirtualFSEntry GetInfo(const std::string& path);
    
    /**
     * @brief Gets entry type
     * @param path Path to entry
     * @return Entry type
     */
    VirtualFSEntryType GetType(const std::string& path);
    
    /**
     * @brief Gets file size
     * @param path Path to file
     * @return Size in bytes
     */
    uint64_t GetSize(const std::string& path);
    
    /**
     * @brief Checks if path is a directory
     * @param path Path to check
     * @return true if directory
     */
    bool IsDirectory(const std::string& path);
    
    /**
     * @brief Checks if path is a file
     * @param path Path to check
     * @return true if file
     */
    bool IsFile(const std::string& path);
    
    // =========================================================================
    // FILE READING
    // =========================================================================
    
    /**
     * @brief Reads entire file into memory
     * @param path File path
     * @param outData Output buffer
     * @return Success or error
     */
    VirtualFSResult ReadFile(const std::string& path, std::vector<uint8_t>& outData);
    
    /**
     * @brief Reads portion of file
     * @param path File path
     * @param offset Start offset
     * @param length Bytes to read
     * @param outData Output buffer
     * @return Success or error
     */
    VirtualFSResult ReadFilePartial(
        const std::string& path,
        uint64_t offset,
        uint64_t length,
        std::vector<uint8_t>& outData);
    
    /**
     * @brief Reads file as text
     * @param path File path
     * @param outText Output string
     * @param encoding Text encoding
     * @return Success or error
     */
    VirtualFSResult ReadFileString(
        const std::string& path,
        std::string& outText,
        const std::string& encoding = "UTF-8");
    
    /**
     * @brief Opens file stream
     * @param path File path
     * @param mode Open mode
     * @return Stream or nullptr
     */
    std::unique_ptr<VirtualFSStream> OpenStream(
        const std::string& path,
        VirtualFSOpenMode mode = VirtualFSOpenMode::Read);
    
    // =========================================================================
    // EXTRACTION
    // =========================================================================
    
    /**
     * @brief Extracts file from archive
     * @param archivePath Archive path
     * @param entryPath Entry path in archive
     * @param destPath Destination path
     * @param options Extract options
     * @return Success or error
     */
    VirtualFSResult ExtractFile(
        const std::string& archivePath,
        const std::string& entryPath,
        const std::string& destPath,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default());
    
    /**
     * @brief Extracts entire archive
     * @param archivePath Archive path
     * @param destDirectory Destination directory
     * @param options Extract options
     * @param progressCallback Progress callback
     * @return Success or error
     */
    VirtualFSResult ExtractAll(
        const std::string& archivePath,
        const std::string& destDirectory,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Extracts filtered entries
     * @param archivePath Archive path
     * @param destDirectory Destination directory
     * @param filter Filter callback
     * @param options Extract options
     * @param progressCallback Progress callback
     * @return Success or error
     */
    VirtualFSResult ExtractFiltered(
        const std::string& archivePath,
        const std::string& destDirectory,
        VirtualFSFilterCallback filter,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Extracts to memory (alias for ReadFile for archive entries)
     * @param path Virtual path
     * @param outData Output buffer
     * @return Success or error
     */
    VirtualFSResult ExtractToMemory(const std::string& path, std::vector<uint8_t>& outData);
    
    // =========================================================================
    // ARCHIVE CREATION
    // =========================================================================
    
    /**
     * @brief Creates new archive from files
     * @param archivePath Path for new archive
     * @param sourcePaths Files to include
     * @param options Creation options
     * @param progressCallback Progress callback
     * @return Success or error
     */
    VirtualFSResult CreateArchive(
        const std::string& archivePath,
        const std::vector<std::string>& sourcePaths,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Adds files to existing archive
     * @param archivePath Archive path
     * @param sourcePaths Files to add
     * @param destPathInArchive Destination in archive
     * @param options Options
     * @return Success or error
     */
    VirtualFSResult AddToArchive(
        const std::string& archivePath,
        const std::vector<std::string>& sourcePaths,
        const std::string& destPathInArchive = "",
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default());
    
    /**
     * @brief Deletes entries from archive
     * @param archivePath Archive path
     * @param entryPaths Entries to delete
     * @return Success or error
     */
    VirtualFSResult DeleteFromArchive(
        const std::string& archivePath,
        const std::vector<std::string>& entryPaths);
    
    // =========================================================================
    // UTILITY
    // =========================================================================
    
    /**
     * @brief Gets error message for result code
     * @param result Result code
     * @return Error message
     */
    std::string GetErrorMessage(VirtualFSResult result) const;
    
    /**
     * @brief Validates archive integrity
     * @param archivePath Archive path
     * @return true if valid
     */
    bool ValidateArchive(const std::string& archivePath);
    
    /**
     * @brief Tests archive by reading all entries
     * @param archivePath Archive path
     * @param progressCallback Progress callback
     * @return Success or error
     */
    VirtualFSResult TestArchive(
        const std::string& archivePath,
        VirtualFSProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Gets archive information
     * @param archivePath Archive path
     * @return Archive info
     */
    VirtualFSArchiveInfo GetArchiveInfo(const std::string& archivePath);
    
    /**
     * @brief Detects archive format by magic bytes
     * @param path File path
     * @return Format name or empty
     */
    std::string DetectFormat(const std::string& path);
    
    /**
     * @brief Gets MIME type for archive
     * @param path File path
     * @return MIME type
     */
    std::string GetMimeType(const std::string& path);
    
    // =========================================================================
    // CACHE MANAGEMENT
    // =========================================================================
    
    /**
     * @brief Clears all cached data
     */
    void ClearCache();
    
    /**
     * @brief Clears cache for specific archive
     * @param archivePath Archive path
     */
    void ClearCacheForPath(const std::string& archivePath);
    
    /**
     * @brief Gets current cache size
     * @return Size in bytes
     */
    size_t GetCacheSize() const;
    
    /**
     * @brief Sets maximum cache size
     * @param maxBytes Max size in bytes
     */
    void SetMaxCacheSize(size_t maxBytes);
    
    /**
     * @brief Enables or disables caching
     * @param enabled Enable flag
     */
    void SetCacheEnabled(bool enabled);
    
    // =========================================================================
    // CONFIGURATION
    // =========================================================================
    
    /**
     * @brief Sets global password callback
     * @param callback Password callback
     */
    void SetPasswordCallback(VirtualFSPasswordCallback callback);
    
    /**
     * @brief Sets global error callback
     * @param callback Error callback
     */
    void SetErrorCallback(VirtualFSErrorCallback callback);
    
    /**
     * @brief Sets temporary directory
     * @param path Temp directory path
     */
    void SetTempDirectory(const std::string& path);
    
    /**
     * @brief Gets temporary directory
     * @return Temp directory path
     */
    std::string GetTempDirectory() const;
    
    /**
     * @brief Sets default filename encoding
     * @param encoding Encoding name
     */
    void SetDefaultEncoding(const std::string& encoding);
    
    /**
     * @brief Gets default encoding
     * @return Encoding name
     */
    std::string GetDefaultEncoding() const;
    
private:
    VirtualFSManager() = default;
    ~VirtualFSManager() = default;
    
    // Provider storage
    std::vector<std::shared_ptr<IVirtualFSProvider>> providers;
    std::map<std::string, std::shared_ptr<IVirtualFSProvider>> extensionMap;
    std::map<std::string, std::shared_ptr<IVirtualFSProvider>> providerNameMap;
    
    // Open archive cache
    struct OpenArchive {
        std::shared_ptr<IVirtualFSProvider> provider;
        std::string path;
        std::string tempFilePath;   // backing temp file (nested archives only)
        std::chrono::steady_clock::time_point lastAccess;
    };
    std::map<std::string, OpenArchive> openArchives;
    
    // Configuration
    bool initialized = false;
    bool cacheEnabled = true;
    size_t maxCacheSize = 64 * 1024 * 1024; // 64MB
    size_t currentCacheSize = 0;
    std::string tempDirectory;
    std::string defaultEncoding = "UTF-8";
    
    // Callbacks
    VirtualFSPasswordCallback passwordCallback;
    VirtualFSErrorCallback errorCallback;
    
    // Thread safety
    mutable std::mutex providerMutex;
    mutable std::mutex cacheMutex;
    
    // Helper methods
    std::shared_ptr<IVirtualFSProvider> GetOrOpenArchive(const std::string& archivePath);
    std::shared_ptr<IVirtualFSProvider> ResolveToProvider(
        const VirtualFSResolvedPath& resolved, std::string& innerPath);
    std::shared_ptr<IVirtualFSProvider> OpenNestedArchive(
        const std::string& cacheKey,
        const std::shared_ptr<IVirtualFSProvider>& outerProvider,
        const std::string& pathInOuter);
    void CloseCachedArchive(OpenArchive& cached);
    void CloseArchive(const std::string& archivePath);
    void CleanupCache();
    void RegisterBuiltInProviders();
    VirtualFSResult ReadFromRealFS(const std::string& path, std::vector<uint8_t>& outData);
    std::vector<VirtualFSEntry> ListRealDirectory(const std::string& path);
    VirtualFSEntry GetRealFSInfo(const std::string& path);
    void NotifyError(VirtualFSResult result, const std::string& message, const std::string& path);
};

} // namespace VirtualFS
