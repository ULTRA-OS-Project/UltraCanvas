// libspecific/Cairo/QoiPixmapCodec.h
// In-memory QOI-style compression for Cairo ARGB32 pixmaps.
//
// This is deliberately separate from qoi.h/qoi.cpp (the QOI *file format*
// codec): blobs produced here are NOT .qoi files. The compression scheme is
// QOI's (run / index / diff / luma ops), but applied directly to a Cairo
// image surface's premultiplied ARGB32 buffer in native byte order — no
// per-pixel BGRA<->RGBA shuffle, no alpha un-premultiplying, so the
// round-trip is bit-exact and runs at memcpy-like speed. On little-endian
// the byte order (B,G,R,A) still places green in the slot QOI's luma op
// optimizes for, so the compression ratio matches standard QOI.
//
// Blobs are for in-process caching only (thumbnail caches and similar):
// they embed the raw pixel dimensions and the HiDPI device scale, but their
// byte layout is endian-local and carries no interchange guarantees.
// Version: 1.0.0
// Last Modified: 2026-07-19
// Author: UltraCanvas Framework
#pragma once
#ifndef QOIPIXMAPCODEC_H
#define QOIPIXMAPCODEC_H
// UltraCanvasImage.h pulls in ImageCairo.h in the correct order (including
// ImageCairo.h directly here would recurse into a half-parsed header).
#include "UltraCanvasImage.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace UltraCanvas {

    // Compress the pixmap's ARGB32 buffer. Returns an empty vector on
    // failure (no surface / zero size). Thread-safe for distinct pixmaps;
    // the pixmap must not be mutated concurrently.
    std::vector<uint8_t> QoiCompressPixmap(UCPixmapCairo& pixmap);

    // Rebuild a pixmap from a blob produced by QoiCompressPixmap, including
    // its device scale. Returns null if the blob is malformed.
    std::shared_ptr<UCPixmapCairo> QoiDecompressPixmap(const uint8_t* data,
                                                       size_t size);
    inline std::shared_ptr<UCPixmapCairo> QoiDecompressPixmap(
            const std::vector<uint8_t>& blob) {
        return QoiDecompressPixmap(blob.data(), blob.size());
    }

}
#endif // QOIPIXMAPCODEC_H
