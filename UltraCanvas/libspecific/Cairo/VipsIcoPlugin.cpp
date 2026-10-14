// libspecific/Cairo/VipsIcoPlugin.cpp
// libvips foreign loader and saver for Windows icons (.ico). See
// VipsIcoPlugin.h.
//
// An icon file is a directory of pictures: a 6-byte header (reserved 0, type
// 1 for an icon or 2 for a cursor, count), 16 bytes per entry (width and
// height - 0 meaning 256 -, colours, planes, bits per pixel, data size and
// offset), then the pictures. Each is a PNG file, or a bitmap without its
// file header: a BITMAPINFOHEADER whose height counts the colour rows and the
// AND mask's together, the palette for 8 bits or fewer, the colour rows and
// the mask, both bottom-up and padded to 32 bits.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "VipsIcoPlugin.h"

#include <vips/vips8>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// ============================================================================
// THE FORMAT
// ============================================================================
namespace {

    uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    uint32_t U32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    void Put16(std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    void Put32(std::vector<uint8_t>& out, uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }

    const uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    struct IcoEntry {
        int width = 0;       // from the directory; the picture's own is read below
        int height = 0;
        int bitCount = 0;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    // The directory, every entry inside the data. Empty when it is no icon.
    std::vector<IcoEntry> ReadDirectory(const uint8_t* data, size_t length) {
        std::vector<IcoEntry> entries;
        if (length < 6 || U16(data) != 0) return entries;
        const uint16_t type = U16(data + 2);
        if (type != 1 && type != 2) return entries;
        const size_t count = U16(data + 4);
        if (count == 0 || 6 + count * 16 > length) return entries;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* e = data + 6 + i * 16;
            IcoEntry entry;
            entry.width = e[0] ? e[0] : 256;
            entry.height = e[1] ? e[1] : 256;
            // A cursor keeps its hotspot where an icon keeps planes and depth.
            entry.bitCount = type == 1 ? U16(e + 6) : 0;
            entry.size = U32(e + 8);
            entry.offset = U32(e + 12);
            if (entry.size == 0 || entry.offset >= length || entry.size > length - entry.offset) continue;
            entries.push_back(entry);
        }
        return entries;
    }

    bool IsPng(const uint8_t* p, size_t n) { return n >= 24 && std::memcmp(p, kPngSignature, 8) == 0; }

    // The picture's own size: a PNG's IHDR, a bitmap's header (half its
    // height: the colour rows and the mask). False when it says nothing usable.
    bool PictureSize(const uint8_t* data, const IcoEntry& entry, int& w, int& h) {
        const uint8_t* p = data + entry.offset;
        if (IsPng(p, entry.size)) {
            w = static_cast<int>((p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19]);
            h = static_cast<int>((p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23]);
        } else {
            if (entry.size < 40 || U32(p) < 40) return false;
            w = static_cast<int32_t>(U32(p + 4));
            h = std::abs(static_cast<int32_t>(U32(p + 8))) / 2;
        }
        return w > 0 && h > 0 && w <= 65535 && h <= 65535;
    }

    // The entry to show: the most pixels, then the most colours.
    bool PickLargest(const uint8_t* data, size_t length, IcoEntry& best, int& w, int& h) {
        bool found = false;
        long long bestArea = -1;
        for (const IcoEntry& entry : ReadDirectory(data, length)) {
            int ew = 0, eh = 0;
            if (!PictureSize(data, entry, ew, eh)) continue;
            const long long area = static_cast<long long>(ew) * eh;
            if (area > bestArea || (area == bestArea && entry.bitCount > best.bitCount)) {
                best = entry;
                w = ew;
                h = eh;
                bestArea = area;
                found = true;
            }
        }
        return found;
    }

    // A bitmap entry as straight RGBA, top row first. Empty on anything the
    // reader does not understand.
    std::vector<uint8_t> DecodeBitmap(const uint8_t* p, size_t n, int w, int h) {
        std::vector<uint8_t> rgba;
        const uint32_t headerSize = U32(p);
        const int bpp = U16(p + 14);
        const uint32_t compression = U32(p + 16);
        // BI_RGB, or BI_BITFIELDS with the usual 32-bit masks.
        if (compression != 0 && !(compression == 3 && bpp == 32)) return rgba;
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return rgba;
        size_t at = headerSize + (compression == 3 && headerSize == 40 ? 12 : 0);
        std::vector<uint8_t> palette;
        if (bpp <= 8) {
            uint32_t colours = U32(p + 32);
            if (colours == 0 || colours > (1u << bpp)) colours = 1u << bpp;
            if (at + colours * 4 > n) return rgba;
            palette.assign(p + at, p + at + colours * 4);
            at += colours * 4;
        }
        const size_t stride = ((static_cast<size_t>(w) * bpp + 31) / 32) * 4;
        const size_t maskStride = ((static_cast<size_t>(w) + 31) / 32) * 4;
        if (at + stride * h > n) return rgba;
        const uint8_t* colour = p + at;
        // A 32-bit picture may leave the mask out; anything else needs it.
        const bool hasMask = at + stride * h + maskStride * h <= n;
        if (!hasMask && bpp != 32) return rgba;
        const uint8_t* mask = colour + stride * h;

        rgba.resize(static_cast<size_t>(w) * h * 4);
        bool anyAlpha = false;
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = colour + stride * (h - 1 - y);
            uint8_t* out = rgba.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, out += 4) {
                if (bpp == 32) {
                    out[0] = row[x * 4 + 2]; out[1] = row[x * 4 + 1]; out[2] = row[x * 4]; out[3] = row[x * 4 + 3];
                    anyAlpha = anyAlpha || out[3] != 0;
                } else if (bpp == 24) {
                    out[0] = row[x * 3 + 2]; out[1] = row[x * 3 + 1]; out[2] = row[x * 3]; out[3] = 255;
                } else {
                    const int perByte = 8 / bpp;
                    const int shift = (perByte - 1 - x % perByte) * bpp;
                    const size_t index = (row[x / perByte] >> shift) & ((1 << bpp) - 1);
                    if (index * 4 + 2 < palette.size()) {
                        out[0] = palette[index * 4 + 2]; out[1] = palette[index * 4 + 1]; out[2] = palette[index * 4];
                    } else {
                        out[0] = out[1] = out[2] = 0;
                    }
                    out[3] = 255;
                }
            }
        }
        // The AND mask: a set bit is a transparent pixel. A 32-bit picture
        // with an alpha channel of its own goes by that instead.
        if (hasMask && !(bpp == 32 && anyAlpha)) {
            for (int y = 0; y < h; ++y) {
                const uint8_t* row = mask + maskStride * (h - 1 - y);
                uint8_t* out = rgba.data() + static_cast<size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x)
                    out[x * 4 + 3] = (row[x / 8] >> (7 - x % 8)) & 1 ? 0 : 255;
            }
        } else if (bpp == 32 && !anyAlpha) {
            for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        }
        return rgba;
    }

    // The entry as straight RGBA bytes, `w` x `h`. False (with a vips error)
    // when it cannot be read.
    bool DecodeEntry(const char* nickname, const uint8_t* data, const IcoEntry& entry, int w, int h,
                     std::vector<uint8_t>& rgba) {
        const uint8_t* p = data + entry.offset;
        if (IsPng(p, entry.size)) {
            try {
                VipsBlob* blob = vips_blob_new(nullptr, p, entry.size);
                vips::VImage png = vips::VImage::pngload_buffer(blob);
                vips_area_unref(VIPS_AREA(blob));
                png = png.colourspace(VIPS_INTERPRETATION_sRGB);
                if (png.format() != VIPS_FORMAT_UCHAR) png = png.cast(VIPS_FORMAT_UCHAR);
                if (png.bands() == 3) png = png.bandjoin(255);
                if (png.bands() != 4 || png.width() != w || png.height() != h) {
                    vips_error(nickname, "an icon's PNG picture is not the size it says");
                    return false;
                }
                size_t size = 0;
                void* memory = png.write_to_memory(&size);
                rgba.assign(static_cast<uint8_t*>(memory), static_cast<uint8_t*>(memory) + size);
                g_free(memory);
                return rgba.size() == static_cast<size_t>(w) * h * 4;
            } catch (const vips::VError& e) {
                vips_error(nickname, "%s", e.what());
                return false;
            }
        }
        rgba = DecodeBitmap(p, entry.size, w, h);
        if (rgba.empty()) {
            vips_error(nickname, "an icon picture this reader does not understand");
            return false;
        }
        return true;
    }

    // An icon holding one PNG picture per size.
    struct EncodedPicture {
        int size = 0;
        std::vector<uint8_t> png;
    };

    std::vector<uint8_t> WriteIcon(const std::vector<EncodedPicture>& pictures) {
        std::vector<uint8_t> out;
        Put16(out, 0);
        Put16(out, 1);
        Put16(out, static_cast<uint16_t>(pictures.size()));
        uint32_t offset = static_cast<uint32_t>(6 + 16 * pictures.size());
        for (const EncodedPicture& picture : pictures) {
            out.push_back(static_cast<uint8_t>(picture.size >= 256 ? 0 : picture.size));
            out.push_back(static_cast<uint8_t>(picture.size >= 256 ? 0 : picture.size));
            out.push_back(0);   // no palette
            out.push_back(0);
            Put16(out, 1);      // planes
            Put16(out, 32);     // bits per pixel
            Put32(out, static_cast<uint32_t>(picture.png.size()));
            Put32(out, offset);
            offset += static_cast<uint32_t>(picture.png.size());
        }
        for (const EncodedPicture& picture : pictures) out.insert(out.end(), picture.png.begin(), picture.png.end());
        return out;
    }

    // The sizes an icon of a `side`-pixel picture holds: those asked for (the
    // usual four when none were), each 1..256, none larger than the picture
    // - an icon is not made by blowing a small picture up - and the
    // picture's own size when every one asked for is.
    std::vector<int> IconSizes(const VipsArrayInt* asked, int side) {
        std::set<int> sizes;
        int count = 0;
        const int* values = asked ? vips_array_int_get(const_cast<VipsArrayInt*>(asked), &count) : nullptr;
        if (values && count > 0) {
            for (int i = 0; i < count; ++i) sizes.insert(std::clamp(values[i], 1, 256));
        } else {
            sizes = {16, 32, 48, 256};
        }
        std::vector<int> out;
        for (int s : sizes)
            if (s <= side) out.push_back(s);
        if (out.empty()) out.push_back(std::clamp(side, 1, 256));
        return out;
    }

} // namespace

// ============================================================================
// LOADER
// ============================================================================
typedef struct _VipsForeignLoadIco {
    VipsForeignLoad parent_object;
    VipsSource* source;
} VipsForeignLoadIco;

typedef VipsForeignLoadClass VipsForeignLoadIcoClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignLoadIco, vips_foreign_load_ico, VIPS_TYPE_FOREIGN_LOAD)

static void vips_foreign_load_ico_dispose(GObject* gobject) {
    VipsForeignLoadIco* ico = reinterpret_cast<VipsForeignLoadIco*>(gobject);
    VIPS_UNREF(ico->source);
    G_OBJECT_CLASS(vips_foreign_load_ico_parent_class)->dispose(gobject);
}

static VipsForeignFlags vips_foreign_load_ico_get_flags(VipsForeignLoad*) {
    return VIPS_FOREIGN_SEQUENTIAL;
}

// The header and a directory whose entries lie inside the file: four bytes
// alone (00 00 01 00) are too common a start to claim a file on.
static gboolean vips_foreign_load_ico_is_a_source(VipsSource* source) {
    const unsigned char* head = static_cast<const unsigned char*>(vips_source_sniff(source, 22));
    if (!head) return FALSE;
    if (head[0] != 0 || head[1] != 0 || (head[2] != 1 && head[2] != 2) || head[3] != 0) return FALSE;
    const int count = head[4] | (head[5] << 8);
    if (count < 1 || count > 512) return FALSE;
    const uint32_t size = U32(head + 14), offset = U32(head + 18);
    // An icon's planes are 0 or 1; a cursor keeps its hotspot there.
    const int planes = head[10] | (head[11] << 8);
    if (head[2] == 1 && planes > 1) return FALSE;
    return size > 0 && offset >= static_cast<uint32_t>(6 + 16 * count) ? TRUE : FALSE;
}

static gboolean vips_foreign_load_ico_file_is_a(const char* filename) {
    VipsSource* source = vips_source_new_from_file(filename);
    if (!source) return FALSE;
    const gboolean result = vips_foreign_load_ico_is_a_source(source);
    VIPS_UNREF(source);
    return result;
}

static int vips_foreign_load_ico_header(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadIco* ico = reinterpret_cast<VipsForeignLoadIco*>(load);
    size_t length = 0;
    const uint8_t* data = static_cast<const uint8_t*>(vips_source_map(ico->source, &length));
    if (!data) return -1;
    IcoEntry entry;
    int w = 0, h = 0;
    if (!PickLargest(data, length, entry, w, h)) {
        vips_error(klass->nickname, "no picture in this icon can be read");
        return -1;
    }
    vips_image_init_fields(load->out, w, h, 4, VIPS_FORMAT_UCHAR, VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB, 1, 1);
    VIPS_SETSTR(load->out->filename, vips_connection_filename(VIPS_CONNECTION(ico->source)));
    return 0;
}

static int vips_foreign_load_ico_load(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadIco* ico = reinterpret_cast<VipsForeignLoadIco*>(load);
    size_t length = 0;
    const uint8_t* data = static_cast<const uint8_t*>(vips_source_map(ico->source, &length));
    if (!data) return -1;
    IcoEntry entry;
    int w = 0, h = 0;
    if (!PickLargest(data, length, entry, w, h)) {
        vips_error(klass->nickname, "no picture in this icon can be read");
        return -1;
    }
    std::vector<uint8_t> rgba;
    if (!DecodeEntry(klass->nickname, data, entry, w, h, rgba)) return -1;

    VipsImage* out = load->real;
    vips_image_init_fields(out, w, h, 4, VIPS_FORMAT_UCHAR, VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB, 1, 1);
    VIPS_SETSTR(out->filename, vips_connection_filename(VIPS_CONNECTION(ico->source)));
    for (int y = 0; y < h; ++y) {
        if (vips_image_write_line(out, y, rgba.data() + static_cast<size_t>(y) * w * 4)) return -1;
    }
    vips_source_decode(ico->source);
    return 0;
}

static void vips_foreign_load_ico_init(VipsForeignLoadIco*) {}

static void vips_foreign_load_ico_class_init(VipsForeignLoadIcoClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass* load_class = VIPS_FOREIGN_LOAD_CLASS(klass);
    gobject_class->dispose = vips_foreign_load_ico_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "icoload_base";
    object_class->description = "Windows icon loader";
    load_class->get_flags = vips_foreign_load_ico_get_flags;
    load_class->is_a_source = vips_foreign_load_ico_is_a_source;
    load_class->header = vips_foreign_load_ico_header;
    load_class->load = vips_foreign_load_ico_load;
}

// ----- from a file -----
typedef struct _VipsForeignLoadIcoFile {
    VipsForeignLoadIco parent;
    char* filename;
} VipsForeignLoadIcoFile;

typedef VipsForeignLoadIcoClass VipsForeignLoadIcoFileClass;

G_DEFINE_TYPE(VipsForeignLoadIcoFile, vips_foreign_load_ico_file, vips_foreign_load_ico_get_type())

static int vips_foreign_load_ico_file_build(VipsObject* object) {
    VipsForeignLoadIco* ico = reinterpret_cast<VipsForeignLoadIco*>(object);
    VipsForeignLoadIcoFile* file = reinterpret_cast<VipsForeignLoadIcoFile*>(object);
    if (file->filename && !(ico->source = vips_source_new_from_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ico_file_parent_class)->build(object);
}

static void vips_foreign_load_ico_file_init(VipsForeignLoadIcoFile*) {}

static const char* vips__ico_suffs[] = {".ico", ".cur", nullptr};
static const char* vips__ico_save_suffs[] = {".ico", nullptr};

static void vips_foreign_load_ico_file_class_init(VipsForeignLoadIcoFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "icoload";
    object_class->description = "load a Windows icon from a file";
    object_class->build = vips_foreign_load_ico_file_build;
    foreign_class->suffs = vips__ico_suffs;
    load_class->is_a = vips_foreign_load_ico_file_is_a;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to load from",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadIcoFile, filename), nullptr);
}

// ----- from memory -----
typedef struct _VipsForeignLoadIcoBuffer {
    VipsForeignLoadIco parent;
    VipsBlob* blob;
} VipsForeignLoadIcoBuffer;

typedef VipsForeignLoadIcoClass VipsForeignLoadIcoBufferClass;

G_DEFINE_TYPE(VipsForeignLoadIcoBuffer, vips_foreign_load_ico_buffer, vips_foreign_load_ico_get_type())

static int vips_foreign_load_ico_buffer_build(VipsObject* object) {
    VipsForeignLoadIco* ico = reinterpret_cast<VipsForeignLoadIco*>(object);
    VipsForeignLoadIcoBuffer* buffer = reinterpret_cast<VipsForeignLoadIcoBuffer*>(object);
    if (buffer->blob && !(ico->source = vips_source_new_from_blob(buffer->blob))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ico_buffer_parent_class)->build(object);
}

static gboolean vips_foreign_load_ico_buffer_is_a(const void* data, size_t size) {
    VipsSource* source = vips_source_new_from_memory(data, size);
    if (!source) return FALSE;
    const gboolean result = vips_foreign_load_ico_is_a_source(source);
    VIPS_UNREF(source);
    return result;
}

static void vips_foreign_load_ico_buffer_init(VipsForeignLoadIcoBuffer*) {}

static void vips_foreign_load_ico_buffer_class_init(VipsForeignLoadIcoBufferClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "icoload_buffer";
    object_class->description = "load a Windows icon from memory";
    object_class->build = vips_foreign_load_ico_buffer_build;
    load_class->is_a_buffer = vips_foreign_load_ico_buffer_is_a;
    VIPS_ARG_BOXED(klass, "buffer", 1, "Buffer", "Buffer to load from",
                   VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadIcoBuffer, blob), VIPS_TYPE_BLOB);
}

// ============================================================================
// SAVER
// ============================================================================
typedef struct _VipsForeignSaveIco {
    VipsForeignSave parent_object;
    VipsTarget* target;
    VipsArrayInt* sizes;
} VipsForeignSaveIco;

typedef VipsForeignSaveClass VipsForeignSaveIcoClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignSaveIco, vips_foreign_save_ico, VIPS_TYPE_FOREIGN_SAVE)

static void vips_foreign_save_ico_dispose(GObject* gobject) {
    VipsForeignSaveIco* ico = reinterpret_cast<VipsForeignSaveIco*>(gobject);
    VIPS_UNREF(ico->target);
    G_OBJECT_CLASS(vips_foreign_save_ico_parent_class)->dispose(gobject);
}

static int vips_foreign_save_ico_build(VipsObject* object) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(object);
    VipsForeignSave* save = reinterpret_cast<VipsForeignSave*>(object);
    VipsForeignSaveIco* ico = reinterpret_cast<VipsForeignSaveIco*>(object);
    if (VIPS_OBJECT_CLASS(vips_foreign_save_ico_parent_class)->build(object)) return -1;

    std::vector<uint8_t> bytes;
    try {
        // `ready` is 8-bit RGB or RGBA (the class's saveable and format table).
        vips::VImage picture(save->ready, vips::NOSTEAL);
        if (picture.bands() == 3) picture = picture.bandjoin(255);
        std::vector<double> clear = {0, 0, 0, 0};
        std::vector<EncodedPicture> pictures;
        for (int size : IconSizes(ico->sizes, std::max(picture.width(), picture.height()))) {
            // Fitted into the square, keeping its shape, on transparency.
            vips::VImage fitted = picture.thumbnail_image(size, vips::VImage::option()->set("height", size));
            fitted = fitted.gravity(VIPS_COMPASS_DIRECTION_CENTRE, size, size, vips::VImage::option()
                    ->set("extend", VIPS_EXTEND_BACKGROUND)
                    ->set("background", clear));
            if (fitted.format() != VIPS_FORMAT_UCHAR) fitted = fitted.cast(VIPS_FORMAT_UCHAR);
            VipsBlob* blob = fitted.pngsave_buffer();
            size_t length = 0;
            const void* png = vips_blob_get(blob, &length);
            EncodedPicture encoded;
            encoded.size = size;
            encoded.png.assign(static_cast<const uint8_t*>(png), static_cast<const uint8_t*>(png) + length);
            vips_area_unref(VIPS_AREA(blob));
            pictures.push_back(std::move(encoded));
        }
        bytes = WriteIcon(pictures);
    } catch (const vips::VError& e) {
        vips_error(klass->nickname, "%s", e.what());
        return -1;
    }
    if (vips_target_write(ico->target, bytes.data(), bytes.size())) return -1;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 13)
    if (vips_target_end(ico->target)) return -1;
#else
    vips_target_finish(ico->target);
#endif
    return 0;
}

// Every band format is saved as 8 bits.
static VipsBandFormat vips_foreign_save_ico_format_table[10] = {
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR,
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR};

static void vips_foreign_save_ico_init(VipsForeignSaveIco*) {}

static void vips_foreign_save_ico_class_init(VipsForeignSaveIcoClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignSaveClass* save_class = reinterpret_cast<VipsForeignSaveClass*>(klass);
    gobject_class->dispose = vips_foreign_save_ico_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "icosave_base";
    object_class->description = "save as a Windows icon";
    object_class->build = vips_foreign_save_ico_build;
    foreign_class->suffs = vips__ico_save_suffs;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 17)
    save_class->saveable = static_cast<VipsForeignSaveable>(VIPS_FOREIGN_SAVEABLE_RGB | VIPS_FOREIGN_SAVEABLE_ALPHA);
#else
    save_class->saveable = VIPS_SAVEABLE_RGBA_ONLY;
#endif
    save_class->format_table = vips_foreign_save_ico_format_table;
    VIPS_ARG_BOXED(klass, "sizes", 14, "Sizes", "The squares to write, 1 to 256 pixels",
                   VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveIco, sizes), VIPS_TYPE_ARRAY_INT);
}

// ----- to a file -----
typedef struct _VipsForeignSaveIcoFile {
    VipsForeignSaveIco parent;
    char* filename;
} VipsForeignSaveIcoFile;

typedef VipsForeignSaveIcoClass VipsForeignSaveIcoFileClass;

G_DEFINE_TYPE(VipsForeignSaveIcoFile, vips_foreign_save_ico_file, vips_foreign_save_ico_get_type())

static int vips_foreign_save_ico_file_build(VipsObject* object) {
    VipsForeignSaveIco* ico = reinterpret_cast<VipsForeignSaveIco*>(object);
    VipsForeignSaveIcoFile* file = reinterpret_cast<VipsForeignSaveIcoFile*>(object);
    if (!(ico->target = vips_target_new_to_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_save_ico_file_parent_class)->build(object);
}

static void vips_foreign_save_ico_file_init(VipsForeignSaveIcoFile*) {}

static void vips_foreign_save_ico_file_class_init(VipsForeignSaveIcoFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "icosave";
    object_class->description = "save as a Windows icon file";
    object_class->build = vips_foreign_save_ico_file_build;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to save to",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignSaveIcoFile, filename), nullptr);
}

// ============================================================================
// PUBLIC API
// ============================================================================
int vips_icoload(const char* filename, VipsImage** out, ...) {
    va_list ap;
    va_start(ap, out);
    const int result = vips_call_split("icoload", ap, filename, out);
    va_end(ap);
    return result;
}

int vips_icoload_buffer(void* buf, size_t len, VipsImage** out, ...) {
    VipsBlob* blob = vips_blob_new(nullptr, buf, len);
    va_list ap;
    va_start(ap, out);
    const int result = vips_call_split("icoload_buffer", ap, blob, out);
    va_end(ap);
    vips_area_unref(VIPS_AREA(blob));
    return result;
}

int vips_icosave(VipsImage* in, const char* filename, ...) {
    va_list ap;
    va_start(ap, filename);
    const int result = vips_call_split("icosave", ap, in, filename);
    va_end(ap);
    return result;
}

void vips_foreign_ico_init_types(void) {
    vips_foreign_load_ico_get_type();
    vips_foreign_load_ico_file_get_type();
    vips_foreign_load_ico_buffer_get_type();
    vips_foreign_save_ico_get_type();
    vips_foreign_save_ico_file_get_type();
}
