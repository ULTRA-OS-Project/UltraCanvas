// include/UltraCanvasDrawnMarks.h
// Small marks drawn as shapes - a tick, a cross, a list bullet, a sort
// triangle - for elements that would otherwise type a symbol character.
// The bundled UI font (Ubuntu) has no "✓", "✗", "◦", "▪", "▲" or "▼"; they
// come from whatever fallback font the system has, and where it has none
// (a stock Windows install, the bundled fonts alone) they show as hex boxes.
// A drawn mark looks the same everywhere and takes the colour it is given.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <string>

namespace UltraCanvas {

    // A tick filling most of a `size` x `size` box centred on `centre`.
    inline void DrawTickMark(IRenderContext* ctx, const Point2Dd& centre, double size, const Color& color) {
        ctx->PushState();
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(std::max(1.5, size * 0.13));
        ctx->SetLineCap(LineCap::Round);
        ctx->SetLineJoin(LineJoin::Round);
        ctx->ClearPath();
        ctx->MoveTo(centre.x - size * 0.34, centre.y + size * 0.02);
        ctx->LineTo(centre.x - size * 0.11, centre.y + size * 0.26);
        ctx->LineTo(centre.x + size * 0.36, centre.y - size * 0.24);
        ctx->Stroke();
        ctx->PopState();
    }

    // A diagonal cross filling most of a `size` x `size` box centred on `centre`.
    inline void DrawCrossMark(IRenderContext* ctx, const Point2Dd& centre, double size, const Color& color) {
        const double r = size * 0.28;
        ctx->PushState();
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(std::max(1.5, size * 0.13));
        ctx->SetLineCap(LineCap::Round);
        ctx->ClearPath();
        ctx->MoveTo(centre.x - r, centre.y - r);
        ctx->LineTo(centre.x + r, centre.y + r);
        ctx->MoveTo(centre.x + r, centre.y - r);
        ctx->LineTo(centre.x - r, centre.y + r);
        ctx->Stroke();
        ctx->PopState();
    }

    // A filled triangle `size` wide pointing up (`up`) or down, centred on
    // `centre`: a column header's sort direction.
    inline void DrawSortTriangle(IRenderContext* ctx, const Point2Dd& centre, double size, bool up,
                                 const Color& color) {
        const double half = size / 2.0;
        const double h = size * 0.55;
        const double tipY = up ? centre.y - h / 2.0 : centre.y + h / 2.0;
        const double baseY = up ? centre.y + h / 2.0 : centre.y - h / 2.0;
        ctx->PushState();
        ctx->SetFillPaint(color);
        ctx->ClearPath();
        ctx->MoveTo(centre.x, tipY);
        ctx->LineTo(centre.x + half, baseY);
        ctx->LineTo(centre.x - half, baseY);
        ctx->ClosePath();
        ctx->Fill();
        ctx->PopState();
    }

    // True for the list bullets DrawBulletMark draws: "•", "◦" and "▪".
    // Each takes the room of a "•" in the font (measure that one, not the
    // bullet, to place text after it).
    inline bool IsDrawnBullet(const std::string& bullet) {
        return bullet == "\xE2\x80\xA2"      // • U+2022
            || bullet == "\xE2\x97\xA6"      // ◦ U+25E6
            || bullet == "\xE2\x96\xAA";     // ▪ U+25AA
    }

    // Draws the list bullet "•" (a disc), "◦" (a ring) or "▪" (a square) as
    // a shape, where DrawText(bullet, topLeft) in `font` would put the
    // character: centred on the font's capitals, and as large as Ubuntu's
    // "•". Returns false, drawing nothing, for any other string; the caller
    // draws that one as text.
    inline bool DrawBulletMark(IRenderContext* ctx, const std::string& bullet, const Point2Dd& topLeft,
                               const FontStyle& font, const Color& color) {
        if (!IsDrawnBullet(bullet)) return false;
        const bool disc = bullet == "\xE2\x80\xA2";
        const bool ring = bullet == "\xE2\x97\xA6";

        // The font's em, from its line box (ascent + descent, about 1.15 em),
        // so the mark follows the font size whatever the screen resolution.
        const double em = ctx->GetLineBoxHeight(font) / 1.15;
        const Point2Dd centre(topLeft.x + em * 0.18, topLeft.y + ctx->GetCapCentreOffset(font));
        ctx->PushState();
        ctx->ClearPath();
        if (disc) {
            ctx->SetFillPaint(color);
            ctx->FillCircle(centre, em * 0.128);
        } else if (ring) {
            const double stroke = std::max(1.0, em * 0.06);
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(stroke);
            ctx->DrawCircle(centre, em * 0.128 - stroke / 2.0);
        } else {
            const double side = em * 0.22;
            ctx->SetFillPaint(color);
            ctx->FillRectangle(Rect2Dd(centre.x - side / 2.0, centre.y - side / 2.0, side, side));
        }
        ctx->PopState();
        return true;
    }

} // namespace UltraCanvas
