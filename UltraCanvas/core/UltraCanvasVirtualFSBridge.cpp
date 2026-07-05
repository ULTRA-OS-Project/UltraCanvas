// UltraCanvas/core/UltraCanvasVirtualFSBridge.cpp
// Implementation of VirtualFS bridge for UltraCanvas
// Version: 1.0.0
// Last Modified: 2026-01-10
// Author: UltraCanvas Framework

#include "UltraCanvasVirtualFSBridge.h"

// VirtualFS includes
#include <VirtualFS/VirtualFS.h>

// Standard library
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>

// Compression libraries
#ifdef VIRTUALFS_HAS_ZLIB
#include <zlib.h>
#endif

#ifdef VIRTUALFS_HAS_ZSTD
#include <zstd.h>
#endif

#ifdef VIRTUALFS_HAS_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

// LZX support via libmspack (optional)
#ifdef VIRTUALFS_HAS_LIBMSPACK
#include <mspack.h>
#endif

namespace UltraCanvas {

// Static members
bool UltraCanvasVirtualFSBridge::initialized = false;
std::string UltraCanvasVirtualFSBridge::lastError;

// ============================================================================
// INITIALIZATION
// ============================================================================

bool UltraCanvasVirtualFSBridge::Initialize() {
    if (initialized) {
        return true;
    }
    
    auto result = VirtualFS::VirtualFS_Initialize();
    if (result != VirtualFS::VirtualFSResult::Success) {
        SetError("Failed to initialize VirtualFS: " + 
                 VirtualFS::VirtualFS_GetErrorMessage(result));
        return false;
    }
    
    initialized = true;
    return true;
}

void UltraCanvasVirtualFSBridge::Shutdown() {
    if (initialized) {
        VirtualFS::VirtualFS_Shutdown();
        initialized = false;
    }
}

bool UltraCanvasVirtualFSBridge::IsInitialized() {
    return initialized;
}

std::string UltraCanvasVirtualFSBridge::GetVersion() {
    return VirtualFS::VirtualFS_GetVersion();
}

// ============================================================================
// RAW DATA COMPRESSION
// ============================================================================

bool UltraCanvasVirtualFSBridge::Compress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    UCVFSCompressionType type,
    UCVFSCompressionLevel level) {
    
    UCVFSCompressionOptions options;
    options.type = type;
    options.level = level;
    return CompressWithOptions(input, output, options);
}

bool UltraCanvasVirtualFSBridge::CompressWithOptions(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    const UCVFSCompressionOptions& options) {
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    switch (options.type) {
        case UCVFSCompressionType::None:
            output = input;
            return true;
            
        case UCVFSCompressionType::Deflate:
            return DeflateCompress(input, output, static_cast<int>(options.level));
            
        case UCVFSCompressionType::GZIP:
            return GzipCompress(input, output, static_cast<int>(options.level));
            
        case UCVFSCompressionType::Zstd:
            return ZstdCompress(input, output, static_cast<int>(options.level));
            
        case UCVFSCompressionType::LZ4:
            return LZ4Compress(input, output, options.level >= UCVFSCompressionLevel::Good);
            
        case UCVFSCompressionType::LZMA:
        case UCVFSCompressionType::LZMA2:
        case UCVFSCompressionType::BZip2:
        case UCVFSCompressionType::XZ:
        case UCVFSCompressionType::Brotli:
            // These require creating a temporary archive through VirtualFS
            // For now, fall back to Zstd
            SetError("Compression type not directly available, using Zstd");
            return ZstdCompress(input, output, static_cast<int>(options.level));
            
        default:
            SetError("Unknown compression type");
            return false;
    }
}

bool UltraCanvasVirtualFSBridge::Decompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    UCVFSCompressionType type,
    size_t expectedSize) {
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    switch (type) {
        case UCVFSCompressionType::None:
            output = input;
            return true;
            
        case UCVFSCompressionType::Deflate:
            return DeflateDecompress(input, output, expectedSize);
            
        case UCVFSCompressionType::GZIP:
            return GzipDecompress(input, output);
            
        case UCVFSCompressionType::Zstd:
            return ZstdDecompress(input, output);
            
        case UCVFSCompressionType::LZ4:
            return LZ4Decompress(input, output, expectedSize);
            
        default:
            SetError("Decompression type not supported");
            return false;
    }
}

bool UltraCanvasVirtualFSBridge::DecompressAuto(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
    UCVFSCompressionType detected = DetectCompressionFormat(input);
    
    if (detected == UCVFSCompressionType::None) {
        // Not compressed or unknown format
        output = input;
        return true;
    }
    
    return Decompress(input, output, detected);
}

// ============================================================================
// DEFLATE IMPLEMENTATION
// ============================================================================

bool UltraCanvasVirtualFSBridge::DeflateCompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    int level) {
    
#ifdef VIRTUALFS_HAS_ZLIB
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    
    // Raw DEFLATE (negative windowBits = no header)
    if (deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        SetError("deflateInit2 failed");
        return false;
    }
    
    stream.next_in = const_cast<uint8_t*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    
    // Allocate output buffer
    output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    
    int ret = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    
    if (ret != Z_STREAM_END) {
        SetError("deflate failed: " + std::to_string(ret));
        return false;
    }
    
    output.resize(stream.total_out);
    return true;
#else
    SetError("zlib not available");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::DeflateDecompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    size_t expectedSize) {
    
#ifdef VIRTUALFS_HAS_ZLIB
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    
    // Raw DEFLATE (negative windowBits = no header)
    if (inflateInit2(&stream, -15) != Z_OK) {
        SetError("inflateInit2 failed");
        return false;
    }
    
    stream.next_in = const_cast<uint8_t*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    
    // Estimate output size
    size_t outSize = expectedSize > 0 ? expectedSize : input.size() * 4;
    output.resize(outSize);
    
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    
    int ret;
    while ((ret = inflate(&stream, Z_NO_FLUSH)) == Z_OK || ret == Z_BUF_ERROR) {
        if (stream.avail_out == 0) {
            // Need more output space
            size_t currentSize = output.size();
            output.resize(currentSize * 2);
            stream.next_out = output.data() + stream.total_out;
            stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);
        } else if (ret == Z_BUF_ERROR && stream.avail_in == 0) {
            break;
        }
    }
    
    inflateEnd(&stream);
    
    if (ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR) {
        SetError("inflate failed: " + std::to_string(ret));
        return false;
    }
    
    output.resize(stream.total_out);
    return true;
#else
    SetError("zlib not available");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::GzipCompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    int level) {
    
#ifdef VIRTUALFS_HAS_ZLIB
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    
    // GZIP format (windowBits + 16)
    if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        SetError("deflateInit2 (gzip) failed");
        return false;
    }
    
    stream.next_in = const_cast<uint8_t*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    
    output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    
    int ret = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    
    if (ret != Z_STREAM_END) {
        SetError("deflate (gzip) failed");
        return false;
    }
    
    output.resize(stream.total_out);
    return true;
#else
    SetError("zlib not available");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::GzipDecompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
#ifdef VIRTUALFS_HAS_ZLIB
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    
    // GZIP format (windowBits + 16)
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        SetError("inflateInit2 (gzip) failed");
        return false;
    }
    
    stream.next_in = const_cast<uint8_t*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    
    output.resize(input.size() * 4);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    
    int ret;
    while ((ret = inflate(&stream, Z_NO_FLUSH)) == Z_OK) {
        if (stream.avail_out == 0) {
            size_t currentSize = output.size();
            output.resize(currentSize * 2);
            stream.next_out = output.data() + stream.total_out;
            stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);
        }
    }
    
    inflateEnd(&stream);
    
    if (ret != Z_STREAM_END) {
        SetError("inflate (gzip) failed: " + std::to_string(ret));
        return false;
    }
    
    output.resize(stream.total_out);
    return true;
#else
    SetError("zlib not available");
    return false;
#endif
}

// ============================================================================
// ZSTANDARD IMPLEMENTATION
// ============================================================================

bool UltraCanvasVirtualFSBridge::ZstdCompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    int level) {
    
#ifdef VIRTUALFS_HAS_ZSTD
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    // Clamp level to valid range (1-22)
    level = std::max(1, std::min(22, level));
    
    size_t compressBound = ZSTD_compressBound(input.size());
    output.resize(compressBound);
    
    size_t compressedSize = ZSTD_compress(
        output.data(), output.size(),
        input.data(), input.size(),
        level);
    
    if (ZSTD_isError(compressedSize)) {
        SetError("ZSTD compress failed: " + std::string(ZSTD_getErrorName(compressedSize)));
        return false;
    }
    
    output.resize(compressedSize);
    return true;
#else
    SetError("Zstandard not available");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::ZstdDecompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
#ifdef VIRTUALFS_HAS_ZSTD
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    // Get decompressed size from frame header
    unsigned long long decompressedSize = ZSTD_getFrameContentSize(input.data(), input.size());
    
    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        SetError("ZSTD: not a valid compressed frame");
        return false;
    }
    
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Size unknown, use streaming decompression
        decompressedSize = input.size() * 4; // Initial estimate
    }
    
    output.resize(static_cast<size_t>(decompressedSize));
    
    size_t result = ZSTD_decompress(
        output.data(), output.size(),
        input.data(), input.size());
    
    if (ZSTD_isError(result)) {
        SetError("ZSTD decompress failed: " + std::string(ZSTD_getErrorName(result)));
        return false;
    }
    
    output.resize(result);
    return true;
#else
    SetError("Zstandard not available");
    return false;
#endif
}

// ============================================================================
// LZ4 IMPLEMENTATION
// ============================================================================

bool UltraCanvasVirtualFSBridge::LZ4Compress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    bool highCompression) {
    
#ifdef VIRTUALFS_HAS_LZ4
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    int maxCompressedSize = LZ4_compressBound(static_cast<int>(input.size()));
    output.resize(static_cast<size_t>(maxCompressedSize));
    
    int compressedSize;
    if (highCompression) {
        compressedSize = LZ4_compress_HC(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(input.size()),
            maxCompressedSize,
            LZ4HC_CLEVEL_DEFAULT);
    } else {
        compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(input.size()),
            maxCompressedSize);
    }
    
    if (compressedSize <= 0) {
        SetError("LZ4 compression failed");
        return false;
    }
    
    output.resize(static_cast<size_t>(compressedSize));
    return true;
#else
    SetError("LZ4 not available");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::LZ4Decompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    size_t expectedSize) {
    
#ifdef VIRTUALFS_HAS_LZ4
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    if (expectedSize == 0) {
        SetError("LZ4 decompression requires expected size");
        return false;
    }
    
    output.resize(expectedSize);
    
    int decompressedSize = LZ4_decompress_safe(
        reinterpret_cast<const char*>(input.data()),
        reinterpret_cast<char*>(output.data()),
        static_cast<int>(input.size()),
        static_cast<int>(expectedSize));
    
    if (decompressedSize < 0) {
        SetError("LZ4 decompression failed");
        return false;
    }
    
    output.resize(static_cast<size_t>(decompressedSize));
    return true;
#else
    SetError("LZ4 not available");
    return false;
#endif
}

// ============================================================================
// ARCHIVE OPERATIONS
// ============================================================================

// ============================================================================
// LZX IMPLEMENTATION (Microsoft formats)
// ============================================================================

bool UltraCanvasVirtualFSBridge::LZXDecompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    uint32_t windowSize,
    uint32_t resetInterval) {
    
#ifdef VIRTUALFS_HAS_LIBMSPACK
    // Use libmspack for LZX decompression
    struct mslzx_decompressor* lzxd = mspack_create_lzx_decompressor(nullptr);
    if (!lzxd) {
        SetError("Failed to create LZX decompressor");
        return false;
    }
    
    // Note: Full implementation requires memory-to-memory interface
    // libmspack primarily works with file handles
    // This is a simplified stub - real implementation needs custom I/O
    
    mspack_destroy_lzx_decompressor(lzxd);
    SetError("Direct LZX decompression not yet implemented - use ReadFromCAB/ReadFromCHM");
    return false;
#else
    // Fallback: Try via libarchive CAB support
    SetError("LZX support requires libmspack. Use ReadFromCAB() for CAB files.");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::ReadFromCAB(
    const std::string& cabPath,
    const std::string& entryName,
    std::vector<uint8_t>& output) {
    
    // CAB files are supported via libarchive (which handles LZX internally)
    return ReadFileFromArchive(cabPath, entryName, output);
}

bool UltraCanvasVirtualFSBridge::ReadFromCHM(
    const std::string& chmPath,
    const std::string& entryPath,
    std::vector<uint8_t>& output) {
    
#ifdef VIRTUALFS_HAS_LIBMSPACK
    struct mschm_decompressor* chmd = mspack_create_chm_decompressor(nullptr);
    if (!chmd) {
        SetError("Failed to create CHM decompressor");
        return false;
    }
    
    struct mschmd_header* chm = chmd->open(chmd, chmPath.c_str());
    if (!chm) {
        SetError("Failed to open CHM file: " + chmPath);
        mspack_destroy_chm_decompressor(chmd);
        return false;
    }
    
    // Find the requested file
    struct mschmd_file* file = chm->files;
    while (file) {
        if (entryPath == file->filename) {
            // Found - extract to temp file and read
            std::string tempPath = "/tmp/chm_extract_" + 
                std::to_string(reinterpret_cast<uintptr_t>(&output));
            
            if (chmd->extract(chmd, file, tempPath.c_str()) == MSPACK_ERR_OK) {
                // Read temp file
                std::ifstream tempFile(tempPath, std::ios::binary | std::ios::ate);
                if (tempFile) {
                    size_t size = tempFile.tellg();
                    tempFile.seekg(0);
                    output.resize(size);
                    tempFile.read(reinterpret_cast<char*>(output.data()), size);
                    tempFile.close();
                    std::remove(tempPath.c_str());
                    
                    chmd->close(chmd, chm);
                    mspack_destroy_chm_decompressor(chmd);
                    return true;
                }
            }
            break;
        }
        file = file->next;
    }
    
    chmd->close(chmd, chm);
    mspack_destroy_chm_decompressor(chmd);
    SetError("File not found in CHM: " + entryPath);
    return false;
#else
    SetError("CHM support requires libmspack library");
    return false;
#endif
}

bool UltraCanvasVirtualFSBridge::IsLZXSupported() {
#ifdef VIRTUALFS_HAS_LIBMSPACK
    return true;
#else
    // Basic LZX (CAB) support via libarchive
    return true; // CAB files work, CHM requires libmspack
#endif
}

bool UltraCanvasVirtualFSBridge::ReadFileFromArchive(
    const std::string& archivePath,
    const std::string& entryPath,
    std::vector<uint8_t>& output) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    std::string virtualPath = archivePath + "/" + entryPath;
    auto result = VirtualFS::VirtualFS_ReadFile(virtualPath, output);
    
    if (result != VirtualFS::VirtualFSResult::Success) {
        SetError(VirtualFS::VirtualFS_GetErrorMessage(result));
        return false;
    }
    
    return true;
}

bool UltraCanvasVirtualFSBridge::ReadVirtualFile(
    const std::string& virtualPath,
    std::vector<uint8_t>& output) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    auto result = VirtualFS::VirtualFS_ReadFile(virtualPath, output);
    
    if (result != VirtualFS::VirtualFSResult::Success) {
        SetError(VirtualFS::VirtualFS_GetErrorMessage(result));
        return false;
    }
    
    return true;
}

bool UltraCanvasVirtualFSBridge::ListArchiveContents(
    const std::string& archivePath,
    std::vector<std::string>& entries) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    auto vfsEntries = VirtualFS::VirtualFS_ListDirectory(archivePath);
    
    entries.clear();
    entries.reserve(vfsEntries.size());
    
    for (const auto& entry : vfsEntries) {
        entries.push_back(entry.name);
    }
    
    return true;
}

bool UltraCanvasVirtualFSBridge::ExtractArchive(
    const std::string& archivePath,
    const std::string& destDirectory,
    UCVFSProgressCallback progressCallback) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    VirtualFS::VirtualFSProgressCallback vfsCallback = nullptr;
    
    if (progressCallback) {
        vfsCallback = [&progressCallback](const VirtualFS::VirtualFSProgress& progress) -> bool {
            return progressCallback(
                progress.totalBytes,
                progress.grandTotalBytes,
                progress.currentFile);
        };
    }
    
    auto result = VirtualFS::VirtualFS_ExtractAll(
        archivePath, destDirectory,
        VirtualFS::VirtualFSExtractOptions::Default(),
        vfsCallback);
    
    if (result != VirtualFS::VirtualFSResult::Success) {
        SetError(VirtualFS::VirtualFS_GetErrorMessage(result));
        return false;
    }
    
    return true;
}

bool UltraCanvasVirtualFSBridge::CreateArchive(
    const std::string& archivePath,
    const std::vector<std::string>& sourcePaths,
    const UCVFSCompressionOptions& options,
    UCVFSProgressCallback progressCallback) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    // Map our compression options to VirtualFS options
    VirtualFS::VirtualFSOpenOptions vfsOptions;
    vfsOptions.mode = VirtualFS::VirtualFSOpenMode::Create;
    
    switch (options.level) {
        case UCVFSCompressionLevel::Store:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Store;
            break;
        case UCVFSCompressionLevel::Fastest:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Fastest;
            break;
        case UCVFSCompressionLevel::Fast:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Fast;
            break;
        case UCVFSCompressionLevel::Normal:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Normal;
            break;
        case UCVFSCompressionLevel::Good:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Good;
            break;
        case UCVFSCompressionLevel::Best:
        case UCVFSCompressionLevel::Ultra:
            vfsOptions.compressionLevel = VirtualFS::VirtualFSCompressionLevel::Best;
            break;
    }
    
    VirtualFS::VirtualFSProgressCallback vfsCallback = nullptr;
    if (progressCallback) {
        vfsCallback = [&progressCallback](const VirtualFS::VirtualFSProgress& progress) -> bool {
            return progressCallback(
                progress.totalBytes,
                progress.grandTotalBytes,
                progress.currentFile);
        };
    }
    
    auto result = VirtualFS::VirtualFS_CreateArchive(
        archivePath, sourcePaths, vfsOptions, vfsCallback);
    
    if (result != VirtualFS::VirtualFSResult::Success) {
        SetError(VirtualFS::VirtualFS_GetErrorMessage(result));
        return false;
    }
    
    return true;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

bool UltraCanvasVirtualFSBridge::IsArchiveFormat(const std::string& path) {
    if (!initialized && !Initialize()) {
        return false;
    }
    
    return VirtualFS::VirtualFS_IsArchive(path);
}

UCVFSCompressionType UltraCanvasVirtualFSBridge::DetectCompressionFormat(
    const std::vector<uint8_t>& data) {
    
    if (data.size() < 4) {
        return UCVFSCompressionType::None;
    }
    
    // GZIP: 1F 8B
    if (data[0] == 0x1F && data[1] == 0x8B) {
        return UCVFSCompressionType::GZIP;
    }
    
    // Zstandard: 28 B5 2F FD
    if (data.size() >= 4 &&
        data[0] == 0x28 && data[1] == 0xB5 &&
        data[2] == 0x2F && data[3] == 0xFD) {
        return UCVFSCompressionType::Zstd;
    }
    
    // LZ4: 04 22 4D 18
    if (data.size() >= 4 &&
        data[0] == 0x04 && data[1] == 0x22 &&
        data[2] == 0x4D && data[3] == 0x18) {
        return UCVFSCompressionType::LZ4;
    }
    
    // XZ: FD 37 7A 58 5A 00
    if (data.size() >= 6 &&
        data[0] == 0xFD && data[1] == 0x37 && data[2] == 0x7A &&
        data[3] == 0x58 && data[4] == 0x5A && data[5] == 0x00) {
        return UCVFSCompressionType::XZ;
    }
    
    // BZIP2: BZ
    if (data[0] == 0x42 && data[1] == 0x5A) {
        return UCVFSCompressionType::BZip2;
    }
    
    // Raw DEFLATE is harder to detect - no magic number
    // Would need to try decompressing
    
    return UCVFSCompressionType::None;
}

float UltraCanvasVirtualFSBridge::GetCompressionRatio(
    size_t originalSize, size_t compressedSize) {
    
    if (originalSize == 0) return 0.0f;
    return 100.0f * (1.0f - static_cast<float>(compressedSize) / 
                            static_cast<float>(originalSize));
}

UCVFSCompressionType UltraCanvasVirtualFSBridge::GetRecommendedCompression(
    size_t dataSize, bool prioritizeSpeed) {
    
    if (prioritizeSpeed) {
        // LZ4 for maximum speed
        return UCVFSCompressionType::LZ4;
    }
    
    if (dataSize < 1024) {
        // Small data: Deflate is good enough
        return UCVFSCompressionType::Deflate;
    }
    
    // Default: Zstandard (best balance)
    return UCVFSCompressionType::Zstd;
}

std::vector<std::string> UltraCanvasVirtualFSBridge::GetSupportedAlgorithms() {
    std::vector<std::string> algorithms;
    
    algorithms.push_back("None");
    
#ifdef VIRTUALFS_HAS_ZLIB
    algorithms.push_back("Deflate");
    algorithms.push_back("GZIP");
#endif

#ifdef VIRTUALFS_HAS_ZSTD
    algorithms.push_back("Zstandard");
#endif

#ifdef VIRTUALFS_HAS_LZ4
    algorithms.push_back("LZ4");
#endif
    
    return algorithms;
}

std::string UltraCanvasVirtualFSBridge::CompressionTypeToString(UCVFSCompressionType type) {
    switch (type) {
        case UCVFSCompressionType::None: return "None";
        case UCVFSCompressionType::Deflate: return "Deflate";
        case UCVFSCompressionType::GZIP: return "GZIP";
        case UCVFSCompressionType::LZMA: return "LZMA";
        case UCVFSCompressionType::LZMA2: return "LZMA2";
        case UCVFSCompressionType::BZip2: return "BZip2";
        case UCVFSCompressionType::XZ: return "XZ";
        case UCVFSCompressionType::Zstd: return "Zstandard";
        case UCVFSCompressionType::LZ4: return "LZ4";
        case UCVFSCompressionType::Brotli: return "Brotli";
        case UCVFSCompressionType::LZX: return "LZX";
        case UCVFSCompressionType::MSZip: return "MS-ZIP";
        case UCVFSCompressionType::Quantum: return "Quantum";
        default: return "Unknown";
    }
}

std::string UltraCanvasVirtualFSBridge::GetLastError() {
    return lastError;
}

void UltraCanvasVirtualFSBridge::SetError(const std::string& error) {
    lastError = error;
}

} // namespace UltraCanvas
