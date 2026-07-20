// libspecific/Cairo/QoiPixmapCodec.cpp
// QOI-style compression applied natively to Cairo ARGB32 buffers — see the
// header for scope and the relation to qoi.cpp (the file-format codec).
//
// The four channel bytes are addressed by memory position (c0..c3), never
// by color name: encode and decode use the same positions, so the scheme is
// correct on any endianness, and alpha semantics (premultiplied) pass
// through untouched. c3 is Cairo's alpha byte on little-endian, which makes
// the "c3 unchanged" fast ops (DIFF/LUMA/RGB) hit exactly like standard
// QOI's alpha-unchanged ops.
// Version: 1.0.0
// Last Modified: 2026-07-19
// Author: UltraCanvas Framework
#include "QoiPixmapCodec.h"
#include <cstring>

namespace UltraCanvas {

    namespace {
        constexpr uint32_t kMagic = 0x58504351;   // "QCPX" little-endian

        struct BlobHeader {
            uint32_t magic;
            uint32_t rawWidth;
            uint32_t rawHeight;
            float deviceScale;
        };

        // Two-bit op tags in the high bits, eight-bit ops — QOI's layout.
        constexpr uint8_t kOpIndex = 0x00;   // 00xxxxxx
        constexpr uint8_t kOpDiff  = 0x40;   // 01xxxxxx
        constexpr uint8_t kOpLuma  = 0x80;   // 10xxxxxx
        constexpr uint8_t kOpRun   = 0xC0;   // 11xxxxxx
        constexpr uint8_t kOpRgb   = 0xFE;   // c0,c1,c2 follow; c3 kept
        constexpr uint8_t kOpRgba  = 0xFF;   // all four bytes follow
        constexpr uint8_t kOpMask  = 0xC0;

        struct Px {
            uint8_t c[4];
            bool operator==(const Px& o) const {
                return std::memcmp(c, o.c, 4) == 0;
            }
        };

        inline int Hash(const Px& p) {
            return (p.c[0] * 3 + p.c[1] * 5 + p.c[2] * 7 + p.c[3] * 11) & 63;
        }

        // Same seed on both sides: index zeroed, previous = opaque black
        // in Cairo terms (alpha byte 255, color bytes 0).
        inline Px SeedPrev() {
            Px p{};
            p.c[3] = 255;
            return p;
        }
    }

    std::vector<uint8_t> QoiCompressPixmap(UCPixmapCairo& pixmap) {
        cairo_surface_t* surf = pixmap.GetSurface();
        const int w = pixmap.GetRawWidth();
        const int h = pixmap.GetRawHeight();
        if (!surf || w <= 0 || h <= 0) return {};
        cairo_surface_flush(surf);
        const uint8_t* pixels = cairo_image_surface_get_data(surf);
        if (!pixels) return {};
        const int stride = cairo_image_surface_get_stride(surf);

        // Worst case: every pixel a 5-byte RGBA op.
        std::vector<uint8_t> out(sizeof(BlobHeader)
                                 + static_cast<size_t>(w) * h * 5 + 1);
        BlobHeader hdr{kMagic, static_cast<uint32_t>(w),
                       static_cast<uint32_t>(h),
                       static_cast<float>(pixmap.GetDeviceScale())};
        std::memcpy(out.data(), &hdr, sizeof hdr);
        size_t o = sizeof(BlobHeader);

        Px index[64] = {};
        Px prev = SeedPrev();
        int run = 0;

        for (int y = 0; y < h; ++y) {
            const uint8_t* row = pixels + static_cast<size_t>(y) * stride;
            for (int x = 0; x < w; ++x) {
                Px px;
                std::memcpy(px.c, row + x * 4, 4);

                if (px == prev) {
                    if (++run == 62) {
                        out[o++] = kOpRun | (run - 1);
                        run = 0;
                    }
                    continue;
                }
                if (run > 0) {
                    out[o++] = kOpRun | (run - 1);
                    run = 0;
                }

                int hash = Hash(px);
                if (index[hash] == px) {
                    out[o++] = kOpIndex | hash;
                } else {
                    index[hash] = px;
                    if (px.c[3] == prev.c[3]) {
                        // Wrapping deltas, exactly as the QOI spec defines.
                        int8_t d0 = static_cast<int8_t>(px.c[0] - prev.c[0]);
                        int8_t d1 = static_cast<int8_t>(px.c[1] - prev.c[1]);
                        int8_t d2 = static_cast<int8_t>(px.c[2] - prev.c[2]);
                        int8_t d0_1 = static_cast<int8_t>(d0 - d1);
                        int8_t d2_1 = static_cast<int8_t>(d2 - d1);
                        if (d0 > -3 && d0 < 2 && d1 > -3 && d1 < 2 &&
                            d2 > -3 && d2 < 2) {
                            out[o++] = kOpDiff | ((d0 + 2) << 4)
                                     | ((d1 + 2) << 2) | (d2 + 2);
                        } else if (d1 > -33 && d1 < 32 &&
                                   d0_1 > -9 && d0_1 < 8 &&
                                   d2_1 > -9 && d2_1 < 8) {
                            out[o++] = kOpLuma | (d1 + 32);
                            out[o++] = static_cast<uint8_t>(((d0_1 + 8) << 4)
                                                            | (d2_1 + 8));
                        } else {
                            out[o++] = kOpRgb;
                            out[o++] = px.c[0];
                            out[o++] = px.c[1];
                            out[o++] = px.c[2];
                        }
                    } else {
                        out[o++] = kOpRgba;
                        std::memcpy(&out[o], px.c, 4);
                        o += 4;
                    }
                }
                prev = px;
            }
        }
        if (run > 0) out[o++] = kOpRun | (run - 1);

        out.resize(o);
        out.shrink_to_fit();
        return out;
    }

    std::shared_ptr<UCPixmapCairo> QoiDecompressPixmap(const uint8_t* data,
                                                       size_t size) {
        if (!data || size < sizeof(BlobHeader)) return nullptr;
        BlobHeader hdr;
        std::memcpy(&hdr, data, sizeof hdr);
        if (hdr.magic != kMagic || hdr.rawWidth == 0 || hdr.rawHeight == 0 ||
            hdr.rawWidth > 0x40000 || hdr.rawHeight > 0x40000) {
            return nullptr;
        }
        const int w = static_cast<int>(hdr.rawWidth);
        const int h = static_cast<int>(hdr.rawHeight);

        auto pm = std::make_shared<UCPixmapCairo>(w, h);
        cairo_surface_t* surf = pm->GetSurface();
        if (!surf || !pm->IsValid()) return nullptr;
        cairo_surface_flush(surf);
        uint8_t* pixels = cairo_image_surface_get_data(surf);
        const int stride = cairo_image_surface_get_stride(surf);

        Px index[64] = {};
        Px px = SeedPrev();
        size_t p = sizeof(BlobHeader);
        int run = 0;

        for (int y = 0; y < h; ++y) {
            uint8_t* row = pixels + static_cast<size_t>(y) * stride;
            for (int x = 0; x < w; ++x) {
                if (run > 0) {
                    --run;
                } else if (p < size) {
                    uint8_t op = data[p++];
                    if (op == kOpRgb) {
                        if (p + 3 > size) return nullptr;
                        px.c[0] = data[p++];
                        px.c[1] = data[p++];
                        px.c[2] = data[p++];
                        index[Hash(px)] = px;
                    } else if (op == kOpRgba) {
                        if (p + 4 > size) return nullptr;
                        std::memcpy(px.c, &data[p], 4);
                        p += 4;
                        index[Hash(px)] = px;
                    } else if ((op & kOpMask) == kOpIndex) {
                        px = index[op & 63];
                    } else if ((op & kOpMask) == kOpDiff) {
                        px.c[0] += ((op >> 4) & 3) - 2;
                        px.c[1] += ((op >> 2) & 3) - 2;
                        px.c[2] += (op & 3) - 2;
                        index[Hash(px)] = px;
                    } else if ((op & kOpMask) == kOpLuma) {
                        if (p + 1 > size) return nullptr;
                        uint8_t b2 = data[p++];
                        int d1 = (op & 63) - 32;
                        px.c[0] += d1 - 8 + ((b2 >> 4) & 15);
                        px.c[1] += d1;
                        px.c[2] += d1 - 8 + (b2 & 15);
                        index[Hash(px)] = px;
                    } else {   // kOpRun
                        run = op & 63;
                    }
                } else {
                    return nullptr;   // truncated blob
                }
                std::memcpy(row + x * 4, px.c, 4);
            }
        }

        pm->MarkDirty();
        if (hdr.deviceScale > 0.0f && hdr.deviceScale != 1.0f) {
            pm->SetDeviceScale(hdr.deviceScale);
        }
        return pm;
    }

}
