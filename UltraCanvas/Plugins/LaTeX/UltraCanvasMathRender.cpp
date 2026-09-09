// Plugins/LaTeX/UltraCanvasMathRender.cpp
// IRenderContext renderer of the native math engine's box tree.
// See UltraCanvasMathRender.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasMathRender.h"
#include "Plugins/LaTeX/UltraCanvasMathFont.h"

#include <cmath>
#include <memory>

namespace UltraCanvas {

namespace {

inline Color ToColor(MathColor c) { return Color::FromARGB(static_cast<uint32_t>(c)); }

std::unique_ptr<ITextLayout> MakeRunLayout(IRenderContext* ctx, const std::string& text,
                                           const MathFontStyle& style, float fontSizePx) {
    if (!ctx) return nullptr;
    auto layout = ctx->CreateTextLayout(text, /*isMarkup*/ false);
    if (!layout) return nullptr;
    layout->SetFontStyle(MathTextRunFontStyle(style, fontSizePx));
    layout->SetWrap(TextWrap::WrapNone);
    layout->SetSingleParagraphMode(true);
    return layout;
}

void EmitGlyph(IRenderContext* ctx, const MathBox& box, double x, double y) {
    if (!box.font) return;
    MathGlyphOutline outline;
    if (!box.font->GetGlyphOutline(box.glyph, outline) || outline.IsEmpty()) return;
    const double s = box.fontSize / static_cast<double>(box.font->GetUnitsPerEm());
    ctx->ClearPath();
    for (const auto& seg : outline.segments) {
        switch (seg.command) {
            case MathOutlineCommand::MoveTo:  ctx->MoveTo(x + seg.x1 * s, y - seg.y1 * s); break;
            case MathOutlineCommand::LineTo:  ctx->LineTo(x + seg.x1 * s, y - seg.y1 * s); break;
            case MathOutlineCommand::QuadTo:  ctx->QuadraticCurveTo(x + seg.x1 * s, y - seg.y1 * s, x + seg.x2 * s, y - seg.y2 * s); break;
            case MathOutlineCommand::CubicTo: ctx->BezierCurveTo(x + seg.x1 * s, y - seg.y1 * s, x + seg.x2 * s, y - seg.y2 * s, x + seg.x3 * s, y - seg.y3 * s); break;
            case MathOutlineCommand::Close:   ctx->ClosePath(); break;
        }
    }
    ctx->Fill();
}

void Draw(IRenderContext* ctx, const MathBox& b, double x, double y, const Color& color) {
    switch (b.type) {
        case MathBoxType::Glyph:
            ctx->SetFillPaint(color);
            EmitGlyph(ctx, b, x, y);
            return;
        case MathBoxType::Rule:
            if (b.width <= 0.f || b.height + b.depth <= 0.f) return;
            ctx->SetFillPaint(color);
            ctx->FillRectangle(Rect2Dd(x, y - b.height, b.width, b.height + b.depth));
            return;
        case MathBoxType::Kern:
            return;
        case MathBoxType::Text: {
            auto layout = MakeRunLayout(ctx, b.text, b.textStyle, b.fontSize);
            if (!layout) return;
            ctx->SetTextPaint(color);
            ctx->DrawTextLayout(*layout, Point2Dd(x, y - layout->GetBaseline()));
            return;
        }
        case MathBoxType::Error: {
            MathFontStyle mono; mono.family = MathFontFamily::Mono;
            auto layout = MakeRunLayout(ctx, b.text, mono, b.fontSize * 0.8f);
            if (!layout) return;
            ctx->SetTextPaint(Color(180, 40, 40, 255));
            ctx->DrawTextLayout(*layout, Point2Dd(x, y - layout->GetBaseline()));
            return;
        }
        case MathBoxType::Color: {
            const Color c = ToColor(b.color);
            for (const auto& ch : b.children) Draw(ctx, *ch.box, x + ch.dx, y + ch.dy, c);
            return;
        }
        case MathBoxType::Background:
            if (b.color != kMathColorNone) {
                ctx->SetFillPaint(ToColor(b.color));
                ctx->FillRectangle(Rect2Dd(x, y - b.height, b.width, b.height + b.depth));
            }
            for (const auto& ch : b.children) Draw(ctx, *ch.box, x + ch.dx, y + ch.dy, color);
            return;
        case MathBoxType::Frame: {
            const double lw = b.lineWidth;
            double w = b.width, h = b.height + b.depth;
            const double top = y - b.height;
            if (b.frameStyle == MathFrameStyle::Shadow) { w -= b.shadowOffset; h -= b.shadowOffset; }
            if (b.frameStyle == MathFrameStyle::Shadow) {
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(x + b.shadowOffset, top + b.shadowOffset, w, h));
                ctx->SetFillPaint(Colors::White);
                ctx->FillRectangle(Rect2Dd(x, top, w, h));
            }
            if (b.color != kMathColorNone) {
                ctx->SetFillPaint(ToColor(b.color));
                if (b.frameStyle == MathFrameStyle::Oval) ctx->FillRoundedRectangle(Rect2Dd(x, top, w, h), b.cornerRadius);
                else ctx->FillRectangle(Rect2Dd(x, top, w, h));
            }
            const Color border = b.borderColor != kMathColorNone ? ToColor(b.borderColor) : color;
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(static_cast<float>(lw));
            ctx->SetLineDash(UCDashPattern{});
            if (b.frameStyle == MathFrameStyle::Oval) {
                ctx->DrawRoundedRectangle(Rect2Dd(x + lw / 2, top + lw / 2, w - lw, h - lw), b.cornerRadius);
            } else if (b.frameStyle == MathFrameStyle::Double) {
                ctx->DrawRectangle(Rect2Dd(x + lw / 2, top + lw / 2, w - lw, h - lw));
                ctx->SetStrokeWidth(static_cast<float>(lw * 0.6));
                ctx->DrawRectangle(Rect2Dd(x + 2.5 * lw, top + 2.5 * lw, w - 5 * lw, h - 5 * lw));
            } else {
                ctx->DrawRectangle(Rect2Dd(x + lw / 2, top + lw / 2, w - lw, h - lw));
            }
            for (const auto& ch : b.children) Draw(ctx, *ch.box, x + ch.dx, y + ch.dy, color);
            return;
        }
        case MathBoxType::Cancel: {
            for (const auto& ch : b.children) Draw(ctx, *ch.box, x + ch.dx, y + ch.dy, color);
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(b.lineWidth);
            ctx->SetLineDash(UCDashPattern{});
            const double top = y - b.height, bottom = y + b.depth;
            if (b.cancelKind != MathCancelKind::BCancel) ctx->DrawLine(Point2Dd(x, bottom), Point2Dd(x + b.width, top));
            if (b.cancelKind != MathCancelKind::Cancel)  ctx->DrawLine(Point2Dd(x, top), Point2Dd(x + b.width, bottom));
            return;
        }
        case MathBoxType::Transform: {
            ctx->PushState();
            ctx->Translate(x + b.transformDx, y + b.transformDy);
            // Column-major (a c e / b d f) as Cairo: x' = a x + c y + e.
            ctx->Transform(b.ma, b.mc, b.mb, b.md, 0.0, 0.0);
            for (const auto& ch : b.children) Draw(ctx, *ch.box, ch.dx, ch.dy, color);
            ctx->PopState();
            return;
        }
        case MathBoxType::List:
            for (const auto& ch : b.children) Draw(ctx, *ch.box, x + ch.dx, y + ch.dy, color);
            return;
    }
}

} // namespace

FontStyle MathTextRunFontStyle(const MathFontStyle& style, float fontSizePx) {
    FontStyle fs;
    switch (style.family) {
        case MathFontFamily::Sans: fs.fontFamily = "Sans"; break;
        case MathFontFamily::Mono: fs.fontFamily = "Monospace"; break;
        default:                   fs.fontFamily = "Serif"; break;
    }
    fs.fontSize = fontSizePx;
    fs.fontWeight = style.bold ? FontWeight::Bold : FontWeight::Normal;
    fs.fontSlant = style.shape == MathFontShape::Italic ? FontSlant::Italic : FontSlant::Normal;
    return fs;
}

bool MathContextTextFallback::MeasureText(const std::string& utf8, const MathFontStyle& style, float fontSizePx,
                                          float& width, float& ascent, float& descent) {
    auto layout = MakeRunLayout(ctx_, utf8, style, fontSizePx);
    if (!layout) return false;
    const UCLayoutExtents ext = layout->GetLayoutExtents();
    const double baseline = layout->GetBaseline();
    width = static_cast<float>(ext.logical.width);
    ascent = static_cast<float>(baseline);
    descent = static_cast<float>(std::max(0.0, ext.logical.height - baseline));
    return true;
}

void DrawMathBox(IRenderContext* ctx, const MathBox& box, double x, double baselineY, const Color& color) {
    if (!ctx) return;
    ctx->PushState();
    ctx->SetLineCap(LineCap::Butt);
    ctx->SetLineJoin(LineJoin::Miter);
    Draw(ctx, box, x, baselineY, color);
    ctx->PopState();
}

} // namespace UltraCanvas
