// include/Plugins/LaTeX/UltraCanvasLaTeXBackend.h
// MicroTeX -> UltraCanvas rendering backend.
//
// Implements the four abstract platform interfaces MicroTeX needs
// (microtex::Graphics2D, microtex::Font, microtex::TextLayout and
// microtex::PlatformFactory) on top of UltraCanvas' IRenderContext.
//
// The engine is built in "Uni-math" mode with glyphs rendered as vector
// paths (compile option GLYPH_RENDER_TYPE=1). In that mode the engine emits
// glyph outlines through the path commands (beginPath/moveTo/.../fillPath),
// so drawGlyph() is never called and is left as a no-op. All drawing is routed
// through the IRenderContext transform/clip stack, which keeps math crisp at
// any zoom instead of blitting a rasterized surface.
//
// This header pulls in MicroTeX headers and is therefore plugin-internal:
// only the .cpp files under Plugins/LaTeX include it. The public widget
// (UltraCanvasLaTeXView.h) is free of any MicroTeX dependency.
//
// Version: 1.0.0
// Last Modified: 2026-06-28
// Author: UltraCanvas Framework
#pragma once

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "UltraCanvasRenderContext.h"

#include "graphic/graphic.h"
#include "graphic/graphic_basic.h"
#include "graphic/font_style.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== Font =====
// In path-render mode the engine only needs an opaque, comparable handle; the
// actual glyph outlines come from the .clm data, not a platform font object.
class Font_ultracanvas : public microtex::Font {
public:
    explicit Font_ultracanvas(std::string file) : file_(std::move(file)) {}
    bool operator==(const microtex::Font& f) const override;
    const std::string& file() const { return file_; }
private:
    std::string file_;
};

// ===== Graphics2D =====
// Wraps a (non-owning) IRenderContext for the duration of one formula draw.
class Graphics2D_ultracanvas : public microtex::Graphics2D {
public:
    explicit Graphics2D_ultracanvas(IRenderContext* ctx);

    IRenderContext* context() const { return ctx_; }

    // colors / stroke / font state
    void setColor(microtex::color c) override;
    microtex::color getColor() const override;
    void setStroke(const microtex::Stroke& s) override;
    const microtex::Stroke& getStroke() const override;
    void setStrokeWidth(float w) override;
    void setDash(const std::vector<float>& dash) override;
    std::vector<float> getDash() override;
    microtex::sptr<microtex::Font> getFont() const override;
    void setFont(const microtex::sptr<microtex::Font>& font) override;
    float getFontSize() const override;
    void setFontSize(float size) override;

    // transforms
    void translate(float dx, float dy) override;
    void scale(float sx, float sy) override;
    void rotate(float angle) override;
    void rotate(float angle, float px, float py) override;
    void reset() override;
    float sx() const override;
    float sy() const override;

    // glyphs (unused in path mode)
    void drawGlyph(microtex::u16 glyph, float x, float y) override;

    // path commands
    bool beginPath(microtex::i32 id) override;
    void moveTo(float x, float y) override;
    void lineTo(float x, float y) override;
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) override;
    void quadTo(float x1, float y1, float x2, float y2) override;
    void closePath() override;
    void fillPath(microtex::i32 id) override;

    // shape commands
    void drawLine(float x1, float y1, float x2, float y2) override;
    void drawRect(float x, float y, float w, float h) override;
    void fillRect(float x, float y, float w, float h) override;
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override;
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override;

private:
    void applyColor(microtex::color c);

    IRenderContext* ctx_;
    microtex::color color_ = microtex::black;
    microtex::Stroke stroke_;
    microtex::sptr<microtex::Font> font_;
    float fontSize_ = 1.f;
    float sx_ = 1.f, sy_ = 1.f;
};

// ===== TextLayout =====
// Backs `\text{...}` runs and any characters the engine cannot map to math
// glyphs, using UltraCanvas' Pango-based text layout for measurement + draw.
class TextLayout_ultracanvas : public microtex::TextLayout {
public:
    TextLayout_ultracanvas(IRenderContext* ctx,
                           const std::string& src,
                           microtex::FontStyle style,
                           float size);

    void getBounds(microtex::Rect& bounds) override;
    void draw(microtex::Graphics2D& g2, float x, float y) override;

private:
    std::unique_ptr<ITextLayout> layout_;
    float ascent_ = 0.f;
};

// ===== PlatformFactory =====
class PlatformFactory_ultracanvas : public microtex::PlatformFactory {
public:
    // The render context is set per-draw by the view before parse()/draw();
    // createTextLayout() captures it for the text runs it builds.
    void setContext(IRenderContext* ctx) { ctx_ = ctx; }

    microtex::sptr<microtex::Font> createFont(const std::string& file) override;
    microtex::sptr<microtex::TextLayout>
    createTextLayout(const std::string& src, microtex::FontStyle style, float size) override;

private:
    IRenderContext* ctx_ = nullptr;
};

// ===== Engine bootstrap =====
// One-time MicroTeX context init: registers the UltraCanvas platform factory and
// loads the bundled Latin Modern Math (clm2 + otf). Returns false if the engine
// could not be initialised (e.g. the font resources were not found). Safe to
// call repeatedly; subsequent calls are no-ops once initialised.
bool EnsureLaTeXEngineInitialized();

// Make the just-parsed/about-to-be-drawn render use `ctx` for any text runs.
void SetLaTeXActiveContext(IRenderContext* ctx);

// Override / augment the directories searched for the bundled math font.
// Takes effect on the next (first) initialization.
void SetLaTeXFontSearchDir(const std::string& dir);

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
