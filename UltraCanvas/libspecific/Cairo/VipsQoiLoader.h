// UltraCanvasVipsQoiLoader.h
// libvips foreign loader for QOI (Quite OK Image) format
// Version: 1.0.0
// Last Modified: 2025-12-26
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_VIPS_QOI_LOADER_H
#define ULTRACANVAS_VIPS_QOI_LOADER_H

#include <vips/vips.h>


// ===== PUBLIC API FUNCTIONS =====

/**
 * vips_qoiload:
 * @filename: file to load
 * @out: (out): output image
 * @...: NULL-terminated list of optional named arguments
 *
 * Load a QOI image from a file.
 *
 * Returns: 0 on success, -1 on error
 */
int vips_qoiload(const char *filename, VipsImage **out, ...);

/**
 * vips_qoiload_buffer:
 * @buf: memory buffer to load
 * @len: size of memory buffer
 * @out: (out): output image
 * @...: NULL-terminated list of optional named arguments
 *
 * Load a QOI image from a memory buffer.
 *
 * Returns: 0 on success, -1 on error
 */
int vips_qoiload_buffer(void *buf, size_t len, VipsImage **out, ...);

/**
 * vips_qoiload_source:
 * @source: source to load from
 * @out: (out): output image
 * @...: NULL-terminated list of optional named arguments
 *
 * Load a QOI image from a VipsSource.
 *
 * Returns: 0 on success, -1 on error
 */
int vips_qoiload_source(VipsSource *source, VipsImage **out, ...);

/**
 * vips_foreign_load_qoi_init_types:
 *
 * Register all QOI loader types with libvips.
 * Call this once during application initialization after VIPS_INIT().
 */
void vips_foreign_load_qoi_init_types(void);

#endif // ULTRACANVAS_VIPS_QOI_LOADER_H