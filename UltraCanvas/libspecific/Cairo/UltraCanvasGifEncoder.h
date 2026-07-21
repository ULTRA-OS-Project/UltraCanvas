// libspecific/Cairo/UltraCanvasGifEncoder.h
// Self-contained single-frame GIF89a encoder (median-cut quantisation + LZW).
// Version: 1.0.0
// Author: UltraCanvas Framework
//
// WHY THIS EXISTS
// ---------------
// libvips can only write GIF when it was compiled with cgif (the native
// `gifsave` operation). Builds without cgif — e.g. the MSYS2/Windows package,
// which passes `-Dcgif=disabled` — expose no `gifsave`, so earlier code fell
// back to `magicksave` with `format=gif`. That routes the write through
// ImageMagick, whose GIF *coder* is itself an optional build-time delegate. On
// systems where ImageMagick was built without it the save fails at run time
// with `magicksave: libMagick error: NoEncodeDelegateForThisImageFormat 'gif'`.
//
// So neither libvips nor ImageMagick can be relied upon to write a GIF. This
// encoder removes that dependency entirely: it operates on a raw RGB(A) byte
// buffer and emits a complete, standards-compliant GIF89a with no external
// library involved — mirroring the bundled BMP/QOI encoders already used in
// the framework. It is deliberately free of any libvips type so it can be unit
// tested in isolation.
//
// SCOPE
// -----
//   * Single frame only (no animation).
//   * <= 256 colours via median-cut quantisation (palette capped at
//     2^maxColorBits, maxColorBits in [1,8]).
//   * Optional binary (1-bit) transparency for RGBA input: pixels with
//     alpha < 128 are written as the GIF transparent index.
//   * Optional interlacing (4-pass row order).

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace UltraCanvas {
namespace GifEncode {

namespace detail {

    struct QPixel {
        uint8_t  r, g, b;
        uint32_t pos;   // original index into the source image, preserved
                        // across the in-place sorts median cut performs
    };

    // A colour box: a half-open [start,end) slice of the pixel array plus the
    // bounding cube of the colours it contains.
    struct Box {
        int start, end;
        uint8_t rmin, rmax, gmin, gmax, bmin, bmax;

        void shrink(const std::vector<QPixel>& px) {
            rmin = gmin = bmin = 255;
            rmax = gmax = bmax = 0;
            for (int i = start; i < end; ++i) {
                const QPixel& p = px[i];
                rmin = std::min(rmin, p.r); rmax = std::max(rmax, p.r);
                gmin = std::min(gmin, p.g); gmax = std::max(gmax, p.g);
                bmin = std::min(bmin, p.b); bmax = std::max(bmax, p.b);
            }
        }
        int longestAxis() const {
            int dr = rmax - rmin, dg = gmax - gmin, db = bmax - bmin;
            if (dr >= dg && dr >= db) return 0;
            if (dg >= db)             return 1;
            return 2;
        }
        int longestExtent() const {
            return std::max(rmax - rmin, std::max(gmax - gmin, bmax - bmin));
        }
    };

    // Accumulates LZW codes (LSB-first) into GIF sub-blocks of <=255 bytes.
    struct BitWriter {
        std::vector<uint8_t>& out;
        uint32_t acc   = 0;
        int      nbits = 0;
        std::vector<uint8_t> block;

        explicit BitWriter(std::vector<uint8_t>& o) : out(o) { block.reserve(255); }

        void writeCode(int code, int codeSize) {
            acc |= (uint32_t)code << nbits;
            nbits += codeSize;
            while (nbits >= 8) {
                block.push_back((uint8_t)(acc & 0xFF));
                acc >>= 8;
                nbits -= 8;
                if (block.size() == 255) flushBlock();
            }
        }
        void flushBlock() {
            if (block.empty()) return;
            out.push_back((uint8_t)block.size());
            out.insert(out.end(), block.begin(), block.end());
            block.clear();
        }
        void finish() {
            if (nbits > 0) {
                block.push_back((uint8_t)(acc & 0xFF));
                acc = 0;
                nbits = 0;
                if (block.size() == 255) flushBlock();
            }
            flushBlock();
            out.push_back(0);   // block terminator
        }
    };

    inline void putLE16(std::vector<uint8_t>& v, uint16_t x) {
        v.push_back((uint8_t)(x & 0xFF));
        v.push_back((uint8_t)((x >> 8) & 0xFF));
    }

} // namespace detail

// Encode an RGB/RGBA image (row-major, 8-bit, no row padding) to a GIF89a file.
// Returns "" on success or a human-readable error string on failure.
inline std::string EncodeGifFile(const std::string& path,
                                 const uint8_t* pixels,
                                 int width, int height,
                                 int channels,
                                 int maxColorBits,
                                 bool interlace) {
    using namespace detail;

    if (!pixels || width <= 0 || height <= 0) return "GIF encode: invalid image dimensions";
    if (channels != 3 && channels != 4)       return "GIF encode: channels must be 3 or 4";
    if (maxColorBits < 1) maxColorBits = 1;
    if (maxColorBits > 8) maxColorBits = 8;

    const size_t N = (size_t)width * (size_t)height;

    // ---- Split out transparent pixels (RGBA, alpha < 128) --------------------
    // 0xFFFF is a sentinel meaning "transparent"; resolved to the transparent
    // palette index once the opaque palette size is known.
    std::vector<uint16_t> indexMap(N, 0);
    std::vector<QPixel>   px;
    px.reserve(N);
    bool hasTransparency = false;
    for (size_t i = 0; i < N; ++i) {
        const uint8_t* p = pixels + i * (size_t)channels;
        if (channels == 4 && p[3] < 128) {
            indexMap[i] = 0xFFFF;
            hasTransparency = true;
            continue;
        }
        px.push_back({ p[0], p[1], p[2], (uint32_t)i });
    }

    // Reserve one slot for the transparent index when needed so the total
    // colour count still fits the power-of-two Global Colour Table.
    const int maxColors = 1 << maxColorBits;
    int budget = hasTransparency ? maxColors - 1 : maxColors;
    if (budget < 1) budget = 1;

    // ---- Median-cut quantisation of the opaque colours -----------------------
    std::vector<Box> boxes;
    if (!px.empty()) {
        Box b{ 0, (int)px.size(), 0, 0, 0, 0, 0, 0 };
        b.shrink(px);
        boxes.push_back(b);

        while ((int)boxes.size() < budget) {
            // Pick the splittable box with the widest colour spread.
            int bi = -1, bestExt = 0;
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (boxes[i].end - boxes[i].start > 1) {
                    int e = boxes[i].longestExtent();
                    if (e > bestExt) { bestExt = e; bi = (int)i; }
                }
            }
            if (bi < 0) break;   // nothing left worth splitting

            Box box = boxes[bi];
            int axis = box.longestAxis();
            std::sort(px.begin() + box.start, px.begin() + box.end,
                      [axis](const QPixel& a, const QPixel& c) {
                          if (axis == 0) return a.r < c.r;
                          if (axis == 1) return a.g < c.g;
                          return a.b < c.b;
                      });
            int mid = box.start + (box.end - box.start) / 2;
            Box left { box.start, mid,      0, 0, 0, 0, 0, 0 };
            Box right{ mid,       box.end,  0, 0, 0, 0, 0, 0 };
            left.shrink(px);
            right.shrink(px);
            boxes[bi] = left;
            boxes.push_back(right);
        }
    }

    // ---- Build the palette and map every opaque pixel to its index -----------
    std::vector<uint8_t> palette;   // flat RGB triples
    palette.reserve(boxes.size() * 3);
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& box = boxes[i];
        uint64_t rs = 0, gs = 0, bs = 0;
        int cnt = box.end - box.start;
        for (int j = box.start; j < box.end; ++j) {
            rs += px[j].r; gs += px[j].g; bs += px[j].b;
        }
        int d = cnt > 0 ? cnt : 1;
        palette.push_back((uint8_t)(rs / d));
        palette.push_back((uint8_t)(gs / d));
        palette.push_back((uint8_t)(bs / d));
        for (int j = box.start; j < box.end; ++j)
            indexMap[px[j].pos] = (uint16_t)i;
    }
    if (palette.empty()) {           // fully transparent (or empty) image
        palette.push_back(0); palette.push_back(0); palette.push_back(0);
    }

    int opaqueCount    = (int)(palette.size() / 3);
    int transparentIdx = -1;
    int totalColors    = opaqueCount;
    if (hasTransparency) {
        transparentIdx = opaqueCount;      // one slot past the opaque colours
        totalColors    = opaqueCount + 1;
        palette.push_back(0); palette.push_back(0); palette.push_back(0);
        for (size_t i = 0; i < N; ++i)
            if (indexMap[i] == 0xFFFF) indexMap[i] = (uint16_t)transparentIdx;
    }

    // Global Colour Table size must be a power of two in [2,256].
    int gctBits = 1;
    while ((1 << gctBits) < totalColors) ++gctBits;
    if (gctBits > 8) gctBits = 8;
    const int tableSize = 1 << gctBits;
    palette.resize((size_t)tableSize * 3, 0);   // pad unused entries with black

    // GIF LZW minimum code size must be >= 2 even for 2-colour images.
    const int minCodeSize = std::max(2, gctBits);

    // ---- Serialise the index stream in the requested row order ---------------
    std::vector<uint8_t> stream;
    stream.reserve(N);
    if (interlace) {
        const int starts[4] = { 0, 4, 2, 1 };
        const int steps [4] = { 8, 8, 4, 2 };
        for (int pass = 0; pass < 4; ++pass)
            for (int y = starts[pass]; y < height; y += steps[pass])
                for (int x = 0; x < width; ++x)
                    stream.push_back((uint8_t)indexMap[(size_t)y * width + x]);
    } else {
        for (size_t i = 0; i < N; ++i) stream.push_back((uint8_t)indexMap[i]);
    }

    // ---- LZW compression -----------------------------------------------------
    std::vector<uint8_t> lzw;
    lzw.reserve(stream.size() / 2 + 16);
    {
        BitWriter bw(lzw);
        const int clearCode = 1 << minCodeSize;
        const int eoiCode   = clearCode + 1;
        int codeSize = minCodeSize + 1;
        int nextCode = eoiCode + 1;
        std::unordered_map<int, int> dict;
        dict.reserve(4096);

        bw.writeCode(clearCode, codeSize);
        if (!stream.empty()) {
            int prefix = stream[0];
            for (size_t i = 1; i < stream.size(); ++i) {
                int c   = stream[i];
                int key = (prefix << 8) | c;
                auto it = dict.find(key);
                if (it != dict.end()) {
                    prefix = it->second;
                } else {
                    bw.writeCode(prefix, codeSize);
                    if (nextCode < 4096) {
                        dict.emplace(key, nextCode);
                        if (nextCode == (1 << codeSize) && codeSize < 12) ++codeSize;
                        ++nextCode;
                    } else {
                        // Code space exhausted — reset the decoder's table.
                        bw.writeCode(clearCode, codeSize);
                        dict.clear();
                        codeSize = minCodeSize + 1;
                        nextCode = eoiCode + 1;
                    }
                    prefix = c;
                }
            }
            bw.writeCode(prefix, codeSize);
        }
        bw.writeCode(eoiCode, codeSize);
        bw.finish();
    }

    // ---- Assemble the file ---------------------------------------------------
    std::vector<uint8_t> out;
    out.reserve(64 + palette.size() + lzw.size());

    const char* sig = "GIF89a";
    out.insert(out.end(), sig, sig + 6);

    // Logical Screen Descriptor
    putLE16(out, (uint16_t)width);
    putLE16(out, (uint16_t)height);
    out.push_back((uint8_t)(0x80 | ((gctBits - 1) << 4) | (gctBits - 1))); // GCT present + res + size
    out.push_back(0);   // background colour index
    out.push_back(0);   // pixel aspect ratio

    // Global Colour Table
    out.insert(out.end(), palette.begin(), palette.end());

    // Graphic Control Extension (only needed to declare the transparent index)
    if (hasTransparency) {
        out.push_back(0x21); out.push_back(0xF9); out.push_back(0x04);
        out.push_back(0x01);            // packed: transparent colour flag set
        putLE16(out, 0);                // delay time
        out.push_back((uint8_t)transparentIdx);
        out.push_back(0x00);            // block terminator
    }

    // Image Descriptor
    out.push_back(0x2C);
    putLE16(out, 0); putLE16(out, 0);           // left, top
    putLE16(out, (uint16_t)width);
    putLE16(out, (uint16_t)height);
    out.push_back((uint8_t)(interlace ? 0x40 : 0x00));

    // Image data
    out.push_back((uint8_t)minCodeSize);
    out.insert(out.end(), lzw.begin(), lzw.end());

    out.push_back(0x3B);   // trailer

    // ---- Write to disk -------------------------------------------------------
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return "GIF encode: failed to open output file";
    size_t written = std::fwrite(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    if (written != out.size()) return "GIF encode: failed to write file";
    return "";
}

} // namespace GifEncode
} // namespace UltraCanvas
