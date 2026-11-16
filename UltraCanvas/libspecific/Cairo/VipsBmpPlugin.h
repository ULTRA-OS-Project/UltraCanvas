// libspecific/Cairo/VipsBmpPlugin.h
// libvips foreign loader for Windows bitmaps (.bmp, .dib), the framework's
// own - read by the bundled stb_image, so ImageMagick is never involved.
//
// libvips has no BMP loader of its own: without this one a .bmp went to
// magickload, and the first file it read paid for ImageMagick's start-up
// (its configuration, and on Windows every coder DLL in
// modules-Q16HDRI\coders) - seconds, on the thread that asked. Here a BMP
// costs what its pixels cost.
//
//   ucbmpload / ucbmpload_buffer   1, 4, 8, 16, 24 and 32-bit BMPs,
//                                  top-down or bottom-up, with bit-field
//                                  masks; RGB, or RGBA when the file carries
//                                  alpha. The resolution in the header
//                                  becomes xres / yres.
//
// What stb_image does not read (RLE-compressed BMPs, OS/2 2.x variants) the
// loader does not claim, so libvips hands it to magickload as before.
//
// Registered by vips_foreign_bmp_init_types(), which the image subsystem
// calls once after VIPS_INIT (UCImageRaster::InitializeImageSubsysterm).
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_VIPS_BMP_PLUGIN_H
#define ULTRACANVAS_VIPS_BMP_PLUGIN_H

#include <vips/vips.h>

// Load a BMP from a file / a memory buffer.
int vips_ucbmpload(const char* filename, VipsImage** out, ...);
int vips_ucbmpload_buffer(void* buf, size_t len, VipsImage** out, ...);

// Register the loader types with libvips (once, after VIPS_INIT).
void vips_foreign_bmp_init_types(void);

#endif // ULTRACANVAS_VIPS_BMP_PLUGIN_H
