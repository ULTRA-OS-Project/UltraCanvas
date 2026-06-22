// Plugins/Documents/eBook/EBookDecompression.h
// Unified eBook Decompression Helper using VirtualFS Bridge
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>

// Include VirtualFS bridge
#include "UltraCanvasVirtualFSBridge.h"

namespace UltraCanvas {

// ============================================================================
// EBOOK COMPRESSION TYPES
// ============================================================================
//
// Maps eBook format-specific compression to VirtualFS compression types:
//
// | Format | Compression | VirtualFS Type |
// |--------|-------------|----------------|
// | EPUB   | ZIP container | Archive access |
// | FB2    | Optional ZIP | Archive access |
// | MOBI   | PalmDOC LZ77 | Custom |
// | AZW3   | PalmDOC LZ77 | Custom |
// | LIT    | LZX | UCVFSCompressionType::LZX |
// | LRF    | ZLIB | UCVFSCompressionType::Deflate |
// | RB     | DEFLATE | UCVFSCompressionType::Deflate |
// | TCR    | ZVR Dictionary | Custom |
//
// ============================================================================

/**
 * @enum EBookCompressionType
 * @brief eBook-specific compression types
 */
enum class EBookCompressionType {
    None = 0,           // No compression (store)
    
    // Standard compression (via VirtualFS)
    Deflate,            // DEFLATE/ZLIB (LRF, RB)
    GZIP,               // GZIP wrapper
    LZX,                // LZX (LIT, CHM)
    
    // Format-specific compression (custom implementations)
    PalmDOC,            // PalmDOC LZ77 (MOBI, AZW)
    HUFFCDIC,           // HUFF/CDIC (MOBI advanced)
    ZVR,                // ZVR Dictionary (TCR)
    
    // Modern compression (for future formats)
    Zstd,               // Zstandard
    LZ4,                // LZ4
    Brotli              // Brotli
};

/**
 * @struct EBookDecompressionOptions
 * @brief Options for decompression operations
 */
struct EBookDecompressionOptions {
    size_t expectedSize = 0;        // Expected uncompressed size (0 = unknown)
    int windowBits = 15;            // Window bits for DEFLATE (-15 for raw)
    uint32_t resetInterval = 0;     // Reset interval for LZX
    bool rawDeflate = false;        // Use raw DEFLATE (no header)
    
    // Factory methods
    static EBookDecompressionOptions ForRawDeflate(size_t expected = 0) {
        EBookDecompressionOptions opts;
        opts.expectedSize = expected;
        opts.windowBits = -15;
        opts.rawDeflate = true;
        return opts;
    }
    
    static EBookDecompressionOptions ForZlib(size_t expected = 0) {
        EBookDecompressionOptions opts;
        opts.expectedSize = expected;
        opts.windowBits = 15;
        opts.rawDeflate = false;
        return opts;
    }
    
    static EBookDecompressionOptions ForLZX(uint32_t windowSize, uint32_t resetInterval) {
        EBookDecompressionOptions opts;
        opts.windowBits = static_cast<int>(windowSize);
        opts.resetInterval = resetInterval;
        return opts;
    }
    
    static EBookDecompressionOptions ForRB() {
        // RB uses DEFLATE with window-bits=13
        EBookDecompressionOptions opts;
        opts.windowBits = 13;
        opts.rawDeflate = true;
        return opts;
    }
};

// ============================================================================
// EBOOK DECOMPRESSION CLASS
// ============================================================================

/**
 * @class EBookDecompression
 * @brief Unified decompression helper for eBook formats
 * 
 * Provides a single interface for all eBook decompression needs,
 * routing to VirtualFS where possible and using custom implementations
 * for format-specific algorithms.
 * 
 * Usage:
 * @code
 * // Decompress DEFLATE data (LRF, RB)
 * std::vector<uint8_t> decompressed;
 * EBookDecompression::Decompress(compressed, decompressed, 
 *     EBookCompressionType::Deflate,
 *     EBookDecompressionOptions::ForRawDeflate(expectedSize));
 * 
 * // Decompress LZX data (LIT)
 * EBookDecompression::Decompress(compressed, decompressed,
 *     EBookCompressionType::LZX,
 *     EBookDecompressionOptions::ForLZX(0x10000, 0x10000));
 * 
 * // Auto-detect and decompress
 * EBookDecompression::DecompressAuto(compressed, decompressed);
 * @endcode
 */
class EBookDecompression {
public:
    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    
    /**
     * @brief Initializes the decompression system (calls VirtualFS init)
     * @return true on success
     */
    static bool Initialize();
    
    /**
     * @brief Shuts down the decompression system
     */
    static void Shutdown();
    
    /**
     * @brief Checks if decompression system is initialized
     * @return true if ready
     */
    static bool IsInitialized();
    
    // =========================================================================
    // GENERIC DECOMPRESSION
    // =========================================================================
    
    /**
     * @brief Decompresses data using specified algorithm
     * @param input Compressed data
     * @param output Decompressed output
     * @param type Compression type
     * @param options Decompression options
     * @return true on success
     */
    static bool Decompress(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        EBookCompressionType type,
        const EBookDecompressionOptions& options = EBookDecompressionOptions());
    
    /**
     * @brief Decompresses with auto-detection
     * @param input Compressed data
     * @param output Decompressed output
     * @return true on success
     */
    static bool DecompressAuto(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    // =========================================================================
    // FORMAT-SPECIFIC DECOMPRESSION
    // =========================================================================
    
    /**
     * @brief Decompresses DEFLATE/ZLIB data (LRF, RB)
     * @param input Compressed data
     * @param output Decompressed output
     * @param options Decompression options
     * @return true on success
     */
    static bool DecompressDeflate(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        const EBookDecompressionOptions& options = EBookDecompressionOptions());
    
    /**
     * @brief Decompresses LZX data (LIT, CHM)
     * @param input Compressed data
     * @param output Decompressed output
     * @param windowSize LZX window size
     * @param resetInterval LZX reset interval
     * @return true on success
     */
    static bool DecompressLZX(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        uint32_t windowSize = 0x10000,
        uint32_t resetInterval = 0x10000);
    
    /**
     * @brief Decompresses PalmDOC LZ77 data (MOBI, AZW)
     * @param input Compressed data
     * @param output Decompressed output
     * @return true on success
     */
    static bool DecompressPalmDOC(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    /**
     * @brief Decompresses ZVR dictionary data (TCR)
     * @param input Compressed data
     * @param output Decompressed output
     * @param dictionary 256-entry dictionary
     * @return true on success
     */
    static bool DecompressZVR(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output,
        const std::vector<std::string>& dictionary);
    
    /**
     * @brief Decompresses RB chunked DEFLATE data
     * @param input Compressed data (with chunk headers)
     * @param output Decompressed output
     * @return true on success
     */
    static bool DecompressRBChunked(
        const std::vector<uint8_t>& input,
        std::vector<uint8_t>& output);
    
    // =========================================================================
    // COMPRESSION DETECTION
    // =========================================================================
    
    /**
     * @brief Detects compression type from data
     * @param data Data to analyze
     * @param size Data size
     * @return Detected compression type
     */
    static EBookCompressionType DetectCompression(
        const uint8_t* data, size_t size);
    
    /**
     * @brief Checks if data is ZLIB compressed
     * @param data Data to check
     * @param size Data size
     * @return true if ZLIB
     */
    static bool IsZlibCompressed(const uint8_t* data, size_t size);
    
    /**
     * @brief Checks if data is GZIP compressed
     * @param data Data to check
     * @param size Data size
     * @return true if GZIP
     */
    static bool IsGzipCompressed(const uint8_t* data, size_t size);
    
    /**
     * @brief Checks if data is LZX compressed
     * @param data Data to check
     * @param size Data size
     * @return true if LZX
     */
    static bool IsLZXCompressed(const uint8_t* data, size_t size);
    
    // =========================================================================
    // ERROR HANDLING
    // =========================================================================
    
    /**
     * @brief Gets the last error message
     * @return Error message
     */
    static std::string GetLastError();
    
private:
    static bool initialized;
    static std::string lastError;
    
    static void SetError(const std::string& error);
    
    // Helper for little-endian reading
    static uint32_t ReadLE32(const uint8_t* data);
};

// ============================================================================
// CONVENIENCE FUNCTIONS
// ============================================================================

/**
 * @brief Decompress eBook data (convenience function)
 */
inline bool DecompressEBookData(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    EBookCompressionType type) {
    return EBookDecompression::Decompress(input, output, type);
}

/**
 * @brief Decompress DEFLATE for eBook (convenience function)
 */
inline bool DecompressEBookDeflate(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    bool rawDeflate = false,
    size_t expectedSize = 0) {
    auto opts = rawDeflate 
        ? EBookDecompressionOptions::ForRawDeflate(expectedSize)
        : EBookDecompressionOptions::ForZlib(expectedSize);
    return EBookDecompression::DecompressDeflate(input, output, opts);
}

/**
 * @brief Decompress LZX for eBook (convenience function)
 */
inline bool DecompressEBookLZX(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    uint32_t windowSize = 0x10000) {
    return EBookDecompression::DecompressLZX(input, output, windowSize);
}

} // namespace UltraCanvas

/*
=============================================================================
USAGE IN EBOOK ENGINES
=============================================================================

### LITEngine (LZX compression)
```cpp
#include "EBookDecompression.h"

std::vector<uint8_t> LITEngine::DecompressLZX(const uint8_t* data, size_t size,
                                               size_t uncompressedSize) {
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    if (EBookDecompression::DecompressLZX(input, output, 0x10000, 0x10000)) {
        return output;
    }
    return std::vector<uint8_t>();
}
```

### LRFEngine (ZLIB compression)
```cpp
#include "EBookDecompression.h"

std::vector<uint8_t> LRFEngine::DecompressData(const uint8_t* data, size_t size) {
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    if (EBookDecompression::DecompressDeflate(input, output, 
            EBookDecompressionOptions::ForZlib())) {
        return output;
    }
    return std::vector<uint8_t>();
}
```

### RBEngine (Chunked DEFLATE)
```cpp
#include "EBookDecompression.h"

std::vector<uint8_t> RBEngine::DecompressDeflate(const uint8_t* data, size_t size) {
    std::vector<uint8_t> input(data, data + size);
    std::vector<uint8_t> output;
    
    if (EBookDecompression::DecompressRBChunked(input, output)) {
        return output;
    }
    return std::vector<uint8_t>();
}
```

=============================================================================
*/
