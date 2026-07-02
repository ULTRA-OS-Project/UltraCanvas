/* ultracanvas_vtracer.h
 * C ABI for the vtracer_c Rust shim crate.
 * Version: 0.1.0
 * Last Modified: 2026-05-21
 * Author: UltraCanvas Framework
 *
 * Memory rule: any non-NULL pointer written into UCVectorizerOutput by
 * ultracanvas_vtracer_from_rgba is owned by the Rust side and must be
 * released with ultracanvas_vtracer_free. The input pixel buffer is
 * borrowed only for the duration of the call.
 */
#ifndef ULTRACANVAS_VTRACER_H
#define ULTRACANVAS_VTRACER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* color_mode  : 0 = Binary (B&W), 1 = Color
 * hierarchy   : 0 = Stacked,      1 = Cutout
 * path_mode   : 0 = Pixel,        1 = Polygon, 2 = Spline
 */
typedef struct UCVectorizerConfig {
    uint8_t color_mode;
    uint8_t hierarchy;
    uint8_t path_mode;
    uint8_t _reserved;
    int32_t filter_speckle;
    int32_t color_precision;
    int32_t layer_difference;
    int32_t corner_threshold;
    double  length_threshold;
    int32_t splice_threshold;
    int32_t max_iterations;
} UCVectorizerConfig;

typedef struct UCVectorizerOutput {
    char*  svg_utf8;     /* NUL-terminated SVG document; NULL on error */
    size_t svg_len;      /* length of svg_utf8 excluding terminator    */
    char*  error_utf8;   /* NUL-terminated message;     NULL on success */
} UCVectorizerOutput;

/* Returns 0 on success, non-zero on error.
 * `rgba` must point to width*height*4 bytes in RGBA order, row-major. */
int32_t ultracanvas_vtracer_from_rgba(const uint8_t*            rgba,
                                      int32_t                   width,
                                      int32_t                   height,
                                      const UCVectorizerConfig* config,
                                      UCVectorizerOutput*       out);

/* Releases all pointers inside `out` and zeroes them. Safe to call on a
 * default-initialised UCVectorizerOutput. */
void ultracanvas_vtracer_free(UCVectorizerOutput* out);

/* Returns a NUL-terminated, statically allocated VTracer version string. */
const char* ultracanvas_vtracer_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ULTRACANVAS_VTRACER_H */
