// libspecific/Cairo/VipsJxlPlugin.cpp
// The framework's own JPEG XL loader and saver for libvips, on libjxl. See
// the header. The classes follow libvips' own foreign-load / foreign-save
// shape (as VipsQOILoader.cpp and VipsGifPlugin.cpp do): an abstract base
// holding the VipsSource / VipsTarget, and a subclass per way in or out.
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework

#include "VipsJxlPlugin.h"

#include <jxl/decode.h>
#include <jxl/encode.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ----- what a JPEG XL file starts with -----
// A bare codestream: FF 0A. The ISOBMFF container: a 12-byte signature box.
const unsigned char kContainerSignature[12] = { 0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A };

bool LooksLikeJxl(const unsigned char* bytes, size_t size) {
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0x0A) return true;
    return size >= sizeof(kContainerSignature) && std::memcmp(bytes, kContainerSignature, sizeof(kContainerSignature)) == 0;
}

// ----- the header, from the basic info -----
struct JxlHeader {
    int width = 0;
    int height = 0;
    int colourChannels = 0;   // 1 or 3
    bool alpha = false;
    int bits = 8;             // 8 or 16 a channel
    int Bands() const { return colourChannels + (alpha ? 1 : 0); }
};

bool FillHeader(const JxlBasicInfo& info, JxlHeader& out, std::string& why) {
    if (info.xsize == 0 || info.ysize == 0 || info.xsize > 0x7FFFFFFF || info.ysize > 0x7FFFFFFF) {
        why = "the picture has no size";
        return false;
    }
    out.width = static_cast<int>(info.xsize);
    out.height = static_cast<int>(info.ysize);
    out.colourChannels = info.num_color_channels == 1 ? 1 : 3;
    out.alpha = info.alpha_bits > 0;
    out.bits = info.bits_per_sample > 8 ? 16 : 8;
    return true;
}

// Reads the basic info, and with `pixels` the first frame, from the whole
// file in memory. One decoder per call: the picture is small beside the
// decode, and libvips keeps the header's result on the image.
bool Decode(const void* data, size_t size, JxlHeader& header, std::vector<unsigned char>* pixels, std::string& why) {
    JxlDecoder* decoder = JxlDecoderCreate(nullptr);
    if (!decoder) {
        why = "libjxl could not make a decoder";
        return false;
    }
    struct Guard { JxlDecoder* d; ~Guard() { JxlDecoderDestroy(d); } } guard{ decoder };
    const int events = JXL_DEC_BASIC_INFO | (pixels ? JXL_DEC_FULL_IMAGE : 0);
    if (JxlDecoderSubscribeEvents(decoder, events) != JXL_DEC_SUCCESS) {
        why = "libjxl refused the event subscription";
        return false;
    }
    if (JxlDecoderSetInput(decoder, static_cast<const uint8_t*>(data), size) != JXL_DEC_SUCCESS) {
        why = "libjxl refused the input";
        return false;
    }
    JxlDecoderCloseInput(decoder);
    bool haveInfo = false;
    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(decoder);
        switch (status) {
            case JXL_DEC_ERROR:
                why = "libjxl could not decode the picture";
                return false;
            case JXL_DEC_NEED_MORE_INPUT:
                why = "the file ends before the picture does";
                return false;
            case JXL_DEC_BASIC_INFO: {
                JxlBasicInfo info;
                if (JxlDecoderGetBasicInfo(decoder, &info) != JXL_DEC_SUCCESS) {
                    why = "libjxl could not read the header";
                    return false;
                }
                if (!FillHeader(info, header, why)) return false;
                haveInfo = true;
                if (!pixels) return true;
                break;
            }
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
                if (!haveInfo || !pixels) {
                    why = "libjxl asked for a buffer before the header";
                    return false;
                }
                JxlPixelFormat format;
                format.num_channels = static_cast<uint32_t>(header.Bands());
                format.data_type = header.bits == 16 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8;
                format.endianness = JXL_NATIVE_ENDIAN;
                format.align = 0;
                size_t needed = 0;
                if (JxlDecoderImageOutBufferSize(decoder, &format, &needed) != JXL_DEC_SUCCESS) {
                    why = "libjxl could not size the picture";
                    return false;
                }
                pixels->resize(needed);
                if (JxlDecoderSetImageOutBuffer(decoder, &format, pixels->data(), needed) != JXL_DEC_SUCCESS) {
                    why = "libjxl refused the picture buffer";
                    return false;
                }
                break;
            }
            case JXL_DEC_FULL_IMAGE:
                return true;   // the first frame is the picture
            case JXL_DEC_SUCCESS:
                if (haveInfo && (!pixels || !pixels->empty())) return true;
                why = "the file holds no picture";
                return false;
            default:
                break;   // an event not subscribed to; keep going
        }
    }
}

void SetImageFields(VipsImage* image, const JxlHeader& header) {
    const VipsBandFormat format = header.bits == 16 ? VIPS_FORMAT_USHORT : VIPS_FORMAT_UCHAR;
    VipsInterpretation interpretation;
    if (header.colourChannels == 1) interpretation = header.bits == 16 ? VIPS_INTERPRETATION_GREY16 : VIPS_INTERPRETATION_B_W;
    else interpretation = header.bits == 16 ? VIPS_INTERPRETATION_RGB16 : VIPS_INTERPRETATION_sRGB;
    vips_image_init_fields(image, header.width, header.height, header.Bands(), format, VIPS_CODING_NONE,
                           interpretation, 1.0, 1.0);
    if (vips_image_pipelinev(image, VIPS_DEMAND_STYLE_THINSTRIP, nullptr)) {
        // nothing: the caller's write_line reports it
    }
}

} // namespace

// ============================================================================
// LOADER
// ============================================================================
typedef struct _VipsForeignLoadUcJxl {
    VipsForeignLoad parent_object;
    VipsSource* source;
} VipsForeignLoadUcJxl;

typedef VipsForeignLoadClass VipsForeignLoadUcJxlClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignLoadUcJxl, vips_foreign_load_ucjxl, VIPS_TYPE_FOREIGN_LOAD)

static void vips_foreign_load_ucjxl_dispose(GObject* gobject) {
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(gobject);
    VIPS_UNREF(jxl->source);
    G_OBJECT_CLASS(vips_foreign_load_ucjxl_parent_class)->dispose(gobject);
}

static VipsForeignFlags vips_foreign_load_ucjxl_get_flags(VipsForeignLoad*) {
    return VIPS_FOREIGN_SEQUENTIAL;
}

static gboolean vips_foreign_load_ucjxl_is_a_source(VipsSource* source) {
    const unsigned char* magic = static_cast<const unsigned char*>(vips_source_sniff(source, 12));
    return magic && LooksLikeJxl(magic, 12) ? TRUE : FALSE;
}

static gboolean vips_foreign_load_ucjxl_is_a(const char* filename) {
    VipsSource* source = vips_source_new_from_file(filename);
    if (!source) return FALSE;
    const gboolean result = vips_foreign_load_ucjxl_is_a_source(source);
    VIPS_UNREF(source);
    return result;
}

static int vips_foreign_load_ucjxl_header(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(load);
    size_t size = 0;
    const void* data = vips_source_map(jxl->source, &size);
    if (!data) return -1;
    JxlHeader header;
    std::string why;
    if (!Decode(data, size, header, nullptr, why)) {
        vips_error(klass->nickname, "%s", why.c_str());
        return -1;
    }
    SetImageFields(load->out, header);
    VIPS_SETSTR(load->out->filename, vips_connection_filename(VIPS_CONNECTION(jxl->source)));
    return 0;
}

static int vips_foreign_load_ucjxl_load(VipsForeignLoad* load) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(load);
    size_t size = 0;
    const void* data = vips_source_map(jxl->source, &size);
    if (!data) return -1;
    JxlHeader header;
    std::vector<unsigned char> pixels;
    std::string why;
    if (!Decode(data, size, header, &pixels, why)) {
        vips_error(klass->nickname, "%s", why.c_str());
        return -1;
    }
    VipsImage* out = load->real;
    SetImageFields(out, header);
    VIPS_SETSTR(out->filename, vips_connection_filename(VIPS_CONNECTION(jxl->source)));
    const size_t rowBytes = static_cast<size_t>(header.width) * header.Bands() * (header.bits / 8);
    if (pixels.size() < rowBytes * static_cast<size_t>(header.height)) {
        vips_error(klass->nickname, "libjxl delivered fewer pixels than the header says");
        return -1;
    }
    for (int y = 0; y < header.height; ++y) {
        if (vips_image_write_line(out, y, reinterpret_cast<VipsPel*>(pixels.data() + y * rowBytes))) return -1;
    }
    vips_source_decode(jxl->source);
    return 0;
}

static void vips_foreign_load_ucjxl_init(VipsForeignLoadUcJxl*) {}

static const char* vips__ucjxl_suffs[] = { ".jxl", nullptr };

static void vips_foreign_load_ucjxl_class_init(VipsForeignLoadUcJxlClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = VIPS_FOREIGN_LOAD_CLASS(klass);
    gobject_class->dispose = vips_foreign_load_ucjxl_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlload_base";
    object_class->description = "load JPEG XL (the framework's loader on libjxl)";
    foreign_class->suffs = vips__ucjxl_suffs;
    load_class->get_flags = vips_foreign_load_ucjxl_get_flags;
    load_class->is_a = vips_foreign_load_ucjxl_is_a;
    load_class->is_a_source = vips_foreign_load_ucjxl_is_a_source;
    load_class->header = vips_foreign_load_ucjxl_header;
    load_class->load = vips_foreign_load_ucjxl_load;
}

// The priority the classes register at: libvips' own loaders sit at 0, so
// where this one stands in for them 0 is right; forced beside them it
// must win.
static int g_ucjxlPriority = 0;

// ----- from a file -----
typedef struct _VipsForeignLoadUcJxlFile {
    VipsForeignLoadUcJxl parent;
    char* filename;
} VipsForeignLoadUcJxlFile;

typedef VipsForeignLoadUcJxlClass VipsForeignLoadUcJxlFileClass;

G_DEFINE_TYPE(VipsForeignLoadUcJxlFile, vips_foreign_load_ucjxl_file, vips_foreign_load_ucjxl_get_type())

static int vips_foreign_load_ucjxl_file_build(VipsObject* object) {
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(object);
    VipsForeignLoadUcJxlFile* file = reinterpret_cast<VipsForeignLoadUcJxlFile*>(object);
    if (file->filename && !(jxl->source = vips_source_new_from_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ucjxl_file_parent_class)->build(object);
}

static void vips_foreign_load_ucjxl_file_init(VipsForeignLoadUcJxlFile*) {}

static void vips_foreign_load_ucjxl_file_class_init(VipsForeignLoadUcJxlFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlload";
    object_class->description = "load JPEG XL from a file";
    object_class->build = vips_foreign_load_ucjxl_file_build;
    foreign_class->suffs = vips__ucjxl_suffs;
    foreign_class->priority = g_ucjxlPriority;
    load_class->is_a = vips_foreign_load_ucjxl_is_a;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to load",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadUcJxlFile, filename), nullptr);
}

// ----- from a buffer -----
typedef struct _VipsForeignLoadUcJxlBuffer {
    VipsForeignLoadUcJxl parent;
    VipsBlob* blob;
} VipsForeignLoadUcJxlBuffer;

typedef VipsForeignLoadUcJxlClass VipsForeignLoadUcJxlBufferClass;

G_DEFINE_TYPE(VipsForeignLoadUcJxlBuffer, vips_foreign_load_ucjxl_buffer, vips_foreign_load_ucjxl_get_type())

static int vips_foreign_load_ucjxl_buffer_build(VipsObject* object) {
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(object);
    VipsForeignLoadUcJxlBuffer* buffer = reinterpret_cast<VipsForeignLoadUcJxlBuffer*>(object);
    if (buffer->blob && !(jxl->source = vips_source_new_from_blob(buffer->blob))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_load_ucjxl_buffer_parent_class)->build(object);
}

static gboolean vips_foreign_load_ucjxl_buffer_is_a(const void* data, size_t size) {
    return LooksLikeJxl(static_cast<const unsigned char*>(data), size) ? TRUE : FALSE;
}

static void vips_foreign_load_ucjxl_buffer_init(VipsForeignLoadUcJxlBuffer*) {}

static void vips_foreign_load_ucjxl_buffer_class_init(VipsForeignLoadUcJxlBufferClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlload_buffer";
    object_class->description = "load JPEG XL from a buffer";
    object_class->build = vips_foreign_load_ucjxl_buffer_build;
    foreign_class->priority = g_ucjxlPriority;
    load_class->is_a_buffer = vips_foreign_load_ucjxl_buffer_is_a;
    VIPS_ARG_BOXED(klass, "buffer", 1, "Buffer", "Buffer to load from",
                   VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadUcJxlBuffer, blob), VIPS_TYPE_BLOB);
}

// ----- from a source -----
typedef struct _VipsForeignLoadUcJxlSource {
    VipsForeignLoadUcJxl parent;
    VipsSource* source;
} VipsForeignLoadUcJxlSource;

typedef VipsForeignLoadUcJxlClass VipsForeignLoadUcJxlSourceClass;

G_DEFINE_TYPE(VipsForeignLoadUcJxlSource, vips_foreign_load_ucjxl_source, vips_foreign_load_ucjxl_get_type())

static int vips_foreign_load_ucjxl_source_build(VipsObject* object) {
    VipsForeignLoadUcJxl* jxl = reinterpret_cast<VipsForeignLoadUcJxl*>(object);
    VipsForeignLoadUcJxlSource* source = reinterpret_cast<VipsForeignLoadUcJxlSource*>(object);
    if (source->source) {
        jxl->source = source->source;
        g_object_ref(jxl->source);
    }
    return VIPS_OBJECT_CLASS(vips_foreign_load_ucjxl_source_parent_class)->build(object);
}

static void vips_foreign_load_ucjxl_source_init(VipsForeignLoadUcJxlSource*) {}

static void vips_foreign_load_ucjxl_source_class_init(VipsForeignLoadUcJxlSourceClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignLoadClass* load_class = reinterpret_cast<VipsForeignLoadClass*>(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlload_source";
    object_class->description = "load JPEG XL from a source";
    object_class->build = vips_foreign_load_ucjxl_source_build;
    foreign_class->priority = g_ucjxlPriority;
    load_class->is_a_source = vips_foreign_load_ucjxl_is_a_source;
    VIPS_ARG_OBJECT(klass, "source", 1, "Source", "Source to load from",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignLoadUcJxlSource, source), VIPS_TYPE_SOURCE);
}

// ============================================================================
// SAVER
// ============================================================================
typedef struct _VipsForeignSaveUcJxl {
    VipsForeignSave parent_object;
    VipsTarget* target;
    int tier;
    double distance;
    int effort;
    gboolean lossless;
    int Q;
} VipsForeignSaveUcJxl;

typedef VipsForeignSaveClass VipsForeignSaveUcJxlClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignSaveUcJxl, vips_foreign_save_ucjxl, VIPS_TYPE_FOREIGN_SAVE)

static void vips_foreign_save_ucjxl_dispose(GObject* gobject) {
    VipsForeignSaveUcJxl* jxl = reinterpret_cast<VipsForeignSaveUcJxl*>(gobject);
    VIPS_UNREF(jxl->target);
    G_OBJECT_CLASS(vips_foreign_save_ucjxl_parent_class)->dispose(gobject);
}

namespace {
    // libvips' own mapping of Q to libjxl's distance (cjxl's quality scale):
    // 100 is lossless-grade 0.0, 90 about 1.0, 30 about 6.4.
    double DistanceForQuality(int q) {
        if (q >= 100) return 0.0;
        if (q >= 30) return 0.1 + (100 - q) * 0.09;
        return 53.0 / 3000.0 * q * q - 23.0 / 20.0 * q + 25.0;
    }
} // namespace

static int vips_foreign_save_ucjxl_build(VipsObject* object) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(object);
    VipsForeignSave* save = reinterpret_cast<VipsForeignSave*>(object);
    VipsForeignSaveUcJxl* jxl = reinterpret_cast<VipsForeignSaveUcJxl*>(object);
    if (VIPS_OBJECT_CLASS(vips_foreign_save_ucjxl_parent_class)->build(object)) return -1;

    // `ready` is 8-bit RGB or RGBA (the class's saveable and format table)
    VipsImage* ready = save->ready;
    size_t size = 0;
    void* pixels = vips_image_write_to_memory(ready, &size);
    if (!pixels) return -1;
    struct PixelGuard { void* p; ~PixelGuard() { g_free(p); } } pixelGuard{ pixels };

    JxlEncoder* encoder = JxlEncoderCreate(nullptr);
    if (!encoder) {
        vips_error(klass->nickname, "libjxl could not make an encoder");
        return -1;
    }
    struct EncoderGuard { JxlEncoder* e; ~EncoderGuard() { JxlEncoderDestroy(e); } } encoderGuard{ encoder };

    const bool alpha = ready->Bands == 4;
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = static_cast<uint32_t>(ready->Xsize);
    info.ysize = static_cast<uint32_t>(ready->Ysize);
    info.bits_per_sample = 8;
    info.num_color_channels = 3;
    info.num_extra_channels = alpha ? 1 : 0;
    info.alpha_bits = alpha ? 8 : 0;
    info.alpha_premultiplied = JXL_FALSE;
    info.uses_original_profile = jxl->lossless ? JXL_TRUE : JXL_FALSE;
    if (JxlEncoderSetBasicInfo(encoder, &info) != JXL_ENC_SUCCESS) {
        vips_error(klass->nickname, "libjxl refused the picture's description");
        return -1;
    }
    JxlColorEncoding colour;
    JxlColorEncodingSetToSRGB(&colour, JXL_FALSE);
    if (JxlEncoderSetColorEncoding(encoder, &colour) != JXL_ENC_SUCCESS) {
        vips_error(klass->nickname, "libjxl refused sRGB");
        return -1;
    }
    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder, nullptr);
    if (!settings) {
        vips_error(klass->nickname, "libjxl could not make the frame settings");
        return -1;
    }
    if (jxl->lossless) {
        JxlEncoderSetFrameDistance(settings, 0.0f);
        JxlEncoderSetFrameLossless(settings, JXL_TRUE);
    } else {
        // A Q the caller set wins over the distance, as libvips' jxlsave
        // decides it; otherwise the distance (its default 1.0).
        double distance = jxl->distance;
        if (vips_object_argument_isset(object, "Q")) distance = DistanceForQuality(jxl->Q);
        JxlEncoderSetFrameDistance(settings, static_cast<float>(distance));
    }
    JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, jxl->effort);

    JxlPixelFormat format;
    format.num_channels = alpha ? 4 : 3;
    format.data_type = JXL_TYPE_UINT8;
    format.endianness = JXL_NATIVE_ENDIAN;
    format.align = 0;
    if (JxlEncoderAddImageFrame(settings, &format, pixels, size) != JXL_ENC_SUCCESS) {
        vips_error(klass->nickname, "libjxl refused the picture");
        return -1;
    }
    JxlEncoderCloseInput(encoder);

    std::vector<uint8_t> out(64 * 1024);
    size_t written = 0;
    for (;;) {
        uint8_t* next = out.data() + written;
        size_t avail = out.size() - written;
        const JxlEncoderStatus status = JxlEncoderProcessOutput(encoder, &next, &avail);
        written = static_cast<size_t>(next - out.data());
        if (status == JXL_ENC_SUCCESS) break;
        if (status == JXL_ENC_NEED_MORE_OUTPUT) {
            out.resize(out.size() * 2);
            continue;
        }
        vips_error(klass->nickname, "libjxl could not encode the picture");
        return -1;
    }
    if (vips_target_write(jxl->target, out.data(), written)) return -1;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 13)
    if (vips_target_end(jxl->target)) return -1;
#else
    vips_target_finish(jxl->target);
#endif
    return 0;
}

// Every band format is saved as 8 bits.
static VipsBandFormat vips_foreign_save_ucjxl_format_table[10] = {
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR,
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR };

static void vips_foreign_save_ucjxl_init(VipsForeignSaveUcJxl* jxl) {
    jxl->tier = 0;
    jxl->distance = 1.0;
    jxl->effort = 7;
    jxl->lossless = FALSE;
    jxl->Q = 75;
}

static void vips_foreign_save_ucjxl_class_init(VipsForeignSaveUcJxlClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignSaveClass* save_class = reinterpret_cast<VipsForeignSaveClass*>(klass);
    gobject_class->dispose = vips_foreign_save_ucjxl_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlsave_base";
    object_class->description = "save as JPEG XL (the framework's saver on libjxl)";
    object_class->build = vips_foreign_save_ucjxl_build;
    foreign_class->suffs = vips__ucjxl_suffs;
    foreign_class->priority = g_ucjxlPriority;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 17)
    save_class->saveable = static_cast<VipsForeignSaveable>(VIPS_FOREIGN_SAVEABLE_RGB | VIPS_FOREIGN_SAVEABLE_ALPHA);
#else
    save_class->saveable = VIPS_SAVEABLE_RGBA_ONLY;
#endif
    save_class->format_table = vips_foreign_save_ucjxl_format_table;
    // The arguments libvips' jxlsave takes, same names, same defaults
    VIPS_ARG_INT(klass, "tier", 10, "Tier", "Decode speed tier (accepted; not used)",
                 VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxl, tier), 0, 4, 0);
    VIPS_ARG_DOUBLE(klass, "distance", 11, "Distance", "Target butteraugli distance",
                    VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxl, distance), 0.0, 15.0, 1.0);
    VIPS_ARG_INT(klass, "effort", 12, "Effort", "Encoding effort",
                 VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxl, effort), 1, 9, 7);
    VIPS_ARG_BOOL(klass, "lossless", 13, "Lossless", "Enable lossless compression",
                  VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxl, lossless), FALSE);
    VIPS_ARG_INT(klass, "Q", 14, "Q", "Quality factor",
                 VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxl, Q), 0, 100, 75);
}

// ----- to a file -----
typedef struct _VipsForeignSaveUcJxlFile {
    VipsForeignSaveUcJxl parent;
    char* filename;
} VipsForeignSaveUcJxlFile;

typedef VipsForeignSaveUcJxlClass VipsForeignSaveUcJxlFileClass;

G_DEFINE_TYPE(VipsForeignSaveUcJxlFile, vips_foreign_save_ucjxl_file, vips_foreign_save_ucjxl_get_type())

static int vips_foreign_save_ucjxl_file_build(VipsObject* object) {
    VipsForeignSaveUcJxl* jxl = reinterpret_cast<VipsForeignSaveUcJxl*>(object);
    VipsForeignSaveUcJxlFile* file = reinterpret_cast<VipsForeignSaveUcJxlFile*>(object);
    if (!(jxl->target = vips_target_new_to_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_save_ucjxl_file_parent_class)->build(object);
}

static void vips_foreign_save_ucjxl_file_init(VipsForeignSaveUcJxlFile*) {}

static void vips_foreign_save_ucjxl_file_class_init(VipsForeignSaveUcJxlFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlsave";
    object_class->description = "save as a JPEG XL file";
    object_class->build = vips_foreign_save_ucjxl_file_build;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to save to",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxlFile, filename), nullptr);
}

// ----- to a buffer -----
typedef struct _VipsForeignSaveUcJxlBuffer {
    VipsForeignSaveUcJxl parent;
    VipsBlob* blob;
} VipsForeignSaveUcJxlBuffer;

typedef VipsForeignSaveUcJxlClass VipsForeignSaveUcJxlBufferClass;

G_DEFINE_TYPE(VipsForeignSaveUcJxlBuffer, vips_foreign_save_ucjxl_buffer, vips_foreign_save_ucjxl_get_type())

static int vips_foreign_save_ucjxl_buffer_build(VipsObject* object) {
    VipsForeignSaveUcJxl* jxl = reinterpret_cast<VipsForeignSaveUcJxl*>(object);
    VipsForeignSaveUcJxlBuffer* buffer = reinterpret_cast<VipsForeignSaveUcJxlBuffer*>(object);
    if (!(jxl->target = vips_target_new_to_memory())) return -1;
    if (VIPS_OBJECT_CLASS(vips_foreign_save_ucjxl_buffer_parent_class)->build(object)) return -1;
    VipsBlob* blob = nullptr;
    g_object_get(jxl->target, "blob", &blob, nullptr);
    g_object_set(buffer, "buffer", blob, nullptr);
    vips_area_unref(VIPS_AREA(blob));
    return 0;
}

static void vips_foreign_save_ucjxl_buffer_init(VipsForeignSaveUcJxlBuffer*) {}

static void vips_foreign_save_ucjxl_buffer_class_init(VipsForeignSaveUcJxlBufferClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlsave_buffer";
    object_class->description = "save as JPEG XL to a buffer";
    object_class->build = vips_foreign_save_ucjxl_buffer_build;
    VIPS_ARG_BOXED(klass, "buffer", 1, "Buffer", "Buffer to save to",
                   VIPS_ARGUMENT_REQUIRED_OUTPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxlBuffer, blob), VIPS_TYPE_BLOB);
}

// ----- to a target -----
typedef struct _VipsForeignSaveUcJxlTarget {
    VipsForeignSaveUcJxl parent;
    VipsTarget* target;
} VipsForeignSaveUcJxlTarget;

typedef VipsForeignSaveUcJxlClass VipsForeignSaveUcJxlTargetClass;

G_DEFINE_TYPE(VipsForeignSaveUcJxlTarget, vips_foreign_save_ucjxl_target, vips_foreign_save_ucjxl_get_type())

static int vips_foreign_save_ucjxl_target_build(VipsObject* object) {
    VipsForeignSaveUcJxl* jxl = reinterpret_cast<VipsForeignSaveUcJxl*>(object);
    VipsForeignSaveUcJxlTarget* target = reinterpret_cast<VipsForeignSaveUcJxlTarget*>(object);
    if (target->target) {
        jxl->target = target->target;
        g_object_ref(jxl->target);
    }
    return VIPS_OBJECT_CLASS(vips_foreign_save_ucjxl_target_parent_class)->build(object);
}

static void vips_foreign_save_ucjxl_target_init(VipsForeignSaveUcJxlTarget*) {}

static void vips_foreign_save_ucjxl_target_class_init(VipsForeignSaveUcJxlTargetClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "jxlsave_target";
    object_class->description = "save as JPEG XL to a target";
    object_class->build = vips_foreign_save_ucjxl_target_build;
    VIPS_ARG_OBJECT(klass, "target", 1, "Target", "Target to save to",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcJxlTarget, target), VIPS_TYPE_TARGET);
}

// ============================================================================
// REGISTRATION
// ============================================================================
static int g_ucjxlActive = 0;

void vips_foreign_jxl_init_types(void) {
    static bool done = false;
    if (done) return;
    done = true;
    const char* mode = std::getenv("UC_VIPS_JXL_PLUGIN");
    const bool force = mode && std::strcmp(mode, "force") == 0;
    const bool libvipsHasIt = vips_type_find("VipsOperation", "jxlload") != 0;
    if (libvipsHasIt && !force) return;
    // Forced beside libvips' own: above them, so a .jxl comes here
    g_ucjxlPriority = libvipsHasIt ? 100 : 0;
    vips_foreign_load_ucjxl_get_type();
    vips_foreign_load_ucjxl_file_get_type();
    vips_foreign_load_ucjxl_buffer_get_type();
    vips_foreign_load_ucjxl_source_get_type();
    vips_foreign_save_ucjxl_get_type();
    vips_foreign_save_ucjxl_file_get_type();
    vips_foreign_save_ucjxl_buffer_get_type();
    vips_foreign_save_ucjxl_target_get_type();
    g_ucjxlActive = 1;
}

int vips_foreign_jxl_plugin_active(void) {
    return g_ucjxlActive;
}
