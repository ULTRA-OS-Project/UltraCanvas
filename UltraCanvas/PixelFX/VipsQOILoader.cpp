// UltraCanvasVipsQoiLoader.cpp
// libvips foreign loader for QOI (Quite OK Image) format - Implementation
// Version: 1.0.0
// Last Modified: 2025-12-26
// Author: UltraCanvas Framework

#include <vips/vips.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define QOI_IMPLEMENTATION 1
#include "qoi.h"
#include <stdio.h>

// ============================================================================
// BASE QOI LOADER (ABSTRACT)
// ============================================================================
typedef struct _VipsForeignLoadQoi {
    VipsForeignLoad parent_object;

    VipsSource *source;
    // Decoded image properties
} VipsForeignLoadQoi;

typedef VipsForeignLoadClass VipsForeignLoadQoiClass;

G_DEFINE_ABSTRACT_TYPE(VipsForeignLoadQoi,
                       vips_foreign_load_qoi,
                       VIPS_TYPE_FOREIGN_LOAD)

static void
vips_foreign_load_qoi_dispose(GObject *gobject)
{
    VipsForeignLoadQoi *qoi = (VipsForeignLoadQoi *) gobject;

    VIPS_UNREF(qoi->source);

    G_OBJECT_CLASS(vips_foreign_load_qoi_parent_class)->dispose(gobject);
}

static VipsForeignFlags
vips_foreign_load_qoi_get_flags(VipsForeignLoad *load)
{
    // QOI requires full decode - no partial or sequential loading
    return VIPS_FOREIGN_SEQUENTIAL;
}

static gboolean
vips_foreign_load_qoi_is_a(VipsSource *source)
{
    unsigned char *magic;

    if ((magic = (unsigned char *)vips_source_sniff(source, 4)) &&
        vips_isprefix("qoif", (char *) magic)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
vips_foreign_load_qoi_file_is_a(const char *filename)
{
    VipsSource *source;
    gboolean result;

    if (!(source = vips_source_new_from_file(filename)))
        return FALSE;
    result = vips_foreign_load_qoi_is_a(source);
    VIPS_UNREF(source);

    return result;
}

static int
vips_foreign_load_qoi_header(VipsForeignLoad *load)
{
    VipsObjectClass *klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadQoi *qoi = (VipsForeignLoadQoi *) load;
    unsigned char header[14];

    if (vips_source_seek(qoi->source, 0, SEEK_SET))
        return -1;

    if (vips_source_read(qoi->source, header, 14) != 14) {
        vips_error(klass->nickname, "Header too small");
        return -1;
    }
    if (!(header[0] == 'q' &&
           header[1] == 'o' &&
           header[2] == 'i' &&
           header[3] == 'f')) {
        vips_error(klass->nickname, "Not QOI image, invalid maigic ");
        return -1;
    }
    int width =
            (header[4] << 24) |
            (header[5] << 16) |
            (header[6] << 8) |
            (header[7]);

    int height =
            (header[8] << 24) |
            (header[9] << 16) |
            (header[10] << 8) |
            (header[11]);

    int channels = header[12];

    if (width == 0 || height == 0) {
        vips_error(klass->nickname, "Invalid image dimensions = 0");
        return -1;
    }
    if (channels != 3 && channels != 4) {
        vips_error(klass->nickname, "Invalid number of channels");
        return -1;
    }

    vips_image_init_fields(load->out,
                           width, height, channels,
                           VIPS_FORMAT_UCHAR,
                           VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB,
                           1, 1);
    VIPS_SETSTR(load->out->filename,
                vips_connection_filename(VIPS_CONNECTION(qoi->source)));

    return 0;
}

static int
vips_foreign_load_qoi_load(VipsForeignLoad *load)
{
    VipsObjectClass *klass = VIPS_OBJECT_GET_CLASS(load);
    VipsForeignLoadQoi *qoi = (VipsForeignLoadQoi *) load;
    VipsImage *out = load->real;

    size_t length;
    const void *data = vips_source_map(qoi->source, &length);
    if (!data)
        return -1;

    qoi_desc desc;
    void *pixels = qoi_decode(data, length, &desc, 0);
    if (!pixels) {
        vips_error(klass->nickname, "QOI decode failed");
        return -1;
    }

    vips_image_init_fields(out,
                           desc.width, desc.height, desc.channels,
                           VIPS_FORMAT_UCHAR,
                           VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB,
                           1, 1);

    VIPS_SETSTR(out->filename,
                vips_connection_filename(VIPS_CONNECTION(qoi->source)));

    size_t row_size = desc.width * desc.channels;

    for (int y = 0; y < desc.height; y++) {
        if (vips_image_write_line(out, y,
                                  (VipsPel *) pixels + y * row_size)) {
            const char *err = vips_error_buffer();
            fprintf(stderr, "Vips err: %s\n", err);
            free(pixels);
            return -1;
        }
    }

    vips_source_decode(qoi->source);
    free(pixels);
    return 0;
}

static void
vips_foreign_load_qoi_init(VipsForeignLoadQoi *qoi)
{
}

static void
vips_foreign_load_qoi_class_init(VipsForeignLoadQoiClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass *object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass *load_class = VIPS_FOREIGN_LOAD_CLASS(klass);

    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;

    object_class->nickname = "qoi_base";
    object_class->description = "QOI image loader";

    load_class->get_flags = vips_foreign_load_qoi_get_flags;
    load_class->is_a = vips_foreign_load_qoi_file_is_a;
    load_class->is_a_source = vips_foreign_load_qoi_is_a;
    load_class->header = vips_foreign_load_qoi_header;
    load_class->load = vips_foreign_load_qoi_load;
}
// ============================================================================
// FILE LOADER
// ============================================================================

typedef struct _VipsForeignLoadQoiFile {
    VipsForeignLoadQoi parent;
    char *filename;
} VipsForeignLoadQoiFile;

typedef VipsForeignLoadQoiClass VipsForeignLoadQoiFileClass;

G_DEFINE_TYPE(VipsForeignLoadQoiFile,
              vips_foreign_load_qoi_file,
              vips_foreign_load_qoi_get_type())

static int
vips_foreign_load_qoi_file_build(VipsObject *object)
{
    VipsForeignLoadQoi *qoi = (VipsForeignLoadQoi *) object;
    VipsForeignLoadQoiFile *file = (VipsForeignLoadQoiFile *) object;

    if (file->filename && !(qoi->source = vips_source_new_from_file(file->filename)))
        return -1;

    return VIPS_OBJECT_CLASS(vips_foreign_load_qoi_file_parent_class)->build(object);
}

static void
vips_foreign_load_qoi_file_init(VipsForeignLoadQoiFile *file)
{
}

const char *vips__qoi_suffs[] = { ".qoi", NULL };

static void
vips_foreign_load_qoi_file_class_init(VipsForeignLoadQoiFileClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass *object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignClass *foreign_class = (VipsForeignClass *) klass;
    VipsForeignLoadClass *load_class = (VipsForeignLoadClass *) klass;

    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;

    object_class->nickname = "qoiload";
    object_class->description = "load QOI from file";

    object_class->build = vips_foreign_load_qoi_file_build;

    foreign_class->suffs = vips__qoi_suffs;

    load_class->is_a = vips_foreign_load_qoi_file_is_a;

    VIPS_ARG_STRING(klass, "filename", 1,
                    "Filename",
                    "Filename to load",
                    VIPS_ARGUMENT_REQUIRED_INPUT,
                    G_STRUCT_OFFSET(VipsForeignLoadQoiFile, filename),
                    NULL);
}

// ============================================================================
// BUFFER LOADER
// ============================================================================
typedef struct _VipsForeignLoadQoiBuffer {
    VipsForeignLoadQoi parent;
    VipsBlob *blob;
} VipsForeignLoadQoiBuffer;

typedef VipsForeignLoadQoiClass VipsForeignLoadQoiBufferClass;

G_DEFINE_TYPE(VipsForeignLoadQoiBuffer,
              vips_foreign_load_qoi_buffer,
              vips_foreign_load_qoi_get_type())

static int
vips_foreign_load_qoi_buffer_build(VipsObject *object)
{
    VipsForeignLoadQoi *qoi = (VipsForeignLoadQoi *) object;
    VipsForeignLoadQoiBuffer *buffer = (VipsForeignLoadQoiBuffer *) object;

    if (buffer->blob && !(qoi->source = vips_source_new_from_blob(buffer->blob))) {
        return -1;
    }

    return VIPS_OBJECT_CLASS(vips_foreign_load_qoi_buffer_parent_class)->build(object);
}

static void
vips_foreign_load_qoi_buffer_init(
        VipsForeignLoadQoiBuffer *buffer)
{
}

static void
vips_foreign_load_qoi_buffer_class_init(VipsForeignLoadQoiBufferClass *klass)
{
    VipsObjectClass *object_class = VIPS_OBJECT_CLASS(klass);

    object_class->build = vips_foreign_load_qoi_buffer_build;

    VIPS_ARG_BOXED(klass, "buffer", 1,
                   "Buffer",
                   "Buffer to load from",
                   VIPS_ARGUMENT_REQUIRED_INPUT,
                   G_STRUCT_OFFSET(VipsForeignLoadQoiBuffer, blob),
                   VIPS_TYPE_BLOB);
}

// ============================================================================
// PUBLIC API FUNCTIONS
// ============================================================================

int
vips_qoiload(const char *filename, VipsImage **out, ...)
{
    va_list ap;
    int result;

    va_start(ap, out);
    result = vips_call_split("qoiload_file", ap, filename, out);
    va_end(ap);

    return result;
}

int
vips_qoiload_buffer(void *buf, size_t len, VipsImage **out, ...)
{
    va_list ap;
    VipsBlob *blob;
    int result;

    // Create blob from buffer (vips does not take ownership, caller must keep buffer alive)
    blob = vips_blob_new(NULL, buf, len);

    va_start(ap, out);
    result = vips_call_split("qoiload_buffer", ap, blob, out);
    va_end(ap);

    vips_area_unref(VIPS_AREA(blob));

    return result;
}

//int
//vips_qoiload_source(VipsSource *source, VipsImage **out, ...)
//{
//    va_list ap;
//    int result;
//
//    va_start(ap, out);
//    result = vips_call_split("qoiload_source", ap, source, out);
//    va_end(ap);
//
//    return result;
//}

// ============================================================================
// TYPE REGISTRATION
// ============================================================================

void vips_foreign_load_qoi_init_types(void)
{
    // Register all QOI loader types
    // This makes them discoverable by vips_foreign_find_load() etc.
//    vips_foreign_load_qoi_get_type();
//    vips_foreign_load_qoi_file_get_type();
//    vips_foreign_load_qoi_buffer_get_type();
//    vips_foreign_load_qoi_source_get_type();

    vips_foreign_load_qoi_get_type();
    vips_foreign_load_qoi_file_get_type();
    vips_foreign_load_qoi_buffer_get_type();
}