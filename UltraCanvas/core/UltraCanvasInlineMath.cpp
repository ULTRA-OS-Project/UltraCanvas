// core/UltraCanvasInlineMath.cpp
// Core-side handle on the LaTeX module's inline typesetting entry points.
// See UltraCanvasInlineMath.h. Compiled into the core whether or not the
// LaTeX plugin is built; without it every call reports "unavailable".
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasInlineMath.h"

#ifdef ULTRACANVAS_PLUGIN_LATEX
#include "Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h"
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"

namespace UltraCanvas {

// Implemented by the module loader (core/UltraCanvasLaTeXModuleLoader.cpp):
// loads the module on first call and hands out the inline entry points, or
// returns false when the module or the entry points are missing.
struct LaTeXInlineEntryPoints {
    UltraCanvasLaTeXModule_TypesetInlineFn typeset = nullptr;
    UltraCanvasLaTeXModule_InlineMetricsFn metrics = nullptr;
    UltraCanvasLaTeXModule_DrawInlineFn draw = nullptr;
    UltraCanvasLaTeXModule_ReleaseInlineFn release = nullptr;
};
bool GetLaTeXInlineEntryPoints(LaTeXInlineEntryPoints& out);

UltraCanvasInlineMath::~UltraCanvasInlineMath() {
    LaTeXInlineEntryPoints ep;
    if (handle_ && GetLaTeXInlineEntryPoints(ep) && ep.release) ep.release(handle_);
}

bool UltraCanvasInlineMath::IsAvailable() {
    LaTeXInlineEntryPoints ep;
    return GetLaTeXInlineEntryPoints(ep);
}

std::shared_ptr<UltraCanvasInlineMath> UltraCanvasInlineMath::Typeset(const std::string& latex, float fontSizePx,
                                                                      const Color& color, bool displayStyle,
                                                                      IRenderContext* ctx) {
    LaTeXInlineEntryPoints ep;
    if (!GetLaTeXInlineEntryPoints(ep)) return nullptr;
    void* h = ep.typeset(latex.c_str(), fontSizePx, color.ToARGB(), displayStyle ? 1 : 0, ctx);
    if (!h) return nullptr;
    std::shared_ptr<UltraCanvasInlineMath> m(new UltraCanvasInlineMath());
    m->handle_ = h;
    const char* err = nullptr;
    ep.metrics(h, &m->width_, &m->ascent_, &m->descent_, &err);
    if (err) m->error_ = err;
    return m;
}

void UltraCanvasInlineMath::Draw(IRenderContext* ctx, double x, double baselineY) const {
    LaTeXInlineEntryPoints ep;
    if (handle_ && ctx && GetLaTeXInlineEntryPoints(ep)) ep.draw(handle_, ctx, x, baselineY);
}

} // namespace UltraCanvas

#else // no LaTeX plugin in this build

namespace UltraCanvas {

UltraCanvasInlineMath::~UltraCanvasInlineMath() = default;
bool UltraCanvasInlineMath::IsAvailable() { return false; }
std::shared_ptr<UltraCanvasInlineMath> UltraCanvasInlineMath::Typeset(const std::string&, float, const Color&, bool,
                                                                      IRenderContext*) { return nullptr; }
void UltraCanvasInlineMath::Draw(IRenderContext*, double, double) const {}

} // namespace UltraCanvas

#endif
