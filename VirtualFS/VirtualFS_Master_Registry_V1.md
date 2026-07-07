# VirtualFS Module - Complete Master Registry

## **VERSION CONTROL**
```cpp
// VirtualFS_Master_Registry.md
// Complete registry of VirtualFS module functions, types, and callbacks
// Version: 1.1.0
// Last Modified: 2026-07-07
// Author: ULTRA OS Framework
// 1.1.0: Added Raw Buffer Compression API (VirtualFSCompression.h)
```

---

## **MODULE OVERVIEW**

VirtualFS is a standalone, cross-platform Virtual File System module that provides transparent access to files inside archives as if they were regular folders. It enables applications to navigate into compressed files (ZIP, 7z, TAR, etc.) seamlessly, treating them as virtual directories.

### **Key Features**

- **Transparent Archive Access**: Browse archives as folders without extraction
- **Plugin Architecture**: Extensible format support via `IVirtualFSProvider` interface
- **Library Agnostic**: Plugins can use any compression library (libarchive, miniz, platform APIs)
- **Cross-Platform**: Consistent API across Windows, Linux, macOS, Android, iOS
- **Nested Archives**: Support for archives within archives
- **Lazy Loading**: Files extracted on-demand, not upfront
- **Caching**: Intelligent caching of archive metadata and extracted files
- **Password Support**: Encrypted archive handling
- **Streaming**: Large file support without full extraction
- **Raw Buffer Compression**: Full compress/decompress support for
  applications on plain memory buffers (Deflate, Zstd, LZ4, Brotli) -
  no archive container required

### **Use Cases**

- File managers showing archive contents as folders
- Document viewers opening files directly from archives
- Asset loading systems for games/applications
- Backup browsers navigating compressed backups
- Package managers inspecting package contents
- Modules with custom binary formats needing a compression primitive
  (e.g. the UltraWeb bundler compressing .ucpkg payloads)

---

## **ARCHITECTURE**

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          APPLICATION LAYER                               │
│         (UltraCanvas FileDialog, File Manager, Custom Apps)             │
├─────────────────────────────────────────────────────────────────────────┤
│                         VirtualFS PUBLIC API                              │
│    VirtualFS_ListDirectory() | VirtualFS_ReadFile() | VirtualFS_Exists()   │
├─────────────────────────────────────────────────────────────────────────┤
│                        VirtualFSManager (Singleton)                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ Path Parser │  │Provider Reg.│  │ Cache Mgr   │  │ Mount Table │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                      IVirtualFSProvider INTERFACE                         │
├──────────┬──────────┬──────────┬──────────┬──────────┬─────────────────┤
│ LocalFS  │   ZIP    │   7z     │  TAR/GZ  │   RAR    │   ISO/UDF       │
│ Provider │ Provider │ Provider │ Provider │ Provider │   Provider      │
├──────────┼──────────┼──────────┼──────────┼──────────┼─────────────────┤
│std::fs   │libarchive│libarchive│libarchive│libarchive│  libarchive     │
│          │  miniz   │  7z SDK  │  zlib    │  unrar   │                 │
└──────────┴──────────┴──────────┴──────────┴──────────┴─────────────────┘
```

---

## **SUPPORTED ARCHIVE FORMATS**

### **Built-in Plugins (via libarchive)**

| Format | Extensions | Read | Write | Password | Streaming | Notes |
|--------|------------|------|-------|----------|-----------|-------|
| ZIP | .zip, .cbz, .jar, .apk, .ipa, .xpi | ✓ | ✓ | ✓ | ✓ | Most common format |
| 7-Zip | .7z | ✓ | ✓ | ✓ | ○ | LZMA/LZMA2 compression |
| TAR | .tar | ✓ | ✓ | ✗ | ✓ | Unix tape archive |
| GZIP | .gz, .tgz, .tar.gz | ✓ | ✓ | ✗ | ✓ | Single file compression |
| BZIP2 | .bz2, .tbz2, .tar.bz2 | ✓ | ✓ | ✗ | ✓ | Higher compression |
| XZ | .xz, .txz, .tar.xz | ✓ | ✓ | ✗ | ✓ | LZMA2 compression |
| LZMA | .lzma | ✓ | ✓ | ✗ | ✓ | Legacy LZMA |
| RAR | .rar, .cbr | ✓ | ✗ | ✓ | ○ | Read-only (licensing) |
| CAB | .cab | ✓ | ✗ | ✗ | ✓ | Windows Cabinet |
| ISO 9660 | .iso | ✓ | ✗ | ✗ | ✓ | CD/DVD images |
| UDF | .udf, .iso | ✓ | ✗ | ✗ | ✓ | DVD/Blu-ray format |
| CPIO | .cpio | ✓ | ✓ | ✗ | ✓ | Unix archive |
| PAX | .pax | ✓ | ✓ | ✗ | ✓ | POSIX archive |
| AR | .a, .deb | ✓ | ✓ | ✗ | ✓ | Unix static libraries |
| ZSTD | .zst, .tar.zst | ✓ | ✓ | ✗ | ✓ | Modern fast compression |
| LZ4 | .lz4, .tar.lz4 | ✓ | ✓ | ✗ | ✓ | Very fast compression |

**Legend:** ✓ = Full Support | ○ = Partial Support | ✗ = Not Supported

### **Platform-Specific Plugins**

| Platform | Additional Formats | Library |
|----------|-------------------|---------|
| Windows | .cab (native) | Cabinet API |
| macOS | .aar, .pkg | Compression.framework, xar |
| Linux | .rpm, .deb (enhanced) | librpm, libdpkg |

---

## **NAMESPACE & NAMING CONVENTIONS**

```cpp
namespace VirtualFS {
    // All VirtualFS types and classes
}

// Function naming: VirtualFS_[Action][Target]()
VirtualFS_ListDirectory()
VirtualFS_ReadFile()
VirtualFS_RegisterProvider()

// Type naming: VirtualFS[TypeName]
VirtualFSEntry
VirtualFSResult
VirtualFSManager

// Callback naming: VirtualFS[Event]Callback
VirtualFSProgressCallback
VirtualFSErrorCallback

// Enum naming: VirtualFS[Category]
VirtualFSCapability
VirtualFSResult
VirtualFSOpenMode
```

---

## **ENUMERATIONS**

### **VirtualFSResult**
*Source: VirtualFSTypes.h*
```cpp
enum class VirtualFSResult {
    Success = 0,            // Operation completed successfully
    
    // General errors (1-99)
    Error = 1,              // Generic error
    NotFound = 2,           // File or directory not found
    AccessDenied = 3,       // Permission denied
    InvalidPath = 4,        // Malformed path
    InvalidArgument = 5,    // Invalid parameter
    NotSupported = 6,       // Operation not supported by provider
    
    // Archive errors (100-199)
    ArchiveNotOpen = 100,   // Archive not opened
    ArchiveCorrupt = 101,   // Archive file is corrupted
    ArchiveUnsupported = 102, // Archive format not recognized
    PasswordRequired = 103, // Archive requires password
    PasswordIncorrect = 104, // Wrong password provided
    
    // I/O errors (200-299)
    ReadError = 200,        // Failed to read data
    WriteError = 201,       // Failed to write data
    SeekError = 202,        // Failed to seek in file
    DiskFull = 203,         // No space left on device
    
    // Resource errors (300-399)
    OutOfMemory = 300,      // Memory allocation failed
    TooManyOpenFiles = 301, // File handle limit reached
    
    // Provider errors (400-499)
    ProviderNotFound = 400, // No provider for format
    ProviderError = 401,    // Provider-specific error
    LibraryMissing = 402    // Required library not available
};
```

### **VirtualFSCapability**
*Source: VirtualFSTypes.h*
```cpp
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
    
    // Composite capabilities
    ReadOnly        = Read | ListDirectory,
    ReadWrite       = Read | Write | Create | Delete | ListDirectory | CreateDirectory,
    Full            = 0xFFFFFFFF
};

// Bitwise operators
inline VirtualFSCapability operator|(VirtualFSCapability a, VirtualFSCapability b);
inline VirtualFSCapability operator&(VirtualFSCapability a, VirtualFSCapability b);
inline bool HasCapability(VirtualFSCapability caps, VirtualFSCapability check);
```

### **VirtualFSEntryType**
*Source: VirtualFSTypes.h*
```cpp
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
```

### **VirtualFSOpenMode**
*Source: VirtualFSTypes.h*
```cpp
enum class VirtualFSOpenMode {
    Read = 0,           // Open for reading only
    Write = 1,          // Open for writing (create/truncate)
    Append = 2,         // Open for appending
    ReadWrite = 3,      // Open for reading and writing
    Create = 4          // Create new archive
};
```

### **VirtualFSCompressionLevel**
*Source: VirtualFSTypes.h*
```cpp
enum class VirtualFSCompressionLevel {
    None = 0,           // No compression (store)
    Fastest = 1,        // Fastest compression
    Fast = 3,           // Fast compression
    Normal = 5,         // Balanced (default)
    Good = 7,           // Better compression
    Best = 9,           // Maximum compression
    Ultra = 10          // Ultra compression (7z LZMA2)
};
```

---

## **STRUCTURES**

### **VirtualFSEntry**
*Source: VirtualFSTypes.h*
```cpp
struct VirtualFSEntry {
    std::string name;               // Entry name (filename only)
    std::string path;               // Full virtual path
    std::string realPath;           // Real filesystem path (if applicable)
    
    VirtualFSEntryType type;         // Entry type
    
    size_t size;                    // Uncompressed size in bytes
    size_t compressedSize;          // Compressed size (0 if not compressed)
    
    std::string createdTime;        // ISO 8601 creation time
    std::string modifiedTime;       // ISO 8601 modification time
    std::string accessedTime;       // ISO 8601 last access time
    
    uint32_t attributes;            // Platform-specific attributes
    uint32_t permissions;           // Unix permissions (mode_t)
    uint32_t crc32;                 // CRC32 checksum (if available)
    
    bool isEncrypted;               // Entry is password protected
    bool isHidden;                  // Hidden file/directory
    bool isReadOnly;                // Read-only entry
    bool isArchive;                 // Entry is a browsable archive
    
    std::string compressionMethod;  // "deflate", "lzma", "store", etc.
    std::string linkTarget;         // Symlink target path
    
    std::string providerName;       // Which provider handles this entry
    
    // Helper methods
    bool IsDirectory() const { return type == VirtualFSEntryType::Directory; }
    bool IsFile() const { return type == VirtualFSEntryType::File; }
    bool IsSymlink() const { return type == VirtualFSEntryType::Symlink; }
    bool CanEnter() const { return IsDirectory() || isArchive; }
    float CompressionRatio() const;
};
```

### **VirtualFSOpenOptions**
*Source: VirtualFSTypes.h*
```cpp
struct VirtualFSOpenOptions {
    VirtualFSOpenMode mode = VirtualFSOpenMode::Read;
    std::string password;                       // For encrypted archives
    VirtualFSCompressionLevel compressionLevel = VirtualFSCompressionLevel::Normal;
    std::string compressionMethod;              // "deflate", "lzma", "zstd", etc.
    bool followSymlinks = true;                 // Resolve symbolic links
    bool caseSensitive = true;                  // Case-sensitive path matching
    size_t cacheSize = 64 * 1024 * 1024;       // 64MB metadata cache
    bool validateCRC = false;                   // Verify checksums on read
    std::string encoding = "UTF-8";             // Filename encoding
    
    static VirtualFSOpenOptions Default() { return VirtualFSOpenOptions(); }
    static VirtualFSOpenOptions ReadOnly() { return VirtualFSOpenOptions(); }
    static VirtualFSOpenOptions ForWriting(VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal);
};
```

### **VirtualFSExtractOptions**
*Source: VirtualFSTypes.h*
```cpp
struct VirtualFSExtractOptions {
    bool overwriteExisting = false;             // Overwrite existing files
    bool preserveTimestamps = true;             // Keep original timestamps
    bool preservePermissions = true;            // Keep original permissions
    bool createDirectories = true;              // Create parent directories
    bool extractSymlinks = true;                // Extract symbolic links
    std::vector<std::string> includePatterns;   // Include only matching (glob)
    std::vector<std::string> excludePatterns;   // Exclude matching (glob)
    size_t maxFileSize = 0;                     // Max file size (0 = unlimited)
    
    static VirtualFSExtractOptions Default() { return VirtualFSExtractOptions(); }
};
```

### **VirtualFSProgress**
*Source: VirtualFSTypes.h*
```cpp
struct VirtualFSProgress {
    std::string currentFile;        // Current file being processed
    size_t currentBytes;            // Bytes processed for current file
    size_t currentTotalBytes;       // Total bytes for current file
    size_t totalBytes;              // Total bytes processed overall
    size_t grandTotalBytes;         // Grand total bytes to process
    size_t filesProcessed;          // Number of files completed
    size_t totalFiles;              // Total number of files
    float percentComplete;          // 0.0 - 100.0
    double bytesPerSecond;          // Current throughput
    double estimatedSecondsRemaining;
    bool isCancellable;             // Can operation be cancelled
};
```

### **VirtualFSProviderInfo**
*Source: VirtualFSTypes.h*
```cpp
struct VirtualFSProviderInfo {
    std::string name;               // Provider name (e.g., "ZIP")
    std::string version;            // Provider version
    std::string description;        // Human-readable description
    std::string libraryName;        // Underlying library (e.g., "libarchive 3.6.0")
    std::vector<std::string> extensions;  // Supported extensions
    std::vector<std::string> mimeTypes;   // Supported MIME types
    VirtualFSCapability capabilities;
};
```

### **VirtualFSResolvedPath**
*Source: VirtualFSPath.h*
```cpp
struct VirtualFSResolvedPath {
    std::string fullPath;           // Original full path
    std::string realPath;           // Path to real filesystem object
    std::string virtualPath;        // Path inside archive (empty if none)
    std::vector<std::string> archiveStack;  // Nested archive paths
    bool isInsideArchive;           // True if path is inside an archive
    std::string providerName;       // Provider that handles this path
    
    // Example for "/home/user/backup.zip/docs/report.7z/data.csv":
    // fullPath = "/home/user/backup.zip/docs/report.7z/data.csv"
    // realPath = "/home/user/backup.zip"
    // virtualPath = "docs/report.7z/data.csv"
    // archiveStack = ["/home/user/backup.zip", "docs/report.7z"]
    // isInsideArchive = true
    // providerName = "7z"
};
```

---

## **CALLBACK SIGNATURES**

### **Progress Callback**
```cpp
using VirtualFSProgressCallback = std::function<bool(const VirtualFSProgress& progress)>;
    // Called during long operations (extract, compress)
    // @param progress - Current progress information
    // @return true to continue, false to cancel operation
```

### **Error Callback**
```cpp
using VirtualFSErrorCallback = std::function<void(VirtualFSResult result, 
                                                  const std::string& message,
                                                  const std::string& path)>;
    // Called when an error occurs
    // @param result - Error code
    // @param message - Human-readable error description
    // @param path - Path that caused the error
```

### **Password Callback**
```cpp
using VirtualFSPasswordCallback = std::function<std::string(const std::string& archivePath,
                                                            bool isRetry)>;
    // Called when password is required
    // @param archivePath - Path to the encrypted archive
    // @param isRetry - True if previous password was incorrect
    // @return Password string, or empty to cancel
```

### **Filter Callback**
```cpp
using VirtualFSFilterCallback = std::function<bool(const VirtualFSEntry& entry)>;
    // Called to filter entries during listing or extraction
    // @param entry - Entry being considered
    // @return true to include, false to exclude
```

### **Entry Callback**
```cpp
using VirtualFSEntryCallback = std::function<void(const VirtualFSEntry& entry)>;
    // Called for each entry during iteration
    // @param entry - Current entry
```

---

## **CORE FUNCTIONS**

### **Initialization & Shutdown**
*Source: VirtualFS.h*

```cpp
VirtualFSResult VirtualFS_Initialize();
    // Initializes VirtualFS system and loads built-in providers
    // Must be called before any other VirtualFS function
    // Thread-safe, can be called multiple times
    
void VirtualFS_Shutdown();
    // Shuts down VirtualFS, closes all open archives, frees resources
    // Should be called at application exit
    
std::string VirtualFS_GetVersion();
    // Returns VirtualFS version string (e.g., "1.0.0")
    
bool VirtualFS_IsInitialized();
    // Returns true if VirtualFS_Initialize() has been called
```

### **Provider Management**
*Source: VirtualFS.h*

```cpp
VirtualFSResult VirtualFS_RegisterProvider(std::shared_ptr<IVirtualFSProvider> provider);
    // Registers a new format provider
    // @param provider - Provider implementation
    // @return Success or error if provider conflicts with existing

void VirtualFS_UnregisterProvider(const std::string& providerName);
    // Removes a registered provider by name
    // @param providerName - Name returned by provider->GetName()

std::vector<VirtualFSProviderInfo> VirtualFS_GetRegisteredProviders();
    // Returns list of all registered providers with their info

VirtualFSProviderInfo VirtualFS_GetProviderInfo(const std::string& providerName);
    // Returns info for a specific provider
    // @param providerName - Provider name

std::shared_ptr<IVirtualFSProvider> VirtualFS_GetProviderForPath(const std::string& path);
    // Returns the provider that handles the given path
    // @param path - File path or extension
    // @return Provider or nullptr if none found

std::shared_ptr<IVirtualFSProvider> VirtualFS_GetProviderForExtension(const std::string& extension);
    // Returns the provider for a file extension
    // @param extension - Extension without dot (e.g., "zip")

std::vector<std::string> VirtualFS_GetSupportedExtensions();
    // Returns list of all supported file extensions
```

### **Path Operations**
*Source: VirtualFS.h*

```cpp
VirtualFSResolvedPath VirtualFS_ResolvePath(const std::string& virtualPath);
    // Resolves a virtual path, identifying archives and nested paths
    // @param virtualPath - Path like "/home/user/archive.zip/folder/file.txt"
    // @return Resolved path components

bool VirtualFS_IsArchivePath(const std::string& path);
    // Checks if path points to or inside an archive
    // @param path - Path to check

bool VirtualFS_IsInsideArchive(const std::string& path);
    // Checks if path is inside an archive (not the archive itself)
    // @param path - Path to check

std::string VirtualFS_GetArchivePath(const std::string& path);
    // Extracts the archive path from a virtual path
    // @param path - Virtual path
    // @return Path to the outermost archive, or empty string

std::string VirtualFS_GetVirtualPath(const std::string& path);
    // Extracts the path inside the archive
    // @param path - Full virtual path
    // @return Path inside archive, or empty string

std::string VirtualFS_NormalizePath(const std::string& path);
    // Normalizes path separators and removes redundant components
    // Converts backslashes to forward slashes
    // Resolves . and .. components

std::string VirtualFS_JoinPath(const std::string& base, const std::string& relative);
    // Joins two path components
    // @param base - Base path
    // @param relative - Relative path to append

std::string VirtualFS_GetParentPath(const std::string& path);
    // Returns parent directory path

std::string VirtualFS_GetFileName(const std::string& path);
    // Returns filename component

std::string VirtualFS_GetExtension(const std::string& path);
    // Returns file extension (without dot)
```

### **Directory Operations**
*Source: VirtualFS.h*

```cpp
std::vector<VirtualFSEntry> VirtualFS_ListDirectory(const std::string& path);
    // Lists contents of directory or archive
    // @param path - Directory or archive path
    // @return Vector of entries (empty if not a directory)

std::vector<VirtualFSEntry> VirtualFS_ListDirectoryFiltered(
    const std::string& path,
    VirtualFSFilterCallback filter);
    // Lists contents with filtering
    // @param path - Directory or archive path
    // @param filter - Filter callback

std::vector<VirtualFSEntry> VirtualFS_ListDirectoryRecursive(
    const std::string& path,
    int maxDepth = -1);
    // Lists contents recursively
    // @param path - Starting directory
    // @param maxDepth - Maximum recursion depth (-1 = unlimited)

void VirtualFS_EnumerateDirectory(
    const std::string& path,
    VirtualFSEntryCallback callback,
    bool recursive = false);
    // Enumerates directory contents via callback (memory efficient)
    // @param path - Directory path
    // @param callback - Called for each entry
    // @param recursive - Include subdirectories
```

### **Entry Information**
*Source: VirtualFS.h*

```cpp
bool VirtualFS_Exists(const std::string& path);
    // Checks if file or directory exists
    // Works with regular files and archive contents
    // @param path - Path to check

VirtualFSEntry VirtualFS_GetInfo(const std::string& path);
    // Gets detailed information about a file or directory
    // @param path - Path to file or directory
    // @return Entry info (check type for validity)

VirtualFSEntryType VirtualFS_GetType(const std::string& path);
    // Gets the type of an entry
    // @param path - Path to check
    // @return Entry type (Unknown if not found)

size_t VirtualFS_GetSize(const std::string& path);
    // Gets file size in bytes
    // @param path - Path to file
    // @return Size or 0 if not a file

bool VirtualFS_IsDirectory(const std::string& path);
    // Checks if path is a directory

bool VirtualFS_IsFile(const std::string& path);
    // Checks if path is a regular file

bool VirtualFS_IsArchive(const std::string& path);
    // Checks if path is a supported archive format
    // Does NOT check if file exists, only extension/magic
```

### **File Reading**
*Source: VirtualFS.h*

```cpp
VirtualFSResult VirtualFS_ReadFile(
    const std::string& path,
    std::vector<uint8_t>& outData);
    // Reads entire file into memory
    // Works with regular files and files inside archives
    // @param path - Path to file
    // @param outData - Output buffer (resized to file size)

VirtualFSResult VirtualFS_ReadFilePartial(
    const std::string& path,
    size_t offset,
    size_t length,
    std::vector<uint8_t>& outData);
    // Reads portion of a file
    // @param path - Path to file
    // @param offset - Starting offset in bytes
    // @param length - Number of bytes to read
    // @param outData - Output buffer

VirtualFSResult VirtualFS_ReadFileString(
    const std::string& path,
    std::string& outText,
    const std::string& encoding = "UTF-8");
    // Reads file as text string
    // @param path - Path to file
    // @param outText - Output string
    // @param encoding - Text encoding

std::unique_ptr<VirtualFSStream> VirtualFS_OpenStream(
    const std::string& path,
    VirtualFSOpenMode mode = VirtualFSOpenMode::Read);
    // Opens file for streaming access
    // @param path - Path to file
    // @param mode - Open mode
    // @return Stream object or nullptr on error
```

### **Extraction**
*Source: VirtualFS.h*

```cpp
VirtualFSResult VirtualFS_ExtractFile(
    const std::string& archivePath,
    const std::string& entryPath,
    const std::string& destPath,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default());
    // Extracts single file from archive
    // @param archivePath - Path to archive file
    // @param entryPath - Path inside archive
    // @param destPath - Destination file path
    // @param options - Extraction options

VirtualFSResult VirtualFS_ExtractAll(
    const std::string& archivePath,
    const std::string& destDirectory,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr);
    // Extracts entire archive
    // @param archivePath - Path to archive file
    // @param destDirectory - Destination directory
    // @param options - Extraction options
    // @param progressCallback - Progress callback (optional)

VirtualFSResult VirtualFS_ExtractFiltered(
    const std::string& archivePath,
    const std::string& destDirectory,
    VirtualFSFilterCallback filter,
    const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr);
    // Extracts filtered entries from archive
    // @param filter - Filter callback to select entries

VirtualFSResult VirtualFS_ExtractToMemory(
    const std::string& path,
    std::vector<uint8_t>& outData);
    // Extracts file directly to memory buffer
    // Alias for VirtualFS_ReadFile for files inside archives
```

### **Archive Creation** (Write Operations)
*Source: VirtualFS.h*

```cpp
VirtualFSResult VirtualFS_CreateArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default(),
    VirtualFSProgressCallback progressCallback = nullptr);
    // Creates new archive from files/directories
    // @param archivePath - Path for new archive
    // @param sourcePaths - Files and directories to include
    // @param options - Compression options
    // @param progressCallback - Progress callback

VirtualFSResult VirtualFS_AddToArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const std::string& destPathInArchive = "",
    const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default());
    // Adds files to existing archive
    // @param archivePath - Path to existing archive
    // @param sourcePaths - Files to add
    // @param destPathInArchive - Destination path inside archive

VirtualFSResult VirtualFS_DeleteFromArchive(
    const std::string& archivePath,
    const std::vector<std::string>& entryPaths);
    // Deletes entries from archive
    // @param archivePath - Path to archive
    // @param entryPaths - Paths inside archive to delete

VirtualFSResult VirtualFS_UpdateInArchive(
    const std::string& archivePath,
    const std::string& entryPath,
    const std::vector<uint8_t>& newData);
    // Updates file content inside archive
    // @param archivePath - Path to archive
    // @param entryPath - Path inside archive
    // @param newData - New file content
```

### **Raw Buffer Compression**
*Source: VirtualFSCompression.h*

Full compress/decompress support for applications on plain memory
buffers, without an archive container. Intended for modules that define
their own binary formats (first consumer: the UltraWeb bundler for
.ucpkg payloads). Availability of each method follows the
`VIRTUALFS_USE_ZLIB` / `_ZSTD` / `_LZ4` / `_BROTLI` build options;
unavailable methods return `VirtualFSResult::NotSupported`.

Stream formats: Deflate produces a zlib stream (decompression also
accepts gzip); Zstd and LZ4 use their standard frame formats with
recorded content size (LZ4 output is readable by any LZ4F decoder);
Brotli is a raw stream.

Note: `UCVFSBridge::LZ4Compress` in the UltraCanvas bridge emits the
LZ4 *block* format (no header, caller must track sizes) and is NOT
interchangeable with this API's frame format. Prefer this API for new
code and for any data crossing a process or network boundary.

```cpp
bool VirtualFS_IsCompressionMethodAvailable(VirtualFSCompressionMethod method);
    // Checks whether a method was compiled into this build
    // @param method - Compression method to query
    // @return true if usable for buffer compression (Store always is;
    //         archive-only methods like LZMA/PPMd always return false)

VirtualFSCompressionMethod VirtualFS_DetectCompressionMethod(
    const uint8_t* data, size_t size);
    // Detects the compression method from magic bytes
    // @param data - Buffer start
    // @param size - Buffer size in bytes
    // @return Detected method, or Auto if unknown (Brotli has no magic
    //         bytes and cannot be detected)

VirtualFSResult VirtualFS_CompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method,
    VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal);
    // Compresses a memory buffer
    // @param data - Input buffer start
    // @param size - Input size in bytes
    // @param output - Receives the compressed stream (replaced)
    // @param method - Store, Deflate, Zstd, LZ4 or Brotli; Auto selects
    //                 the best available (Zstd > LZ4 > Deflate > Store)
    // @param level - Compression level preset
    // @return Success; NotSupported if method not compiled in;
    //         InvalidArgument on null/empty input; Error on codec failure
    // Vector overload: VirtualFS_CompressBuffer(input, output, method, level)

VirtualFSResult VirtualFS_DecompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method = VirtualFSCompressionMethod::Auto,
    size_t sizeHint = 0);
    // Decompresses a memory buffer
    // @param data - Input buffer start (compressed stream)
    // @param size - Input size in bytes
    // @param output - Receives the decompressed data (replaced)
    // @param method - Compression method; Auto detects from magic bytes
    // @param sizeHint - Expected decompressed size (0 = unknown); used to
    //                   pre-allocate when the stream does not record it
    // @return Success; NotSupported if method not compiled in;
    //         ArchiveUnsupported if Auto detection fails;
    //         ArchiveCorrupt on a malformed stream
    // Vector overload: VirtualFS_DecompressBuffer(input, output, method, sizeHint)
```

### **Utility Functions**
*Source: VirtualFS.h*

```cpp
std::string VirtualFS_GetErrorMessage(VirtualFSResult result);
    // Returns human-readable error message
    // @param result - Error code

bool VirtualFS_ValidateArchive(const std::string& archivePath);
    // Validates archive integrity
    // @param archivePath - Path to archive
    // @return true if archive is valid

VirtualFSResult VirtualFS_RepairArchive(
    const std::string& archivePath,
    const std::string& outputPath);
    // Attempts to repair corrupted archive
    // @param archivePath - Path to corrupted archive
    // @param outputPath - Path for repaired archive

VirtualFSResult VirtualFS_TestArchive(
    const std::string& archivePath,
    VirtualFSProgressCallback progressCallback = nullptr);
    // Tests archive by extracting to null
    // @param archivePath - Path to archive
    // @return Success if all entries are readable

size_t VirtualFS_GetArchiveSize(const std::string& archivePath);
    // Gets total uncompressed size of archive contents

size_t VirtualFS_GetArchiveEntryCount(const std::string& archivePath);
    // Gets number of entries in archive

std::string VirtualFS_DetectFormat(const std::string& path);
    // Detects archive format by magic bytes
    // @param path - Path to file
    // @return Format name or empty string

std::string VirtualFS_GetMimeType(const std::string& path);
    // Returns MIME type for archive format
```

### **Cache Management**
*Source: VirtualFS.h*

```cpp
void VirtualFS_ClearCache();
    // Clears all cached archive metadata

void VirtualFS_ClearCacheForPath(const std::string& archivePath);
    // Clears cache for specific archive
    // Call after modifying an archive externally

size_t VirtualFS_GetCacheSize();
    // Returns current cache memory usage in bytes

void VirtualFS_SetMaxCacheSize(size_t maxBytes);
    // Sets maximum cache size
    // Default: 64MB

void VirtualFS_SetCacheEnabled(bool enabled);
    // Enables or disables caching
```

### **Configuration**
*Source: VirtualFS.h*

```cpp
void VirtualFS_SetPasswordCallback(VirtualFSPasswordCallback callback);
    // Sets global password callback for encrypted archives

void VirtualFS_SetErrorCallback(VirtualFSErrorCallback callback);
    // Sets global error callback

void VirtualFS_SetTempDirectory(const std::string& path);
    // Sets temporary directory for extraction
    // Default: system temp directory

std::string VirtualFS_GetTempDirectory();
    // Returns current temp directory

void VirtualFS_SetDefaultEncoding(const std::string& encoding);
    // Sets default filename encoding
    // Default: "UTF-8"
```

---

## **PROVIDER INTERFACE**

### **IVirtualFSProvider**
*Source: VirtualFSProvider.h*

```cpp
namespace VirtualFS {

class IVirtualFSProvider {
public:
    virtual ~IVirtualFSProvider() = default;
    
    // ===== IDENTIFICATION =====
    
    virtual std::string GetName() const = 0;
        // Returns provider name (e.g., "ZIP", "7z", "TAR")
    
    virtual std::string GetVersion() const = 0;
        // Returns provider version string
    
    virtual std::string GetDescription() const = 0;
        // Returns human-readable description
    
    virtual std::string GetLibraryInfo() const = 0;
        // Returns underlying library info (e.g., "libarchive 3.6.0")
    
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;
        // Returns list of supported file extensions (without dots)
    
    virtual std::vector<std::string> GetSupportedMimeTypes() const = 0;
        // Returns list of supported MIME types
    
    virtual VirtualFSCapability GetCapabilities() const = 0;
        // Returns provider capabilities bitmask
    
    // ===== DETECTION =====
    
    virtual bool CanHandle(const std::string& path) const = 0;
        // Checks if provider can handle file by extension
        // @param path - File path or just extension
    
    virtual bool CanHandleByMagic(const uint8_t* data, size_t size) const = 0;
        // Checks if provider recognizes file by magic bytes
        // @param data - First bytes of file (usually 16-64 bytes)
        // @param size - Size of data buffer
    
    virtual int GetPriority() const { return 0; }
        // Returns priority for format conflicts (higher = preferred)
    
    // ===== ARCHIVE LIFECYCLE =====
    
    virtual VirtualFSResult Open(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) = 0;
        // Opens archive for reading/writing
    
    virtual void Close() = 0;
        // Closes archive and releases resources
    
    virtual bool IsOpen() const = 0;
        // Returns true if archive is currently open
    
    virtual std::string GetOpenPath() const = 0;
        // Returns path of currently open archive
    
    // ===== DIRECTORY OPERATIONS =====
    
    virtual std::vector<VirtualFSEntry> ListDirectory(
        const std::string& virtualPath = "") = 0;
        // Lists entries in directory inside archive
        // @param virtualPath - Path inside archive (empty for root)
    
    virtual bool Exists(const std::string& virtualPath) = 0;
        // Checks if entry exists in archive
    
    virtual VirtualFSEntry GetInfo(const std::string& virtualPath) = 0;
        // Gets entry information
    
    // ===== FILE READING =====
    
    virtual VirtualFSResult ReadFile(
        const std::string& virtualPath,
        std::vector<uint8_t>& outData) = 0;
        // Reads file from archive into memory
    
    virtual VirtualFSResult ReadFilePartial(
        const std::string& virtualPath,
        size_t offset,
        size_t length,
        std::vector<uint8_t>& outData) {
        // Reads portion of file (optional, may not be supported)
        return VirtualFSResult::NotSupported;
    }
    
    // ===== EXTRACTION =====
    
    virtual VirtualFSResult ExtractFile(
        const std::string& virtualPath,
        const std::string& destPath,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default()) = 0;
        // Extracts single file to filesystem
    
    virtual VirtualFSResult ExtractAll(
        const std::string& destDirectory,
        const VirtualFSExtractOptions& options = VirtualFSExtractOptions::Default(),
        VirtualFSProgressCallback progressCallback = nullptr) = 0;
        // Extracts all files to directory
    
    // ===== WRITE OPERATIONS (Optional) =====
    
    virtual VirtualFSResult CreateArchive(
        const std::string& archivePath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    virtual VirtualFSResult AddFile(
        const std::string& sourcePath,
        const std::string& virtualPath,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    virtual VirtualFSResult AddFromMemory(
        const std::string& virtualPath,
        const std::vector<uint8_t>& data,
        const VirtualFSOpenOptions& options = VirtualFSOpenOptions::Default()) {
        return VirtualFSResult::NotSupported;
    }
    
    virtual VirtualFSResult CreateDirectory(const std::string& virtualPath) {
        return VirtualFSResult::NotSupported;
    }
    
    virtual VirtualFSResult Delete(const std::string& virtualPath) {
        return VirtualFSResult::NotSupported;
    }
    
    virtual VirtualFSResult Rename(
        const std::string& oldPath,
        const std::string& newPath) {
        return VirtualFSResult::NotSupported;
    }
    
    // ===== UTILITY =====
    
    virtual bool Validate() = 0;
        // Validates archive integrity
    
    virtual VirtualFSResult Test(VirtualFSProgressCallback progressCallback = nullptr) {
        // Tests archive by reading all entries
        return VirtualFSResult::NotSupported;
    }
};

} // namespace VirtualFS
```

---

## **STREAM INTERFACE**

### **VirtualFSStream**
*Source: VirtualFSTypes.h*

```cpp
namespace VirtualFS {

class VirtualFSStream {
public:
    virtual ~VirtualFSStream() = default;
    
    // ===== BASIC I/O =====
    
    virtual size_t Read(void* buffer, size_t size) = 0;
        // Reads up to 'size' bytes into buffer
        // @return Actual bytes read
    
    virtual size_t Write(const void* buffer, size_t size) = 0;
        // Writes 'size' bytes from buffer
        // @return Actual bytes written
    
    virtual bool Seek(int64_t offset, int origin = SEEK_SET) = 0;
        // Seeks to position
        // @param origin - SEEK_SET, SEEK_CUR, or SEEK_END
    
    virtual int64_t Tell() const = 0;
        // Returns current position
    
    virtual int64_t GetSize() const = 0;
        // Returns total stream size
    
    // ===== STATE =====
    
    virtual bool IsOpen() const = 0;
    virtual bool IsReadable() const = 0;
    virtual bool IsWritable() const = 0;
    virtual bool IsSeekable() const = 0;
    virtual bool IsEOF() const = 0;
    virtual bool HasError() const = 0;
    virtual std::string GetErrorMessage() const = 0;
    
    // ===== CONTROL =====
    
    virtual void Flush() = 0;
    virtual void Close() = 0;
};

} // namespace VirtualFS
```

---

## **PLUGIN CREATION GUIDE**

### **Creating a Custom Provider**

```cpp
// MyFormatPlugin.h
#pragma once
#include <VirtualFS/VirtualFSProvider.h>

namespace VirtualFS {

class MyFormatProvider : public IVirtualFSProvider {
public:
    MyFormatProvider();
    ~MyFormatProvider() override;
    
    // Identification
    std::string GetName() const override { return "MYFORMAT"; }
    std::string GetVersion() const override { return "1.0.0"; }
    std::string GetDescription() const override { 
        return "My Custom Archive Format"; 
    }
    std::string GetLibraryInfo() const override { 
        return "Custom implementation"; 
    }
    
    std::vector<std::string> GetSupportedExtensions() const override {
        return {"myf", "myformat"};
    }
    
    std::vector<std::string> GetSupportedMimeTypes() const override {
        return {"application/x-myformat"};
    }
    
    VirtualFSCapability GetCapabilities() const override {
        return VirtualFSCapability::Read | 
               VirtualFSCapability::ListDirectory |
               VirtualFSCapability::Streaming;
    }
    
    // Detection
    bool CanHandle(const std::string& path) const override;
    bool CanHandleByMagic(const uint8_t* data, size_t size) const override {
        // Check for "MYF\x01" magic
        return size >= 4 && 
               data[0] == 'M' && data[1] == 'Y' && 
               data[2] == 'F' && data[3] == 0x01;
    }
    
    // Implement required methods...
    VirtualFSResult Open(const std::string& archivePath,
                        const VirtualFSOpenOptions& options) override;
    void Close() override;
    bool IsOpen() const override;
    // ... etc
    
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Factory function
inline std::shared_ptr<IVirtualFSProvider> CreateMyFormatProvider() {
    return std::make_shared<MyFormatProvider>();
}

} // namespace VirtualFS
```

### **Registering the Plugin**

```cpp
#include <VirtualFS/VirtualFS.h>
#include "MyFormatPlugin.h"

int main() {
    // Initialize VirtualFS
    VirtualFS::VirtualFS_Initialize();
    
    // Register custom provider
    VirtualFS::VirtualFS_RegisterProvider(VirtualFS::CreateMyFormatProvider());
    
    // Now .myf files can be browsed like folders!
    auto entries = VirtualFS::VirtualFS_ListDirectory("/path/to/file.myf");
    
    // ...
    
    VirtualFS::VirtualFS_Shutdown();
    return 0;
}
```

---

## **INTEGRATION EXAMPLES**

### **Basic Usage**

```cpp
#include <VirtualFS/VirtualFS.h>
#include <iostream>

int main() {
    using namespace VirtualFS;
    
    // Initialize
    VirtualFS_Initialize();
    
    // List contents of a ZIP file
    auto entries = VirtualFS_ListDirectory("/home/user/archive.zip");
    
    for (const auto& entry : entries) {
        std::cout << (entry.IsDirectory() ? "[DIR] " : "      ")
                  << entry.name << " (" << entry.size << " bytes)\n";
    }
    
    // Read file from inside archive
    std::vector<uint8_t> data;
    auto result = VirtualFS_ReadFile("/home/user/archive.zip/docs/readme.txt", data);
    
    if (result == VirtualFSResult::Success) {
        std::string content(data.begin(), data.end());
        std::cout << "Content:\n" << content << "\n";
    }
    
    // Nested archive support
    // Read from: archive.zip → inner.7z → data.csv
    VirtualFS_ReadFile("/home/user/archive.zip/backup/inner.7z/data.csv", data);
    
    VirtualFS_Shutdown();
    return 0;
}
```

### **Integration with UltraCanvas FileDialog**

```cpp
// In UltraCanvasFileDialog.cpp
#include <VirtualFS/VirtualFS.h>

void UltraCanvasFileDialog::RefreshFileList() {
    directoryList.clear();
    fileList.clear();
    
    // Use VirtualFS for listing - transparently handles archives
    auto entries = VirtualFS::VirtualFS_ListDirectory(currentPath);
    
    for (const auto& entry : entries) {
        if (entry.IsDirectory() || entry.isArchive) {
            // Directories and archives can be navigated into
            directoryList.push_back(entry.name);
        } else {
            // Apply file filter
            if (IsFileMatchingFilter(entry.name)) {
                fileList.push_back(entry.name);
            }
        }
    }
    
    std::sort(directoryList.begin(), directoryList.end());
    std::sort(fileList.begin(), fileList.end());
}

void UltraCanvasFileDialog::NavigateToDirectory(const std::string& dirName) {
    std::string newPath = VirtualFS::VirtualFS_JoinPath(currentPath, dirName);
    
    // VirtualFS handles navigation into archives automatically
    if (VirtualFS::VirtualFS_Exists(newPath) && 
        (VirtualFS::VirtualFS_IsDirectory(newPath) || 
         VirtualFS::VirtualFS_IsArchive(newPath))) {
        
        currentPath = newPath;
        RefreshFileList();
        
        if (onDirectoryChanged) {
            onDirectoryChanged(currentPath);
        }
    }
}
```

### **Password-Protected Archives**

```cpp
#include <VirtualFS/VirtualFS.h>

int main() {
    using namespace VirtualFS;
    
    VirtualFS_Initialize();
    
    // Set global password callback
    VirtualFS_SetPasswordCallback([](const std::string& archivePath, bool isRetry) {
        if (isRetry) {
            std::cout << "Incorrect password. Try again.\n";
        }
        std::cout << "Enter password for " << archivePath << ": ";
        std::string password;
        std::cin >> password;
        return password;
    });
    
    // Or provide password directly
    VirtualFSOpenOptions options;
    options.password = "secret123";
    
    // The password will be used automatically when needed
    auto entries = VirtualFS_ListDirectory("/path/to/encrypted.zip");
    
    VirtualFS_Shutdown();
    return 0;
}
```

### **Extraction with Progress**

```cpp
#include <VirtualFS/VirtualFS.h>
#include <iostream>

int main() {
    using namespace VirtualFS;
    
    VirtualFS_Initialize();
    
    auto progressCallback = [](const VirtualFSProgress& progress) -> bool {
        std::cout << "\r[" << std::fixed << std::setprecision(1) 
                  << progress.percentComplete << "%] "
                  << progress.currentFile << "     " << std::flush;
        
        // Return false to cancel
        return true;
    };
    
    VirtualFSExtractOptions options;
    options.overwriteExisting = true;
    options.preserveTimestamps = true;
    
    auto result = VirtualFS_ExtractAll(
        "/home/user/large-backup.7z",
        "/home/user/extracted/",
        options,
        progressCallback
    );
    
    std::cout << "\n";
    
    if (result == VirtualFSResult::Success) {
        std::cout << "Extraction complete!\n";
    } else {
        std::cout << "Error: " << VirtualFS_GetErrorMessage(result) << "\n";
    }
    
    VirtualFS_Shutdown();
    return 0;
}
```

---

## **DEPENDENCIES**

### **Required**
| Library | Version | Purpose | Package |
|---------|---------|---------|---------|
| C++17 STL | - | Core functionality | Built-in |

### **Optional (Plugin Dependencies)**

| Library | Version | Purpose | Formats | Package (Linux) |
|---------|---------|---------|---------|-----------------|
| libarchive | 3.0+ | Multi-format support | ZIP, 7z, TAR, RAR, ISO, CAB, etc. | `libarchive-dev` |
| zlib | 1.2+ | GZIP/DEFLATE | .gz, .zip | `zlib1g-dev` |
| liblzma | 5.0+ | LZMA/XZ | .xz, .7z | `liblzma-dev` |
| lz4 | 1.9+ | LZ4 compression | .lz4 | `liblz4-dev` |
| zstd | 1.4+ | Zstandard | .zst | `libzstd-dev` |
| brotli | 1.0+ | Brotli | .br | `libbrotli-dev` |
| minizip | 1.0+ | ZIP alternative | .zip | `libminizip-dev` |

### **Platform-Specific**

| Platform | Library | Purpose |
|----------|---------|---------|
| Windows | Cabinet API | .cab (native) |
| macOS | Compression.framework | System compression |
| macOS | xar | .pkg, .xar |

---

## **BUILD CONFIGURATION**

### **CMake Example**

```cmake
# VirtualFS/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(VirtualFS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(VIRTUAFS_USE_LIBARCHIVE "Use libarchive for multi-format support" ON)
option(VIRTUAFS_USE_ZLIB "Use zlib for GZIP support" ON)
option(VIRTUAFS_USE_LZ4 "Use LZ4 for fast compression" OFF)
option(VIRTUAFS_USE_ZSTD "Use Zstandard compression" OFF)
option(VIRTUAFS_BUILD_TESTS "Build unit tests" OFF)

# Find dependencies
if(VIRTUAFS_USE_LIBARCHIVE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
    add_compile_definitions(VIRTUAFS_HAS_LIBARCHIVE)
endif()

if(VIRTUAFS_USE_ZLIB)
    find_package(ZLIB REQUIRED)
    add_compile_definitions(VIRTUAFS_HAS_ZLIB)
endif()

# Sources
set(VIRTUAFS_SOURCES
    core/VirtualFSManager.cpp
    core/VirtualFSPath.cpp
    core/VirtualFSLocalProvider.cpp
    providers/VirtualFSArchiveProvider.cpp
)

if(VIRTUAFS_USE_LIBARCHIVE)
    list(APPEND VIRTUAFS_SOURCES
        providers/plugins/VirtualFSZipPlugin.cpp
        providers/plugins/VirtualFS7zPlugin.cpp
        providers/plugins/VirtualFSTarPlugin.cpp
        providers/plugins/VirtualFSRarPlugin.cpp
        providers/plugins/VirtualFSIsoPlugin.cpp
    )
endif()

# Library
add_library(VirtualFS STATIC ${VIRTUAFS_SOURCES})

target_include_directories(VirtualFS
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${LIBARCHIVE_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(VirtualFS
    PRIVATE
        ${LIBARCHIVE_LIBRARIES}
        ${ZLIB_LIBRARIES}
)

# Install
install(TARGETS VirtualFS EXPORT VirtualFSTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
```

---

## **VERSION HISTORY**

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-01-10 | Initial VirtualFS master registry |

---

## **CRITICAL RULES**

### **Before Creating VirtualFS Functions:**
1. ☐ Check this registry for existing functions
2. ☐ Follow `VirtualFS_[Action]()` naming pattern
3. ☐ Return `VirtualFSResult` for operations that can fail
4. ☐ Use `VirtualFS[TypeName]` for structures
5. ☐ Support both path and options parameters
6. ☐ Handle archives and regular files transparently

### **Plugin Development Rules:**
1. ☐ Implement `IVirtualFSProvider` interface completely
2. ☐ Register all supported extensions
3. ☐ Implement `CanHandleByMagic()` for reliable detection
4. ☐ Handle password-protected archives gracefully
5. ☐ Support progress callbacks for long operations
6. ☐ Clean up resources in destructor and `Close()`
7. ☐ Thread-safety for concurrent access

### **Reserved Function Patterns:**

```cpp
// NEVER CREATE:
✗ OpenArchive()              // Use VirtualFS_ListDirectory or provider->Open
✗ ReadArchiveFile()          // Use VirtualFS_ReadFile
✗ VFS_*()                    // Wrong prefix
✗ virtuafs_*()               // Wrong case

// ALLOWED PATTERNS:
✓ VirtualFS_[Action]()        // Standard function
✓ VirtualFS[TypeName]         // Structure/class
✓ IVirtualFSProvider          // Interface
✓ VirtualFS[Event]Callback    // Callback type
```

---

*This master registry ensures consistent VirtualFS module development across the ULTRA OS framework.*
