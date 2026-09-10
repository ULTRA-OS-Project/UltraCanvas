// Plugins/LaTeX/UltraCanvasLaTeXBackend.cpp
// MicroTeX -> UltraCanvas rendering backend implementation.
// See UltraCanvasLaTeXBackend.h for the design overview.
// Version: 1.1.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasLaTeXBackend.h"

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "UltraCanvasConfig.h"  // GetResourcesDir
#include "UltraCanvasUtils.h"   // GetExecutableDir, NormalizePath

#include "Plugins/LaTeX/UltraCanvasMathEngine.h"

#include "microtex.h"
#include "unimath/font_src.h"

#include <cstdlib>              // getenv
#include <fstream>
#include <memory>

using namespace microtex;

namespace UltraCanvas {

namespace {

// Color helper: microtex::color is a packed 0xAARRGGBB value, matching
// UltraCanvas Color::FromARGB exactly.
inline Color ToUC(color c) { return Color::FromARGB(static_cast<uint32_t>(c)); }

inline LineCap ToUCCap(Cap c) {
    switch (c) {
        case CAP_BUTT:   return LineCap::Butt;
        case CAP_ROUND:  return LineCap::Round;
        case CAP_SQUARE: return LineCap::Square;
    }
    return LineCap::Butt;
}

inline LineJoin ToUCJoin(Join j) {
    switch (j) {
        case JOIN_BEVEL: return LineJoin::Bevel;
        case JOIN_ROUND: return LineJoin::Round;
        case JOIN_MITER: return LineJoin::Miter;
    }
    return LineJoin::Miter;
}

bool FileExists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

// Directories to probe for the bundled math font, in priority order.
std::string g_userFontDir;
// Bumped whenever the user font dir changes, so a view whose font lookup
// failed knows a retry may now succeed (see GetLaTeXEngineFontDirGeneration).
unsigned g_fontDirGeneration = 0;

std::vector<std::string> FontSearchDirs() {
    std::vector<std::string> dirs;
    if (!g_userFontDir.empty()) dirs.push_back(g_userFontDir);
    if (const char* env = std::getenv("MICROTEX_FONTDIR"); env && *env) dirs.emplace_back(env);
    // The framework's own resource root first - every other media/ consumer
    // (icons, fonts, the demo's .tex files) resolves through it. That is
    // <exe>/share/ in a dev build (the root CMake copies media/ to
    // <build>/share/media) and <exe>/../share/ in a package.
    dirs.push_back(NormalizePath(GetResourcesDir() + "media/microtex"));
    const std::string exe = GetExecutableDir();
    if (!exe.empty()) {
        dirs.push_back(exe + "/media/microtex");
        dirs.push_back(exe + "/../media/microtex");
        dirs.push_back(exe + "/share/media/microtex");
        dirs.push_back(exe + "/../share/media/microtex");
        dirs.push_back(exe + "/share/UltraCanvas/media/microtex");
        dirs.push_back(exe + "/../share/UltraCanvas/media/microtex");
    }
    // Last resort: relative to the working directory (running from the
    // repository root, as an IDE launch typically does).
    dirs.push_back("media/microtex");
    dirs.push_back("share/media/microtex");
    dirs.push_back("share/UltraCanvas/media/microtex");
    return dirs;
}

std::string LocateFont(const std::string& fileName) {
    for (const auto& d : FontSearchDirs()) {
        const std::string p = d + "/" + fileName;
        if (FileExists(p)) return p;
    }
    return {};
}

} // namespace

// ===== Font_ultracanvas =====

bool Font_ultracanvas::operator==(const microtex::Font& f) const {
    return file_ == static_cast<const Font_ultracanvas&>(f).file_;
}

// ===== Graphics2D_ultracanvas =====

Graphics2D_ultracanvas::Graphics2D_ultracanvas(IRenderContext* ctx) : ctx_(ctx) {
    setColor(black);
    setStroke(Stroke());
}

void Graphics2D_ultracanvas::applyColor(color c) {
    const Color uc = ToUC(c);
    ctx_->SetFillPaint(uc);
    ctx_->SetStrokePaint(uc);
    ctx_->SetTextPaint(uc);
}

void Graphics2D_ultracanvas::setColor(color c) {
    color_ = c;
    applyColor(c);
}

color Graphics2D_ultracanvas::getColor() const { return color_; }

void Graphics2D_ultracanvas::setStroke(const Stroke& s) {
    stroke_ = s;
    ctx_->SetStrokeWidth(s.lineWidth);
    ctx_->SetLineCap(ToUCCap(s.cap));
    ctx_->SetLineJoin(ToUCJoin(s.join));
    ctx_->SetMiterLimit(s.miterLimit);
}

const Stroke& Graphics2D_ultracanvas::getStroke() const { return stroke_; }

void Graphics2D_ultracanvas::setStrokeWidth(float w) {
    stroke_.lineWidth = w;
    ctx_->SetStrokeWidth(w);
}

void Graphics2D_ultracanvas::setDash(const std::vector<float>& dash) {
    UCDashPattern pattern;
    pattern.dashes.assign(dash.begin(), dash.end());
    ctx_->SetLineDash(pattern);
}

std::vector<float> Graphics2D_ultracanvas::getDash() { return {}; }

microtex::sptr<microtex::Font> Graphics2D_ultracanvas::getFont() const { return font_; }
void Graphics2D_ultracanvas::setFont(const microtex::sptr<microtex::Font>& font) { font_ = font; }
float Graphics2D_ultracanvas::getFontSize() const { return fontSize_; }
void Graphics2D_ultracanvas::setFontSize(float size) { fontSize_ = size; }

void Graphics2D_ultracanvas::translate(float dx, float dy) { ctx_->Translate(dx, dy); }

void Graphics2D_ultracanvas::scale(float sx, float sy) {
    sx_ *= sx;
    sy_ *= sy;
    ctx_->Scale(sx, sy);
}

void Graphics2D_ultracanvas::rotate(float angle) { ctx_->Rotate(angle); }

void Graphics2D_ultracanvas::rotate(float angle, float px, float py) {
    ctx_->Translate(px, py);
    ctx_->Rotate(angle);
    ctx_->Translate(-px, -py);
}

void Graphics2D_ultracanvas::reset() {
    // The engine uses only balanced translate/scale pairs while drawing and
    // never calls reset() mid-draw, so we intentionally do NOT reset the
    // context matrix here (doing so would clobber the element's own origin
    // transform). We only clear our tracked scale.
    sx_ = sy_ = 1.f;
}

float Graphics2D_ultracanvas::sx() const { return sx_; }
float Graphics2D_ultracanvas::sy() const { return sy_; }

void Graphics2D_ultracanvas::drawGlyph(u16, float, float) {
    // No-op: GLYPH_RENDER_TYPE=1 (path mode) emits glyph outlines via the path
    // commands below, so this is never invoked.
}

bool Graphics2D_ultracanvas::beginPath(i32) {
    ctx_->ClearPath();
    return false; // path caching not supported
}

void Graphics2D_ultracanvas::moveTo(float x, float y) { ctx_->MoveTo(x, y); }
void Graphics2D_ultracanvas::lineTo(float x, float y) { ctx_->LineTo(x, y); }

void Graphics2D_ultracanvas::cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    ctx_->BezierCurveTo(x1, y1, x2, y2, x3, y3);
}

void Graphics2D_ultracanvas::quadTo(float x1, float y1, float x2, float y2) {
    ctx_->QuadraticCurveTo(x1, y1, x2, y2);
}

void Graphics2D_ultracanvas::closePath() { ctx_->ClosePath(); }

void Graphics2D_ultracanvas::fillPath(i32) { ctx_->Fill(); }

void Graphics2D_ultracanvas::drawLine(float x1, float y1, float x2, float y2) {
    ctx_->DrawLine(Point2Dd(x1, y1), Point2Dd(x2, y2));
}

void Graphics2D_ultracanvas::drawRect(float x, float y, float w, float h) {
    ctx_->DrawRectangle(Rect2Dd(x, y, w, h));
}

void Graphics2D_ultracanvas::fillRect(float x, float y, float w, float h) {
    ctx_->FillRectangle(Rect2Dd(x, y, w, h));
}

void Graphics2D_ultracanvas::drawRoundRect(float x, float y, float w, float h, float rx, float ry) {
    ctx_->DrawRoundedRectangle(Rect2Dd(x, y, w, h), std::max(rx, ry));
}

void Graphics2D_ultracanvas::fillRoundRect(float x, float y, float w, float h, float rx, float ry) {
    ctx_->FillRoundedRectangle(Rect2Dd(x, y, w, h), std::max(rx, ry));
}

// ===== TextLayout_ultracanvas =====

TextLayout_ultracanvas::TextLayout_ultracanvas(IRenderContext* ctx,
                                               const std::string& src,
                                               microtex::FontStyle style,
                                               float size) {
    if (!ctx) return;
    layout_ = ctx->CreateTextLayout(src, /*isMarkup*/ false);
    if (!layout_) return;

    UltraCanvas::FontStyle fs;
    fs.fontFamily = "Serif";
    if (microtex::isSansSerif(style)) fs.fontFamily = "Sans";
    if (microtex::isMono(style))      fs.fontFamily = "Monospace";
    fs.fontSize   = size;
    fs.fontWeight = microtex::isBold(style)   ? FontWeight::Bold   : FontWeight::Normal;
    fs.fontSlant  = microtex::isItalic(style) ? FontSlant::Italic  : FontSlant::Normal;
    layout_->SetFontStyle(fs);

    ascent_ = static_cast<float>(layout_->GetBaseline());
}

void TextLayout_ultracanvas::getBounds(microtex::Rect& bounds) {
    if (!layout_) { bounds = microtex::Rect(); return; }
    const UCLayoutExtents ext = layout_->GetLayoutExtents();
    bounds.x = 0;
    bounds.w = static_cast<float>(ext.logical.width);
    // y is the ascent (distance above baseline, negative); h is the ink height.
    bounds.y = -ascent_ + static_cast<float>(ext.ink.y);
    bounds.h = static_cast<float>(ext.ink.height);
}

void TextLayout_ultracanvas::draw(Graphics2D& g2, float x, float y) {
    if (!layout_) return;
    auto& g = static_cast<Graphics2D_ultracanvas&>(g2);
    IRenderContext* ctx = g.context();
    ctx->SetTextPaint(ToUC(g2.getColor()));
    // y is baseline-aligned; the layout draws from its top-left.
    ctx->DrawTextLayout(*layout_, Point2Dd(x, y - ascent_));
}

// ===== PlatformFactory_ultracanvas =====

microtex::sptr<microtex::Font> PlatformFactory_ultracanvas::createFont(const std::string& file) {
    return std::make_shared<Font_ultracanvas>(file);
}

microtex::sptr<microtex::TextLayout>
PlatformFactory_ultracanvas::createTextLayout(const std::string& src, microtex::FontStyle style, float size) {
    return std::make_shared<TextLayout_ultracanvas>(ctx_, src, style, size);
}

// ===== Engine bootstrap =====

void SetLaTeXEngineFontDir(const std::string& dir) {
    if (g_userFontDir == dir) return;
    g_userFontDir = dir;
    ++g_fontDirGeneration;
}

unsigned GetLaTeXEngineFontDirGeneration() { return g_fontDirGeneration; }

void SetLaTeXActiveContext(IRenderContext* ctx) {
    auto* factory = static_cast<PlatformFactory_ultracanvas*>(PlatformFactory::get());
    if (factory) factory->setContext(ctx);
}

std::vector<std::string> GetLaTeXEngineFontSearchDirs() { return FontSearchDirs(); }

bool UseNativeLaTeXEngine() {
    static const bool native = [] {
        if (const char* env = std::getenv("ULTRACANVAS_LATEX_ENGINE"); env && *env) {
            const std::string v = env;
            if (v == "native" || v == "1" || v == "on") return true;
            if (v == "microtex" || v == "0" || v == "off") return false;
        }
#if defined(ULTRACANVAS_LATEX_DEFAULT_ENGINE_NATIVE) && ULTRACANVAS_LATEX_DEFAULT_ENGINE_NATIVE
        return true;
#else
        return false;
#endif
    }();
    return native;
}

namespace {
std::string g_nativeError;
unsigned g_nativeTriedGeneration = ~0u;
}

const std::string& GetNativeLaTeXEngineError() { return g_nativeError; }

bool EnsureNativeLaTeXEngineInitialized() {
    UltraCanvasMathEngine& engine = GetSharedMathEngine();
    if (engine.IsReady()) return true;
    if (g_nativeTriedGeneration == g_fontDirGeneration) return false;   // already failed at this search path
    g_nativeTriedGeneration = g_fontDirGeneration;
    if (engine.LoadFontFrom(FontSearchDirs(), {"latinmodern-math.otf"})) {
        g_nativeError.clear();
        return true;
    }
    g_nativeError = "math font latinmodern-math.otf not found (" + engine.GetLastError() + ")";
    return false;
}

bool EnsureLaTeXEngineInitialized() {
    if (MicroTeX::isInited()) return true;

    // Register the platform factory exactly once; a failed font lookup below
    // may bring us back here (after SetLaTeXFontSearchDir), and re-registering
    // would replace the factory a live view's context pointer was set on.
    static bool factoryRegistered = false;
    if (!factoryRegistered) {
        PlatformFactory::registerFactory("ultracanvas",
                                         std::make_unique<PlatformFactory_ultracanvas>());
        PlatformFactory::activate("ultracanvas");
        factoryRegistered = true;
    }

    const std::string clm = LocateFont("latinmodern-math.clm2");
    if (clm.empty()) return false;
    const std::string otf = LocateFont("latinmodern-math.otf"); // optional in path mode

    try {
        const FontSrcFile src(clm, otf);
        MicroTeX::init(src);
    } catch (...) {
        return false;
    }
    return MicroTeX::isInited();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
