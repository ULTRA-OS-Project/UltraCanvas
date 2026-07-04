// VirtualFS/include/VirtualFSProvider.h
// Provider interface for archive format plugins
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

#include "VirtualFSTypes.h"
#include <memory>

namespace VirtualFS {

// ============================================================================
// PROVIDER INTERFACE
// ============================================================================

/**
 * @class IVirtualFSProvider
 * @brief Abstract interface for archive format providers
 * 
 * Implement this interface to add support for new archive formats.
 * Each provider handles one or more file extensions and implements
 * the core operations for reading (and optionally writing) archives.
 * 
 * Example implementation:
 * @code
 * class MyFormatProvider : public IVirtualFSProvider {
 * public:
 *     std::string GetName() const override { return "MYFORMAT"; }
 *     // ... implement other methods
 * };
 * @endcode
 */
class IVirtualFSProvider {
public:
    virtual ~IVirtualFSProvider() = default;
    
    // =========================================================================
    // IDENTIFICATION
    // =========================================================================
    
    /**
     * @brief Returns the provider name (e.g., "ZIP", "7z", "TAR")
     * @return Provider name string
     */
    virtual std::string GetName() const = 0;
    
    /**
     * @brief Returns the provider version string
     * @return Version string (e.g., "1.0.0")
     */
    virtual std::string GetVersion() const = 0;
    
    /**
     * @brief Returns a human-readable description
     * @return Description string
     */
    virtual std::string GetDescription() const = 0;
    
    /**
     * @brief Returns information about the underlying library
     * @return Library info (e.g., "libarchive 3.6.0")
     */
    virtual std::string GetLibraryInfo() const = 0;
    
    /**
     * @brief Returns list of supported file extensions (without dots)
     * @return Vector of extensions (e.g., {"zip", "cbz", "jar"})
     */
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;
    
    /**
     * @brief Returns list of supported MIME types
     * @return Vector of MIME types
     */
    virtual std::vector<std::string> GetSupportedMimeTypes() const = 0;
    
    /**
     * @brief Returns provider capabilities bitmask
     * @return Capabilities flags
     */
    virtual VirtualFSCapability GetCapabilities() const = 0;
    
    /**
     * @brief Returns priority for format conflicts (higher = preferred)
     * @return Priority value (default: 0)
     */
    virtual int GetPriority() const { return 0; }
    
    /**
     * @brief Creates a fresh, unopened instance of this provider
     *
     * The manager keeps the registered instance as a prototype and calls
     * this to obtain a dedicated instance for each archive it opens, so
     * multiple archives can be open concurrently.
     *
     * @return New provider instance, or nullptr if the provider does not
     *         support per-archive instances (the registered instance is
     *         then shared - only one archive can be open at a time)
     */
    virtual std::shared_ptr<IVirtualFSProvider> CreateInstance() const {
        return nullptr;
    }

    /**
     * @brief Returns complete provider information
     * @return VirtualFSProviderInfo structure
     */
    virtual VirtualFSProviderInfo GetInfo() const {
        VirtualFSProviderInfo info;
        info.name = GetName();
        info.version = GetVersion();
        info.description = GetDescription();
        info.libraryName = GetLibraryInfo();
        info.extensions = GetSupportedExtensions();
        info.mimeTypes = GetSupportedMimeTypes();
        info.capabilities = GetCapabilities();
        info.priority = GetPriority();
        return info;
    }
    
    // =========================================================================
    // DETECTION
    // =========================================================================
    
    /**
     * @brief Checks if provider can handle file by extension
     * @param path File path or just extension
     * @return true if provider can handle the format
     */
    virtual bool CanHandle(const std::string& path) const = 0;
    
    /**
     * @brief Checks if provider recognizes file by magic bytes
     * @param data First bytes of file (usually 16-64 bytes)
     * @param size Size of data buffer
     * @return true if provider recognizes the format
     */
    virtual bool CanHandleByMagic(const uint8_t* data, size_t size) const = 0;
    
    // =========================================================================
    // ARCHIVE LIFECYCLE
    // =========================================================================
    
    /**
     * @brief Opens an archive for reading/writing
     * @param archivePath Path to the archive file
     * @param options Open options (mode, password, etc.)
     * @return Success or error code
     */
    virtual VirtualFSResult Open(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) = 0;
    
    /**
     * @brief Closes the archive and releases resources
     */
    virtual void Close() = 0;
    
    /**
     * @brief Checks if an archive is currently open
     * @return true if archive is open
     */
    virtual bool IsOpen() const = 0;
    
    /**
     * @brief Returns the path of the currently open archive
     * @return Archive path or empty string if not open
     */
    virtual std::string GetOpenPath() const = 0;
    
    /**
     * @brief Returns information about the open archive
     * @return Archive info structure
     */
    virtual VirtualFSArchiveInfo GetArchiveInfo() const = 0;
    
    // =========================================================================
    // DIRECTORY OPERATIONS
    // =========================================================================
    
    /**
     * @brief Lists entries in a directory inside the archive
     * @param virtualPath Path inside archive (empty for root)
     * @return Vector of entries
     */
    virtual std::vector<VirtualFSEntry> ListDirectory(
        const std::string& virtualPath = "") = 0;
    
    /**
     * @brief Lists all entries in the archive (flat list)
     * @return Vector of all entries
     */
    virtual std::vector<VirtualFSEntry> ListAll() = 0;
    
    /**
     * @brief Checks if an entry exists in the archive
     * @param virtualPath Path inside archive
     * @return true if entry exists
     */
    virtual bool Exists(const std::string& virtualPath) = 0;
    
    /**
     * @brief Gets detailed information about an entry
     * @param virtualPath Path inside archive
     * @return Entry info (check IsValid() for validity)
     */
    virtual VirtualFSEntry GetInfo(const std::string& virtualPath) = 0;
    
    /**
     * @brief Gets the entry type
     * @param virtualPath Path inside archive
     * @return Entry type (Unknown if not found)
     */
    virtual VirtualFSEntryType GetEntryType(const std::string& virtualPath) {
        return GetInfo(virtualPath).type;
    }
    
    // =========================================================================
    // FILE READING
    // =========================================================================
    
    /**
     * @brief Reads entire file from archive into memory
     * @param virtualPath Path inside archive
     * @param outData Output buffer (resized to file size)
     * @return Success or error code
     */
    virtual VirtualFSResult ReadFile(
        const std::string& virtualPath,
        std::vector<uint8_t>& outData) = 0;
    
    /**
     * @brief Reads portion of a file (optional, may not be supported)
     * @param virtualPath Path inside archive
     * @param offset Starting offset in bytes
     * @param length Number of bytes to read
     * @param outData Output buffer
     * @return Success or error code
     */
    virtual VirtualFSResult ReadFilePartial(
        const std::string& virtualPath,
        uint64_t offset,
        uint64_t length,
        std::vector<uint8_t>& outData) {
        // Default implementation: read entire file and extract portion
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
    
    /**
     * @brief Opens a stream for reading a file
     * @param virtualPath Path inside archive
     * @return Stream object or nullptr on error
     */
    virtual std::unique_ptr<VirtualFSStream> OpenStream(
        const std::string& virtualPath) {
        // Default: not supported
        return nullptr;
    }
    
    // =========================================================================
    // EXTRACTION
    // =========================================================================
    
    /**
     * @brief Extracts a single file to the filesystem
     * @param virtualPath Path inside archive
     * @param destPath Destination file path
     * @param options Extraction options
     * @return Success or error code
     */
    virtual VirtualFSResult ExtractFile(
        const std::string& virtualPath,
        const std::string& destPath,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default()) = 0;
    
    /**
     * @brief Extracts all files to a directory
     * @param destDirectory Destination directory
     * @param options Extraction options
     * @param progressCallback Optional progress callback
     * @return Success or error code
     */
    virtual VirtualFSResult ExtractAll(
        const std::string& destDirectory,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) = 0;
    
    /**
     * @brief Extracts filtered entries
     * @param destDirectory Destination directory
     * @param filter Filter callback
     * @param options Extraction options
     * @param progressCallback Optional progress callback
     * @return Success or error code
     */
    virtual VirtualFSResult ExtractFiltered(
        const std::string& destDirectory,
        VirtualFSFilterCallback filter,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) {
        // Default implementation using ExtractAll with pattern matching
        // Providers can override for better performance
        return VirtualFSResult::NotSupported;
    }
    
    // =========================================================================
    // WRITE OPERATIONS (Optional)
    // =========================================================================
    
    /**
     * @brief Creates a new empty archive
     * @param archivePath Path for the new archive
     * @param options Creation options
     * @return Success or error code
     */
    virtual VirtualFSResult CreateArchive(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Adds a file from the filesystem to the archive
     * @param sourcePath Source file path
     * @param virtualPath Destination path inside archive
     * @param options Compression options
     * @return Success or error code
     */
    virtual VirtualFSResult AddFile(
        const std::string& sourcePath,
        const std::string& virtualPath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Adds data from memory to the archive
     * @param virtualPath Destination path inside archive
     * @param data File content
     * @param options Compression options
     * @return Success or error code
     */
    virtual VirtualFSResult AddFromMemory(
        const std::string& virtualPath,
        const std::vector<uint8_t>& data,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Adds a directory to the archive
     * @param sourcePath Source directory path
     * @param virtualPath Destination path inside archive
     * @param recursive Include subdirectories
     * @param options Compression options
     * @param progressCallback Optional progress callback
     * @return Success or error code
     */
    virtual VirtualFSResult AddDirectory(
        const std::string& sourcePath,
        const std::string& virtualPath,
        bool recursive = true,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Creates an empty directory inside the archive
     * @param virtualPath Path for the new directory
     * @return Success or error code
     */
    virtual VirtualFSResult CreateDirectory(const std::string& virtualPath) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Deletes an entry from the archive
     * @param virtualPath Path inside archive
     * @return Success or error code
     */
    virtual VirtualFSResult Delete(const std::string& virtualPath) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Renames an entry in the archive
     * @param oldPath Current path
     * @param newPath New path
     * @return Success or error code
     */
    virtual VirtualFSResult Rename(
        const std::string& oldPath,
        const std::string& newPath) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Updates file content inside archive
     * @param virtualPath Path inside archive
     * @param data New file content
     * @return Success or error code
     */
    virtual VirtualFSResult UpdateFile(
        const std::string& virtualPath,
        const std::vector<uint8_t>& data) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Sets archive comment
     * @param comment Comment text
     * @return Success or error code
     */
    virtual VirtualFSResult SetComment(const std::string& comment) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Finalizes and closes a writable archive
     * @return Success or error code
     */
    virtual VirtualFSResult Finalize() {
        Close();
        return VirtualFSResult::Success;
    }
    
    // =========================================================================
    // UTILITY
    // =========================================================================
    
    /**
     * @brief Validates archive integrity
     * @return true if archive is valid
     */
    virtual bool Validate() = 0;
    
    /**
     * @brief Tests archive by reading all entries
     * @param progressCallback Optional progress callback
     * @return Success or error code
     */
    virtual VirtualFSResult Test(VirtualFSProgressCallback progressCallback = nullptr) {
        return VirtualFSResult::NotSupported;
    }
    
    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    virtual std::string GetLastError() const {
        return "";
    }
    
    /**
     * @brief Sets password for encrypted archives
     * @param password Password string
     */
    virtual void SetPassword(const std::string& password) {
        // Default: store password for later use
        currentPassword = password;
    }
    
protected:
    std::string currentPassword;
};

// ============================================================================
// STREAM INTERFACE
// ============================================================================

/**
 * @class VirtualFSStream
 * @brief Abstract stream interface for file access
 * 
 * Provides streaming read/write access to files inside archives.
 * Not all providers support streaming; check GetCapabilities().
 */
class VirtualFSStream {
public:
    virtual ~VirtualFSStream() = default;
    
    // =========================================================================
    // BASIC I/O
    // =========================================================================
    
    /**
     * @brief Reads up to 'size' bytes into buffer
     * @param buffer Destination buffer
     * @param size Maximum bytes to read
     * @return Actual bytes read
     */
    virtual size_t Read(void* buffer, size_t size) = 0;
    
    /**
     * @brief Writes 'size' bytes from buffer
     * @param buffer Source buffer
     * @param size Bytes to write
     * @return Actual bytes written
     */
    virtual size_t Write(const void* buffer, size_t size) = 0;
    
    /**
     * @brief Seeks to position in stream
     * @param offset Offset in bytes
     * @param origin SEEK_SET, SEEK_CUR, or SEEK_END
     * @return true on success
     */
    virtual bool Seek(int64_t offset, int origin = 0) = 0;
    
    /**
     * @brief Returns current position
     * @return Current position in bytes
     */
    virtual int64_t Tell() const = 0;
    
    /**
     * @brief Returns total stream size
     * @return Size in bytes
     */
    virtual int64_t GetSize() const = 0;
    
    // =========================================================================
    // STATE
    // =========================================================================
    
    virtual bool IsOpen() const = 0;
    virtual bool IsReadable() const = 0;
    virtual bool IsWritable() const = 0;
    virtual bool IsSeekable() const = 0;
    virtual bool IsEOF() const = 0;
    virtual bool HasError() const = 0;
    virtual std::string GetErrorMessage() const = 0;
    
    // =========================================================================
    // CONTROL
    // =========================================================================
    
    virtual void Flush() = 0;
    virtual void Close() = 0;
    
    // =========================================================================
    // CONVENIENCE METHODS
    // =========================================================================
    
    /**
     * @brief Reads entire stream into vector
     * @return Vector containing all data
     */
    std::vector<uint8_t> ReadAll() {
        std::vector<uint8_t> data;
        int64_t size = GetSize();
        if (size > 0) {
            data.resize(static_cast<size_t>(size));
            Seek(0, 0);
            Read(data.data(), data.size());
        }
        return data;
    }
    
    /**
     * @brief Reads stream as string
     * @return String containing all data
     */
    std::string ReadString() {
        auto data = ReadAll();
        return std::string(data.begin(), data.end());
    }
    
    /**
     * @brief Reads a line from stream
     * @return Line string (without newline)
     */
    std::string ReadLine() {
        std::string line;
        char ch;
        while (Read(&ch, 1) == 1 && ch != '\n') {
            if (ch != '\r') {
                line += ch;
            }
        }
        return line;
    }
};

// ============================================================================
// PROVIDER FACTORY FUNCTION TYPE
// ============================================================================

/**
 * @brief Factory function type for creating providers
 * 
 * Use this type to register provider factory functions:
 * @code
 * std::shared_ptr<IVirtualFSProvider> CreateMyProvider() {
 *     return std::make_shared<MyProvider>();
 * }
 * @endcode
 */
using VirtualFSProviderFactory = std::function<std::shared_ptr<IVirtualFSProvider>()>;

} // namespace VirtualFS
