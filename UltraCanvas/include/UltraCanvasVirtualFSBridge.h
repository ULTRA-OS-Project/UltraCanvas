// UltraCanvas/include/UltraCanvasVirtualFSBridge.h
// Bridge module connecting UltraCanvas to VirtualFS for compression and archive operations
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: UltraCanvas Framework
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

// Forward declaration - avoid circular dependency
namespace VirtualFS {
    class VirtualFSManager;
    enum class VirtualFSResult;
    enum class VirtualFSCompressionLevel;
    enum class VirtualFSCompressionMethod;
    struct VirtualFSEntry;
    struct VirtualFSProgress;
}

namespace UltraCanvas {

// ============================================================================
// COMPRESSION TYPES (Extended from existing UltraCanvas types)
// ============================================================================

/**
 * @enum UCVFSCompressionType
 * @brief Extended compression types available through VirtualFS
 * 
 * Extends the existing UCCompressionType and UCACompressionType enums
 * with additional algorithms available through VirtualFS/libarchive
 */
enum class UCVFSCompressionType {
    None = 0,           // No compression (store)
    
    // Standard compression (existing UltraCanvas support)
    Deflate = 1,        // DEFLATE (ZIP/GZIP default) - zlib
    GZIP = 2,           // GZIP wrapper around DEFLATE
    
    // Extended compression via VirtualFS
    LZMA = 10,          // LZMA (7-Zip)
    LZMA2 = 11,         // LZMA2 (7-Zip default)
    BZip2 = 12,         // BZIP2
    XZ = 13,            // XZ (LZMA2 container)
    
    // Modern fast compression
    Zstd = 20,          // Zstandard (recommended default)
    LZ4 = 21,           // LZ4 (fastest)
    Brotli = 22,        // Brotli (web-optimized)
    
    // Microsoft compression (via libmspack/wimlib)
    LZX = 30,           // LZX (CAB, CHM, WIM, LIT)
    MSZip = 31,         // MS-ZIP (CAB variant)
    Quantum = 32        // Quantum (older CAB)
};

/**
 * @enum UCVFSCompressionLevel
 * @brief Compression level presets
 */
enum class UCVFSCompressionLevel {
    Store = 0,          // No compression
    Fastest = 1,        // Fastest (lowest ratio)
    Fast = 3,           // Fast
    Normal = 5,         // Balanced (default)
    Good = 7,           // Better ratio
    Best = 9,           // Maximum compression
    Ultra = 10          // Ultra (slowest, best ratio)
};

// ============================================================================
// PROGRESS CALLBACK
// ============================================================================

/**
 * @brief Progress callback for compression/decompression operations
 * @param currentBytes Bytes processed so far
 * @param totalBytes Total bytes to process (0 if unknown)
 * @param currentFile Current file being processed (for archives)
 * @return true to continue, false to cancel
 */
using UCVFSProgressCallback = std::function<bool(
    uint64_t currentBytes, 
    uint64_t totalBytes,
    const std::string& currentFile)>;

// ============================================================================
// COMPRESSION OPTIONS
// ============================================================================

/**
 * @struct UCVFSCompressionOptions
 * @brief Options for compression operations
 */
struct UCVFSCompressionOptions {
    UCVFSCompressionType type = UCVFSCompressionType::Zstd;
    UCVFSCompressionLevel level = UCVFSCompressionLevel::Normal;
    bool includeHeader = false;     // Include format header (e.g., gzip header)
    int windowBits = 15;            // For DEFLATE/GZIP (8-15)
    int memLevel = 8;               // Memory usage level (1-9)
    
    // Factory methods
    static UCVFSCompressionOptions Default() {
        return UCVFSCompressionOptions();
    }
    
    static UCVFSCompressionOptions Fast() {
        UCVFSCompressionOptions opts;
        opts.type = UCVFSCompressionType::LZ4;
        opts.level = UCVFSCompressionLevel::Fastest;
        return opts;
    }
    
    static UCVFSCompressionOptions Maximum() {
        UCVFSCompressionOptions opts;
        opts.type = UCVFSCompressionType::LZMA2;
        opts.level = UCVFSCompressionLevel::Best;
        return opts;
    }
    
    static UCVFSCompressionOptions ForZip() {
        UCVFSCompressionOptions opts;
        opts.type = UCVFSCompressionType::Deflate;
        opts.level = UCVFSCompressionLevel::Normal;
        return opts;
    }
    
    static UCVFSCompressionOptions ForWeb() {
        UCVFSCompressionOptions opts;
        opts.type = UCVFSCompressionType::Brotli;
        opts.level = UCVFSCompressionLevel::Good;
        return opts;
    }
};

// ============================================================================
// MAIN BRIDGE CLASS
// ============================================================================

/**
 * @class UltraCanvasVirtualFSBridge
 * @brief Bridge class connecting UltraCanvas to VirtualFS functionality
 * 
 * Provides:
 * - Data compression/decompression using VirtualFS-supported algorithms
 * - Archive creation and extraction
 * - File reading from archives (transparent access)
 * - Integration with UltraCanvas file dialogs
 * 
 * Usage:
 * @code
 * using namespace UltraCanvas;
 * 
 * // Compress data
 * std::vector<uint8_t> data = LoadSomeData();
 * std::vector<uint8_t> compressed;
 * UCVFSBridge::Compress(data, compressed, UCVFSCompressionType::Zstd);
 * 
 * // Decompress data
 * std::vector<uint8_t> decompressed;
 * UCVFSBridge::Decompress(compressed, decompressed, UCVFSCompressionType::Zstd);
 * 
 * // Read file from archive
 * std::vector<uint8_t> fileData;
 * UCVFSBridge::ReadFileFromArchive("/path/to/archive.zip", "folder/file.txt", fileData);
 * @endcode
 */
class UltraCanvasVirtualFSBridge {
public:
    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    
    /**
     * @brief Initializes the VirtualFS bridge
     * @return true on success
     */
    static bool Initialize();
    
    /**
     * @brief Shuts down the VirtualFS bridge
     */
    static void Shutdown();
    
    /**
     * @brief Checks if bridge is initialized
     * @return true if initialized
     */
    static bool IsInitialized();
    
    /**
     * @brief Gets VirtualFS version string
     * @return Version string
     */
    static std::string GetVersion();
    
    // =========================================================================
    // RAW DATA COMPRESSION
    // =========================================================================
    
    /**
     * @brief Compresses raw data using specified algorithm
     * @param input Input data to compress
     * @param output Output compressed data
     * @param type Compression algorithm
     * @param level Compression level
     * @return true on success
     */
    static bool Compress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        UCVFSCompressionType type = UCVFSCompressionType::Zstd,
        UCVFSCompressionLevel level = UCVFSCompressionLevel::Normal);
    
    /**
     * @brief Compresses raw data with full options
     * @param input Input data
     * @param output Output data
     * @param options Compression options
     * @return true on success
     */
    static bool CompressWithOptions(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        const UCVFSCompressionOptions& options);
    
    /**
     * @brief Decompresses data using specified algorithm
     * @param input Compressed data
     * @param output Decompressed output
     * @param type Compression algorithm used
     * @param expectedSize Expected output size (0 = unknown)
     * @return true on success
     */
    static bool Decompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        UCVFSCompressionType type,
        size_t expectedSize = 0);
    
    /**
     * @brief Auto-detects compression format and decompresses
     * @param input Compressed data
     * @param output Decompressed output
     * @return true on success
     */
    static bool DecompressAuto(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    // =========================================================================
    // DEFLATE-SPECIFIC (Compatibility with existing UltraCanvas code)
    // =========================================================================
    
    /**
     * @brief Compresses using DEFLATE (raw, no header)
     * @param input Input data
     * @param output Output data
     * @param level Compression level (1-9)
     * @return true on success
     * 
     * Compatible with existing zlib usage in UltraCanvas
     */
    static bool DeflateCompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        int level = 6);
    
    /**
     * @brief Decompresses DEFLATE data (raw, no header)
     * @param input Compressed data
     * @param output Output data
     * @param expectedSize Expected output size (0 = auto-grow)
     * @return true on success
     */
    static bool DeflateDecompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        size_t expectedSize = 0);
    
    /**
     * @brief Compresses using GZIP format (with header)
     * @param input Input data
     * @param output Output data
     * @param level Compression level
     * @return true on success
     */
    static bool GzipCompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        int level = 6);
    
    /**
     * @brief Decompresses GZIP data
     * @param input Compressed data
     * @param output Output data
     * @return true on success
     */
    static bool GzipDecompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    // =========================================================================
    // ZSTANDARD (Recommended for new code)
    // =========================================================================
    
    /**
     * @brief Compresses using Zstandard
     * @param input Input data
     * @param output Output data
     * @param level Compression level (1-22, default 3)
     * @return true on success
     * 
     * Zstandard provides the best speed-to-ratio balance and is recommended
     * for new code. Decompression is always fast regardless of compression level.
     */
    static bool ZstdCompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        int level = 3);
    
    /**
     * @brief Decompresses Zstandard data
     * @param input Compressed data
     * @param output Output data
     * @return true on success
     */
    static bool ZstdDecompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    // =========================================================================
    // LZ4 (For real-time/streaming)
    // =========================================================================
    
    /**
     * @brief Compresses using LZ4 (fastest)
     * @param input Input data
     * @param output Output data
     * @param highCompression Use HC mode for better ratio
     * @return true on success
     */
    static bool LZ4Compress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        bool highCompression = false);
    
    /**
     * @brief Decompresses LZ4 data
     * @param input Compressed data
     * @param output Output data
     * @param expectedSize Expected output size (required for LZ4)
     * @return true on success
     */
    static bool LZ4Decompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        size_t expectedSize);
    
    // =========================================================================
    // LZX (Microsoft formats - CAB, CHM, WIM)
    // =========================================================================
    
    /**
     * @brief Decompresses LZX data (used in CAB, CHM, WIM formats)
     * @param input Compressed data
     * @param output Output data
     * @param windowSize LZX window size (32KB to 2MB, power of 2)
     * @param resetInterval Reset interval for streaming (0 = no reset)
     * @return true on success
     * 
     * LZX is Microsoft's proprietary compression used in:
     * - CAB files (cabinet archives)
     * - CHM files (compiled HTML help)
     * - WIM files (Windows imaging format)
     * - LIT files (Microsoft Reader eBooks)
     * 
     * Note: LZX compression (not just decompression) requires libmspack
     */
    static bool LZXDecompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        uint32_t windowSize = 32768,
        uint32_t resetInterval = 0);
    
    /**
     * @brief Reads file from CAB archive (LZX compressed)
     * @param cabPath Path to CAB file
     * @param entryName File name inside CAB
     * @param output Output data
     * @return true on success
     * 
     * CAB files typically use LZX compression with window sizes up to 2MB
     */
    static bool ReadFromCAB(
        const std::string& cabPath,
        const std::string& entryName,
        std::vector<uint8_t>& output);
    
    /**
     * @brief Reads file from CHM archive
     * @param chmPath Path to CHM file
     * @param entryPath Path inside CHM (e.g., "/topic.htm")
     * @param output Output data
     * @return true on success
     * 
     * CHM (Compiled HTML Help) files use LZX compression
     * Requires libmspack for full support
     */
    static bool ReadFromCHM(
        const std::string& chmPath,
        const std::string& entryPath,
        std::vector<uint8_t>& output);
    
    /**
     * @brief Checks if LZX support is available
     * @return true if libmspack/wimlib is linked
     */
    static bool IsLZXSupported();
    
    // =========================================================================
    // ARCHIVE OPERATIONS (via VirtualFS)
    // =========================================================================
    
    /**
     * @brief Reads a file from inside an archive
     * @param archivePath Path to archive file
     * @param entryPath Path inside archive
     * @param output Output file data
     * @return true on success
     */
    static bool ReadFileFromArchive(
        const std::string& archivePath,
        const std::string& entryPath,
        std::vector<uint8_t>& output);
    
    /**
     * @brief Reads a file using virtual path (transparently handles archives)
     * @param virtualPath Virtual path (e.g., "/path/archive.zip/folder/file.txt")
     * @param output Output file data
     * @return true on success
     */
    static bool ReadVirtualFile(
        const std::string& virtualPath,
        std::vector<uint8_t>& output);
    
    /**
     * @brief Lists contents of an archive
     * @param archivePath Path to archive
     * @param entries Output entry names
     * @return true on success
     */
    static bool ListArchiveContents(
        const std::string& archivePath,
        std::vector<std::string>& entries);
    
    /**
     * @brief Extracts entire archive to directory
     * @param archivePath Path to archive
     * @param destDirectory Destination directory
     * @param progressCallback Progress callback (optional)
     * @return true on success
     */
    static bool ExtractArchive(
        const std::string& archivePath,
        const std::string& destDirectory,
        UCVFSProgressCallback progressCallback = nullptr);
    
    /**
     * @brief Creates a new archive from files
     * @param archivePath Output archive path
     * @param sourcePaths Files/directories to include
     * @param options Compression options
     * @param progressCallback Progress callback (optional)
     * @return true on success
     */
    static bool CreateArchive(
        const std::string& archivePath,
        const std::vector<std::string>& sourcePaths,
        const UCVFSCompressionOptions& options = UCVFSCompressionOptions::Default(),
        UCVFSProgressCallback progressCallback = nullptr);
    
    // =========================================================================
    // UTILITY
    // =========================================================================
    
    /**
     * @brief Checks if path is a supported archive format
     * @param path File path
     * @return true if archive format
     */
    static bool IsArchiveFormat(const std::string& path);
    
    /**
     * @brief Detects compression format from data header
     * @param data Data to check (first 16+ bytes)
     * @return Detected compression type (None if unknown)
     */
    static UCVFSCompressionType DetectCompressionFormat(const std::vector<uint8_t>& data);
    
    /**
     * @brief Gets compression ratio for data
     * @param originalSize Original data size
     * @param compressedSize Compressed size
     * @return Compression ratio as percentage (0-100)
     */
    static float GetCompressionRatio(size_t originalSize, size_t compressedSize);
    
    /**
     * @brief Gets recommended compression type for data
     * @param dataSize Size of data to compress
     * @param prioritizeSpeed Prefer speed over ratio
     * @return Recommended compression type
     */
    static UCVFSCompressionType GetRecommendedCompression(
        size_t dataSize, 
        bool prioritizeSpeed = false);
    
    /**
     * @brief Gets list of supported compression algorithms
     * @return Vector of compression type names
     */
    static std::vector<std::string> GetSupportedAlgorithms();
    
    /**
     * @brief Converts compression type to string name
     * @param type Compression type
     * @return Algorithm name
     */
    static std::string CompressionTypeToString(UCVFSCompressionType type);
    
    /**
     * @brief Gets last error message
     * @return Error message or empty string
     */
    static std::string GetLastError();

private:
    static bool initialized;
    static std::string lastError;
    
    // Internal helpers
    static void SetError(const std::string& error);
};

// Convenience alias
using UCVFSBridge = UltraCanvasVirtualFSBridge;

// ============================================================================
// LEGACY COMPATIBILITY FUNCTIONS
// ============================================================================

/**
 * These functions provide drop-in replacements for existing UltraCanvas
 * compression code using zlib directly.
 */

/**
 * @brief Drop-in replacement for existing CompressWithZlib
 * @param input Input data
 * @param output Output data  
 * @param useGzip Use GZIP header (true) or raw DEFLATE (false)
 * @return true on success
 */
inline bool CompressWithZlib(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    bool useGzip = false) {
    if (useGzip) {
        return UCVFSBridge::GzipCompress(input, output);
    }
    return UCVFSBridge::DeflateCompress(input, output);
}

/**
 * @brief Drop-in replacement for existing DecompressWithZlib
 * @param input Compressed data
 * @param output Output data
 * @param isGzip Data is GZIP format
 * @return true on success
 */
inline bool DecompressWithZlib(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    bool isGzip = false) {
    if (isGzip) {
        return UCVFSBridge::GzipDecompress(input, output);
    }
    return UCVFSBridge::DeflateDecompress(input, output);
}

} // namespace UltraCanvas
