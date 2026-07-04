// VirtualFS/include/VirtualFS.h
// Main public header for VirtualFS module
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

/**
 * @file VirtualFS.h
 * @brief Virtual File System - Transparent archive access as folders
 * 
 * VirtualFS is a standalone, cross-platform module that provides transparent
 * access to files inside archives. Navigate into ZIP, 7z, TAR, and other
 * archive formats as if they were regular folders.
 * 
 * @section features Key Features
 * - Transparent archive access (browse archives as folders)
 * - Plugin architecture (extensible format support)
 * - Nested archive support (archives within archives)
 * - Password-protected archive handling
 * - Streaming support for large files
 * - Cross-platform (Windows, Linux, macOS, Android, iOS)
 * 
 * @section usage Basic Usage
 * @code
 * #include <VirtualFS/VirtualFS.h>
 * using namespace VirtualFS;
 * 
 * int main() {
 *     // Initialize
 *     VirtualFS_Initialize();
 *     
 *     // List contents of a ZIP file
 *     auto entries = VirtualFS_ListDirectory("/path/to/archive.zip");
 *     for (const auto& entry : entries) {
 *         std::cout << entry.name << " (" << entry.size << " bytes)\n";
 *     }
 *     
 *     // Read file from inside archive
 *     std::vector<uint8_t> data;
 *     VirtualFS_ReadFile("/path/to/archive.zip/docs/readme.txt", data);
 *     
 *     // Shutdown
 *     VirtualFS_Shutdown();
 *     return 0;
 * }
 * @endcode
 * 
 * @section formats Supported Formats
 * - ZIP (.zip, .cbz, .jar, .apk)
 * - 7-Zip (.7z)
 * - TAR (.tar, .tar.gz, .tar.bz2, .tar.xz)
 * - RAR (.rar, .cbr) - read only
 * - ISO 9660 (.iso)
 * - Zstandard (.zst, .tar.zst)
 * - And many more via libarchive
 * 
 * @author ULTRA OS Framework
 * @version 1.0.0
 */

// Core includes
#include "VirtualFSTypes.h"
#include "VirtualFSPath.h"
#include "VirtualFSProvider.h"
#include "VirtualFSManager.h"

namespace VirtualFS {

// ============================================================================
// CONVENIENCE FUNCTIONS (C-style API)
// ============================================================================

/**
 * @defgroup GlobalAPI Global API Functions
 * @brief Convenience functions that wrap VirtualFSManager
 * @{
 */

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

/**
 * @brief Initializes VirtualFS system
 * @return Success or error code
 */
inline VirtualFSResult VirtualFS_Initialize() {
    return VirtualFSManager::Instance().Initialize();
}

/**
 * @brief Shuts down VirtualFS system
 */
inline void VirtualFS_Shutdown() {
    VirtualFSManager::Instance().Shutdown();
}

/**
 * @brief Checks if VirtualFS is initialized
 * @return true if initialized
 */
inline bool VirtualFS_IsInitialized() {
    return VirtualFSManager::Instance().IsInitialized();
}

/**
 * @brief Gets VirtualFS version
 * @return Version string
 */
inline std::string VirtualFS_GetVersion() {
    return VirtualFSManager::Instance().GetVersion();
}

// ---------------------------------------------------------------------------
// Provider Management
// ---------------------------------------------------------------------------

/**
 * @brief Registers a format provider
 * @param provider Provider to register
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_RegisterProvider(std::shared_ptr<IVirtualFSProvider> provider) {
    return VirtualFSManager::Instance().RegisterProvider(provider);
}

/**
 * @brief Unregisters a provider
 * @param providerName Provider name
 */
inline void VirtualFS_UnregisterProvider(const std::string& providerName) {
    VirtualFSManager::Instance().UnregisterProvider(providerName);
}

/**
 * @brief Gets all registered providers
 * @return Vector of provider info
 */
inline std::vector<VirtualFSProviderInfo> VirtualFS_GetRegisteredProviders() {
    return VirtualFSManager::Instance().GetRegisteredProviders();
}

/**
 * @brief Gets provider for a path
 * @param path File path
 * @return Provider or nullptr
 */
inline std::shared_ptr<IVirtualFSProvider> VirtualFS_GetProviderForPath(const std::string& path) {
    return VirtualFSManager::Instance().GetProviderForPath(path);
}

/**
 * @brief Gets all supported extensions
 * @return Vector of extensions
 */
inline std::vector<std::string> VirtualFS_GetSupportedExtensions() {
    return VirtualFSManager::Instance().GetSupportedExtensions();
}

// ---------------------------------------------------------------------------
// Path Operations
// ---------------------------------------------------------------------------

/**
 * @brief Resolves a virtual path
 * @param virtualPath Path to resolve
 * @return Resolved path
 */
inline VirtualFSResolvedPath VirtualFS_ResolvePath(const std::string& virtualPath) {
    return VirtualFSManager::Instance().ResolvePath(virtualPath);
}

/**
 * @brief Checks if path is inside an archive
 * @param path Path to check
 * @return true if inside archive
 */
inline bool VirtualFS_IsInsideArchive(const std::string& path) {
    return VirtualFSManager::Instance().IsInsideArchive(path);
}

/**
 * @brief Checks if path is a supported archive
 * @param path Path to check
 * @return true if archive
 */
inline bool VirtualFS_IsArchive(const std::string& path) {
    return VirtualFSManager::Instance().IsArchive(path);
}

/**
 * @brief Normalizes a path
 * @param path Input path
 * @return Normalized path
 */
inline std::string VirtualFS_NormalizePath(const std::string& path) {
    return VirtualFSManager::Instance().NormalizePath(path);
}

/**
 * @brief Joins path components
 * @param base Base path
 * @param relative Relative path
 * @return Combined path
 */
inline std::string VirtualFS_JoinPath(const std::string& base, const std::string& relative) {
    return VirtualFSManager::Instance().JoinPath(base, relative);
}

/**
 * @brief Gets parent path
 * @param path Input path
 * @return Parent path
 */
inline std::string VirtualFS_GetParentPath(const std::string& path) {
    return VirtualFSManager::Instance().GetParentPath(path);
}

/**
 * @brief Gets filename from path
 * @param path Input path
 * @return Filename
 */
inline std::string VirtualFS_GetFileName(const std::string& path) {
    return VirtualFSManager::Instance().GetFileName(path);
}

/**
 * @brief Gets file extension
 * @param path Input path
 * @return Extension
 */
inline std::string VirtualFS_GetExtension(const std::string& path) {
    return VirtualFSManager::Instance().GetExtension(path);
}

// ---------------------------------------------------------------------------
// Directory Operations
// ---------------------------------------------------------------------------

/**
 * @brief Lists directory contents
 * @param path Directory or archive path
 * @return Vector of entries
 */
inline std::vector<VirtualFSEntry> VirtualFS_ListDirectory(const std::string& path) {
    return VirtualFSManager::Instance().ListDirectory(path);
}

/**
 * @brief Lists directory with filter
 * @param path Directory path
 * @param filter Filter callback
 * @return Filtered entries
 */
inline std::vector<VirtualFSEntry> VirtualFS_ListDirectoryFiltered(
    const std::string& path,
    VirtualFSFilterCallback filter) {
    return VirtualFSManager::Instance().ListDirectoryFiltered(path, filter);
}

/**
 * @brief Lists directory recursively
 * @param path Start path
 * @param maxDepth Max depth
 * @return All entries
 */
inline std::vector<VirtualFSEntry> VirtualFS_ListDirectoryRecursive(
    const std::string& path,
    int maxDepth = -1) {
    return VirtualFSManager::Instance().ListDirectoryRecursive(path, maxDepth);
}

/**
 * @brief Enumerates directory via callback
 * @param path Directory path
 * @param callback Entry callback
 * @param recursive Include subdirs
 */
inline void VirtualFS_EnumerateDirectory(
    const std::string& path,
    VirtualFSEntryCallback callback,
    bool recursive = false) {
    VirtualFSManager::Instance().EnumerateDirectory(path, callback, recursive);
}

// ---------------------------------------------------------------------------
// Entry Information
// ---------------------------------------------------------------------------

/**
 * @brief Checks if path exists
 * @param path Path to check
 * @return true if exists
 */
inline bool VirtualFS_Exists(const std::string& path) {
    return VirtualFSManager::Instance().Exists(path);
}

/**
 * @brief Gets entry info
 * @param path Path to entry
 * @return Entry info
 */
inline VirtualFSEntry VirtualFS_GetInfo(const std::string& path) {
    return VirtualFSManager::Instance().GetInfo(path);
}

/**
 * @brief Gets entry type
 * @param path Path to entry
 * @return Entry type
 */
inline VirtualFSEntryType VirtualFS_GetType(const std::string& path) {
    return VirtualFSManager::Instance().GetType(path);
}

/**
 * @brief Gets file size
 * @param path File path
 * @return Size in bytes
 */
inline uint64_t VirtualFS_GetSize(const std::string& path) {
    return VirtualFSManager::Instance().GetSize(path);
}

/**
 * @brief Checks if path is directory
 * @param path Path to check
 * @return true if directory
 */
inline bool VirtualFS_IsDirectory(const std::string& path) {
    return VirtualFSManager::Instance().IsDirectory(path);
}

/**
 * @brief Checks if path is file
 * @param path Path to check
 * @return true if file
 */
inline bool VirtualFS_IsFile(const std::string& path) {
    return VirtualFSManager::Instance().IsFile(path);
}

// ---------------------------------------------------------------------------
// File Reading
// ---------------------------------------------------------------------------

/**
 * @brief Reads entire file into memory
 * @param path File path
 * @param outData Output buffer
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ReadFile(const std::string& path, std::vector<uint8_t>& outData) {
    return VirtualFSManager::Instance().ReadFile(path, outData);
}

/**
 * @brief Reads portion of file
 * @param path File path
 * @param offset Start offset
 * @param length Bytes to read
 * @param outData Output buffer
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ReadFilePartial(
    const std::string& path,
    uint64_t offset,
    uint64_t length,
    std::vector<uint8_t>& outData) {
    return VirtualFSManager::Instance().ReadFilePartial(path, offset, length, outData);
}

/**
 * @brief Reads file as string
 * @param path File path
 * @param outText Output string
 * @param encoding Text encoding
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ReadFileString(
    const std::string& path,
    std::string& outText,
    const std::string& encoding = "UTF-8") {
    return VirtualFSManager::Instance().ReadFileString(path, outText, encoding);
}

/**
 * @brief Opens file stream
 * @param path File path
 * @param mode Open mode
 * @return Stream or nullptr
 */
inline std::unique_ptr<VirtualFSStream> VirtualFS_OpenStream(
    const std::string& path,
    VirtualFSOpenMode mode = VirtualFSOpenMode::Read) {
    return VirtualFSManager::Instance().OpenStream(path, mode);
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

/**
 * @brief Extracts file from archive
 * @param archivePath Archive path
 * @param entryPath Entry in archive
 * @param destPath Destination
 * @param options Extract options
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ExtractFile(
    const std::string& archivePath,
    const std::string& entryPath,
    const std::string& destPath,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default()) {
    return VirtualFSManager::Instance().ExtractFile(archivePath, entryPath, destPath, options);
}

/**
 * @brief Extracts entire archive
 * @param archivePath Archive path
 * @param destDirectory Destination
 * @param options Extract options
 * @param progressCallback Progress callback
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ExtractAll(
    const std::string& archivePath,
    const std::string& destDirectory,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr) {
    return VirtualFSManager::Instance().ExtractAll(archivePath, destDirectory, options, progressCallback);
}

/**
 * @brief Extracts filtered entries
 * @param archivePath Archive path
 * @param destDirectory Destination
 * @param filter Filter callback
 * @param options Extract options
 * @param progressCallback Progress callback
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ExtractFiltered(
    const std::string& archivePath,
    const std::string& destDirectory,
    VirtualFSFilterCallback filter,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr) {
    return VirtualFSManager::Instance().ExtractFiltered(archivePath, destDirectory, filter, options, progressCallback);
}

/**
 * @brief Extracts to memory
 * @param path Virtual path
 * @param outData Output buffer
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_ExtractToMemory(const std::string& path, std::vector<uint8_t>& outData) {
    return VirtualFSManager::Instance().ExtractToMemory(path, outData);
}

// ---------------------------------------------------------------------------
// Archive Creation
// ---------------------------------------------------------------------------

/**
 * @brief Creates new archive
 * @param archivePath Archive path
 * @param sourcePaths Source files
 * @param options Options
 * @param progressCallback Progress callback
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_CreateArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr) {
    return VirtualFSManager::Instance().CreateArchive(archivePath, sourcePaths, options, progressCallback);
}

/**
 * @brief Adds files to archive
 * @param archivePath Archive path
 * @param sourcePaths Files to add
 * @param destPathInArchive Destination in archive
 * @param options Options
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_AddToArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const std::string& destPathInArchive = "",
    const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
    return VirtualFSManager::Instance().AddToArchive(archivePath, sourcePaths, destPathInArchive, options);
}

/**
 * @brief Deletes from archive
 * @param archivePath Archive path
 * @param entryPaths Entries to delete
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_DeleteFromArchive(
    const std::string& archivePath,
    const std::vector<std::string>& entryPaths) {
    return VirtualFSManager::Instance().DeleteFromArchive(archivePath, entryPaths);
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

/**
 * @brief Gets error message
 * @param result Result code
 * @return Error message
 */
inline std::string VirtualFS_GetErrorMessage(VirtualFSResult result) {
    return VirtualFSManager::Instance().GetErrorMessage(result);
}

/**
 * @brief Validates archive
 * @param archivePath Archive path
 * @return true if valid
 */
inline bool VirtualFS_ValidateArchive(const std::string& archivePath) {
    return VirtualFSManager::Instance().ValidateArchive(archivePath);
}

/**
 * @brief Tests archive
 * @param archivePath Archive path
 * @param progressCallback Progress callback
 * @return Success or error
 */
inline VirtualFSResult VirtualFS_TestArchive(
    const std::string& archivePath,
    VirtualFSProgressCallback progressCallback = nullptr) {
    return VirtualFSManager::Instance().TestArchive(archivePath, progressCallback);
}

/**
 * @brief Gets archive info
 * @param archivePath Archive path
 * @return Archive info
 */
inline VirtualFSArchiveInfo VirtualFS_GetArchiveInfo(const std::string& archivePath) {
    return VirtualFSManager::Instance().GetArchiveInfo(archivePath);
}

/**
 * @brief Detects archive format
 * @param path File path
 * @return Format name
 */
inline std::string VirtualFS_DetectFormat(const std::string& path) {
    return VirtualFSManager::Instance().DetectFormat(path);
}

/**
 * @brief Gets MIME type
 * @param path File path
 * @return MIME type
 */
inline std::string VirtualFS_GetMimeType(const std::string& path) {
    return VirtualFSManager::Instance().GetMimeType(path);
}

// ---------------------------------------------------------------------------
// Cache Management
// ---------------------------------------------------------------------------

/**
 * @brief Clears all cache
 */
inline void VirtualFS_ClearCache() {
    VirtualFSManager::Instance().ClearCache();
}

/**
 * @brief Clears cache for path
 * @param archivePath Archive path
 */
inline void VirtualFS_ClearCacheForPath(const std::string& archivePath) {
    VirtualFSManager::Instance().ClearCacheForPath(archivePath);
}

/**
 * @brief Gets cache size
 * @return Size in bytes
 */
inline size_t VirtualFS_GetCacheSize() {
    return VirtualFSManager::Instance().GetCacheSize();
}

/**
 * @brief Sets max cache size
 * @param maxBytes Max size
 */
inline void VirtualFS_SetMaxCacheSize(size_t maxBytes) {
    VirtualFSManager::Instance().SetMaxCacheSize(maxBytes);
}

/**
 * @brief Enables/disables cache
 * @param enabled Enable flag
 */
inline void VirtualFS_SetCacheEnabled(bool enabled) {
    VirtualFSManager::Instance().SetCacheEnabled(enabled);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/**
 * @brief Sets password callback
 * @param callback Password callback
 */
inline void VirtualFS_SetPasswordCallback(VirtualFSPasswordCallback callback) {
    VirtualFSManager::Instance().SetPasswordCallback(callback);
}

/**
 * @brief Sets error callback
 * @param callback Error callback
 */
inline void VirtualFS_SetErrorCallback(VirtualFSErrorCallback callback) {
    VirtualFSManager::Instance().SetErrorCallback(callback);
}

/**
 * @brief Sets temp directory
 * @param path Temp path
 */
inline void VirtualFS_SetTempDirectory(const std::string& path) {
    VirtualFSManager::Instance().SetTempDirectory(path);
}

/**
 * @brief Gets temp directory
 * @return Temp path
 */
inline std::string VirtualFS_GetTempDirectory() {
    return VirtualFSManager::Instance().GetTempDirectory();
}

/**
 * @brief Sets default encoding
 * @param encoding Encoding name
 */
inline void VirtualFS_SetDefaultEncoding(const std::string& encoding) {
    VirtualFSManager::Instance().SetDefaultEncoding(encoding);
}

/** @} */ // end of GlobalAPI group

} // namespace VirtualFS
