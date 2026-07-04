// VirtualFS/include/VirtualFSTypes.h
// Core types, enumerations, and structures for VirtualFS
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: ULTRA OS Framework
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace VirtualFS {

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class IVirtualFSProvider;
class VirtualFSStream;
class VirtualFSManager;

// ============================================================================
// RESULT ENUMERATION
// ============================================================================

enum class VirtualFSResult {
    Success = 0,            // Operation completed successfully
    
    // General errors (1-99)
    Error = 1,              // Generic error
    NotFound = 2,           // File or directory not found
    AccessDenied = 3,       // Permission denied
    InvalidPath = 4,        // Malformed path
    InvalidArgument = 5,    // Invalid parameter
    NotSupported = 6,       // Operation not supported by provider
    AlreadyExists = 7,      // File or directory already exists
    NotEmpty = 8,           // Directory not empty
    Cancelled = 9,          // Operation cancelled by user
    
    // Archive errors (100-199)
    ArchiveNotOpen = 100,   // Archive not opened
    ArchiveCorrupt = 101,   // Archive file is corrupted
    ArchiveUnsupported = 102, // Archive format not recognized
    PasswordRequired = 103, // Archive requires password
    PasswordIncorrect = 104, // Wrong password provided
    ArchiveReadOnly = 105,  // Archive opened in read-only mode
    ArchiveFull = 106,      // Archive size limit reached
    
    // I/O errors (200-299)
    ReadError = 200,        // Failed to read data
    WriteError = 201,       // Failed to write data
    SeekError = 202,        // Failed to seek in file
    DiskFull = 203,         // No space left on device
    FileInUse = 204,        // File is locked by another process
    
    // Resource errors (300-399)
    OutOfMemory = 300,      // Memory allocation failed
    TooManyOpenFiles = 301, // File handle limit reached
    
    // Provider errors (400-499)
    ProviderNotFound = 400, // No provider for format
    ProviderError = 401,    // Provider-specific error
    LibraryMissing = 402,   // Required library not available
    ProviderBusy = 403      // Provider is busy with another operation
};

// ============================================================================
// CAPABILITY FLAGS
// ============================================================================

enum class VirtualFSCapability : uint32_t {
    None            = 0,
    
    // Basic operations
    Read            = 1 << 0,   // Can read files
    Write           = 1 << 1,   // Can write/modify files
    Create          = 1 << 2,   // Can create new entries
    Delete          = 1 << 3,   // Can delete entries
    Rename          = 1 << 4,   // Can rename entries
    
    // Directory operations
    ListDirectory   = 1 << 5,   // Can list contents
    CreateDirectory = 1 << 6,   // Can create directories
    
    // Advanced features
    RandomAccess    = 1 << 7,   // Supports seeking
    Streaming       = 1 << 8,   // Supports streaming reads
    Password        = 1 << 9,   // Supports encryption
    Compression     = 1 << 10,  // Supports compression levels
    Timestamps      = 1 << 11,  // Preserves timestamps
    Permissions     = 1 << 12,  // Preserves permissions
    Symlinks        = 1 << 13,  // Supports symbolic links
    LargeFiles      = 1 << 14,  // Supports files > 4GB
    Unicode         = 1 << 15,  // Supports Unicode filenames
    Comments        = 1 << 16,  // Supports file comments
    SolidArchive    = 1 << 17,  // Supports solid compression
    MultiVolume     = 1 << 18,  // Supports split archives
    
    // Composite capabilities
    ReadOnly        = Read | ListDirectory,
    ReadWrite       = Read | Write | Create | Delete | ListDirectory | CreateDirectory,
    Full            = 0xFFFFFFFF
};

// Bitwise operators for VirtualFSCapability
inline VirtualFSCapability operator|(VirtualFSCapability a, VirtualFSCapability b) {
    return static_cast<VirtualFSCapability>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
    );
}

inline VirtualFSCapability operator&(VirtualFSCapability a, VirtualFSCapability b) {
    return static_cast<VirtualFSCapability>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
    );
}

inline VirtualFSCapability& operator|=(VirtualFSCapability& a, VirtualFSCapability b) {
    a = a | b;
    return a;
}

inline bool HasCapability(VirtualFSCapability caps, VirtualFSCapability check) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(check)) == 
           static_cast<uint32_t>(check);
}

// ============================================================================
// ENTRY TYPE ENUMERATION
// ============================================================================

enum class VirtualFSEntryType {
    Unknown = 0,
    File = 1,
    Directory = 2,
    Symlink = 3,
    Archive = 4,        // Browsable archive (can enter)
    MountPoint = 5,     // Virtual mount point
    HardLink = 6,
    BlockDevice = 7,    // Unix block device
    CharDevice = 8,     // Unix character device
    Fifo = 9,           // Named pipe
    Socket = 10         // Unix socket
};

// ============================================================================
// OPEN MODE ENUMERATION
// ============================================================================

enum class VirtualFSOpenMode {
    Read = 0,           // Open for reading only
    Write = 1,          // Open for writing (create/truncate)
    Append = 2,         // Open for appending
    ReadWrite = 3,      // Open for reading and writing
    Create = 4          // Create new archive
};

// ============================================================================
// COMPRESSION LEVEL ENUMERATION
// ============================================================================

enum class VirtualFSCompressionLevel {
    Store = 0,          // No compression (store)
    Fastest = 1,        // Fastest compression
    Fast = 3,           // Fast compression
    Normal = 5,         // Balanced (default)
    Good = 7,           // Better compression
    Best = 9,           // Maximum compression
    Ultra = 10          // Ultra compression (7z LZMA2)
};

// ============================================================================
// COMPRESSION METHOD ENUMERATION
// ============================================================================

enum class VirtualFSCompressionMethod {
    Auto = 0,           // Automatic selection based on format
    Store = 1,          // No compression
    Deflate = 2,        // DEFLATE (ZIP default)
    Deflate64 = 3,      // Enhanced DEFLATE
    BZip2 = 4,          // BZIP2
    LZMA = 5,           // LZMA (7z)
    LZMA2 = 6,          // LZMA2 (7z default)
    PPMd = 7,           // PPMd
    Zstd = 8,           // Zstandard (recommended)
    LZ4 = 9,            // LZ4 (fastest)
    Brotli = 10,        // Brotli
    XZ = 11             // XZ/LZMA2
};

// ============================================================================
// ENTRY STRUCTURE
// ============================================================================

struct VirtualFSEntry {
    std::string name;               // Entry name (filename only)
    std::string path;               // Full virtual path
    std::string realPath;           // Real filesystem path (if applicable)
    
    VirtualFSEntryType type = VirtualFSEntryType::Unknown;
    
    uint64_t size = 0;              // Uncompressed size in bytes
    uint64_t compressedSize = 0;    // Compressed size (0 if not compressed)
    
    std::string createdTime;        // ISO 8601 creation time
    std::string modifiedTime;       // ISO 8601 modification time
    std::string accessedTime;       // ISO 8601 last access time
    
    uint32_t attributes = 0;        // Platform-specific attributes
    uint32_t permissions = 0644;    // Unix permissions (mode_t)
    uint32_t crc32 = 0;             // CRC32 checksum (if available)
    
    bool isEncrypted = false;       // Entry is password protected
    bool isHidden = false;          // Hidden file/directory
    bool isReadOnly = false;        // Read-only entry
    bool isArchive = false;         // Entry is a browsable archive
    
    std::string compressionMethod;  // "deflate", "lzma", "store", etc.
    std::string linkTarget;         // Symlink target path
    std::string comment;            // Entry comment (if supported)
    
    std::string providerName;       // Which provider handles this entry
    
    // User/group (Unix)
    uint32_t uid = 0;
    uint32_t gid = 0;
    std::string userName;
    std::string groupName;
    
    // Helper methods
    bool IsDirectory() const { return type == VirtualFSEntryType::Directory; }
    bool IsFile() const { return type == VirtualFSEntryType::File; }
    bool IsSymlink() const { return type == VirtualFSEntryType::Symlink; }
    bool IsValid() const { return type != VirtualFSEntryType::Unknown; }
    bool CanEnter() const { return IsDirectory() || isArchive; }
    
    float CompressionRatio() const {
        if (size == 0 || compressedSize == 0) return 0.0f;
        return 100.0f * (1.0f - static_cast<float>(compressedSize) / static_cast<float>(size));
    }
    
    // Default constructor
    VirtualFSEntry() = default;
    
    // Convenience constructor
    VirtualFSEntry(const std::string& entryName, VirtualFSEntryType entryType)
        : name(entryName), type(entryType) {}
};

// ============================================================================
// OPEN OPTIONS STRUCTURE
// ============================================================================

struct VirtualFSOpenOptions {
    VirtualFSOpenMode mode = VirtualFSOpenMode::Read;
    std::string password;
    VirtualFSCompressionLevel compressionLevel = VirtualFSCompressionLevel::Normal;
    VirtualFSCompressionMethod compressionMethod = VirtualFSCompressionMethod::Auto;
    bool followSymlinks = true;
    bool caseSensitive = true;
    size_t cacheSize = 64 * 1024 * 1024;  // 64MB metadata cache
    bool validateCRC = false;
    std::string encoding = "UTF-8";
    
    // Factory methods
    static VirtualFSOpenOptions Default() { 
        return VirtualFSOpenOptions(); 
    }
    
    static VirtualFSOpenOptions ReadOnly() { 
        return VirtualFSOpenOptions(); 
    }
    
    static VirtualFSOpenOptions ForWriting(
        VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal,
        VirtualFSCompressionMethod method = VirtualFSCompressionMethod::Auto) {
        VirtualFSOpenOptions opts;
        opts.mode = VirtualFSOpenMode::Write;
        opts.compressionLevel = level;
        opts.compressionMethod = method;
        return opts;
    }
    
    static VirtualFSOpenOptions ForCreating(
        VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal,
        VirtualFSCompressionMethod method = VirtualFSCompressionMethod::Auto) {
        VirtualFSOpenOptions opts;
        opts.mode = VirtualFSOpenMode::Create;
        opts.compressionLevel = level;
        opts.compressionMethod = method;
        return opts;
    }
    
    static VirtualFSOpenOptions WithPassword(const std::string& pwd) {
        VirtualFSOpenOptions opts;
        opts.password = pwd;
        return opts;
    }
};

// ============================================================================
// EXTRACT OPTIONS STRUCTURE
// ============================================================================

struct VirtualFSExtractOptions {
    bool overwriteExisting = false;
    bool preserveTimestamps = true;
    bool preservePermissions = true;
    bool createDirectories = true;
    bool extractSymlinks = true;
    bool flattenStructure = false;      // Extract all files to single directory
    std::vector<std::string> includePatterns;  // Include only matching (glob)
    std::vector<std::string> excludePatterns;  // Exclude matching (glob)
    uint64_t maxFileSize = 0;           // Max file size (0 = unlimited)
    std::string password;               // Password for encrypted entries
    
    static VirtualFSExtractOptions Default() { 
        return VirtualFSExtractOptions(); 
    }
    
    static VirtualFSExtractOptions Overwrite() {
        VirtualFSExtractOptions opts;
        opts.overwriteExisting = true;
        return opts;
    }
};

// ============================================================================
// PROGRESS STRUCTURE
// ============================================================================

struct VirtualFSProgress {
    std::string currentFile;            // Current file being processed
    uint64_t currentBytes = 0;          // Bytes processed for current file
    uint64_t currentTotalBytes = 0;     // Total bytes for current file
    uint64_t totalBytes = 0;            // Total bytes processed overall
    uint64_t grandTotalBytes = 0;       // Grand total bytes to process
    size_t filesProcessed = 0;          // Number of files completed
    size_t totalFiles = 0;              // Total number of files
    float percentComplete = 0.0f;       // 0.0 - 100.0
    double bytesPerSecond = 0.0;        // Current throughput
    double estimatedSecondsRemaining = 0.0;
    bool isCancellable = true;          // Can operation be cancelled
    
    // Helper to calculate overall percent
    void UpdatePercent() {
        if (grandTotalBytes > 0) {
            percentComplete = 100.0f * static_cast<float>(totalBytes) / 
                             static_cast<float>(grandTotalBytes);
        } else if (totalFiles > 0) {
            percentComplete = 100.0f * static_cast<float>(filesProcessed) / 
                             static_cast<float>(totalFiles);
        }
    }
};

// ============================================================================
// PROVIDER INFO STRUCTURE
// ============================================================================

struct VirtualFSProviderInfo {
    std::string name;                   // Provider name (e.g., "ZIP")
    std::string version;                // Provider version
    std::string description;            // Human-readable description
    std::string libraryName;            // Underlying library (e.g., "libarchive 3.6.0")
    std::vector<std::string> extensions;     // Supported extensions
    std::vector<std::string> mimeTypes;      // Supported MIME types
    VirtualFSCapability capabilities = VirtualFSCapability::None;
    int priority = 0;                   // Higher = preferred for conflicts
};

// ============================================================================
// RESOLVED PATH STRUCTURE
// ============================================================================

struct VirtualFSResolvedPath {
    std::string fullPath;               // Original full path
    std::string realPath;               // Path to real filesystem object
    std::string virtualPath;            // Path inside archive (empty if none)
    std::vector<std::string> archiveStack;   // Nested archive paths
    bool isInsideArchive = false;       // True if path is inside an archive
    std::string providerName;           // Provider that handles this path
    
    // Example for "/home/user/backup.zip/docs/report.7z/data.csv":
    // fullPath = "/home/user/backup.zip/docs/report.7z/data.csv"
    // realPath = "/home/user/backup.zip"
    // virtualPath = "docs/report.7z/data.csv"
    // archiveStack = ["/home/user/backup.zip", "docs/report.7z"]
    // isInsideArchive = true
    // providerName = "7z"
    
    bool IsValid() const { return !fullPath.empty(); }
    bool IsNested() const { return archiveStack.size() > 1; }
    int NestingDepth() const { return static_cast<int>(archiveStack.size()); }
};

// ============================================================================
// ARCHIVE INFO STRUCTURE
// ============================================================================

struct VirtualFSArchiveInfo {
    std::string path;                   // Archive file path
    std::string format;                 // Format name (ZIP, 7z, TAR, etc.)
    std::string formatVersion;          // Format version if applicable
    
    uint64_t archiveSize = 0;           // Archive file size
    uint64_t uncompressedSize = 0;      // Total uncompressed size
    size_t entryCount = 0;              // Number of entries
    size_t fileCount = 0;               // Number of files
    size_t directoryCount = 0;          // Number of directories
    
    bool isEncrypted = false;           // Archive has encrypted entries
    bool isSolid = false;               // Solid archive (7z)
    bool isMultiVolume = false;         // Split archive
    int volumeCount = 1;                // Number of volumes
    
    std::string comment;                // Archive comment
    std::string createdTime;            // Archive creation time
    std::string modifiedTime;           // Archive modification time
    
    VirtualFSCompressionMethod compressionMethod = VirtualFSCompressionMethod::Auto;
    float compressionRatio = 0.0f;      // Overall compression ratio
};

// ============================================================================
// CALLBACK TYPE DEFINITIONS
// ============================================================================

// Progress callback - return false to cancel operation
using VirtualFSProgressCallback = std::function<bool(const VirtualFSProgress& progress)>;

// Error callback
using VirtualFSErrorCallback = std::function<void(
    VirtualFSResult result, 
    const std::string& message,
    const std::string& path)>;

// Password callback - return password or empty to cancel
using VirtualFSPasswordCallback = std::function<std::string(
    const std::string& archivePath,
    bool isRetry)>;

// Filter callback - return true to include entry
using VirtualFSFilterCallback = std::function<bool(const VirtualFSEntry& entry)>;

// Entry enumeration callback
using VirtualFSEntryCallback = std::function<void(const VirtualFSEntry& entry)>;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Convert result to human-readable string
inline std::string VirtualFSResultToString(VirtualFSResult result) {
    switch (result) {
        case VirtualFSResult::Success: return "Success";
        case VirtualFSResult::Error: return "Generic error";
        case VirtualFSResult::NotFound: return "File or directory not found";
        case VirtualFSResult::AccessDenied: return "Access denied";
        case VirtualFSResult::InvalidPath: return "Invalid path";
        case VirtualFSResult::InvalidArgument: return "Invalid argument";
        case VirtualFSResult::NotSupported: return "Operation not supported";
        case VirtualFSResult::AlreadyExists: return "Already exists";
        case VirtualFSResult::NotEmpty: return "Directory not empty";
        case VirtualFSResult::Cancelled: return "Operation cancelled";
        case VirtualFSResult::ArchiveNotOpen: return "Archive not open";
        case VirtualFSResult::ArchiveCorrupt: return "Archive is corrupted";
        case VirtualFSResult::ArchiveUnsupported: return "Unsupported archive format";
        case VirtualFSResult::PasswordRequired: return "Password required";
        case VirtualFSResult::PasswordIncorrect: return "Incorrect password";
        case VirtualFSResult::ArchiveReadOnly: return "Archive is read-only";
        case VirtualFSResult::ArchiveFull: return "Archive is full";
        case VirtualFSResult::ReadError: return "Read error";
        case VirtualFSResult::WriteError: return "Write error";
        case VirtualFSResult::SeekError: return "Seek error";
        case VirtualFSResult::DiskFull: return "Disk full";
        case VirtualFSResult::FileInUse: return "File is in use";
        case VirtualFSResult::OutOfMemory: return "Out of memory";
        case VirtualFSResult::TooManyOpenFiles: return "Too many open files";
        case VirtualFSResult::ProviderNotFound: return "No provider for format";
        case VirtualFSResult::ProviderError: return "Provider error";
        case VirtualFSResult::LibraryMissing: return "Required library missing";
        case VirtualFSResult::ProviderBusy: return "Provider is busy";
        default: return "Unknown error";
    }
}

// Convert entry type to string
inline std::string VirtualFSEntryTypeToString(VirtualFSEntryType type) {
    switch (type) {
        case VirtualFSEntryType::Unknown: return "Unknown";
        case VirtualFSEntryType::File: return "File";
        case VirtualFSEntryType::Directory: return "Directory";
        case VirtualFSEntryType::Symlink: return "Symlink";
        case VirtualFSEntryType::Archive: return "Archive";
        case VirtualFSEntryType::MountPoint: return "MountPoint";
        case VirtualFSEntryType::HardLink: return "HardLink";
        case VirtualFSEntryType::BlockDevice: return "BlockDevice";
        case VirtualFSEntryType::CharDevice: return "CharDevice";
        case VirtualFSEntryType::Fifo: return "Fifo";
        case VirtualFSEntryType::Socket: return "Socket";
        default: return "Unknown";
    }
}

// Convert compression method to string
inline std::string VirtualFSCompressionMethodToString(VirtualFSCompressionMethod method) {
    switch (method) {
        case VirtualFSCompressionMethod::Auto: return "Auto";
        case VirtualFSCompressionMethod::Store: return "Store";
        case VirtualFSCompressionMethod::Deflate: return "Deflate";
        case VirtualFSCompressionMethod::Deflate64: return "Deflate64";
        case VirtualFSCompressionMethod::BZip2: return "BZip2";
        case VirtualFSCompressionMethod::LZMA: return "LZMA";
        case VirtualFSCompressionMethod::LZMA2: return "LZMA2";
        case VirtualFSCompressionMethod::PPMd: return "PPMd";
        case VirtualFSCompressionMethod::Zstd: return "Zstandard";
        case VirtualFSCompressionMethod::LZ4: return "LZ4";
        case VirtualFSCompressionMethod::Brotli: return "Brotli";
        case VirtualFSCompressionMethod::XZ: return "XZ";
        default: return "Unknown";
    }
}

} // namespace VirtualFS
