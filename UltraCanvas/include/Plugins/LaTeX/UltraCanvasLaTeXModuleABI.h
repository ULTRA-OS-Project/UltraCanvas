// include/Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h
// Stable C ABI between the UltraCanvas core loader and the on-demand LaTeX
// module (libUltraCanvasLaTeX.so / .dylib / .dll).
//
// The core never links the LaTeX module; it dlopen()s it on first use and
// resolves the symbols below. Both sides include this header so the contract
// stays in sync. Keep it free of any MicroTeX dependency.
//
// Versioning: bump ULTRACANVAS_LATEX_ABI_VERSION on any incompatible change to
// the factory signatures. The loader refuses a module whose reported version
// differs.
//
// Version: 1.1.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include <string>

#include <cstdint>

namespace UltraCanvas {
class UltraCanvasLaTeXView; // abstract interface, defined in UltraCanvasLaTeXView.h
class IRenderContext;       // UltraCanvasRenderContext.h
}

#define ULTRACANVAS_LATEX_ABI_VERSION 3

// Exported entry-point symbol names (resolved via dlsym/GetProcAddress).
#define ULTRACANVAS_LATEX_SYM_ABI_VERSION   "UltraCanvasLaTeXModule_ABIVersion"
#define ULTRACANVAS_LATEX_SYM_CREATE_VIEW   "UltraCanvasLaTeXModule_CreateView"
#define ULTRACANVAS_LATEX_SYM_DESTROY_VIEW  "UltraCanvasLaTeXModule_DestroyView"
#define ULTRACANVAS_LATEX_SYM_SET_FONT_DIR  "UltraCanvasLaTeXModule_SetFontSearchDir"
// Inline math for the text stack (ABI 3): typeset a formula into an opaque
// handle, read its metrics, draw it at a baseline, release it.
#define ULTRACANVAS_LATEX_SYM_TYPESET_INLINE "UltraCanvasLaTeXModule_TypesetInline"
#define ULTRACANVAS_LATEX_SYM_INLINE_METRICS "UltraCanvasLaTeXModule_InlineMetrics"
#define ULTRACANVAS_LATEX_SYM_DRAW_INLINE    "UltraCanvasLaTeXModule_DrawInline"
#define ULTRACANVAS_LATEX_SYM_RELEASE_INLINE "UltraCanvasLaTeXModule_ReleaseInline"

extern "C" {

// Returns ULTRACANVAS_LATEX_ABI_VERSION the module was built against.
typedef int (*UltraCanvasLaTeXModule_ABIVersionFn)(void);

// Construct a concrete LaTeX view. Ownership transfers to the caller, which
// MUST free it via the matching DestroyView (so allocation/deallocation stay
// inside the module). Returns nullptr on failure.
typedef UltraCanvas::UltraCanvasLaTeXView* (*UltraCanvasLaTeXModule_CreateViewFn)(
    const char* id, float x, float y, float w, float h);

// Destroy a view previously returned by CreateView.
typedef void (*UltraCanvasLaTeXModule_DestroyViewFn)(UltraCanvas::UltraCanvasLaTeXView*);

// Optional: set the directory searched for the bundled math font.
typedef void (*UltraCanvasLaTeXModule_SetFontSearchDirFn)(const char* dir);

// Typeset `latex` (math mode) at `fontSizePx` in colour `argb` (0xAARRGGBB),
// in display style when `displayStyle` is non-zero, else text style. `ctx`
// may be null; when given it measures text runs the math font lacks. Returns
// an opaque handle (never null while the module has a font; null when the
// math font could not be loaded - see the view's GetLastError path).
typedef void* (*UltraCanvasLaTeXModule_TypesetInlineFn)(
    const char* latex, float fontSizePx, uint32_t argb, int displayStyle,
    UltraCanvas::IRenderContext* ctx);

// Metrics of a typeset formula in pixels: advance width, ascent above and
// descent below the baseline. `error` receives the first diagnostic (owned by
// the handle; valid until release) or null. Returns 1 when the formula had no
// diagnostics, 0 otherwise.
typedef int (*UltraCanvasLaTeXModule_InlineMetricsFn)(
    void* handle, float* width, float* ascent, float* descent, const char** error);

// Draw a typeset formula with the left end of its baseline at (x, baselineY)
// in the context's current coordinate space.
typedef void (*UltraCanvasLaTeXModule_DrawInlineFn)(
    void* handle, UltraCanvas::IRenderContext* ctx, double x, double baselineY);

// Release a handle from TypesetInline.
typedef void (*UltraCanvasLaTeXModule_ReleaseInlineFn)(void* handle);

} // extern "C"
