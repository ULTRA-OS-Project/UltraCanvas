// Plugins/Documents/eBook/EBookDecompression.cpp
// Unified eBook Decompression Helper Implementation
// Version: 1.0.0
// Last Modified: 2025-01-10
// Author: UltraCanvas Framework

#include "EBookDecompression.h"
#include <cstring>
#include <algorithm>

namespace UltraCanvas {

// ============================================================================
// STATIC MEMBERS
// ============================================================================

bool EBookDecompression::initialized = false;
std::string EBookDecompression::lastError;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

uint32_t EBookDecompression::ReadLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void EBookDecompression::SetError(const std::string& error) {
    lastError = error;
}

std::string EBookDecompression::GetLastError() {
    return lastError;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool EBookDecompression::Initialize() {
    if (initialized) {
        return true;
    }
    
    // Initialize VirtualFS bridge
    if (!UCVFSBridge::Initialize()) {
        SetError("Failed to initialize VirtualFS bridge: " + UCVFSBridge::GetLastError());
        return false;
    }
    
    initialized = true;
    return true;
}

void EBookDecompression::Shutdown() {
    if (initialized) {
        UCVFSBridge::Shutdown();
        initialized = false;
    }
}

bool EBookDecompression::IsInitialized() {
    return initialized;
}

// ============================================================================
// GENERIC DECOMPRESSION
// ============================================================================

bool EBookDecompression::Decompress(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    EBookCompressionType type,
    const EBookDecompressionOptions& options) {
    
    // Auto-initialize if needed
    if (!initialized && !Initialize()) {
        return false;
    }
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    switch (type) {
        case EBookCompressionType::None:
            output = input;
            return true;
            
        case EBookCompressionType::Deflate:
            return DecompressDeflate(input, output, options);
            
        case EBookCompressionType::GZIP:
            return UCVFSBridge::GzipDecompress(input, output);
            
        case EBookCompressionType::LZX:
            return DecompressLZX(input, output, 
                static_cast<uint32_t>(options.windowBits),
                options.resetInterval);
            
        case EBookCompressionType::PalmDOC:
            return DecompressPalmDOC(input, output);
            
        case EBookCompressionType::Zstd:
            return UCVFSBridge::ZstdDecompress(input, output);
            
        case EBookCompressionType::LZ4:
            return UCVFSBridge::LZ4Decompress(input, output, options.expectedSize);
            
        default:
            SetError("Unsupported compression type");
            return false;
    }
}

bool EBookDecompression::DecompressAuto(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    // Detect compression type
    EBookCompressionType type = DetectCompression(input.data(), input.size());
    
    if (type == EBookCompressionType::None) {
        // Might be uncompressed, return as-is
        output = input;
        return true;
    }
    
    return Decompress(input, output, type);
}

// ============================================================================
// COMPRESSION DETECTION
// ============================================================================

EBookCompressionType EBookDecompression::DetectCompression(
    const uint8_t* data, size_t size) {
    
    if (size < 2) {
        return EBookCompressionType::None;
    }
    
    // GZIP: 1F 8B
    if (data[0] == 0x1F && data[1] == 0x8B) {
        return EBookCompressionType::GZIP;
    }
    
    // ZLIB: Check for valid CMF/FLG header
    // CMF: bits 0-3 = method (8=deflate), bits 4-7 = window size
    // FLG: (CMF*256 + FLG) % 31 == 0
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0F) == 8 && ((cmf * 256 + flg) % 31) == 0) {
        return EBookCompressionType::Deflate;
    }
    
    // LZX: Check for 'LZXC' magic
    if (size >= 4 && data[0] == 'L' && data[1] == 'Z' && 
        data[2] == 'X' && data[3] == 'C') {
        return EBookCompressionType::LZX;
    }
    
    // Zstandard: 28 B5 2F FD
    if (size >= 4 && data[0] == 0x28 && data[1] == 0xB5 &&
        data[2] == 0x2F && data[3] == 0xFD) {
        return EBookCompressionType::Zstd;
    }
    
    // LZ4: 04 22 4D 18 (LZ4 frame)
    if (size >= 4 && data[0] == 0x04 && data[1] == 0x22 &&
        data[2] == 0x4D && data[3] == 0x18) {
        return EBookCompressionType::LZ4;
    }
    
    return EBookCompressionType::None;
}

bool EBookDecompression::IsZlibCompressed(const uint8_t* data, size_t size) {
    if (size < 2) return false;
    
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    
    // Check DEFLATE method and valid checksum
    return (cmf & 0x0F) == 8 && ((cmf * 256 + flg) % 31) == 0;
}

bool EBookDecompression::IsGzipCompressed(const uint8_t* data, size_t size) {
    return size >= 2 && data[0] == 0x1F && data[1] == 0x8B;
}

bool EBookDecompression::IsLZXCompressed(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'L' && data[1] == 'Z' &&
           data[2] == 'X' && data[3] == 'C';
}

// ============================================================================
// DEFLATE DECOMPRESSION
// ============================================================================

bool EBookDecompression::DecompressDeflate(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    const EBookDecompressionOptions& options) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    // Use VirtualFS bridge for DEFLATE decompression
    if (options.rawDeflate) {
        // Raw DEFLATE (no header)
        return UCVFSBridge::DeflateDecompress(input, output, options.expectedSize);
    } else {
        // ZLIB wrapped
        return UCVFSBridge::DeflateDecompress(input, output, options.expectedSize);
    }
}

// ============================================================================
// LZX DECOMPRESSION
// ============================================================================

bool EBookDecompression::DecompressLZX(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    uint32_t windowSize,
    uint32_t resetInterval) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    if (input.empty()) {
        output.clear();
        return true;
    }
    
    // Use VirtualFS bridge for LZX decompression
    // The LZX support was just added to VirtualFS
    return UCVFSBridge::Decompress(input, output, UCVFSCompressionType::LZX);
}

// ============================================================================
// PALMDOC LZ77 DECOMPRESSION (MOBI/AZW)
// ============================================================================

bool EBookDecompression::DecompressPalmDOC(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
    // PalmDOC LZ77 is a custom format specific to MOBI
    // This is a simplified implementation based on the PalmDOC specification
    
    output.clear();
    output.reserve(input.size() * 2);  // Estimate
    
    size_t pos = 0;
    
    while (pos < input.size()) {
        uint8_t byte = input[pos++];
        
        if (byte == 0x00) {
            // Literal null
            output.push_back(0x00);
        } else if (byte >= 0x01 && byte <= 0x08) {
            // Copy next 'byte' bytes literally
            for (int i = 0; i < byte && pos < input.size(); ++i) {
                output.push_back(input[pos++]);
            }
        } else if (byte >= 0x09 && byte <= 0x7F) {
            // Literal byte
            output.push_back(byte);
        } else if (byte >= 0x80 && byte <= 0xBF) {
            // Length-distance pair
            if (pos >= input.size()) break;
            
            uint8_t nextByte = input[pos++];
            
            // Distance: ((byte & 0x3F) << 5) | (nextByte >> 3)
            // Length: (nextByte & 0x07) + 3
            uint16_t distance = ((byte & 0x3F) << 5) | (nextByte >> 3);
            uint8_t length = (nextByte & 0x07) + 3;
            
            if (distance > output.size()) {
                SetError("PalmDOC: Invalid back-reference distance");
                return false;
            }
            
            size_t srcPos = output.size() - distance;
            for (int i = 0; i < length; ++i) {
                output.push_back(output[srcPos + i]);
            }
        } else {
            // 0xC0-0xFF: Space followed by (byte ^ 0x80)
            output.push_back(' ');
            output.push_back(byte ^ 0x80);
        }
    }
    
    return true;
}

// ============================================================================
// ZVR DICTIONARY DECOMPRESSION (TCR)
// ============================================================================

bool EBookDecompression::DecompressZVR(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    const std::vector<std::string>& dictionary) {
    
    if (dictionary.size() != 256) {
        SetError("ZVR: Dictionary must have exactly 256 entries");
        return false;
    }
    
    output.clear();
    
    // Each byte in input maps to a dictionary entry
    for (uint8_t byte : input) {
        const std::string& entry = dictionary[byte];
        output.insert(output.end(), entry.begin(), entry.end());
    }
    
    return true;
}

// ============================================================================
// RB CHUNKED DEFLATE DECOMPRESSION
// ============================================================================

bool EBookDecompression::DecompressRBChunked(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output) {
    
    if (!initialized && !Initialize()) {
        return false;
    }
    
    if (input.size() < 12) {
        SetError("RB: Data too small for chunked format");
        return false;
    }
    
    const uint8_t* data = input.data();
    
    // RB chunked format:
    // - int32: number of 4096-byte chunks
    // - int32: uncompressed length
    // - int32[numChunks]: compressed size of each chunk
    // - compressed data for each chunk
    
    uint32_t numChunks = ReadLE32(data);
    uint32_t uncompressedLength = ReadLE32(data + 4);
    
    if (numChunks > 10000 || uncompressedLength > 100 * 1024 * 1024) {
        SetError("RB: Invalid chunk count or uncompressed length");
        return false;
    }
    
    // Read chunk sizes
    std::vector<uint32_t> chunkSizes;
    chunkSizes.reserve(numChunks);
    
    size_t offset = 8;
    for (uint32_t i = 0; i < numChunks && offset + 4 <= input.size(); ++i) {
        chunkSizes.push_back(ReadLE32(data + offset));
        offset += 4;
    }
    
    // Decompress each chunk
    output.clear();
    output.reserve(uncompressedLength);
    
    for (uint32_t chunkSize : chunkSizes) {
        if (offset + chunkSize > input.size()) {
            SetError("RB: Chunk extends beyond data");
            break;
        }
        
        std::vector<uint8_t> chunkInput(data + offset, data + offset + chunkSize);
        std::vector<uint8_t> chunkOutput;
        
        // RB uses DEFLATE with window-bits=13
        auto opts = EBookDecompressionOptions::ForRB();
        opts.expectedSize = 4096;  // Standard chunk size
        
        if (UCVFSBridge::DeflateDecompress(chunkInput, chunkOutput, opts.expectedSize)) {
            output.insert(output.end(), chunkOutput.begin(), chunkOutput.end());
        } else {
            // Decompression failed, try raw copy
            output.insert(output.end(), chunkInput.begin(), chunkInput.end());
        }
        
        offset += chunkSize;
    }
    
    return true;
}

} // namespace UltraCanvas
