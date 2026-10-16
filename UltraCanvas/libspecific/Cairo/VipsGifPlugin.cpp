// libspecific/Cairo/VipsGifPlugin.cpp
// libvips foreign saver for GIF with the framework's encoder. See
// VipsGifPlugin.h.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework

#include "VipsGifPlugin.h"
#include "UltraCanvasGifEncoder.h"

#include <vips/vips8>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// SAVER
// ============================================================================
typedef struct _VipsForeignSaveUcGif {
    VipsForeignSave parent_object;
    VipsTarget* target;
    int bitdepth;
    gboolean interlace;
    double dither;
} VipsForeignSaveUcGif;

typedef VipsForeignSaveClass VipsForeignSaveUcGifClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignSaveUcGif, vips_foreign_save_ucgif, VIPS_TYPE_FOREIGN_SAVE)

static void vips_foreign_save_ucgif_dispose(GObject* gobject) {
    VipsForeignSaveUcGif* gif = reinterpret_cast<VipsForeignSaveUcGif*>(gobject);
    VIPS_UNREF(gif->target);
    G_OBJECT_CLASS(vips_foreign_save_ucgif_parent_class)->dispose(gobject);
}

static int vips_foreign_save_ucgif_build(VipsObject* object) {
    VipsObjectClass* klass = VIPS_OBJECT_GET_CLASS(object);
    VipsForeignSave* save = reinterpret_cast<VipsForeignSave*>(object);
    VipsForeignSaveUcGif* gif = reinterpret_cast<VipsForeignSaveUcGif*>(object);
    if (VIPS_OBJECT_CLASS(vips_foreign_save_ucgif_parent_class)->build(object)) return -1;

    std::vector<uint8_t> bytes;
    std::string error;
    try {
        // `ready` is 8-bit RGB or RGBA (the class's saveable and format table).
        vips::VImage picture = vips::VImage(save->ready, vips::NOSTEAL).copy_memory();
        error = UltraCanvas::GifEncode::EncodeGif(bytes, static_cast<const uint8_t*>(picture.data()),
                                                  picture.width(), picture.height(), picture.bands(),
                                                  gif->bitdepth, gif->interlace != FALSE);
    } catch (const vips::VError& e) {
        error = e.what();
    }
    if (!error.empty()) {
        vips_error(klass->nickname, "%s", error.c_str());
        return -1;
    }
    if (vips_target_write(gif->target, bytes.data(), bytes.size())) return -1;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 13)
    if (vips_target_end(gif->target)) return -1;
#else
    vips_target_finish(gif->target);
#endif
    return 0;
}

// Every band format is saved as 8 bits.
static VipsBandFormat vips_foreign_save_ucgif_format_table[10] = {
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR,
    VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR, VIPS_FORMAT_UCHAR};

static const char* vips__ucgif_suffs[] = {".gif", nullptr};

static void vips_foreign_save_ucgif_init(VipsForeignSaveUcGif* gif) {
    gif->bitdepth = 8;
    gif->dither = 1.0;
}

static void vips_foreign_save_ucgif_class_init(VipsForeignSaveUcGifClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass* foreign_class = reinterpret_cast<VipsForeignClass*>(klass);
    VipsForeignSaveClass* save_class = reinterpret_cast<VipsForeignSaveClass*>(klass);
    gobject_class->dispose = vips_foreign_save_ucgif_dispose;
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucgifsave_base";
    object_class->description = "save as GIF with the framework's encoder";
    object_class->build = vips_foreign_save_ucgif_build;
    foreign_class->suffs = vips__ucgif_suffs;
    // Below cgif's gifsave (0), above ImageMagick's savers (-100): libvips
    // picks this one for a .gif only where it has no GIF writer of its own.
    foreign_class->priority = -50;
#if VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 17)
    save_class->saveable = static_cast<VipsForeignSaveable>(VIPS_FOREIGN_SAVEABLE_RGB | VIPS_FOREIGN_SAVEABLE_ALPHA);
#else
    save_class->saveable = VIPS_SAVEABLE_RGBA_ONLY;
#endif
    save_class->format_table = vips_foreign_save_ucgif_format_table;
    VIPS_ARG_INT(klass, "bitdepth", 10, "Bit depth", "Bits per palette index, 1 to 8 (2 to 256 colours)",
                 VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcGif, bitdepth), 1, 8, 8);
    VIPS_ARG_BOOL(klass, "interlace", 11, "Interlace", "Write the rows interlaced",
                  VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcGif, interlace), FALSE);
    VIPS_ARG_DOUBLE(klass, "dither", 12, "Dither", "Accepted as gifsave takes it; not used",
                    VIPS_ARGUMENT_OPTIONAL_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcGif, dither), 0.0, 1.0, 1.0);
}

// ----- to a file -----
typedef struct _VipsForeignSaveUcGifFile {
    VipsForeignSaveUcGif parent;
    char* filename;
} VipsForeignSaveUcGifFile;

typedef VipsForeignSaveUcGifClass VipsForeignSaveUcGifFileClass;

G_DEFINE_TYPE(VipsForeignSaveUcGifFile, vips_foreign_save_ucgif_file, vips_foreign_save_ucgif_get_type())

static int vips_foreign_save_ucgif_file_build(VipsObject* object) {
    VipsForeignSaveUcGif* gif = reinterpret_cast<VipsForeignSaveUcGif*>(object);
    VipsForeignSaveUcGifFile* file = reinterpret_cast<VipsForeignSaveUcGifFile*>(object);
    if (!(gif->target = vips_target_new_to_file(file->filename))) return -1;
    return VIPS_OBJECT_CLASS(vips_foreign_save_ucgif_file_parent_class)->build(object);
}

static void vips_foreign_save_ucgif_file_init(VipsForeignSaveUcGifFile*) {}

static void vips_foreign_save_ucgif_file_class_init(VipsForeignSaveUcGifFileClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucgifsave";
    object_class->description = "save as a GIF file with the framework's encoder";
    object_class->build = vips_foreign_save_ucgif_file_build;
    VIPS_ARG_STRING(klass, "filename", 1, "Filename", "Filename to save to",
                    VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignSaveUcGifFile, filename), nullptr);
}

// ----- to memory -----
typedef struct _VipsForeignSaveUcGifBuffer {
    VipsForeignSaveUcGif parent;
    VipsArea* buf;
} VipsForeignSaveUcGifBuffer;

typedef VipsForeignSaveUcGifClass VipsForeignSaveUcGifBufferClass;

G_DEFINE_TYPE(VipsForeignSaveUcGifBuffer, vips_foreign_save_ucgif_buffer, vips_foreign_save_ucgif_get_type())

static int vips_foreign_save_ucgif_buffer_build(VipsObject* object) {
    VipsForeignSaveUcGif* gif = reinterpret_cast<VipsForeignSaveUcGif*>(object);
    if (!(gif->target = vips_target_new_to_memory())) return -1;
    if (VIPS_OBJECT_CLASS(vips_foreign_save_ucgif_buffer_parent_class)->build(object)) return -1;
    VipsBlob* blob = nullptr;
    g_object_get(gif->target, "blob", &blob, nullptr);
    g_object_set(object, "buffer", blob, nullptr);
    vips_area_unref(VIPS_AREA(blob));
    return 0;
}

static void vips_foreign_save_ucgif_buffer_init(VipsForeignSaveUcGifBuffer*) {}

static void vips_foreign_save_ucgif_buffer_class_init(VipsForeignSaveUcGifBufferClass* klass) {
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass* object_class = VIPS_OBJECT_CLASS(klass);
    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;
    object_class->nickname = "ucgifsave_buffer";
    object_class->description = "save as GIF in memory with the framework's encoder";
    object_class->build = vips_foreign_save_ucgif_buffer_build;
    VIPS_ARG_BOXED(klass, "buffer", 1, "Buffer", "Buffer to save to",
                   VIPS_ARGUMENT_REQUIRED_OUTPUT, G_STRUCT_OFFSET(VipsForeignSaveUcGifBuffer, buf), VIPS_TYPE_BLOB);
}

// ============================================================================
// PUBLIC API
// ============================================================================
const char* vips_foreign_gif_saver(void) {
    return vips_type_find("VipsOperation", "gifsave") != 0 ? "gifsave" : "ucgifsave";
}

int vips_ucgifsave(VipsImage* in, const char* filename, ...) {
    va_list ap;
    va_start(ap, filename);
    const int result = vips_call_split("ucgifsave", ap, in, filename);
    va_end(ap);
    return result;
}

void vips_foreign_gif_init_types(void) {
    vips_foreign_save_ucgif_get_type();
    vips_foreign_save_ucgif_file_get_type();
    vips_foreign_save_ucgif_buffer_get_type();
}
