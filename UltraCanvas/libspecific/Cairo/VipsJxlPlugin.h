// libspecific/Cairo/VipsJxlPlugin.h
// The framework's own JPEG XL loader and saver for libvips, on libjxl:
// registered as jxlload / jxlload_buffer / jxlload_source and jxlsave /
// jxlsave_buffer / jxlsave_target - the names libvips gives its own - and
// only where this libvips has none (Ubuntu 22.04's is built without libjxl),
// so every build reads and writes .jxl the same way: through
// UCImageRaster, the export encoder and the format inventory, which ask
// libvips for "jxlload" and "jxlsave" and never know which answered.
//
// The loader takes the still picture (an animation's first frame) at 8 or
// 16 bits a channel, grey or RGB, with or without alpha, as libvips' does;
// the saver writes 8-bit RGB or RGBA, lossless or at a distance, with the
// Q, distance, effort and lossless arguments libvips' jxlsave takes, so
// ExportVImage's call needs no other spelling.
//
// UC_VIPS_JXL_PLUGIN=force registers it beside a libvips that has its own,
// at a higher priority, so the plug-in can be exercised on a machine whose
// libvips reads JPEG XL already (ImageJxlTest and ImageFormatsTest then
// run through it).
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_VIPS_JXL_PLUGIN_H
#define ULTRACANVAS_VIPS_JXL_PLUGIN_H

#include <vips/vips.h>

// Registers the loader and saver classes with libvips, after VIPS_INIT.
// Does nothing where libvips already has a "jxlload" (unless forced, see
// above). Safe to call more than once.
void vips_foreign_jxl_init_types(void);

// True when the framework's classes are the ones registered (this process
// reads JPEG XL through them, not libvips' own).
int vips_foreign_jxl_plugin_active(void);

#endif // ULTRACANVAS_VIPS_JXL_PLUGIN_H
