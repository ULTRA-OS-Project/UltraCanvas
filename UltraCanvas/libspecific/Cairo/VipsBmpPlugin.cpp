// libspecific/Cairo/VipsBmpPlugin.cpp
// libvips foreign loader for Windows bitmaps (.bmp, .dib), read by the
// bundled stb_image. See VipsBmpPlugin.h.
// Version: 1.1.0 - RLE8 and RLE4 bitmaps read here too, not by ImageMagick
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "VipsBmpPlugin.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

// stb_image for BMP alone, and private to this file: static, so its symbols
// meet no other copy of stb_image a linked library may carry.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "../../third_party/stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

// ============================================================================
// THE FORMAT
// ============================================================================
namespace {

    // The file header (14 bytes) and the largest info header (BITMAPV5HEADER,
    // 124) with its bit-field masks: all stb_image reads to say what it is.
    constexpr size_t kSniffBytes = 14 + 124 + 16;

    int32_t S32(const uint8_t* p) {
        return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                    (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }

    uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

    // ----- RLE: what stb_image does not read -----
    // BI_RLE8 (compression 1, 8 bits) and BI_RLE4 (2, 4 bits), as Windows
    // writes them: a BITMAPINFOHEADER or later, bottom-up, a palette.
    struct RleHeader {
        int width = 0;
        int height = 0;
        int bpp = 0;              // 8 or 4
        uint32_t pixelOffset = 0; // bfOffBits
        uint32_t paletteOffset = 0;
        uint32_t colours = 0;
    };

    bool ReadRleHeader(const uint8_t* data, size_t length, RleHeader& rle) {
        if (length < 14 + 40) return false;
        const int32_t headerSize = S32(data + 14);
        if (headerSize < 40 || headerSize > 1024) return false;
        const int32_t w = S32(data + 18), h = S32(data + 22);
        const int bpp = U16(data + 28);
        const int32_t compression = S32(data + 30);
        if (!((compression == 1 && bpp == 8) || (compression == 2 && bpp == 4))) return false;
        // An RLE bitmap is bottom-up: a negative height is not allowed.
        if (w <= 0 || h <= 0 || w > VIPS_MAX_COORD || h > VIPS_MAX_COORD) return false;
        uint32_t colours = static_cast<uint32_t>(S32(data + 46));
        if (colours == 0 || colours > (1u << bpp)) colours = 1u << bpp;
        rle.width = w;
        rle.height = h;
        rle.bpp = bpp;
        rle.pixelOffset = static_cast<uint32_t>(S32(data + 10));
        rle.paletteOffset = 14 + static_cast<uint32_t>(headerSize);
        rle.colours = colours;
        return true;
    }

    // The runs decoded to RGB, top row first. A pixel the runs skip (an
    // end of line, a delta, an early end of the bitmap) is palette entry 0,
    // as ImageMagick leaves it.
    bool DecodeRle(const uint8_t* data, size_t length, const RleHeader& rle, std::vector<uint8_t>& rgb) {
        if (rle.paletteOffset + rle.colours * 4 > length || rle.pixelOffset >= length) return false;
        const uint8_t* palette = data + rle.paletteOffset;
        const int w = rle.width, h = rle.height;
        rgb.assign(static_cast<size_t>(w) * h * 3, 0);
        auto put = [&](int x, int y, unsigned index) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            if (index >= rle.colours) index = 0;
            uint8_t* out = rgb.data() + (static_cast<size_t>(h - 1 - y) * w + x) * 3;
            out[0] = palette[index * 4 + 2];
            out[1] = palette[index * 4 + 1];
            out[2] = palette[index * 4];
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) put(x, y, 0);

        const uint8_t* p = data + rle.pixelOffset;
        const uint8_t* end = data + length;
        int x = 0, y = 0;
        while (end - p >= 2 && y < h) {
            const unsigned count = p[0], code = p[1];
            p += 2;
            if (count > 0) {                       // a run of one index (or a pair)
                for (unsigned i = 0; i < count; ++i, ++x)
                    put(x, y, rle.bpp == 8 ? code : (i % 2 == 0 ? code >> 4 : code & 0x0F));
            } else if (code == 0) {                // end of line
                x = 0;
                ++y;
            } else if (code == 1) {                // end of bitmap
                break;
            } else if (code == 2) {                // delta
                if (end - p < 2) break;
                x += p[0];
                y += p[1];
                p += 2;
            } else {                               // `code` indices as they are
                const size_t bytes = rle.bpp == 8 ? code : (code + 1) / 2;
                if (static_cast<size_t>(end - p) < bytes) return false;
                for (unsigned i = 0; i < code; ++i, ++x)
                    put(x, y, rle.bpp == 8 ? p[i] : (i % 2 == 0 ? p[i / 2] >> 4 : p[i / 2] & 0x0F));
                p += bytes + (bytes & 1);          // padded to a 16-bit boundary
            }
        }
        return true;
    }

    // Width, height and band count (3, or 4 when the file carries alpha),
    // when these bytes are a BMP this loader reads: stb_image's, or an RLE
    // one. An OS/2 2.x Huffman or RLE24 bitmap is left to the next loader.
    bool BmpInfo(const uint8_t* data, size_t length, int& w, int& h, int& bands) {
        if (!data || length < 26 || data[0] != 'B' || data[1] != 'M') return false;
        if (length > static_cast<size_t>(INT_MAX)) return false;
        int comp = 0;
        if (stbi_info_from_memory(data, static_cast<int>(length), &w, &h, &comp)) {
            if (w <= 0 || h <= 0 || w > VIPS_MAX_COORD || h > VIPS_MAX_COORD) return false;
            bands = comp == 4 ? 4 : 3;
            return true;
        }
        RleHeader rle;
        if (!ReadRleHeader(data, length, rle)) return false;
        w = rle.width;
        h = rle.height;
        bands = 3;
        return true;
    }

    // The whole picture, `bands` bytes a pixel, top row first. False when it
    // is damaged or truncated.
    bool DecodeBmp(const uint8_t* data, size_t length, int w, int h, int bands, std::vector<uint8_t>& pixels) {
        RleHeader rle;
        if (ReadRleHeader(data, length, rle)) {
            return rle.width == w && rle.height == h && bands == 3 && DecodeRle(data, length, rle, pixels);
        }
        int dw = 0, dh = 0, comp = 0;
        // As many bands as the header said, whatever stb_image finds on the way.
        stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(length), &dw, &dh, &comp, bands);
        if (!decoded) return false;
        const bool ok = dw == w && dh == h;
        if (ok) pixels.assign(decoded, decoded + static_cast<size_t>(w) * h * bands);
        stbi_image_free(decoded);
        return ok;
    }

    // The header's resolution in pixels per millimetre, as libvips keeps it;
    // libvips' 1.0 (72-ish dpi is not claimed) when the file gives none.
    void BmpResolution(const uint8_t* data, size_t length, double& xres, double& yres) {
        xres = yres = 1.0;
        // A BITMAPCOREHEADER (12 bytes) has no resolution fields.
        if (length < 14 + 40 || S32(data + 14) < 40) return;
        const int32_t xppm = S32(data + 14 + 24);
        const int32_t yppm = S32(data + 14 + 28);
        if (xppm > 0) xres = xppm / 1000.0;
        if (yppm > 0) yres = yppm / 1000.0;
    }

    gboolean IsBmp(const uint8_t* data, size_t length) {
        int w = 0, h = 0, bands = 0;
        return BmpInfo(data, length, w, h, bands) ? TRUE : FALSE;
    }

} // namespace

// ============================================================================
// LOADER
// ============================================================================
typedef struct _VipsForeignLoadUcBmp {
    VipsForeignLoad parent_object;
    VipsSource* source;
} VipsForeignLoadUcBmp;

typedef VipsForeignLoadClass VipsForeignLoadUcBmpClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignLoadUcBmp, vips_foreign_load_ucbmp, VIPS_TYPE_FOREIGN_LOAD)

static void vips_foreign_load_ucbmp_dispose(GObject* gobject) {
    VipsForeignLoadUcBmp* bmp = reinterpret_cast<VipsForeignLoadUcBmp*>(gobject);
    VIPS_UNREF(bmp->source);
    G_OBJECT_CLASS(vips_foreign_load_ucbmp_parent_class)->dispose(gobject);
}

static VipsForeignFlags vips_foreign_load_ucbmp_get_flags(VipsForeignLoad*) {
    return VIPS_FOREIGN_SEQUENTIAL;
}

static gboolean vips_foreign_load_ucbmp_is_a_source(VipsSource* source) {
    unsigned char* head = nullptr;
    const gint64 got = vips_source_sniff_at_most(source, &head, kSniffBytes);
    if (got <= 0 || !head) return FALSE;
    return IsBmp(head, static_cast<size_t>(got));
}

static gboolean vips_foreign_load_ucbmp_file_is_a(const char* filename) {
    VipsSource* source = vips_source_new_from_file(filename);
    if (!source) return FALSE;
    const gboolean result = vips_foreign_load_ucbmp_is_a_source(source);
    VIPS_UNREF(source);
    return result;
}

// The fields both header() and load() give the image.
static void vips_foreign_load_ucbmp_set_fields(VipsForeignLoadUcBmp* bmp, VipsImage* out, const uint8_t* data,
                                               size_t length, int w, int h, int bands) {
    double xres = 1.0, yres = 1.0;
    BmpResolution(data, length, xres, yres);
    vips_image_init_fields(out, w, h, bands, VIPS_FORMAT_UCHAR, VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB, xres, yres);
    VIPS_SETSTR(out->filename, vips_connection_filename(VIPS_CONNECTION(bmp->source)));
}

static int vips_foreign_load_ucbmp_header(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadUcBmp* bmp = reinterpret_cast<VipsForeignLoadUcBmp*>(load);
    size_t length = 0;
    const uint8_t* data = static_cast<const uint8_t*>(vips_source_map(bmp->source, &length));
    if (!data) return -1;
    int w = 0, h = 0, bands = 0;
    if (!BmpInfo(data, length, w, h, bands)) {
        vips_error(klass->nickname, "not a BMP this reader understands");
        return -1;
    }
    vips_foreign_load_ucbmp_set_fields(bmp, load->out, data, length, w, h, bands);
    return 0;
}

static int vips_foreign_load_ucbmp_load(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadUcBmp* bmp = reinterpret_cast<VipsForeignLoadUcBmp*>(load);
    size_t length = 0;
    const uint8_t* data = static_cast<const uint8_t*>(vips_source_map(bmp->source, &length));
    if (!data) return -1;
    int w = 0, h = 0, bands = 0;
    if (!BmpInfo(data, length, w, h, bands)) {
        vips_error(klass->nickname, "not a BMP this reader understands");
        return -1;
    }
    std::vector<uint8_t> pixels;
    if (!DecodeBmp(data, length, w, h, bands, pixels)) {
        vips_error(klass->nickname, "the BMP is damaged or truncated");
        return -1;
    }

    VipsImage* out = load->real;
    vips_foreign_load_ucbmp_set_fields(bmp, out, data, length, w, h, bands);
    const size_t rowBytes = static_cast<size_t>(w) * bands;
    for (int y = 0; y < h; ++y) {
        if (vips_image_write_line(out, y, pixels.data() + static_cast<size_t>(y) * rowBytes)) return -1;
    }
    vips_source_decode(bmp->source);
    return 0;
}

static void vips_foreign_load_ucbmp_init(VipsForeignLoadUcBmp*) {}

static void vips_foreign_load_ucbmp_class_init(VipsForeignLoadUcBmpClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass* load_class = VIPS_FOREIGN_LOAD_CLASS(klass);
    gobject_class->dispose = vips_foreign_load_ucbmp_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucbmpload_base";
    object_class->description = "Windows bitmap loader";
    load_class->get_flags = vips_foreign_load_ucbmp_get_flags;
    load_class->is_a_source = vips_foreign_load_ucbmp_is_a_source;
    load_class->header = vips_foreign_load_ucbmp_header;
    load_class->load = vips_foreign_load_ucbmp_load;
}

// ----- from a file -----
typedef struct _VipsForeignLoadUcBmpFile {
    VipsForeignLoadUcBmp parent;
    char* filename;
} VipsForeignLoadUcBmpFile;

typedef VipsForeignLoadUcBmpClass VipsForeignLoadUcBmpFileClass;

G_DEFINE_TYPE(VipsForeignLoadUcBmpFile, vips_foreign_load_ucbmp_file, vips_foreign_load_ucbmp_get_type())

static int vips_foreign_load_ucbmp_file_build(VipsObject* object) {
    VipsForeignLoadUcBmp* bmp = reinterpret_cast<VipsForeignLoadUcBmp*>(object);
    VipsForeignLoadUcBmpFile* file = reinterpret_cast<VipsForeignLoadUcBmpFile*>(object);
    if (file->filename && !(bmp->source = vips_source_new_from_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ucbmp_file_parent_class)->build(object);
}

static void vips_foreign_load_ucbmp_file_init(VipsForeignLoadUcBmpFile*) {}

static const char* vips__ucbmp_suffs[] = {".bmp", ".dib", nullptr};

static void vips_foreign_load_ucbmp_file_class_init(VipsForeignLoadUcBmpFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucbmpload";
    object_class->description = "load a Windows bitmap from a file";
    object_class->build = vips_foreign_load_ucbmp_file_build;
    foreign_class->suffs = vips__ucbmp_suffs;
    load_class->is_a = vips_foreign_load_ucbmp_file_is_a;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to load from",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadUcBmpFile, filename), nullptr);
}

// ----- from memory -----
typedef struct _VipsForeignLoadUcBmpBuffer {
    VipsForeignLoadUcBmp parent;
    VipsBlob* blob;
} VipsForeignLoadUcBmpBuffer;

typedef VipsForeignLoadUcBmpClass VipsForeignLoadUcBmpBufferClass;

G_DEFINE_TYPE(VipsForeignLoadUcBmpBuffer, vips_foreign_load_ucbmp_buffer, vips_foreign_load_ucbmp_get_type())

static int vips_foreign_load_ucbmp_buffer_build(VipsObject* object) {
    VipsForeignLoadUcBmp* bmp = reinterpret_cast<VipsForeignLoadUcBmp*>(object);
    VipsForeignLoadUcBmpBuffer* buffer = reinterpret_cast<VipsForeignLoadUcBmpBuffer*>(object);
    if (buffer->blob && !(bmp->source = vips_source_new_from_blob(buffer->blob))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ucbmp_buffer_parent_class)->build(object);
}

static gboolean vips_foreign_load_ucbmp_buffer_is_a(const void* data, size_t size) {
    return IsBmp(static_cast<const uint8_t*>(data), size < kSniffBytes ? size : kSniffBytes);
}

static void vips_foreign_load_ucbmp_buffer_init(VipsForeignLoadUcBmpBuffer*) {}

static void vips_foreign_load_ucbmp_buffer_class_init(VipsForeignLoadUcBmpBufferClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucbmpload_buffer";
    object_class->description = "load a Windows bitmap from memory";
    object_class->build = vips_foreign_load_ucbmp_buffer_build;
    load_class->is_a_buffer = vips_foreign_load_ucbmp_buffer_is_a;
    VIPS_ARG_BOXED(klass, "buffer", 1, "Buffer", "Buffer to load from",
                   VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadUcBmpBuffer, blob), VIPS_TYPE_BLOB);
}

// ============================================================================
// PUBLIC API
// ============================================================================
int vips_ucbmpload(const char* filename, VipsImage** out, ...) {
    va_list ap;
    va_start(ap, out);
    const int result = vips_call_split("ucbmpload", ap, filename, out);
    va_end(ap);
    return result;
}

int vips_ucbmpload_buffer(void* buf, size_t len, VipsImage** out, ...) {
    VipsBlob* blob = vips_blob_new(nullptr, buf, len);
    va_list ap;
    va_start(ap, out);
    const int result = vips_call_split("ucbmpload_buffer", ap, blob, out);
    va_end(ap);
    vips_area_unref(VIPS_AREA(blob));
    return result;
}

void vips_foreign_bmp_init_types(void) {
    vips_foreign_load_ucbmp_get_type();
    vips_foreign_load_ucbmp_file_get_type();
    vips_foreign_load_ucbmp_buffer_get_type();
}
