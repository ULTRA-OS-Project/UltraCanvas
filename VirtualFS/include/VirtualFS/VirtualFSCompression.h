// VirtualFS/include/VirtualFS/VirtualFSCompression.h
// Raw buffer compression/decompression API
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: ULTRA OS Framework
#pragma once

/**
 * @file VirtualFSCompression.h
 * @brief Raw buffer compression - compress/decompress memory buffers directly
 *
 * Unlike the archive-oriented VirtualFS API (CreateArchive / AddFromMemory /
 * ExtractToMemory), these functions operate on plain byte buffers with no
 * container format around them. They are intended for modules that define
 * their own binary formats and only need a compression primitive - e.g. the
 * UltraWeb bundler compressing .ucpkg section payloads.
 *
 * Availability of each method depends on the libraries VirtualFS was built
 * with (VIRTUALFS_USE_ZLIB / _ZSTD / _LZ4 / _BROTLI). Query at runtime with
 * VirtualFS_IsCompressionMethodAvailable(); unavailable methods return
 * VirtualFSResult::NotSupported.
 *
 * Stream formats produced:
 * - Deflate: zlib stream (RFC 1950); decompression also accepts gzip (RFC 1952)
 * - Zstd:    Zstandard frame with content size recorded
 * - LZ4:     LZ4 frame format (magic 04 22 4D 18) with content size recorded,
 *            interoperable with any standard LZ4 frame decoder
 * - Brotli:  raw Brotli stream
 *
 * @section usage Basic Usage
 * @code
 * #include <VirtualFS/VirtualFSCompression.h>
 * using namespace VirtualFS;
 *
 * std::vector<uint8_t> compressed;
 * VirtualFS_CompressBuffer(input, compressed, VirtualFSCompressionMethod::LZ4);
 *
 * std::vector<uint8_t> restored;
 * VirtualFS_DecompressBuffer(compressed, restored); // method auto-detected
 * @endcode
 */

#include "VirtualFSTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VirtualFS {

/**
 * @brief Checks whether a compression method was compiled into this build
 * @param method Compression method to query
 * @return true if the method can be used for buffer compression
 *
 * Store is always available. Only Deflate, Zstd, LZ4 and Brotli are
 * candidates for buffer compression; archive-only methods (LZMA, PPMd, ...)
 * always return false here.
 */
bool VirtualFS_IsCompressionMethodAvailable(VirtualFSCompressionMethod method);

/**
 * @brief Detects the compression method of a buffer from its magic bytes
 * @param data Buffer start
 * @param size Buffer size in bytes
 * @return Detected method, or VirtualFSCompressionMethod::Auto if unknown
 */
VirtualFSCompressionMethod VirtualFS_DetectCompressionMethod(
    const uint8_t* data, size_t size);

/**
 * @brief Compresses a memory buffer
 * @param data Input buffer start
 * @param size Input size in bytes
 * @param output Receives the compressed stream (replaced, not appended)
 * @param method Compression method (Auto selects the best available:
 *               Zstd > LZ4 > Deflate > Store)
 * @param level Compression level preset
 * @return Success, NotSupported if the method is not compiled in,
 *         InvalidArgument on null/empty input, Error on codec failure
 */
VirtualFSResult VirtualFS_CompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method,
    VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal);

/// Vector overload of VirtualFS_CompressBuffer
inline VirtualFSResult VirtualFS_CompressBuffer(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method,
    VirtualFSCompressionLevel level = VirtualFSCompressionLevel::Normal) {
    return VirtualFS_CompressBuffer(input.data(), input.size(), output, method, level);
}

/**
 * @brief Decompresses a memory buffer
 * @param data Input buffer start (compressed stream)
 * @param size Input size in bytes
 * @param output Receives the decompressed data (replaced, not appended)
 * @param method Compression method; Auto detects from magic bytes
 * @param sizeHint Expected decompressed size (0 = unknown); used to
 *                 pre-allocate when the stream does not record its size
 * @return Success, NotSupported if the method is not compiled in,
 *         ArchiveUnsupported if Auto detection fails,
 *         ArchiveCorrupt on a malformed stream
 */
VirtualFSResult VirtualFS_DecompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method = VirtualFSCompressionMethod::Auto,
    size_t sizeHint = 0);

/// Vector overload of VirtualFS_DecompressBuffer
inline VirtualFSResult VirtualFS_DecompressBuffer(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method = VirtualFSCompressionMethod::Auto,
    size_t sizeHint = 0) {
    return VirtualFS_DecompressBuffer(input.data(), input.size(), output, method, sizeHint);
}

} // namespace VirtualFS
