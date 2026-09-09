// core/UltraCanvasIconResource.cpp
// Portable reader for the icon files the other two desktops use: Windows
// ".ico" files and the RT_GROUP_ICON / RT_ICON resources of a PE binary, and
// Apple ".icns" files. No platform API and no new dependency - the PE
// resource directory is a simple tree of records, an icns is a flat list of
// them, and a frame inside either is either a PNG (handed to the image
// pipeline) or a bitmap with a separate transparency mask (decoded here).
//
// Sizes: an icon file holds the same picture several times over, and the
// frame nearest the requested size is the one picked - upscaling a 16px
// frame into a 128px tile looks like a mistake, while a 256px frame drawn
// into a details row costs memory for detail nobody sees.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasIconResource.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // An icon file is small; a claim past this is not one.
        constexpr size_t kMaxIconFileBytes = 32u * 1024 * 1024;
        // Enough of a program to cover its DOS stub, PE header and section
        // table - the biggest of those is the section table, and even a
        // heavily sectioned binary stays well inside this.
        constexpr size_t kPeHeaderReadBytes = 128u * 1024;
        // The resource section is read whole; a program whose resources are
        // larger than this holds video, not icons.
        constexpr uint32_t kMaxResourceSectionBytes = 128u * 1024 * 1024;
        // Icon frames are small; anything claiming more is a broken record.
        constexpr uint32_t kMaxFrameBytes = 16u * 1024 * 1024;
        // The largest rendition either container can hold - an .icns goes to
        // 1024 - and, separately, what a 0 in an .ico's one-byte size field
        // means, which is 256 and not "the biggest there is".
        constexpr int kMaxIconEdge = 1024;
        constexpr int kIcoImpliedEdge = 256;
        // Resource trees are three levels deep (type / name / language); the
        // guard is against a file whose records point back at themselves.
        constexpr int kMaxResourceDepth = 8;

        constexpr uint32_t kResourceTypeIcon = 3;        // RT_ICON
        constexpr uint32_t kResourceTypeGroupIcon = 14;  // RT_GROUP_ICON

        uint16_t U16(const std::vector<uint8_t>& b, size_t at) {
            if (at + 2 > b.size()) return 0;
            return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
        }

        uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
            if (at + 4 > b.size()) return 0;
            return static_cast<uint32_t>(b[at]) |
                   (static_cast<uint32_t>(b[at + 1]) << 8) |
                   (static_cast<uint32_t>(b[at + 2]) << 16) |
                   (static_cast<uint32_t>(b[at + 3]) << 24);
        }

        bool InBounds(const std::vector<uint8_t>& b, size_t at, size_t len) {
            return at <= b.size() && len <= b.size() - at;
        }

        // Reads at most `length` bytes from `offset`. A program is read one
        // range at a time rather than whole: its headers are the first few
        // kilobytes and its icons are one section, while the file itself can
        // be hundreds of megabytes that a thumbnail worker must not pull
        // into memory to find a 32-pixel picture.
        bool ReadFileRange(const std::string& path, std::uintmax_t offset,
                           size_t length, std::vector<uint8_t>& out) {
            out.clear();
            if (length == 0) return false;
            std::error_code ec;
            if (!fs::is_regular_file(path, ec) || ec) return false;
            const std::uintmax_t size = fs::file_size(path, ec);
            if (ec || size == 0 || offset >= size) return false;
            const size_t want = static_cast<size_t>(
                    std::min<std::uintmax_t>(length, size - offset));
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            in.seekg(static_cast<std::streamoff>(offset));
            if (!in) return false;
            out.resize(want);
            in.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(want));
            out.resize(static_cast<size_t>(in.gcount()));
            return !out.empty();
        }

        std::string LowerExtensionOf(const std::string& path) {
            const size_t dot = path.find_last_of('.');
            if (dot == std::string::npos) return {};
            std::string ext = path.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }

        // ===== ONE FRAME OF AN ICON =====
        struct IconFrame {
            int width = 0;          // as the directory declares it (0 = 256)
            int height = 0;
            int bitCount = 0;
            size_t offset = 0;      // into the buffer the frame lives in
            uint32_t size = 0;
        };

        // Which frame to draw at `desiredSize`: the exact size if it is
        // there, else the smallest frame larger than it (downscaling keeps
        // detail), else the largest frame there is. Colour depth breaks a
        // tie, so a 32-bit frame wins over the 4-bit one beside it.
        size_t PickFrame(const std::vector<IconFrame>& frames, int desiredSize) {
            size_t best = 0;
            int bestScore = 0;
            for (size_t i = 0; i < frames.size(); ++i) {
                const int edge = std::max(frames[i].width, frames[i].height);
                // Distance, with frames at least as big as wanted preferred.
                const int distance = edge >= desiredSize
                                             ? (edge - desiredSize)
                                             : (desiredSize - edge) * 4 + 4096;
                const int score = -distance * 64 + std::min(frames[i].bitCount, 32);
                if (i == 0 || score > bestScore) { best = i; bestScore = score; }
            }
            return best;
        }

        std::shared_ptr<UCPixmap> DecodePngFrame(const std::vector<uint8_t>& b,
                                                 size_t at, uint32_t size) {
            if (!InBounds(b, at, size)) return nullptr;
            auto image = UCImage::LoadFromMemory(b.data() + at, size);
            if (!image || image->GetWidth() <= 0 || image->GetHeight() <= 0)
                return nullptr;
            return image->GetPixmap(0, 0, ImageFitMode::Contain, 1.0f);
        }

        // A classic icon frame: a BITMAPINFOHEADER whose height covers the
        // colour bitmap and the 1-bit AND mask below it, an optional
        // palette, then bottom-up rows of both.
        std::shared_ptr<UCPixmap> DecodeDibFrame(const std::vector<uint8_t>& b,
                                                 size_t at, uint32_t size,
                                                 int declaredHeight) {
            if (!InBounds(b, at, size) || size < 40) return nullptr;
            const uint32_t headerSize = U32(b, at);
            if (headerSize < 40 || headerSize > size) return nullptr;
            const int32_t width = static_cast<int32_t>(U32(b, at + 4));
            const int32_t storedHeight = static_cast<int32_t>(U32(b, at + 8));
            const uint16_t bitCount = U16(b, at + 14);
            const uint32_t compression = U32(b, at + 16);
            uint32_t paletteCount = U32(b, at + 32);
            if (width <= 0 || width > 4096) return nullptr;
            if (storedHeight <= 0 || storedHeight > 8192) return nullptr;
            // BI_RGB, plus BI_BITFIELDS at 32 bits - where the masks that
            // follow the header are the BGRA order this decodes anyway.
            if (compression != 0 && !(compression == 3 && bitCount == 32))
                return nullptr;

            // The stored bitmap is twice as tall as the icon when it carries
            // the mask, which is the normal case.
            int height = storedHeight;
            bool hasMask = false;
            if (storedHeight % 2 == 0 &&
                (declaredHeight <= 0 || storedHeight == declaredHeight * 2)) {
                height = storedHeight / 2;
                hasMask = true;
            }
            if (height <= 0 || height > 4096) return nullptr;

            size_t cursor = at + headerSize;
            // With the plain 40-byte header the three colour masks of
            // BI_BITFIELDS sit between it and the pixels; the later header
            // versions carry them inside the header itself.
            if (compression == 3 && headerSize == 40) cursor += 12;
            if (bitCount <= 8) {
                if (paletteCount == 0) paletteCount = 1u << bitCount;
                if (paletteCount > 256) return nullptr;
                if (!InBounds(b, cursor, paletteCount * 4u)) return nullptr;
            } else {
                paletteCount = 0;
            }
            const size_t paletteAt = cursor;
            cursor += paletteCount * 4u;

            const size_t colorStride =
                    ((static_cast<size_t>(width) * bitCount + 31) / 32) * 4;
            const size_t maskStride = ((static_cast<size_t>(width) + 31) / 32) * 4;
            const size_t colorBytes = colorStride * static_cast<size_t>(height);
            if (!InBounds(b, cursor, colorBytes)) return nullptr;
            const size_t colorAt = cursor;
            const size_t maskAt = cursor + colorBytes;
            if (hasMask && !InBounds(b, maskAt, maskStride * static_cast<size_t>(height)))
                hasMask = false;

            auto pixmap = std::make_shared<UCPixmap>(width, height);
            if (!pixmap->IsValid()) return nullptr;

            // True when the frame's alpha channel is unused: 32-bit icons
            // written before Windows XP leave it zero and mean the AND mask.
            bool anyAlpha = false;
            if (bitCount == 32) {
                for (size_t row = 0; row < static_cast<size_t>(height) && !anyAlpha; ++row) {
                    const size_t rowAt = colorAt + row * colorStride;
                    for (int x = 0; x < width; ++x) {
                        if (b[rowAt + static_cast<size_t>(x) * 4 + 3] != 0) {
                            anyAlpha = true;
                            break;
                        }
                    }
                }
            }

            for (int y = 0; y < height; ++y) {
                // Rows are stored bottom-up.
                const size_t rowAt = colorAt +
                        static_cast<size_t>(height - 1 - y) * colorStride;
                const size_t maskRowAt = maskAt +
                        static_cast<size_t>(height - 1 - y) * maskStride;
                for (int x = 0; x < width; ++x) {
                    uint8_t r = 0, g = 0, bl = 0, a = 255;
                    switch (bitCount) {
                        case 1:
                        case 4:
                        case 8: {
                            const size_t bitAt = static_cast<size_t>(x) * bitCount;
                            const uint8_t byte = b[rowAt + bitAt / 8];
                            const int shift = 8 - bitCount -
                                              static_cast<int>(bitAt % 8);
                            const uint32_t index =
                                    (byte >> shift) & ((1u << bitCount) - 1u);
                            if (index >= paletteCount) break;
                            const size_t entry = paletteAt + index * 4u;
                            bl = b[entry]; g = b[entry + 1]; r = b[entry + 2];
                            break;
                        }
                        case 16: {
                            const uint16_t value = U16(b, rowAt + static_cast<size_t>(x) * 2);
                            // X1R5G5B5, the only 16-bit layout BI_RGB defines.
                            r = static_cast<uint8_t>(((value >> 10) & 0x1F) * 255 / 31);
                            g = static_cast<uint8_t>(((value >> 5) & 0x1F) * 255 / 31);
                            bl = static_cast<uint8_t>((value & 0x1F) * 255 / 31);
                            break;
                        }
                        case 24: {
                            const size_t at3 = rowAt + static_cast<size_t>(x) * 3;
                            bl = b[at3]; g = b[at3 + 1]; r = b[at3 + 2];
                            break;
                        }
                        case 32: {
                            const size_t at4 = rowAt + static_cast<size_t>(x) * 4;
                            bl = b[at4]; g = b[at4 + 1]; r = b[at4 + 2];
                            a = anyAlpha ? b[at4 + 3] : 255;
                            break;
                        }
                        default:
                            return nullptr;
                    }
                    if (hasMask && !(bitCount == 32 && anyAlpha)) {
                        const uint8_t maskByte = b[maskRowAt + static_cast<size_t>(x) / 8];
                        // A set mask bit means "let the background through".
                        if ((maskByte >> (7 - (x % 8))) & 1) a = 0;
                    }
                    // Cairo's ARGB32 is premultiplied, native-endian words.
                    const uint32_t pixel =
                            (static_cast<uint32_t>(a) << 24) |
                            (static_cast<uint32_t>(r * a / 255) << 16) |
                            (static_cast<uint32_t>(g * a / 255) << 8) |
                            static_cast<uint32_t>(bl * a / 255);
                    pixmap->SetPixel(x, y, pixel);
                }
            }
            pixmap->MarkDirty();
            return pixmap;
        }

        std::shared_ptr<UCPixmap> DecodeFrame(const std::vector<uint8_t>& b,
                                              const IconFrame& frame) {
            if (!InBounds(b, frame.offset, frame.size) || frame.size < 8)
                return nullptr;
            static const uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G',
                                                 0x0D, 0x0A, 0x1A, 0x0A};
            if (std::memcmp(b.data() + frame.offset, kPngMagic, 8) == 0)
                return DecodePngFrame(b, frame.offset, frame.size);
            return DecodeDibFrame(b, frame.offset, frame.size, frame.height);
        }

        int DeclaredEdge(uint8_t value) {
            return value == 0 ? kIcoImpliedEdge : value;
        }

        // ===== ".ico" FILES =====
        bool ParseIconDirectory(const std::vector<uint8_t>& b,
                                std::vector<IconFrame>& out) {
            if (b.size() < 6) return false;
            if (U16(b, 0) != 0) return false;
            const uint16_t type = U16(b, 2);
            if (type != 1 && type != 2) return false;     // icon or cursor
            const uint16_t count = U16(b, 4);
            if (count == 0 || !InBounds(b, 6, static_cast<size_t>(count) * 16))
                return false;
            for (uint16_t i = 0; i < count; ++i) {
                const size_t at = 6 + static_cast<size_t>(i) * 16;
                IconFrame frame;
                frame.width = DeclaredEdge(b[at]);
                frame.height = DeclaredEdge(b[at + 1]);
                frame.bitCount = U16(b, at + 6);
                frame.size = U32(b, at + 8);
                frame.offset = U32(b, at + 12);
                if (frame.size == 0 || frame.size > kMaxFrameBytes) continue;
                if (!InBounds(b, frame.offset, frame.size)) continue;
                out.push_back(frame);
            }
            return !out.empty();
        }

        // ===== PE RESOURCES =====
        // Everything a PE binary's icons need is in one section, so that is
        // all that is read: the headers say where the resource section is,
        // and the section itself is the only part loaded. Offsets below are
        // therefore offsets into that section, not into the file.
        struct PeResources {
            std::vector<uint8_t> data;   // the resource section, verbatim
            uint32_t sectionRva = 0;     // the RVA it is mapped at
            size_t directoryAt = 0;      // resource root, within `data`
            bool valid = false;

            bool RvaToOffset(uint32_t rva, size_t& out) const {
                if (rva < sectionRva) return false;
                const uint32_t delta = rva - sectionRva;
                if (delta >= data.size()) return false;
                out = delta;
                return true;
            }
        };

        PeResources LoadPeResources(const std::string& path) {
            PeResources pe;
            std::vector<uint8_t> head;
            if (!ReadFileRange(path, 0, kPeHeaderReadBytes, head)) return pe;
            if (head.size() < 0x40 || head[0] != 'M' || head[1] != 'Z') return pe;
            const uint32_t peAt = U32(head, 0x3C);
            if (!InBounds(head, peAt, 24) || U32(head, peAt) != 0x00004550)
                return pe;
            const uint16_t sectionCount = U16(head, peAt + 6);
            const uint16_t optionalSize = U16(head, peAt + 20);
            const size_t optionalAt = peAt + 24;
            if (!InBounds(head, optionalAt, optionalSize) || optionalSize < 96)
                return pe;
            const uint16_t magic = U16(head, optionalAt);
            // The data directory follows the optional header's fixed part,
            // which is 16 bytes longer in the 64-bit layout.
            size_t dataDirectoryAt = 0;
            if (magic == 0x010B)      dataDirectoryAt = optionalAt + 96;
            else if (magic == 0x020B) dataDirectoryAt = optionalAt + 112;
            else                      return pe;
            // Entry 2 of the data directory is the resource table.
            const size_t resourceEntryAt = dataDirectoryAt + 2 * 8;
            if (!InBounds(head, resourceEntryAt, 8)) return pe;
            const uint32_t directoryRva = U32(head, resourceEntryAt);
            if (directoryRva == 0) return pe;

            const size_t sectionsAt = optionalAt + optionalSize;
            for (uint16_t i = 0; i < sectionCount; ++i) {
                const size_t at = sectionsAt + static_cast<size_t>(i) * 40;
                if (!InBounds(head, at, 40)) return pe;
                const uint32_t virtualSize = U32(head, at + 8);
                const uint32_t virtualAddress = U32(head, at + 12);
                const uint32_t rawSize = U32(head, at + 16);
                const uint32_t rawOffset = U32(head, at + 20);
                const uint32_t span = std::max(virtualSize, rawSize);
                if (directoryRva < virtualAddress ||
                    directoryRva >= virtualAddress + span)
                    continue;
                if (rawSize == 0 || rawSize > kMaxResourceSectionBytes) return pe;
                if (!ReadFileRange(path, rawOffset, rawSize, pe.data)) return pe;
                pe.sectionRva = virtualAddress;
                if (!pe.RvaToOffset(directoryRva, pe.directoryAt)) {
                    pe.data.clear();
                    return pe;
                }
                pe.valid = true;
                return pe;
            }
            return pe;
        }

        struct ResourceEntry {
            uint32_t id = 0;
            bool isNamed = false;
            bool isDirectory = false;
            uint32_t offset = 0;    // relative to the resource root
        };

        std::vector<ResourceEntry> ReadResourceEntries(const PeResources& pe,
                                                       uint32_t relativeOffset) {
            std::vector<ResourceEntry> entries;
            const std::vector<uint8_t>& b = pe.data;
            const size_t at = pe.directoryAt + relativeOffset;
            if (!InBounds(b, at, 16)) return entries;
            const uint16_t named = U16(b, at + 12);
            const uint16_t ids = U16(b, at + 14);
            const size_t total = static_cast<size_t>(named) + ids;
            if (!InBounds(b, at + 16, total * 8)) return entries;
            for (size_t i = 0; i < total; ++i) {
                const size_t entryAt = at + 16 + i * 8;
                const uint32_t name = U32(b, entryAt);
                const uint32_t child = U32(b, entryAt + 4);
                ResourceEntry entry;
                entry.isNamed = (name & 0x80000000u) != 0;
                entry.id = name & 0x7FFFFFFFu;
                entry.isDirectory = (child & 0x80000000u) != 0;
                entry.offset = child & 0x7FFFFFFFu;
                entries.push_back(entry);
            }
            return entries;
        }

        // The bytes of the first leaf under `relativeOffset` - resources are
        // keyed by language below the name level, and every language holds
        // the same picture.
        bool ReadResourceData(const PeResources& pe, uint32_t relativeOffset,
                              bool isDirectory, size_t& outAt, uint32_t& outSize,
                              int depth = 0) {
            if (depth > kMaxResourceDepth) return false;
            if (isDirectory) {
                for (const ResourceEntry& e :
                     ReadResourceEntries(pe, relativeOffset)) {
                    if (ReadResourceData(pe, e.offset, e.isDirectory, outAt,
                                         outSize, depth + 1))
                        return true;
                }
                return false;
            }
            const size_t at = pe.directoryAt + relativeOffset;
            if (!InBounds(pe.data, at, 16)) return false;
            const uint32_t rva = U32(pe.data, at);
            const uint32_t size = U32(pe.data, at + 4);
            if (size == 0 || size > kMaxFrameBytes) return false;
            size_t offset = 0;
            if (!pe.RvaToOffset(rva, offset)) return false;
            if (!InBounds(pe.data, offset, size)) return false;
            outAt = offset;
            outSize = size;
            return true;
        }

        bool FindResourceType(const PeResources& pe, uint32_t type,
                              ResourceEntry& out) {
            for (const ResourceEntry& e : ReadResourceEntries(pe, 0)) {
                if (!e.isNamed && e.id == type && e.isDirectory) {
                    out = e;
                    return true;
                }
            }
            return false;
        }

        // The RT_ICON resource with this id, as a frame.
        bool FindIconResource(const PeResources& pe, uint32_t iconId,
                              IconFrame& out) {
            ResourceEntry icons;
            if (!FindResourceType(pe, kResourceTypeIcon, icons)) return false;
            for (const ResourceEntry& e : ReadResourceEntries(pe, icons.offset)) {
                if (e.isNamed || e.id != iconId) continue;
                size_t at = 0;
                uint32_t size = 0;
                if (!ReadResourceData(pe, e.offset, e.isDirectory, at, size))
                    return false;
                out.offset = at;
                out.size = size;
                return true;
            }
            return false;
        }

        // A group icon resource is the directory of an ".ico" without the
        // frame data: each record names the RT_ICON resource holding it.
        std::shared_ptr<UCPixmap> LoadFromGroupIcon(const PeResources& pe,
                                                    size_t groupAt,
                                                    uint32_t groupSize,
                                                    int desiredSize) {
            const std::vector<uint8_t>& b = pe.data;
            if (groupSize < 6 || !InBounds(b, groupAt, groupSize)) return nullptr;
            const uint16_t count = U16(b, groupAt + 4);
            if (count == 0) return nullptr;
            std::vector<IconFrame> frames;
            std::vector<uint32_t> iconIds;
            for (uint16_t i = 0; i < count; ++i) {
                const size_t at = groupAt + 6 + static_cast<size_t>(i) * 14;
                if (!InBounds(b, at, 14) || at + 14 > groupAt + groupSize) break;
                IconFrame frame;
                frame.width = DeclaredEdge(b[at]);
                frame.height = DeclaredEdge(b[at + 1]);
                frame.bitCount = U16(b, at + 6);
                frames.push_back(frame);
                iconIds.push_back(U16(b, at + 12));
            }
            if (frames.empty()) return nullptr;
            // The best-fitting frame first, then the rest: a file can name a
            // resource that is not there, and the picture at the wrong size
            // beats no picture at all.
            std::vector<size_t> order;
            const size_t best = PickFrame(frames, desiredSize);
            order.push_back(best);
            for (size_t i = 0; i < frames.size(); ++i)
                if (i != best) order.push_back(i);
            for (size_t index : order) {
                IconFrame frame = frames[index];
                if (!FindIconResource(pe, iconIds[index], frame)) continue;
                if (auto pixmap = DecodeFrame(b, frame)) return pixmap;
            }
            return nullptr;
        }

        std::shared_ptr<UCPixmap> LoadFromPortableExecutable(
                const std::string& path, int index, int desiredSize) {
            const PeResources pe = LoadPeResources(path);
            if (!pe.valid) return nullptr;
            ResourceEntry groups;
            if (!FindResourceType(pe, kResourceTypeGroupIcon, groups))
                return nullptr;
            const std::vector<ResourceEntry> entries =
                    ReadResourceEntries(pe, groups.offset);
            if (entries.empty()) return nullptr;

            // Windows' index convention, which is what a shortcut's icon
            // index means: negative names a resource id, non-negative counts
            // the file's icons in resource order.
            std::vector<const ResourceEntry*> ordered;
            if (index < 0) {
                const uint32_t wanted = static_cast<uint32_t>(-index);
                for (const ResourceEntry& e : entries)
                    if (!e.isNamed && e.id == wanted) ordered.push_back(&e);
            } else if (static_cast<size_t>(index) < entries.size()) {
                ordered.push_back(&entries[static_cast<size_t>(index)]);
            }
            // An index that names nothing falls back to the first icon: that
            // is the program's own icon, and showing it beats showing the
            // generic sheet because a stale index was written into a link.
            if (ordered.empty()) ordered.push_back(&entries.front());

            for (const ResourceEntry* entry : ordered) {
                size_t at = 0;
                uint32_t size = 0;
                if (!ReadResourceData(pe, entry->offset, entry->isDirectory,
                                      at, size))
                    continue;
                if (auto pixmap = LoadFromGroupIcon(pe, at, size, desiredSize))
                    return pixmap;
            }
            return nullptr;
        }

        // ===== APPLE ICON FILES (.icns) =====
        // A flat list of elements - a four-character type, a length, and the
        // data - inside an 8-byte header. Every element is one rendition of
        // the same icon: modern ones hold a PNG, older ones a run-length
        // encoded bitmap whose transparency arrives as a separate element.
        struct IcnsElement {
            std::string type;
            size_t offset = 0;
            uint32_t size = 0;
        };

        uint32_t U32Big(const std::vector<uint8_t>& b, size_t at) {
            if (at + 4 > b.size()) return 0;
            return (static_cast<uint32_t>(b[at]) << 24) |
                   (static_cast<uint32_t>(b[at + 1]) << 16) |
                   (static_cast<uint32_t>(b[at + 2]) << 8) |
                   static_cast<uint32_t>(b[at + 3]);
        }

        // The edge length an element type stands for, and whether it is the
        // 24-bit RLE form (which needs the mask element beside it) or the
        // ARGB form. 0 = a type that is not a rendition of the icon (the
        // table of contents, the version, the 1-bit ancestors).
        struct IcnsType {
            int edge = 0;
            bool rleRgb = false;    // is32 / il32 / ih32 / it32
            bool rleArgb = false;   // ic04 / ic05
        };

        IcnsType ClassifyIcnsType(const std::string& type) {
            // Modern renditions: the data is a PNG (or a JPEG 2000, which the
            // image pipeline may or may not read).
            if (type == "icp4") return {16, false, false};
            if (type == "icp5") return {32, false, false};
            if (type == "icp6") return {64, false, false};
            if (type == "ic07") return {128, false, false};
            if (type == "ic08") return {256, false, false};
            if (type == "ic09") return {512, false, false};
            if (type == "ic10") return {1024, false, false};
            if (type == "ic11") return {32, false, false};
            if (type == "ic12") return {64, false, false};
            if (type == "ic13") return {256, false, false};
            if (type == "ic14") return {512, false, false};
            // Small ARGB renditions, run-length encoded.
            if (type == "ic04") return {16, false, true};
            if (type == "ic05") return {32, false, true};
            // The classic 24-bit renditions, with their masks below.
            if (type == "is32") return {16, true, false};
            if (type == "il32") return {32, true, false};
            if (type == "ih32") return {48, true, false};
            if (type == "it32") return {128, true, false};
            return {};
        }

        // The mask element that belongs to a 24-bit rendition.
        std::string IcnsMaskType(const std::string& type) {
            if (type == "is32") return "s8mk";
            if (type == "il32") return "l8mk";
            if (type == "ih32") return "h8mk";
            if (type == "it32") return "t8mk";
            return {};
        }

        // Apple's variant of PackBits, run over one channel at a time: a
        // lead byte < 0x80 means (n+1) literal bytes, >= 0x80 means the next
        // byte repeated (n - 0x7D) times.
        bool DecodeIcnsRuns(const std::vector<uint8_t>& b, size_t at, size_t end,
                            size_t& cursor, std::vector<uint8_t>& out,
                            size_t wanted) {
            out.clear();
            out.reserve(wanted);
            cursor = at;
            while (out.size() < wanted) {
                if (cursor >= end) return false;
                const uint8_t lead = b[cursor++];
                if (lead < 0x80) {
                    const size_t count = static_cast<size_t>(lead) + 1;
                    if (cursor + count > end) return false;
                    out.insert(out.end(), b.begin() + cursor,
                               b.begin() + cursor + count);
                    cursor += count;
                } else {
                    const size_t count = static_cast<size_t>(lead) - 0x7D;
                    if (cursor >= end) return false;
                    const uint8_t value = b[cursor++];
                    out.insert(out.end(), count, value);
                }
            }
            return out.size() >= wanted;
        }

        std::shared_ptr<UCPixmap> BuildPixmapFromChannels(
                int edge, const std::vector<uint8_t>& red,
                const std::vector<uint8_t>& green,
                const std::vector<uint8_t>& blue,
                const std::vector<uint8_t>& alpha) {
            const size_t pixels = static_cast<size_t>(edge) * edge;
            if (red.size() < pixels || green.size() < pixels || blue.size() < pixels)
                return nullptr;
            auto pixmap = std::make_shared<UCPixmap>(edge, edge);
            if (!pixmap->IsValid()) return nullptr;
            for (int y = 0; y < edge; ++y) {
                for (int x = 0; x < edge; ++x) {
                    const size_t i = static_cast<size_t>(y) * edge + x;
                    const uint8_t a = i < alpha.size() ? alpha[i] : 255;
                    const uint32_t pixel =
                            (static_cast<uint32_t>(a) << 24) |
                            (static_cast<uint32_t>(red[i] * a / 255) << 16) |
                            (static_cast<uint32_t>(green[i] * a / 255) << 8) |
                            static_cast<uint32_t>(blue[i] * a / 255);
                    pixmap->SetPixel(x, y, pixel);
                }
            }
            pixmap->MarkDirty();
            return pixmap;
        }

        // A 24-bit rendition: the three colour channels run-length encoded one
        // after another, with the transparency in a separate element. The
        // 128px rendition starts with four bytes of padding.
        std::shared_ptr<UCPixmap> DecodeIcnsRgbElement(
                const std::vector<uint8_t>& b, const IcnsElement& element,
                int edge, const std::vector<uint8_t>& alpha) {
            size_t at = element.offset;
            const size_t end = element.offset + element.size;
            if (edge == 128 && at + 4 <= end && U32Big(b, at) == 0) at += 4;
            const size_t pixels = static_cast<size_t>(edge) * edge;
            std::vector<uint8_t> channels[3];
            size_t cursor = at;
            for (std::vector<uint8_t>& channel : channels) {
                if (!DecodeIcnsRuns(b, cursor, end, cursor, channel, pixels))
                    return nullptr;
            }
            return BuildPixmapFromChannels(edge, channels[0], channels[1],
                                           channels[2], alpha);
        }

        // An ARGB rendition: the same encoding with an alpha channel first,
        // behind an "ARGB" marker.
        std::shared_ptr<UCPixmap> DecodeIcnsArgbElement(
                const std::vector<uint8_t>& b, const IcnsElement& element,
                int edge) {
            size_t at = element.offset;
            const size_t end = element.offset + element.size;
            if (at + 4 <= end && b[at] == 'A' && b[at + 1] == 'R' &&
                b[at + 2] == 'G' && b[at + 3] == 'B')
                at += 4;
            const size_t pixels = static_cast<size_t>(edge) * edge;
            std::vector<uint8_t> channels[4];
            size_t cursor = at;
            for (std::vector<uint8_t>& channel : channels) {
                if (!DecodeIcnsRuns(b, cursor, end, cursor, channel, pixels))
                    return nullptr;
            }
            return BuildPixmapFromChannels(edge, channels[1], channels[2],
                                           channels[3], channels[0]);
        }

        std::shared_ptr<UCPixmap> DecodeIcnsBytes(const std::vector<uint8_t>& b,
                                                  int desiredSize) {
            if (b.size() < 8 || b[0] != 'i' || b[1] != 'c' || b[2] != 'n' ||
                b[3] != 's')
                return nullptr;
            const uint32_t declared = U32Big(b, 4);
            const size_t end = std::min<size_t>(
                    b.size(), declared >= 8 ? declared : b.size());

            std::vector<IcnsElement> elements;
            size_t at = 8;
            while (at + 8 <= end) {
                IcnsElement element;
                element.type.assign(b.begin() + at, b.begin() + at + 4);
                const uint32_t size = U32Big(b, at + 4);
                if (size < 8 || at + size > end) break;
                element.offset = at + 8;
                element.size = size - 8;
                elements.push_back(std::move(element));
                at += size;
            }
            if (elements.empty()) return nullptr;

            // Every rendition, as a frame the shared size preference can
            // choose between.
            std::vector<IconFrame> frames;
            std::vector<const IcnsElement*> sources;
            std::vector<IcnsType> kinds;
            for (const IcnsElement& element : elements) {
                const IcnsType kind = ClassifyIcnsType(element.type);
                if (kind.edge == 0) continue;
                IconFrame frame;
                frame.width = frame.height = kind.edge;
                frame.bitCount = kind.rleRgb ? 24 : 32;
                frame.offset = element.offset;
                frame.size = element.size;
                frames.push_back(frame);
                sources.push_back(&element);
                kinds.push_back(kind);
            }
            if (frames.empty()) return nullptr;

            std::vector<size_t> order;
            order.push_back(PickFrame(frames, desiredSize));
            for (size_t i = 0; i < frames.size(); ++i)
                if (i != order.front()) order.push_back(i);

            for (size_t index : order) {
                const IcnsElement& element = *sources[index];
                const IcnsType& kind = kinds[index];
                if (kind.rleArgb) {
                    if (auto pixmap = DecodeIcnsArgbElement(b, element, kind.edge))
                        return pixmap;
                    continue;
                }
                if (kind.rleRgb) {
                    // The transparency of a classic rendition lives in its
                    // own element; without it the icon is a solid square.
                    std::vector<uint8_t> alpha;
                    const std::string maskType = IcnsMaskType(element.type);
                    for (const IcnsElement& mask : elements) {
                        if (mask.type != maskType) continue;
                        const size_t pixels =
                                static_cast<size_t>(kind.edge) * kind.edge;
                        if (mask.size >= pixels)
                            alpha.assign(b.begin() + mask.offset,
                                         b.begin() + mask.offset + pixels);
                        break;
                    }
                    if (auto pixmap = DecodeIcnsRgbElement(b, element, kind.edge,
                                                           alpha))
                        return pixmap;
                    continue;
                }
                // A modern rendition: PNG, or a JPEG 2000 this build may not
                // read - either way it is the image pipeline's to decode.
                if (auto pixmap = DecodePngFrame(b, element.offset, element.size))
                    return pixmap;
            }
            return nullptr;
        }

    } // namespace

    bool HasIconResourceExtension(const std::string& path) {
        const std::string ext = LowerExtensionOf(path);
        return ext == "ico" || ext == "icns" || ext == "exe" || ext == "dll" ||
               ext == "icl" || ext == "cpl" || ext == "ocx" || ext == "scr" ||
               ext == "mun";
    }

    std::shared_ptr<UCPixmap> DecodeIconFileBytes(const std::vector<uint8_t>& bytes,
                                                  int desiredSize) {
        // The two icon containers are told apart by their first bytes, not by
        // the name they arrived under.
        if (auto pixmap = DecodeIcnsBytes(bytes, desiredSize)) return pixmap;
        std::vector<IconFrame> frames;
        if (!ParseIconDirectory(bytes, frames)) return nullptr;
        const size_t best = PickFrame(frames, desiredSize);
        if (auto pixmap = DecodeFrame(bytes, frames[best])) return pixmap;
        // The chosen frame is corrupt or in a form this build cannot decode
        // (a PNG frame without an image pipeline): try the rest.
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i == best) continue;
            if (auto pixmap = DecodeFrame(bytes, frames[i])) return pixmap;
        }
        return nullptr;
    }

    std::shared_ptr<UCPixmap> LoadIconResource(const std::string& path,
                                               int index, int desiredSize) {
        if (path.empty() || !HasIconResourceExtension(path)) return nullptr;
        const int wanted = std::max(1, std::min(desiredSize, kMaxIconEdge));
        // The first bytes decide which of the two readers this file is for -
        // the extension only decides whether to look at all, because an
        // ".exe" that is a script and a ".ico" that is a PNG both exist.
        std::vector<uint8_t> magic;
        if (!ReadFileRange(path, 0, 2, magic) || magic.size() < 2) return nullptr;
        if (magic[0] == 'M' && magic[1] == 'Z')
            return LoadFromPortableExecutable(path, index, wanted);
        std::vector<uint8_t> bytes;
        if (!ReadFileRange(path, 0, kMaxIconFileBytes, bytes)) return nullptr;
        return DecodeIconFileBytes(bytes, wanted);
    }

} // namespace UltraCanvas
