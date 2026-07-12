// VirtualFS/core/VirtualFSCompression.cpp
// Raw buffer compression/decompression implementation
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: ULTRA OS Framework

#include "VirtualFS/VirtualFSCompression.h"

#include <cstring>

#ifdef VIRTUALFS_HAS_ZLIB
#include <zlib.h>
#endif

#ifdef VIRTUALFS_HAS_ZSTD
#include <zstd.h>
#endif

#ifdef VIRTUALFS_HAS_LZ4
#include <lz4frame.h>
#endif

#ifdef VIRTUALFS_HAS_BROTLI
#include <brotli/decode.h>
#include <brotli/encode.h>
#endif

namespace VirtualFS {

namespace {

// ============================================================================
// MAGIC BYTES
// ============================================================================

constexpr uint8_t LZ4_MAGIC[4]  = {0x04, 0x22, 0x4D, 0x18};
constexpr uint8_t ZSTD_MAGIC[4] = {0x28, 0xB5, 0x2F, 0xFD};
constexpr uint8_t GZIP_MAGIC[2] = {0x1F, 0x8B};

bool IsZlibHeader(const uint8_t* data, size_t size) {
    // zlib stream: CMF/FLG pair where (CMF*256 + FLG) % 31 == 0 and
    // compression method is deflate (low nibble of CMF == 8)
    if (size < 2) return false;
    if ((data[0] & 0x0F) != 0x08) return false;
    return (static_cast<uint32_t>(data[0]) * 256 + data[1]) % 31 == 0;
}

// ============================================================================
// DEFLATE (zlib)
// ============================================================================

#ifdef VIRTUALFS_HAS_ZLIB

int ZlibLevel(VirtualFSCompressionLevel level) {
    switch (level) {
        case VirtualFSCompressionLevel::Store:   return 0;
        case VirtualFSCompressionLevel::Fastest: return 1;
        case VirtualFSCompressionLevel::Fast:    return 3;
        case VirtualFSCompressionLevel::Normal:  return 6;
        case VirtualFSCompressionLevel::Good:    return 8;
        case VirtualFSCompressionLevel::Best:    return 9;
        case VirtualFSCompressionLevel::Ultra:   return 9;
        default:                                 return 6;
    }
}

VirtualFSResult DeflateCompress(const uint8_t* data, size_t size,
                                std::vector<uint8_t>& output,
                                VirtualFSCompressionLevel level) {
    uLongf bound = compressBound(static_cast<uLong>(size));
    output.resize(bound);

    int rc = compress2(output.data(), &bound, data,
                       static_cast<uLong>(size), ZlibLevel(level));
    if (rc != Z_OK) {
        output.clear();
        return VirtualFSResult::Error;
    }

    output.resize(bound);
    return VirtualFSResult::Success;
}

VirtualFSResult DeflateDecompress(const uint8_t* data, size_t size,
                                  std::vector<uint8_t>& output,
                                  size_t sizeHint) {
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));

    // windowBits 15 + 32: auto-detect zlib or gzip envelope
    if (inflateInit2(&stream, 15 + 32) != Z_OK) {
        return VirtualFSResult::Error;
    }

    output.clear();
    output.resize(sizeHint > 0 ? sizeHint : size * 4 + 64);

    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);

    size_t written = 0;
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        if (written == output.size()) {
            output.resize(output.size() * 2);
        }
        stream.next_out = output.data() + written;
        stream.avail_out = static_cast<uInt>(output.size() - written);

        rc = inflate(&stream, Z_NO_FLUSH);
        written = output.size() - stream.avail_out;

        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&stream);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
    }

    inflateEnd(&stream);
    output.resize(written);
    return VirtualFSResult::Success;
}

#endif // VIRTUALFS_HAS_ZLIB

// ============================================================================
// ZSTD
// ============================================================================

#ifdef VIRTUALFS_HAS_ZSTD

int ZstdLevel(VirtualFSCompressionLevel level) {
    switch (level) {
        case VirtualFSCompressionLevel::Store:   return 1;
        case VirtualFSCompressionLevel::Fastest: return 1;
        case VirtualFSCompressionLevel::Fast:    return 2;
        case VirtualFSCompressionLevel::Normal:  return 3;
        case VirtualFSCompressionLevel::Good:    return 9;
        case VirtualFSCompressionLevel::Best:    return 19;
        case VirtualFSCompressionLevel::Ultra:   return 22;
        default:                                 return 3;
    }
}

VirtualFSResult ZstdCompress(const uint8_t* data, size_t size,
                             std::vector<uint8_t>& output,
                             VirtualFSCompressionLevel level) {
    size_t bound = ZSTD_compressBound(size);
    output.resize(bound);

    size_t written = ZSTD_compress(output.data(), bound, data, size, ZstdLevel(level));
    if (ZSTD_isError(written)) {
        output.clear();
        return VirtualFSResult::Error;
    }

    output.resize(written);
    return VirtualFSResult::Success;
}

VirtualFSResult ZstdDecompress(const uint8_t* data, size_t size,
                               std::vector<uint8_t>& output,
                               size_t sizeHint) {
    unsigned long long contentSize = ZSTD_getFrameContentSize(data, size);
    if (contentSize == ZSTD_CONTENTSIZE_ERROR) {
        return VirtualFSResult::ArchiveCorrupt;
    }

    if (contentSize != ZSTD_CONTENTSIZE_UNKNOWN) {
        output.resize(static_cast<size_t>(contentSize));
        size_t written = ZSTD_decompress(output.data(), output.size(), data, size);
        if (ZSTD_isError(written) || written != output.size()) {
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
        return VirtualFSResult::Success;
    }

    // Content size not recorded: streaming decompression with a growing buffer
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream) return VirtualFSResult::Error;

    output.clear();
    output.resize(sizeHint > 0 ? sizeHint : size * 4 + 64);

    ZSTD_inBuffer input = {data, size, 0};
    size_t written = 0;
    size_t rc = 1;
    while (rc != 0) {
        if (written == output.size()) {
            output.resize(output.size() * 2);
        }
        ZSTD_outBuffer out = {output.data() + written, output.size() - written, 0};
        rc = ZSTD_decompressStream(stream, &out, &input);
        written += out.pos;

        if (ZSTD_isError(rc)) {
            ZSTD_freeDStream(stream);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
        if (rc != 0 && out.pos == 0 && input.pos == input.size) {
            // No progress and input exhausted: truncated stream
            ZSTD_freeDStream(stream);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
    }

    ZSTD_freeDStream(stream);
    output.resize(written);
    return VirtualFSResult::Success;
}

#endif // VIRTUALFS_HAS_ZSTD

// ============================================================================
// LZ4 (frame format)
// ============================================================================

#ifdef VIRTUALFS_HAS_LZ4

int LZ4Level(VirtualFSCompressionLevel level) {
    // 0 = fast mode (default); >= 3 switches to LZ4HC
    switch (level) {
        case VirtualFSCompressionLevel::Store:   return 0;
        case VirtualFSCompressionLevel::Fastest: return 0;
        case VirtualFSCompressionLevel::Fast:    return 0;
        case VirtualFSCompressionLevel::Normal:  return 0;
        case VirtualFSCompressionLevel::Good:    return 4;
        case VirtualFSCompressionLevel::Best:    return 9;
        case VirtualFSCompressionLevel::Ultra:   return 12;
        default:                                 return 0;
    }
}

VirtualFSResult LZ4Compress(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& output,
                            VirtualFSCompressionLevel level) {
    LZ4F_preferences_t prefs;
    std::memset(&prefs, 0, sizeof(prefs));
    prefs.compressionLevel = LZ4Level(level);
    // Record the content size so decoders can pre-allocate
    prefs.frameInfo.contentSize = static_cast<unsigned long long>(size);
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;

    size_t bound = LZ4F_compressFrameBound(size, &prefs);
    output.resize(bound);

    size_t written = LZ4F_compressFrame(output.data(), bound, data, size, &prefs);
    if (LZ4F_isError(written)) {
        output.clear();
        return VirtualFSResult::Error;
    }

    output.resize(written);
    return VirtualFSResult::Success;
}

VirtualFSResult LZ4Decompress(const uint8_t* data, size_t size,
                              std::vector<uint8_t>& output,
                              size_t sizeHint) {
    LZ4F_dctx* dctx = nullptr;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION))) {
        return VirtualFSResult::Error;
    }

    // Use the recorded content size to pre-allocate when present
    size_t initialSize = sizeHint;
    {
        LZ4F_frameInfo_t frameInfo;
        std::memset(&frameInfo, 0, sizeof(frameInfo));
        size_t peekSize = size;
        const uint8_t* peek = data;
        size_t rc = LZ4F_getFrameInfo(dctx, &frameInfo, peek, &peekSize);
        if (LZ4F_isError(rc)) {
            LZ4F_freeDecompressionContext(dctx);
            return VirtualFSResult::ArchiveCorrupt;
        }
        if (frameInfo.contentSize > 0) {
            initialSize = static_cast<size_t>(frameInfo.contentSize);
        }
        data += peekSize;
        size -= peekSize;
    }

    output.clear();
    output.resize(initialSize > 0 ? initialSize : size * 4 + 64);

    size_t consumed = 0;
    size_t written = 0;
    size_t rc = 1;
    while (rc != 0) {
        if (written == output.size()) {
            output.resize(output.size() * 2);
        }
        size_t dstSize = output.size() - written;
        size_t srcSize = size - consumed;

        rc = LZ4F_decompress(dctx, output.data() + written, &dstSize,
                             data + consumed, &srcSize, nullptr);
        written += dstSize;
        consumed += srcSize;

        if (LZ4F_isError(rc)) {
            LZ4F_freeDecompressionContext(dctx);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
        if (rc != 0 && dstSize == 0 && consumed == size) {
            // No progress and input exhausted: truncated frame
            LZ4F_freeDecompressionContext(dctx);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
    }

    LZ4F_freeDecompressionContext(dctx);
    output.resize(written);
    return VirtualFSResult::Success;
}

#endif // VIRTUALFS_HAS_LZ4

// ============================================================================
// BROTLI
// ============================================================================

#ifdef VIRTUALFS_HAS_BROTLI

int BrotliLevel(VirtualFSCompressionLevel level) {
    switch (level) {
        case VirtualFSCompressionLevel::Store:   return 0;
        case VirtualFSCompressionLevel::Fastest: return 1;
        case VirtualFSCompressionLevel::Fast:    return 3;
        case VirtualFSCompressionLevel::Normal:  return 5;
        case VirtualFSCompressionLevel::Good:    return 8;
        case VirtualFSCompressionLevel::Best:    return 11;
        case VirtualFSCompressionLevel::Ultra:   return 11;
        default:                                 return 5;
    }
}

VirtualFSResult BrotliCompress(const uint8_t* data, size_t size,
                               std::vector<uint8_t>& output,
                               VirtualFSCompressionLevel level) {
    size_t bound = BrotliEncoderMaxCompressedSize(size);
    if (bound == 0) return VirtualFSResult::Error;
    output.resize(bound);

    size_t written = bound;
    if (!BrotliEncoderCompress(BrotliLevel(level), BROTLI_DEFAULT_WINDOW,
                               BROTLI_DEFAULT_MODE, size, data,
                               &written, output.data())) {
        output.clear();
        return VirtualFSResult::Error;
    }

    output.resize(written);
    return VirtualFSResult::Success;
}

VirtualFSResult BrotliDecompress(const uint8_t* data, size_t size,
                                 std::vector<uint8_t>& output,
                                 size_t sizeHint) {
    BrotliDecoderState* state =
        BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) return VirtualFSResult::Error;

    output.clear();
    output.resize(sizeHint > 0 ? sizeHint : size * 4 + 64);

    const uint8_t* nextIn = data;
    size_t availIn = size;
    size_t written = 0;

    BrotliDecoderResult rc = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (rc != BROTLI_DECODER_RESULT_SUCCESS) {
        if (written == output.size()) {
            output.resize(output.size() * 2);
        }
        uint8_t* nextOut = output.data() + written;
        size_t availOut = output.size() - written;

        rc = BrotliDecoderDecompressStream(state, &availIn, &nextIn,
                                           &availOut, &nextOut, nullptr);
        written = output.size() - availOut;

        if (rc == BROTLI_DECODER_RESULT_ERROR ||
            rc == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
            BrotliDecoderDestroyInstance(state);
            output.clear();
            return VirtualFSResult::ArchiveCorrupt;
        }
    }

    BrotliDecoderDestroyInstance(state);
    output.resize(written);
    return VirtualFSResult::Success;
}

#endif // VIRTUALFS_HAS_BROTLI

VirtualFSCompressionMethod PickAutoMethod() {
#if defined(VIRTUALFS_HAS_ZSTD)
    return VirtualFSCompressionMethod::Zstd;
#elif defined(VIRTUALFS_HAS_LZ4)
    return VirtualFSCompressionMethod::LZ4;
#elif defined(VIRTUALFS_HAS_ZLIB)
    return VirtualFSCompressionMethod::Deflate;
#else
    return VirtualFSCompressionMethod::Store;
#endif
}

} // namespace

// ============================================================================
// PUBLIC API
// ============================================================================

bool VirtualFS_IsCompressionMethodAvailable(VirtualFSCompressionMethod method) {
    switch (method) {
        case VirtualFSCompressionMethod::Store:
            return true;
        case VirtualFSCompressionMethod::Deflate:
#ifdef VIRTUALFS_HAS_ZLIB
            return true;
#else
            return false;
#endif
        case VirtualFSCompressionMethod::Zstd:
#ifdef VIRTUALFS_HAS_ZSTD
            return true;
#else
            return false;
#endif
        case VirtualFSCompressionMethod::LZ4:
#ifdef VIRTUALFS_HAS_LZ4
            return true;
#else
            return false;
#endif
        case VirtualFSCompressionMethod::Brotli:
#ifdef VIRTUALFS_HAS_BROTLI
            return true;
#else
            return false;
#endif
        default:
            // Archive-only methods (LZMA, PPMd, XZ, ...) are not exposed
            // through the raw buffer API
            return false;
    }
}

VirtualFSCompressionMethod VirtualFS_DetectCompressionMethod(
    const uint8_t* data, size_t size) {
    if (!data || size < 2) {
        return VirtualFSCompressionMethod::Auto;
    }
    if (size >= 4 && std::memcmp(data, LZ4_MAGIC, 4) == 0) {
        return VirtualFSCompressionMethod::LZ4;
    }
    if (size >= 4 && std::memcmp(data, ZSTD_MAGIC, 4) == 0) {
        return VirtualFSCompressionMethod::Zstd;
    }
    if (std::memcmp(data, GZIP_MAGIC, 2) == 0 || IsZlibHeader(data, size)) {
        return VirtualFSCompressionMethod::Deflate;
    }
    // Brotli has no magic bytes and cannot be detected
    return VirtualFSCompressionMethod::Auto;
}

VirtualFSResult VirtualFS_CompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method,
    VirtualFSCompressionLevel level) {
    output.clear();
    if (!data || size == 0) {
        return VirtualFSResult::InvalidArgument;
    }

    if (method == VirtualFSCompressionMethod::Auto) {
        method = PickAutoMethod();
    }

    switch (method) {
        case VirtualFSCompressionMethod::Store:
            output.assign(data, data + size);
            return VirtualFSResult::Success;
#ifdef VIRTUALFS_HAS_ZLIB
        case VirtualFSCompressionMethod::Deflate:
            return DeflateCompress(data, size, output, level);
#endif
#ifdef VIRTUALFS_HAS_ZSTD
        case VirtualFSCompressionMethod::Zstd:
            return ZstdCompress(data, size, output, level);
#endif
#ifdef VIRTUALFS_HAS_LZ4
        case VirtualFSCompressionMethod::LZ4:
            return LZ4Compress(data, size, output, level);
#endif
#ifdef VIRTUALFS_HAS_BROTLI
        case VirtualFSCompressionMethod::Brotli:
            return BrotliCompress(data, size, output, level);
#endif
        default:
            return VirtualFSResult::NotSupported;
    }
}

VirtualFSResult VirtualFS_DecompressBuffer(
    const uint8_t* data, size_t size,
    std::vector<uint8_t>& output,
    VirtualFSCompressionMethod method,
    size_t sizeHint) {
    output.clear();
    if (!data || size == 0) {
        return VirtualFSResult::InvalidArgument;
    }

    if (method == VirtualFSCompressionMethod::Auto) {
        method = VirtualFS_DetectCompressionMethod(data, size);
        if (method == VirtualFSCompressionMethod::Auto) {
            return VirtualFSResult::ArchiveUnsupported;
        }
    }

    switch (method) {
        case VirtualFSCompressionMethod::Store:
            output.assign(data, data + size);
            return VirtualFSResult::Success;
#ifdef VIRTUALFS_HAS_ZLIB
        case VirtualFSCompressionMethod::Deflate:
            return DeflateDecompress(data, size, output, sizeHint);
#endif
#ifdef VIRTUALFS_HAS_ZSTD
        case VirtualFSCompressionMethod::Zstd:
            return ZstdDecompress(data, size, output, sizeHint);
#endif
#ifdef VIRTUALFS_HAS_LZ4
        case VirtualFSCompressionMethod::LZ4:
            return LZ4Decompress(data, size, output, sizeHint);
#endif
#ifdef VIRTUALFS_HAS_BROTLI
        case VirtualFSCompressionMethod::Brotli:
            return BrotliDecompress(data, size, output, sizeHint);
#endif
        default:
            return VirtualFSResult::NotSupported;
    }
}

} // namespace VirtualFS
