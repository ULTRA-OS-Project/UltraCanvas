// libspecific/Cairo/VipsBmpPlugin.cpp
// libvips foreign loader for Windows bitmaps (.bmp, .dib), read by the
// bundled stb_image. See VipsBmpPlugin.h.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "VipsBmpPlugin.h"

#include <climits>
#include <cstdint>
#include <cstring>

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

    // Width, height and band count (3, or 4 when the file carries alpha),
    // when stb_image reads these bytes as a BMP. An RLE-compressed one it
    // does not, and so is left to the next loader.
    bool BmpInfo(const uint8_t* data, size_t length, int& w, int& h, int& bands) {
        if (!data || length < 26 || data[0] != 'B' || data[1] != 'M') return false;
        if (length > static_cast<size_t>(INT_MAX)) return false;
        int comp = 0;
        if (!stbi_info_from_memory(data, static_cast<int>(length), &w, &h, &comp)) return false;
        if (w <= 0 || h <= 0 || w > VIPS_MAX_COORD || h > VIPS_MAX_COORD) return false;
        bands = comp == 4 ? 4 : 3;
        return true;
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
    // As many bands as the header said, whatever stb_image finds on the way.
    int dw = 0, dh = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(length), &dw, &dh, &comp, bands);
    if (!pixels) {
        vips_error(klass->nickname, "the BMP is damaged or truncated");
        return -1;
    }
    if (dw != w || dh != h) {
        stbi_image_free(pixels);
        vips_error(klass->nickname, "the BMP is not the size its header says");
        return -1;
    }

    VipsImage* out = load->real;
    vips_foreign_load_ucbmp_set_fields(bmp, out, data, length, w, h, bands);
    const size_t rowBytes = static_cast<size_t>(w) * bands;
    for (int y = 0; y < h; ++y) {
        if (vips_image_write_line(out, y, pixels + static_cast<size_t>(y) * rowBytes)) {
            stbi_image_free(pixels);
            return -1;
        }
    }
    stbi_image_free(pixels);
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
