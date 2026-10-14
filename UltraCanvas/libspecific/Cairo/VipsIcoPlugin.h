// libspecific/Cairo/VipsIcoPlugin.h
// libvips foreign loader and saver for Windows icons (.ico), the framework's
// own - no ImageMagick involved, so every build reads and writes them.
//
//   icoload / icoload_buffer   the largest picture in the file: a PNG entry,
//                              or a bitmap one (32-bit with alpha; 24, 8, 4 or
//                              1-bit with the AND mask for transparency).
//                              Cursors (.cur) read the same way.
//   icosave                    one PNG entry per size in `sizes` (16, 32, 48
//                              and 256 unless told), each the picture fitted
//                              into that square on transparency; never
//                              larger than the picture itself.
//
// Registered by vips_foreign_ico_init_types(), which the image subsystem calls
// once after VIPS_INIT (UCImageRaster::InitializeImageSubsysterm), so
// new_from_file / write_to_file - and so PixelFX, UCImageRaster and the
// export encoder - take .ico like any other format.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_VIPS_ICO_PLUGIN_H
#define ULTRACANVAS_VIPS_ICO_PLUGIN_H

#include <vips/vips.h>

// Load an icon from a file / a memory buffer: its largest picture, RGBA.
int vips_icoload(const char* filename, VipsImage** out, ...);
int vips_icoload_buffer(void* buf, size_t len, VipsImage** out, ...);

// Save `in` as an icon. Optional: "sizes" (VipsArrayInt), the squares to
// write, each 1..256.
int vips_icosave(VipsImage* in, const char* filename, ...);

// Register the loader and saver types with libvips (once, after VIPS_INIT).
void vips_foreign_ico_init_types(void);

#endif // ULTRACANVAS_VIPS_ICO_PLUGIN_H
