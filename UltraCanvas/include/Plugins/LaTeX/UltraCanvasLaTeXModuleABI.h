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
// Version: 1.0.0
// Last Modified: 2026-06-29
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {
class UltraCanvasLaTeXView; // abstract interface, defined in UltraCanvasLaTeXView.h
}

#define ULTRACANVAS_LATEX_ABI_VERSION 1

// Exported entry-point symbol names (resolved via dlsym/GetProcAddress).
#define ULTRACANVAS_LATEX_SYM_ABI_VERSION   "UltraCanvasLaTeXModule_ABIVersion"
#define ULTRACANVAS_LATEX_SYM_CREATE_VIEW   "UltraCanvasLaTeXModule_CreateView"
#define ULTRACANVAS_LATEX_SYM_DESTROY_VIEW  "UltraCanvasLaTeXModule_DestroyView"
#define ULTRACANVAS_LATEX_SYM_SET_FONT_DIR  "UltraCanvasLaTeXModule_SetFontSearchDir"

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

} // extern "C"
