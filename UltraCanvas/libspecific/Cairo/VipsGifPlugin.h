// libspecific/Cairo/VipsGifPlugin.h
// libvips foreign saver for GIF, the framework's own (UltraCanvasGifEncoder.h)
// - so every build writes GIF, whether or not its libvips has a GIF writer.
//
// libvips writes GIF only when it was built with cgif (its `gifsave`); the
// MSYS2 package (-Dcgif=disabled) and the macOS libraries have none, and
// ImageMagick's GIF coder is an optional delegate too. Those builds could not
// save a GIF through write_to_file, and FileLoader's format list left GIF out.
//
//   ucgifsave / ucgifsave_buffer   one frame, at most 256 colours (median cut),
//                                  transparency where alpha < 128.
//                                  Optional: "bitdepth" (1..8, the palette's
//                                  bits), "interlace". "dither" is accepted,
//                                  as cgif's gifsave takes it, and not used:
//                                  each pixel gets its nearest palette colour.
//
// Its priority is below cgif's gifsave and above ImageMagick's savers, so
// libvips picks it for a .gif only where it has no GIF writer of its own.
// Registered by vips_foreign_gif_init_types(), which the image subsystem calls
// once after VIPS_INIT (UCImageRaster::InitializeImageSubsysterm), so
// write_to_file / write_to_buffer - and so PixelFX, UCImageRaster, the export
// encoder and FileLoader's list - take .gif on every build.
// Version: 1.0.0
// Last Modified: 2026-10-15
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_VIPS_GIF_PLUGIN_H
#define ULTRACANVAS_VIPS_GIF_PLUGIN_H

#include <vips/vips.h>

// The GIF saver to call by name: libvips' own "gifsave" where it was built
// with cgif, else the framework's "ucgifsave". Both take "bitdepth",
// "interlace" and "dither". (A file named .gif needs no name: write_to_file
// picks the same saver.)
const char* vips_foreign_gif_saver(void);

// Save `in` as a GIF with the framework's encoder. Optional: "bitdepth",
// "interlace", "dither" (see above).
int vips_ucgifsave(VipsImage* in, const char* filename, ...);

// Register the saver types with libvips (once, after VIPS_INIT).
void vips_foreign_gif_init_types(void);

#endif // ULTRACANVAS_VIPS_GIF_PLUGIN_H
