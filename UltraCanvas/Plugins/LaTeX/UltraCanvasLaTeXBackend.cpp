// Plugins/LaTeX/UltraCanvasLaTeXBackend.cpp
// MicroTeX -> UltraCanvas rendering backend implementation.
// See UltraCanvasLaTeXBackend.h for the design overview.
// Version: 1.0.0
// Last Modified: 2026-06-28
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasLaTeXBackend.h"

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "UltraCanvasUtils.h"   // GetExecutableDir

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

std::vector<std::string> FontSearchDirs() {
    std::vector<std::string> dirs;
    if (!g_userFontDir.empty()) dirs.push_back(g_userFontDir);
    if (const char* env = std::getenv("MICROTEX_FONTDIR"); env && *env) dirs.emplace_back(env);
    const std::string exe = GetExecutableDir();
    if (!exe.empty()) {
        dirs.push_back(exe + "/media/microtex");
        dirs.push_back(exe + "/../media/microtex");
        // Build/install layout: the root CMake copies media/ to
        // <build>/share/UltraCanvas/media (see CMakeLists.txt copy_directory).
        dirs.push_back(exe + "/share/UltraCanvas/media/microtex");
        dirs.push_back(exe + "/../share/UltraCanvas/media/microtex");
    }
    dirs.push_back("media/microtex");
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

void SetLaTeXEngineFontDir(const std::string& dir) { g_userFontDir = dir; }

void SetLaTeXActiveContext(IRenderContext* ctx) {
    auto* factory = static_cast<PlatformFactory_ultracanvas*>(PlatformFactory::get());
    if (factory) factory->setContext(ctx);
}

bool EnsureLaTeXEngineInitialized() {
    if (MicroTeX::isInited()) return true;

    PlatformFactory::registerFactory("ultracanvas",
                                     std::make_unique<PlatformFactory_ultracanvas>());
    PlatformFactory::activate("ultracanvas");

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
