// Plugins/Vector/XAR/UltraCanvasXARPlugin.cpp
// Xara XAR vector graphics format plugin implementation for UltraCanvas
// Version: 2.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasXARPlugin.h"
#include "UltraCanvasFileError.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <zlib.h>

namespace UltraCanvas {

    static constexpr float PI = 3.14159265358979323846f;

    static inline float MPtoPx(int32_t mp, float scale = 1.0f) {
        return static_cast<float>(mp) * XARConstants::MILLIPOINTS_TO_PIXELS * scale;
    }

    // While set, path nodes render as a flat silhouette in this paint instead
    // of their own fill/stroke — the shadow pass of a shadow controller.
    // Rendering is single-threaded per document, so a file-scope slot is safe.
    struct SilhouettePaint {
        Color color;
        int32_t penumbraMP = 0;     // soft-edge width, millipoints
    };
    static const SilhouettePaint* g_silhouettePaint = nullptr;

    static Color BlendColors(const Color& a, const Color& b, float t) {
        t = std::max(0.0f, std::min(1.0f, t));
        return Color(
            static_cast<uint8_t>(a.r * (1 - t) + b.r * t),
            static_cast<uint8_t>(a.g * (1 - t) + b.g * t),
            static_cast<uint8_t>(a.b * (1 - t) + b.b * t),
            static_cast<uint8_t>(a.a * (1 - t) + b.a * t));
    }

    static std::vector<GradientStop> BuildGradientStops(const XARFillAttribute& f) {
        std::vector<GradientStop> stops;
        if (!f.stops.empty()) {
            stops.reserve(f.stops.size());
            for (const auto& s : f.stops) {
                stops.push_back(GradientStop(static_cast<float>(s.position), s.color));
            }
        } else {
            stops.push_back(GradientStop(0.0f, f.startColor));
            stops.push_back(GradientStop(1.0f, f.endColor));
        }
        if (f.effect == XARFillEffect::Rainbow || f.effect == XARFillEffect::AltRainbow) {
            // Best-effort: keep endpoints, insert intermediate hues for visual cue
            if (stops.size() == 2) {
                Color mid;
                mid.r = static_cast<uint8_t>((stops[0].color.g + stops[1].color.b) / 2);
                mid.g = static_cast<uint8_t>((stops[0].color.b + stops[1].color.r) / 2);
                mid.b = static_cast<uint8_t>((stops[0].color.r + stops[1].color.g) / 2);
                mid.a = 255;
                stops.insert(stops.begin() + 1, GradientStop(0.5f, mid));
            }
        }
        return stops;
    }

// ===== XAR NODE BOUNDS =====

    Rect2Dd XARNode::CalculateBounds() const {
        if (boundsCached && bounds.width > 0 && bounds.height > 0) return bounds;
        if (children.empty()) return bounds;
        Rect2Dd total = bounds;
        for (const auto& child : children) {
            Rect2Dd cb = child->CalculateBounds();
            if (cb.width <= 0 || cb.height <= 0) continue;
            if (total.width == 0 && total.height == 0) {
                total = cb;
            } else {
                float minX = std::min(total.x, cb.x);
                float minY = std::min(total.y, cb.y);
                float maxX = std::max(total.x + total.width, cb.x + cb.width);
                float maxY = std::max(total.y + total.height, cb.y + cb.height);
                total = Rect2Dd(minX, minY, maxX - minX, maxY - minY);
            }
        }
        return total;
    }

// ===== APPLY ATTRIBUTES TO PATH RENDERING =====

    static void ApplyFillToContext(IRenderContext* ctx, const XARFillAttribute& f, float scale) {
        switch (f.type) {
            case XARFillType::NoneFill:
                ctx->SetFillPaint(Color(0, 0, 0, 0));
                break;
            case XARFillType::Flat:
                ctx->SetFillPaint(f.startColor);
                break;
            case XARFillType::LinearGradient: {
                Point2Dd s = MillipointsToPixels(f.startPoint, scale);
                Point2Dd e = MillipointsToPixels(f.endPoint, scale);
                auto stops = BuildGradientStops(f);
                auto pat = ctx->CreateLinearGradientPattern(s.x, s.y, e.x, e.y, stops);
                if (pat) ctx->SetFillPaint(pat); else ctx->SetFillPaint(f.startColor);
                break;
            }
            case XARFillType::CircularGradient:
            case XARFillType::EllipticalGradient: {
                Point2Dd c = MillipointsToPixels(f.startPoint, scale);
                Point2Dd ed = MillipointsToPixels(f.endPoint, scale);
                float r = std::sqrt((ed.x - c.x) * (ed.x - c.x) + (ed.y - c.y) * (ed.y - c.y));
                auto stops = BuildGradientStops(f);
                auto pat = ctx->CreateRadialGradientPattern(c.x, c.y, 0, c.x, c.y, r, stops);
                if (pat) ctx->SetFillPaint(pat); else ctx->SetFillPaint(f.startColor);
                break;
            }
            case XARFillType::ConicalGradient: {
                // No native conic; fall back to linear between centre and edge
                Point2Dd s = MillipointsToPixels(f.startPoint, scale);
                Point2Dd e = MillipointsToPixels(f.endPoint, scale);
                auto stops = BuildGradientStops(f);
                auto pat = ctx->CreateLinearGradientPattern(s.x, s.y, e.x, e.y, stops);
                if (pat) ctx->SetFillPaint(pat); else ctx->SetFillPaint(f.startColor);
                break;
            }
            case XARFillType::Diamond: {
                // Render as radial best-effort
                Point2Dd c = MillipointsToPixels(f.startPoint, scale);
                Point2Dd ed = MillipointsToPixels(f.endPoint, scale);
                float r = std::sqrt((ed.x - c.x) * (ed.x - c.x) + (ed.y - c.y) * (ed.y - c.y));
                auto stops = BuildGradientStops(f);
                auto pat = ctx->CreateRadialGradientPattern(c.x, c.y, 0, c.x, c.y, r, stops);
                if (pat) ctx->SetFillPaint(pat); else ctx->SetFillPaint(f.startColor);
                break;
            }
            case XARFillType::ThreeColour:
            case XARFillType::FourColour: {
                Point2Dd s = MillipointsToPixels(f.startPoint, scale);
                Point2Dd e = MillipointsToPixels(f.endPoint, scale);
                std::vector<GradientStop> stops;
                stops.push_back(GradientStop(0.0f, f.startColor));
                stops.push_back(GradientStop(1.0f, f.endColor));
                auto pat = ctx->CreateLinearGradientPattern(s.x, s.y, e.x, e.y, stops);
                if (pat) ctx->SetFillPaint(pat); else ctx->SetFillPaint(f.startColor);
                break;
            }
            case XARFillType::Bitmap:
            case XARFillType::ContoneBitmap:
                // Bitmap fills not natively expressible without offscreen pattern
                // support; fall back to start colour.
                ctx->SetFillPaint(f.startColor);
                break;
            case XARFillType::Fractal:
            case XARFillType::Noise: {
                // Fall back to averaged colour
                Color avg(
                    static_cast<uint8_t>((f.startColor.r + f.endColor.r) / 2),
                    static_cast<uint8_t>((f.startColor.g + f.endColor.g) / 2),
                    static_cast<uint8_t>((f.startColor.b + f.endColor.b) / 2),
                    255);
                ctx->SetFillPaint(avg);
                break;
            }
        }
    }

    static void ApplyLineToContext(IRenderContext* ctx, const XARLineAttribute& l, float scale) {
        Color c = l.color;
        if (l.lineTransparency > 0) {
            c.a = static_cast<uint8_t>(c.a * (255 - l.lineTransparency) / 255);
        }
        ctx->SetStrokePaint(c);
        ctx->SetStrokeWidth(l.GetWidthInPixels() * scale);
        ctx->SetLineCap(l.cap);
        ctx->SetLineJoin(l.join);
        ctx->SetMiterLimit(l.mitreLimit);
        if (!l.dashPattern.empty()) {
            UCDashPattern pat(l.dashPattern, 0.0);
            ctx->SetLineDash(pat);
        }
    }

// ===== PATH NODE =====

    // Decode (and for contone fills tint) a bitmap fill's image, caching the
    // result on the definition. Contone maps the bitmap's luminance between
    // the fill's start and end colours, keeping the bitmap's alpha; pixmap
    // pixels are premultiplied ARGB, so the tint un-premultiplies first.
    static UCPixmap* BitmapFillPixmap(const XARFillAttribute& fill) {
        XARBitmapDefinition* def = fill.bitmapDef;
        if (!def || def->data.empty()) return nullptr;

        if (fill.type != XARFillType::ContoneBitmap) {
            // Plain bitmap fill: xar-embedded bitmaps store transparency in
            // the alpha channel (255 = fully transparent), so decode with
            // the channel inverted; images without an alpha band decode
            // normally.
            if (!def->plainPixmap) {
                auto image = UCImage::GetFromMemory(def->data.data(), def->data.size());
                if (!image || !image->IsValid()) return nullptr;
                def->plainPixmap = image->CreatePixmapAlphaInverted();
                if (!def->plainPixmap) def->plainPixmap = image->GetPixmap();
            }
            return (def->plainPixmap && def->plainPixmap->IsValid())
                    ? def->plainPixmap.get() : nullptr;
        }

        if (!def->fillPixmap) {
            auto image = UCImage::GetFromMemory(def->data.data(), def->data.size());
            if (!image || !image->IsValid()) return nullptr;
            // Contone sources use the same inverted alpha semantics as plain
            // bitmap fills (255 = fully transparent).
            def->fillPixmap = image->CreatePixmapAlphaInverted();
            if (!def->fillPixmap) def->fillPixmap = image->GetPixmap();
        }
        if (!def->fillPixmap || !def->fillPixmap->IsValid()) return nullptr;

        auto sameColor = [](const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        if (def->tintedPixmap && sameColor(def->tintStart, fill.startColor) &&
            sameColor(def->tintEnd, fill.endColor)) {
            return def->tintedPixmap.get();
        }

        int w = def->fillPixmap->GetWidth();
        int h = def->fillPixmap->GetHeight();
        auto tinted = std::make_shared<UCPixmap>();
        if (!tinted->Init(w, h)) return def->fillPixmap.get();
        // Empirically verified against Xara's own renderings (the cogwheel in
        // media/xar/demo.xar against the file's embedded preview, and a
        // flattened soft-shadow contone against its author's PDF export):
        // luminance 0 maps to the fill's START colour, luminance 255 to the
        // END colour, and the bitmap's own (inverted) alpha is preserved so
        // the page shows through transparent regions.
        const Color& start = fill.startColor;
        const Color& end = fill.endColor;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t p = def->fillPixmap->GetPixel(x, y);
                uint32_t a = p >> 24;
                if (a == 0) { tinted->SetPixel(x, y, 0); continue; }
                uint32_t r = ((p >> 16) & 0xFF) * 255 / a;
                uint32_t g = ((p >> 8) & 0xFF) * 255 / a;
                uint32_t b = (p & 0xFF) * 255 / a;
                uint32_t lum = (r * 54 + g * 183 + b * 19) >> 8;
                if (lum > 255) lum = 255;
                r = start.r + (end.r - start.r) * static_cast<int>(lum) / 255;
                g = start.g + (end.g - start.g) * static_cast<int>(lum) / 255;
                b = start.b + (end.b - start.b) * static_cast<int>(lum) / 255;
                // Re-premultiply against the preserved alpha
                r = r * a / 255; g = g * a / 255; b = b * a / 255;
                tinted->SetPixel(x, y, (a << 24) | (r << 16) | (g << 8) | b);
            }
        }
        tinted->MarkDirty();
        def->tintedPixmap = std::move(tinted);
        def->tintStart = fill.startColor;
        def->tintEnd = fill.endColor;
        return def->tintedPixmap.get();
    }

    void XARPathNode::Render(IRenderContext* ctx, float scale) {
        if (commands.empty()) { XARNode::Render(ctx, scale); return; }
        ctx->PushState();
        if (hasTransform) transform.ApplyToContext(ctx);

        if (g_silhouettePaint) {
            // Shadow pass: the node's own geometry in the silhouette paint.
            // The penumbra is faked with two widened, fainter stroke passes
            // (widest first) around the hard silhouette.
            const SilhouettePaint& sp = *g_silhouettePaint;
            float pen = MPtoPx(sp.penumbraMP, scale);
            float w = (isStroked && hasLine) ? line.GetWidthInPixels() * scale
                                             : 0.0f;
            bool fillIt = isFilled && hasFill;
            EmitPath(ctx, scale);
            ctx->SetLineCap(LineCap::Round);
            ctx->SetLineJoin(LineJoin::Round);
            if (pen > 0.5f) {
                Color soft = sp.color;
                soft.a = static_cast<uint8_t>(sp.color.a / 6);
                ctx->SetStrokePaint(soft);
                ctx->SetStrokeWidth(w + 2.0f * pen);
                ctx->StrokePathPreserve();
                soft.a = static_cast<uint8_t>(sp.color.a / 3);
                ctx->SetStrokePaint(soft);
                ctx->SetStrokeWidth(w + pen);
                ctx->StrokePathPreserve();
            }
            if (fillIt) {
                ctx->SetFillPaint(sp.color);
                ctx->FillPathPreserve();
            }
            if (w > 0.0f) {
                ctx->SetStrokePaint(sp.color);
                ctx->SetStrokeWidth(w);
                ctx->SetLineCap(line.cap);
                ctx->SetLineJoin(line.join);
                ctx->StrokePathPreserve();
            }
            ctx->ClearPath();
            ctx->PopState();
            XARNode::Render(ctx, scale);
            return;
        }

        float fillAlpha = 1.0f;
        if (hasTransparency && transparency.type != XARTransparencyType::NoTrans) {
            fillAlpha = 1.0f - static_cast<float>(transparency.startTransparency) / 255.0f;
        }

        bool wantFill = isFilled && hasFill;
        if (wantFill && (fill.type == XARFillType::Bitmap ||
                         fill.type == XARFillType::ContoneBitmap)) {
            Point2Dd bl = MillipointsToPixels(fill.startPoint, scale);
            Point2Dd br = MillipointsToPixels(fill.endPoint, scale);
            Point2Dd tl = MillipointsToPixels(fill.endPoint2, scale);
            // A degenerate parallelogram would make the transform
            // non-invertible and poison the whole surface — fall back to the
            // flat approximation instead.
            double det = (br.x - bl.x) * (bl.y - tl.y) -
                         (br.y - bl.y) * (bl.x - tl.x);
            UCPixmap* pixmap = (std::abs(det) > 1e-6) ? BitmapFillPixmap(fill)
                                                      : nullptr;
            if (pixmap) {
                // Clip to the path and map the bitmap onto the fill
                // parallelogram: origin at the fill's top-left so the image's
                // top row stays at the visual top under the document's
                // global Y-flip.
                ctx->PushState();
                EmitPath(ctx, scale);
                ctx->ClipPath();
                ctx->ClearPath();
                ctx->SetAlpha(fillAlpha);
                ctx->Transform(br.x - bl.x, br.y - bl.y,
                               bl.x - tl.x, bl.y - tl.y,
                               tl.x, tl.y);
                ctx->DrawPixmap(*pixmap, Rect2Dd(0, 0, 1, 1), ImageFitMode::Fill);
                ctx->PopState();
                wantFill = false;   // painted; keep only the stroke below
            }
            // Decode failure falls through to the flat approximation.
        }

        EmitPath(ctx, scale);

        if (wantFill) {
            ApplyFillToContext(ctx, fill, scale);
            ctx->SetAlpha(fillAlpha);
            ctx->FillPathPreserve();
        }
        if (isStroked && hasLine) {
            ApplyLineToContext(ctx, line, scale);
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

    void XARPathNode::EmitPath(IRenderContext* ctx, float scale, const XARMatrix* extra) const {
        ctx->ClearPath();
        for (const auto& cmd : commands) {
            switch (cmd.verb) {
                case XARPathVerb::MoveTo:
                    if (!cmd.points.empty()) {
                        Point2Di p = extra ? extra->Transform(cmd.points[0]) : cmd.points[0];
                        Point2Dd px = MillipointsToPixels(p, scale);
                        ctx->MoveTo(px.x, px.y);
                    }
                    break;
                case XARPathVerb::LineTo:
                    if (!cmd.points.empty()) {
                        Point2Di p = extra ? extra->Transform(cmd.points[0]) : cmd.points[0];
                        Point2Dd px = MillipointsToPixels(p, scale);
                        ctx->LineTo(px.x, px.y);
                    }
                    break;
                case XARPathVerb::BezierTo:
                    if (cmd.points.size() >= 3) {
                        Point2Di c1 = extra ? extra->Transform(cmd.points[0]) : cmd.points[0];
                        Point2Di c2 = extra ? extra->Transform(cmd.points[1]) : cmd.points[1];
                        Point2Di e = extra ? extra->Transform(cmd.points[2]) : cmd.points[2];
                        Point2Dd p1 = MillipointsToPixels(c1, scale);
                        Point2Dd p2 = MillipointsToPixels(c2, scale);
                        Point2Dd p3 = MillipointsToPixels(e, scale);
                        ctx->BezierCurveTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                    }
                    break;
                case XARPathVerb::ClosePath:
                    ctx->ClosePath();
                    break;
            }
        }
    }

// ===== RECTANGLE NODE =====

    void XARRectangleNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (!transform.IsIdentity()) transform.ApplyToContext(ctx);
        Point2Dd c = MillipointsToPixels(centre, scale);
        float hw, hh;
        if (isSimple) {
            // simple rectangles use majorAxis/minorAxis as half-width/height vectors
            Point2Dd ma = MillipointsToPixels(majorAxis, scale);
            Point2Dd mi = MillipointsToPixels(minorAxis, scale);
            hw = std::sqrt(ma.x * ma.x + ma.y * ma.y);
            hh = std::sqrt(mi.x * mi.x + mi.y * mi.y);
        } else {
            hw = MPtoPx(halfWidth, scale);
            hh = MPtoPx(halfHeight, scale);
        }
        Rect2Dd rect{c.x - hw, c.y - hh, hw * 2.0f, hh * 2.0f};
        float r = MPtoPx(cornerRadius, scale);

        if (hasFill) {
            ApplyFillToContext(ctx, fill, scale);
            if (isRounded && r > 0) ctx->FillRoundedRectangle(rect, r);
            else ctx->FillRectangle(rect);
        }
        if (hasLine) {
            ApplyLineToContext(ctx, line, scale);
            if (isRounded && r > 0) ctx->DrawRoundedRectangle(rect, r);
            else ctx->DrawRectangle(rect);
        }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== ELLIPSE NODE =====

    void XAREllipseNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (!transform.IsIdentity()) transform.ApplyToContext(ctx);
        Point2Dd c = MillipointsToPixels(centre, scale);
        Point2Dd ma = MillipointsToPixels(majorAxis, scale);
        Point2Dd mi = MillipointsToPixels(minorAxis, scale);
        float rx = std::sqrt(ma.x * ma.x + ma.y * ma.y);
        float ry = std::sqrt(mi.x * mi.x + mi.y * mi.y);

        if (hasFill) {
            ApplyFillToContext(ctx, fill, scale);
            ctx->FillEllipse({c.x, c.y, rx, ry});
        }
        if (hasLine) {
            ApplyLineToContext(ctx, line, scale);
            ctx->DrawEllipse({c.x, c.y, rx, ry});
        }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== POLYGON NODE =====

    std::vector<Point2Dd> XARPolygonNode::GeneratePolygonPoints(float scale) const {
        // Vertices in millipoint space, then through `transform` (also
        // millipoints), then to pixels — the caller must NOT apply the
        // node transform again.
        std::vector<Point2Dd> pts;
        int n = std::clamp(numSides, 3, 512);
        const double step = 2.0 * M_PI / n;

        // The axes are the radii of the shape's circumscribing ellipse. A
        // circle/ellipse samples it directly; a polygon's vertices lie ON
        // that ellipse, offset half a step from the axes — so a 4-sided
        // shape with axis-aligned axes is an axis-aligned square whose
        // corners sit at (±major + ±minor)/√2. Verified against the square
        // mosaics in backside.xar (21.5pt grid pitch = the square side).
        const bool asEllipse = isCircular;
        const int count = asEllipse ? 64 : n;
        const double vertexStep = asEllipse ? (2.0 * M_PI / count) : step;
        const double phase = asEllipse ? 0.0 : (M_PI / n);

        for (int i = 0; i < count; ++i) {
            double phi = phase + i * vertexStep;
            double cs = std::cos(phi), sn = std::sin(phi);
            double x = centre.x + majorAxis.x * cs + minorAxis.x * sn;
            double y = centre.y + majorAxis.y * cs + minorAxis.y * sn;
            double tx = transform.a * x + transform.c * y + transform.e;
            double ty = transform.b * x + transform.d * y + transform.f;
            pts.push_back(Point2Dd(tx * XARConstants::MILLIPOINTS_TO_PIXELS * scale,
                                   ty * XARConstants::MILLIPOINTS_TO_PIXELS * scale));
        }
        return pts;
    }

    void XARPolygonNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        // GeneratePolygonPoints already applies the node transform.
        auto pts = GeneratePolygonPoints(scale);
        if (!pts.empty()) {
            ctx->ClearPath();
            ctx->MoveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) ctx->LineTo(pts[i].x, pts[i].y);
            ctx->ClosePath();
            if (hasFill) { ApplyFillToContext(ctx, fill, scale); ctx->FillPathPreserve(); }
            if (hasLine) { ApplyLineToContext(ctx, line, scale); ctx->StrokePathPreserve(); }
            ctx->ClearPath();
        }
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== GROUP =====

    void XARGroupNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (hasTransparency && transparency.type != XARTransparencyType::NoTrans) {
            ctx->SetAlpha(1.0f - static_cast<float>(transparency.startTransparency) / 255.0f);
        }
        XARNode::Render(ctx, scale);
        ctx->PopState();
    }

// ===== TEXT =====

    void XARTextStoryNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (hasTransform) transform.ApplyToContext(ctx);
        Point2Dd p = MillipointsToPixels(position, scale);
        ctx->Translate(p.x, p.y);
        XARNode::Render(ctx, scale);
        ctx->PopState();
    }

    // Set a string's font on the context; returns the pixel size used.
    static float ApplyTextFont(IRenderContext* ctx, const XARTextStringNode& s, float scale) {
        float sizePx = s.textAttr.GetFontSizeInPixels() * scale;
        if (sizePx <= 0.01f) sizePx = 12.0f * scale;
        ctx->SetFontFace(s.textAttr.fontName.empty() ? "Sans" : s.textAttr.fontName,
                         s.textAttr.bold ? FontWeight::Bold : FontWeight::Normal,
                         s.textAttr.italic ? FontSlant::Italic : FontSlant::Normal);
        // The context's font size is in points at the text system's 96 dpi
        // resolution, so glyphs come out size×96/72 device pixels; XAR page
        // pixels are 72 dpi points. Verified against a Designer Pro X19 PDF
        // export: without the factor every glyph rendered 4/3 too large.
        ctx->SetFontSize(sizePx * (72.0f / 96.0f));
        return sizePx;
    }

    // Measure and draw span text through the same layout engine. FillText's
    // toy-cairo path resolves fonts differently from the pango layouts
    // behind GetTextLineWidth, so mixing them makes measured span widths
    // and drawn glyphs disagree — and every caret position in the text
    // layout depends on those two agreeing.
    static double MeasureSpanText(IRenderContext* ctx, const std::string& text) {
        if (text.empty()) return 0;
        auto layout = ctx->GetOrCreateTextLayout(text, Size2Di(0, 0), false);
        if (layout && layout->IsValid()) return layout->GetLayoutWidth();
        return ctx->GetTextLineWidth(text);
    }

    static void DrawSpanText(IRenderContext* ctx, const std::string& text, const Color& color) {
        if (text.empty()) return;
        auto layout = ctx->GetOrCreateTextLayout(text, Size2Di(0, 0), false);
        if (layout && layout->IsValid()) {
            ctx->SetTextPaint(color);
            // Anchor the layout so its baseline lands on y = 0, like FillText.
            ctx->DrawTextLayout(*layout, Point2Dd(0, -layout->GetBaseline()));
        } else {
            ctx->SetFillPaint(color);
            ctx->FillText(text, 0, 0);
        }
    }

    void XARTextLineNode::Render(IRenderContext* ctx, float scale) {
        ctx->PushState();
        if (hasYOffset) {
            // Xara's own baseline offset (negative = down, and the document
            // space is Y-up, so it applies directly).
            ctx->Translate(0, yOffsetMP * XARConstants::MILLIPOINTS_TO_PIXELS * scale);
        } else if (lineIndex > 0) {
            float leading = textAttr.GetFontSizeInPixels() * 1.15f;
            if (leading <= 0.01f) leading = 12.0f;
            // The document space is Y-up, so the next line down is negative Y.
            ctx->Translate(0, -lineIndex * leading * scale);
        }

        // Lay the line's spans out along the caret: strings advance by their
        // measured width, TAG_TEXT_KERN records adjust the caret explicitly.
        // The paragraph justification anchors the whole line on the story
        // origin — centred text centres on it, right-aligned text ends at
        // it — preferring Xara's own line width (TAG_TEXT_LINE_INFO) over
        // our measurement so the anchor is exact even where fonts differ.
        float fontEm = textAttr.GetFontSizeInPixels() * scale;
        if (fontEm <= 0.01f) fontEm = 12.0f * scale;
        // List geometry, calibrated against a Designer Pro X19 PDF export:
        // marker glyphs from 0.375 em, the item text (and every wrapped
        // continuation line) from 1.5 em.
        const double markerIndent = isListItem ? 0.375 * fontEm : 0.0;
        const double hangIndent = 1.5 * fontEm;
        const bool inList = isListItem || hangingIndent;

        double total = 0;
        std::vector<double> widths;
        for (const auto& c : children) {
            if (c->type == XARNodeType::TextKern) {
                auto k = std::static_pointer_cast<XARTextKernNode>(c);
                total += k->offset.x * XARConstants::MILLIPOINTS_TO_PIXELS * scale;
                continue;
            }
            if (c->type != XARNodeType::TextString) continue;
            auto s = std::static_pointer_cast<XARTextStringNode>(c);
            double w = 0;
            if (!s->text.empty()) {
                ApplyTextFont(ctx, *s, scale);
                w = MeasureSpanText(ctx, s->text);
            }
            widths.push_back(w);
            total += w;
        }

        double anchorWidth = (lineWidthMP > 0)
                ? lineWidthMP * XARConstants::MILLIPOINTS_TO_PIXELS * scale
                : total;

        // Full justification spreads the slack between the natural width
        // and Xara's line width (= the column width on all but the last
        // line of a paragraph) across the line's word gaps. The line that
        // ends its paragraph stays at natural width, as Xara leaves it.
        if (textAttr.justification == XARTextAttribute::Justification::Full &&
            lineWidthMP > 0 && !inList && !endsParagraph &&
            RenderJustified(ctx, scale, anchorWidth)) {
            ctx->PopState();
            return;
        }

        // Our fonts rarely match the author's exactly, so measured span
        // widths drift from Xara's caret positions (visible as gaps after a
        // bold span, or letter-spaced runs of single characters). Xara's own
        // line width is authoritative: scale the measured widths so their
        // sum lands every span boundary where Xara put it. List lines skip
        // this — their indents make the measured and stored widths disagree
        // by design.
        double measuredSum = 0;
        for (double w : widths) measuredSum += w;
        double kernSum = total - measuredSum;
        if (lineWidthMP > 0 && measuredSum > 0.001 && !inList) {
            double factor = (anchorWidth - kernSum) / measuredSum;
            if (factor > 0.01 && factor < 100.0) {
                for (double& w : widths) w *= factor;
            }
        }

        double x = hangingIndent ? hangIndent : markerIndent, y = 0;
        switch (textAttr.justification) {
            case XARTextAttribute::Justification::Centre: x = -anchorWidth / 2.0; break;
            case XARTextAttribute::Justification::Right:  x = -anchorWidth; break;
            default: break;
        }

        size_t wi = 0;
        bool listTextStarted = false;
        for (const auto& c : children) {
            if (c->type == XARNodeType::TextString) {
                auto s = std::static_pointer_cast<XARTextStringNode>(c);
                // Marker-to-text transition of a list item: the item text
                // starts at the hanging indent, or at least half an em past
                // wherever the marker glyphs ended.
                if (isListItem && !s->fromCharRecord && !listTextStarted) {
                    listTextStarted = true;
                    x = std::max(x + 0.5 * fontEm, hangIndent);
                }
                ctx->PushState();
                ctx->Translate(x, y);
                c->Render(ctx, scale);
                ctx->PopState();
                x += widths[wi++];
            } else if (c->type == XARNodeType::TextKern) {
                auto k = std::static_pointer_cast<XARTextKernNode>(c);
                x += k->offset.x * XARConstants::MILLIPOINTS_TO_PIXELS * scale;
                y += k->offset.y * XARConstants::MILLIPOINTS_TO_PIXELS * scale;
            } else {
                c->Render(ctx, scale);
            }
        }
        ctx->PopState();
    }

    // Word-spread layout for fully-justified lines. Returns false when the
    // line has nothing to spread (no word gaps, no slack, or explicit kerns
    // take over) — the caller then falls back to the plain span layout.
    bool XARTextLineNode::RenderJustified(IRenderContext* ctx, float scale,
                                          double anchorWidth) {
        struct Token {
            XARTextStringNode* span;
            std::string word;
            bool gapBefore;
            double spaceW;      // width of the gap's space, in the gap's font
            double wordW;
        };
        std::vector<Token> tokens;
        bool pendingGap = false;
        double pendingSpaceW = 0;

        // Find the last text span so its trailing spaces (Xara keeps the
        // line-break space in the string) don't count as a gap.
        XARTextStringNode* lastSpan = nullptr;
        for (const auto& c : children) {
            if (c->type == XARNodeType::TextKern) return false;
            if (c->type == XARNodeType::TextString) {
                lastSpan = std::static_pointer_cast<XARTextStringNode>(c).get();
            }
        }

        double natural = 0;
        int gaps = 0;
        for (const auto& c : children) {
            if (c->type != XARNodeType::TextString) continue;
            auto s = std::static_pointer_cast<XARTextStringNode>(c);
            std::string text = s->text;
            if (s.get() == lastSpan) {
                while (!text.empty() && text.back() == ' ') text.pop_back();
            }
            if (text.empty()) continue;
            ApplyTextFont(ctx, *s, scale);
            // Trailing-space-safe space width: bracket the space with ink.
            double spaceW = MeasureSpanText(ctx, "| |") - MeasureSpanText(ctx, "||");
            if (spaceW <= 0) spaceW = 0.25 * textAttr.GetFontSizeInPixels() * scale;
            size_t pos = 0;
            while (pos < text.size()) {
                if (text[pos] == ' ') {
                    pendingGap = true;
                    pendingSpaceW = spaceW;
                    ++pos;
                    continue;
                }
                size_t end = text.find(' ', pos);
                if (end == std::string::npos) end = text.size();
                Token t;
                t.span = s.get();
                t.word = text.substr(pos, end - pos);
                t.gapBefore = pendingGap;
                t.spaceW = pendingSpaceW;
                t.wordW = MeasureSpanText(ctx, t.word);
                if (t.gapBefore) { natural += t.spaceW; ++gaps; }
                natural += t.wordW;
                tokens.push_back(std::move(t));
                pendingGap = false;
                pos = end;
            }
        }
        if (tokens.empty() || gaps == 0) return false;

        double extra = anchorWidth - natural;
        float fontEm = textAttr.GetFontSizeInPixels() * scale;
        if (fontEm <= 0.01f) fontEm = 12.0f * scale;
        // A slightly negative slack just means our font measures wider than
        // the author's — compressing the gaps keeps the right margin true.
        // Guard against degenerate stretches either way.
        double extraPerGap = extra / gaps;
        if (extraPerGap > 4.0 * fontEm) return false;
        if (extraPerGap < -0.15 * fontEm) extraPerGap = -0.15 * fontEm;

        double x = 0;
        for (const auto& t : tokens) {
            if (t.gapBefore) x += t.spaceW + extraPerGap;
            ctx->PushState();
            ctx->Translate(x, 0);
            ctx->Scale(1.0f, -1.0f);
            ApplyTextFont(ctx, *t.span, scale);
            DrawSpanText(ctx, t.word,
                         t.span->hasFill ? t.span->fill.startColor : Color(0, 0, 0, 255));
            ctx->PopState();
            x += t.wordW;
        }
        return true;
    }

    void XARTextStringNode::Render(IRenderContext* ctx, float scale) {
        if (text.empty()) return;
        ctx->PushState();
        // The document renders under a global Y-flip (Y-up file coordinates);
        // un-flip locally around the baseline so glyphs come out upright.
        // ApplyTextFont also guards against a zero size, which would make the
        // cairo font matrix non-invertible and poison the whole context.
        ctx->Scale(1.0f, -1.0f);
        ApplyTextFont(ctx, *this, scale);
        DrawSpanText(ctx, text, hasFill ? fill.startColor : Color(0, 0, 0, 255));
        ctx->PopState();
    }

// ===== BITMAP NODE =====

    void XARBitmapNode::Render(IRenderContext* ctx, float scale) {
        // Bitmap object rendering requires decoding the embedded image, then
        // mapping it onto a parallelogram defined by bottomLeft/bottomRight/topLeft.
        // The current IRenderContext has no native parallelogram-mapped DrawPixmap
        // primitive; we leave this as a TODO and fall back to a placeholder rect
        // outline so that bitmap-positioned objects don't disappear entirely.
        ctx->PushState();
        Point2Dd bl = MillipointsToPixels(bottomLeft, scale);
        Point2Dd br = MillipointsToPixels(bottomRight, scale);
        Point2Dd tl = MillipointsToPixels(topLeft, scale);
        Point2Dd tr(br.x + (tl.x - bl.x), br.y + (tl.y - bl.y));
        ctx->ClearPath();
        ctx->MoveTo(bl.x, bl.y);
        ctx->LineTo(br.x, br.y);
        ctx->LineTo(tr.x, tr.y);
        ctx->LineTo(tl.x, tl.y);
        ctx->ClosePath();
        ctx->SetFillPaint(Color(220, 220, 220, 200));
        ctx->FillPathPreserve();
        ctx->SetStrokePaint(Color(120, 120, 120, 255));
        ctx->SetStrokeWidth(1.0f);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
        ctx->PopState();
        XARNode::Render(ctx, scale);
    }

// ===== SHADOW NODE =====

    void XARShadowNode::Render(IRenderContext* ctx, float scale) {
        if (children.empty()) return;
        // Shadow pass: the children's silhouette in the shadow paint, at the
        // shadow offset, beneath the objects themselves. Nested shadows keep
        // the outermost paint.
        if (!g_silhouettePaint && shadowColor.a > 0) {
            SilhouettePaint paint{shadowColor, blurRadius};
            ctx->PushState();
            // A glow (type 3) is a symmetric halo; wall/floor shadows are
            // displaced copies (verified against a MAGIX Designer export).
            if (shadowType != 3) {
                ctx->Translate(MPtoPx(offsetX, scale), MPtoPx(offsetY, scale));
            }
            g_silhouettePaint = &paint;
            XARNode::Render(ctx, scale);
            g_silhouettePaint = nullptr;
            ctx->PopState();
        }
        XARNode::Render(ctx, scale);
    }

// ===== CLIPVIEW NODE =====

    void XARClipViewNode::Render(IRenderContext* ctx, float scale) {
        if (children.empty()) return;
        ctx->PushState();
        // First child is the clipping path; remaining are clipped content
        auto clipChild = children.front();
        if (auto pathNode = std::dynamic_pointer_cast<XARPathNode>(clipChild)) {
            pathNode->EmitPath(ctx, scale);
            ctx->ClipPath();
            ctx->ClearPath();
        }
        for (size_t i = 1; i < children.size(); ++i) {
            children[i]->Render(ctx, scale);
        }
        ctx->PopState();
    }

// ===== XAR DOCUMENT =====

    XARDocument::XARDocument() {
        root = std::make_shared<XARNode>();
        root->type = XARNodeType::Document;
        currentContext.fill.type = XARFillType::Flat;
        currentContext.fill.startColor = Color(0, 0, 0, 255);
        currentContext.line.color = Color(0, 0, 0, 255);
        currentContext.line.width = 250;
    }

    XARDocument::~XARDocument() = default;

    bool XARDocument::LoadFromFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(fileSize);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) return false;
        return LoadFromMemory(buffer.data(), buffer.size());
    }

    bool XARDocument::LoadFromMemory(const uint8_t* data, size_t size) {
        diagnostics = XARParseDiagnostics{};
        if (!data || size < 16) {
            Warn("File too small to be a XAR document");
            return false;
        }
        StreamFrame outer{data, size, 0};
        if (!ParseHeader(outer)) {
            Warn("Missing XARA file signature");
            return false;
        }
        curData = outer.data;
        curSize = outer.size;
        curOffset = outer.offset;
        if (!ParseRecords()) return false;

        if (haveSpreadInfo) {
            width = static_cast<float>(spreadWidthMP) * XARConstants::MILLIPOINTS_TO_PIXELS;
            height = static_cast<float>(spreadHeightMP) * XARConstants::MILLIPOINTS_TO_PIXELS;
        } else if (haveViewport) {
            width = viewport.width;
            height = viewport.height;
        } else {
            Rect2Dd b = root->CalculateBounds();
            width = b.width;
            height = b.height;
        }
        if (width <= 0) width = 800;
        if (height <= 0) height = 600;
        return true;
    }

// ===== HEADER =====

    bool XARDocument::ParseHeader(StreamFrame& outer) {
        if (outer.offset + 8 > outer.size) return false;
        size_t off = outer.offset;
        uint32_t magic1 = ReadUInt32(outer.data, off);
        uint32_t magic2 = ReadUInt32(outer.data, off);
        if (magic1 != XARConstants::MAGIC_XARA || magic2 != XARConstants::MAGIC_SIGNATURE) {
            return false;
        }
        outer.offset = off;
        return true;
    }

// ===== RECORDS =====

    bool XARDocument::ParseRecords() {
        while (true) {
            // If current stream is exhausted, unwind compression frames if any
            if (curOffset + 8 > curSize) {
                if (!streamStack.empty()) {
                    StreamFrame outer = streamStack.back();
                    streamStack.pop_back();
                    curData = outer.data;
                    curSize = outer.size;
                    curOffset = outer.offset;
                    continue;
                }
                break;
            }

            XARTag tag = static_cast<XARTag>(ReadUInt32(curData, curOffset));
            uint32_t recSize = ReadUInt32(curData, curOffset);

            currentSequenceNumber++;
            diagnostics.recordCount++;

            if (tag == XARTag::TAG_ENDOFFILE) {
                return true;
            }

            if (tag == XARTag::TAG_STARTCOMPRESSION) {
                // Consume the 4-byte payload (3 BYTE version + 1 BYTE type)
                if (curOffset + recSize > curSize) return false;
                curOffset += recSize;

                // Inflate the rest of curData starting here. Xar uses raw
                // deflate (DecompressZlib calls inflateInit2 with -15).
                std::vector<uint8_t> inflated;
                size_t consumed = 0;
                if (!DecompressZlib(curData + curOffset, curSize - curOffset, inflated, consumed)) {
                    Warn("Compressed section failed to inflate at record " +
                         std::to_string(currentSequenceNumber));
                    return false;
                }
                StreamFrame outer{curData, curSize, curOffset + consumed};
                streamStack.push_back(outer);
                inflatedBuffers.push_back(std::move(inflated));
                curData = inflatedBuffers.back().data();
                curSize = inflatedBuffers.back().size();
                curOffset = 0;
                continue;
            }

            if (tag == XARTag::TAG_ENDCOMPRESSION) {
                // The 8-byte data section lives OUTSIDE the compressed stream.
                // We've just read its tag+size from the inflated stream — discard
                // any remaining inflated bytes and unwind to the outer stream,
                // skipping the 8 outer bytes.
                if (streamStack.empty()) return true;
                StreamFrame outer = streamStack.back();
                streamStack.pop_back();
                curData = outer.data;
                curSize = outer.size;
                curOffset = outer.offset;
                if (curOffset + 8 <= curSize) curOffset += 8;
                continue;
            }

            // Normal record: read payload
            XARRecord record;
            record.tag = tag;
            record.size = recSize;
            if (curOffset + recSize > curSize) {
                Warn("Truncated record " + std::to_string(currentSequenceNumber) +
                     " (tag " + std::to_string(static_cast<uint32_t>(tag)) + ")");
                return false;
            }
            if (recSize > 0) {
                record.data.assign(curData + curOffset, curData + curOffset + recSize);
            }
            curOffset += recSize;

            ProcessRecord(record);
        }
        return true;
    }

    bool XARDocument::ReadRecord(XARRecord& record) {
        if (curOffset + 8 > curSize) return false;
        record.tag = static_cast<XARTag>(ReadUInt32(curData, curOffset));
        record.size = ReadUInt32(curData, curOffset);
        if (curOffset + record.size > curSize) return false;
        if (record.size > 0) {
            record.data.assign(curData + curOffset, curData + curOffset + record.size);
            curOffset += record.size;
        }
        return true;
    }

    bool XARDocument::DecompressZlib(const uint8_t* compressedData, size_t compressedSize,
                                     std::vector<uint8_t>& decompressedData,
                                     size_t& consumedCompressedBytes) {
        decompressedData.resize(std::max<size_t>(compressedSize * 4, 65536));
        z_stream stream;
        std::memset(&stream, 0, sizeof(stream));
        stream.next_in = const_cast<uint8_t*>(compressedData);
        stream.avail_in = static_cast<uInt>(compressedSize);
        stream.next_out = decompressedData.data();
        stream.avail_out = static_cast<uInt>(decompressedData.size());

        // Xar uses raw deflate (no zlib 2-byte header / Adler32 trailer).
        // -15 = MAX_WBITS with sign flipped to request raw deflate mode.
        if (inflateInit2(&stream, -15) != Z_OK) return false;
        int result;
        while (true) {
            result = inflate(&stream, Z_NO_FLUSH);
            if (result == Z_STREAM_END) break;
            if (result == Z_BUF_ERROR || result == Z_OK) {
                if (stream.avail_out == 0) {
                    size_t used = stream.total_out;
                    decompressedData.resize(decompressedData.size() * 2);
                    stream.next_out = decompressedData.data() + used;
                    stream.avail_out = static_cast<uInt>(decompressedData.size() - used);
                }
                if (stream.avail_in == 0 && result == Z_OK) {
                    // Need more input but none available
                    inflateEnd(&stream);
                    return false;
                }
                continue;
            }
            inflateEnd(&stream);
            return false;
        }
        decompressedData.resize(stream.total_out);
        consumedCompressedBytes = stream.total_in;
        inflateEnd(&stream);
        return true;
    }

// ===== DISPATCH =====

    void XARDocument::ProcessRecord(const XARRecord& record) {
        switch (record.tag) {
            // Navigation. The Xar tree grammar is: object record, TAG_DOWN,
            // child records (an object's attributes, a container's members),
            // TAG_UP. So TAG_DOWN descends into the node the previous record
            // created; attributes inside that scope belong to it.
            case XARTag::TAG_DOWN:
                contextStack.push(currentContext);
                nodeStack.push(lastNode ? lastNode : CurrentNode());
                lastNode.reset();
                break;
            case XARTag::TAG_UP:
                if (!contextStack.empty()) {
                    currentContext = contextStack.top();
                    contextStack.pop();
                }
                PopNode();
                lastNode.reset();
                break;

            // File framework
            case XARTag::TAG_FILEHEADER: ParseFileHeaderRecord(record); break;
            case XARTag::TAG_ATOMICTAGS:
            case XARTag::TAG_ESSENTIALTAGS:
            case XARTag::TAG_TAGDESCRIPTION:
                // Tag management metadata; safe to skip
                break;

            // Document structure
            case XARTag::TAG_DOCUMENT: {
                auto docNode = std::make_shared<XARNode>();
                docNode->type = XARNodeType::Document;
                AttachNode(docNode);
                break;
            }
            case XARTag::TAG_CHAPTER:
                AttachNode(std::make_shared<XARChapterNode>());
                break;
            case XARTag::TAG_SPREAD:
            case XARTag::TAG_SPREAD_PHASE2:
                ParseSpreadRecord(record); break;
            case XARTag::TAG_SPREADINFORMATION: ParseSpreadInfoRecord(record); break;
            case XARTag::TAG_LAYER: ParseLayerRecord(record); break;
            case XARTag::TAG_LAYERDETAILS: ParseLayerDetailsRecord(record, false); break;
            case XARTag::TAG_GUIDELAYERDETAILS: ParseLayerDetailsRecord(record, true); break;
            case XARTag::TAG_PAGE: ParsePageRecord(record); break;
            case XARTag::TAG_GUIDELINE:
            case XARTag::TAG_GRIDRULERSETTINGS:
            case XARTag::TAG_GRIDRULERORIGIN:
            case XARTag::TAG_SPREADSCALING_ACTIVE:
            case XARTag::TAG_SPREADSCALING_INACTIVE:
            case XARTag::TAG_LAYER_FRAMEPROPS:
            case XARTag::TAG_SPREAD_ANIMPROPS:
            case XARTag::TAG_SPREAD_FLASHPROPS:
            case XARTag::TAG_SPREAD_FLASHPROPS2:
                // metadata not affecting render
                break;

            // View
            case XARTag::TAG_VIEWPORT: ParseViewportRecord(record); break;
            case XARTag::TAG_VIEWQUALITY:
            case XARTag::TAG_DOCUMENTVIEW:
            case XARTag::TAG_QUALITY:
                break;

            // Document units / metadata
            case XARTag::TAG_DEFINE_PREFIXUSERUNIT:
            case XARTag::TAG_DEFINE_SUFFIXUSERUNIT:
            case XARTag::TAG_DEFINE_DEFAULTUNITS:
            case XARTag::TAG_DOCUMENTCOMMENT:
            case XARTag::TAG_DOCUMENTDATES:
            case XARTag::TAG_DOCUMENTUNDOSIZE:
            case XARTag::TAG_DOCUMENTFLAGS:
            case XARTag::TAG_DOCUMENTNUDGE:
            case XARTag::TAG_BITMAP_PROPERTIES:
            case XARTag::TAG_DOCUMENTBITMAPSMOOTHING:
            case XARTag::TAG_XPE_BITMAP_PROPERTIES:
                break;
            case XARTag::TAG_DOCUMENTINFORMATION: ParseDocumentInfoRecord(record); break;

            // Bitmap definitions
            case XARTag::TAG_DEFINEBITMAP_JPEG:
                ParseBitmapDefRecord(record, XARBitmapDefinition::Format::JPEG); break;
            case XARTag::TAG_DEFINEBITMAP_PNG:
                ParseBitmapDefRecord(record, XARBitmapDefinition::Format::PNG); break;
            case XARTag::TAG_DEFINEBITMAP_JPEG8BPP:
                ParseBitmapDefRecord(record, XARBitmapDefinition::Format::JPEG8BPP); break;
            case XARTag::TAG_DEFINEBITMAP_PNG_REAL:
                ParseBitmapDefRecord(record, XARBitmapDefinition::Format::PNG_REAL); break;
            case XARTag::TAG_DEFINEBITMAP_XPE:
                ParseBitmapDefRecord(record, XARBitmapDefinition::Format::XPE); break;
            case XARTag::TAG_PREVIEWBITMAP_GIF:
            case XARTag::TAG_PREVIEWBITMAP_JPEG:
            case XARTag::TAG_PREVIEWBITMAP_PNG:
                // Document preview thumbnail — not needed for rendering
                break;

            // Bitmap object nodes
            case XARTag::TAG_NODE_BITMAP: ParseNodeBitmapRecord(record, false); break;
            case XARTag::TAG_NODE_CONTONEDBITMAP: ParseNodeBitmapRecord(record, true); break;

            // Colours
            case XARTag::TAG_DEFINERGBCOLOUR: ParseColorRecord(record); break;
            case XARTag::TAG_DEFINECOMPLEXCOLOUR: ParseComplexColorRecord(record); break;

            // Path objects
            case XARTag::TAG_PATH: ParsePathRecord(record, false, false, false); break;
            case XARTag::TAG_PATH_FILLED: ParsePathRecord(record, true, false, false); break;
            case XARTag::TAG_PATH_STROKED: ParsePathRecord(record, false, true, false); break;
            case XARTag::TAG_PATH_FILLED_STROKED: ParsePathRecord(record, true, true, false); break;
            case XARTag::TAG_PATH_RELATIVE: ParsePathRecord(record, false, false, true); break;
            case XARTag::TAG_PATH_RELATIVE_FILLED: ParsePathRecord(record, true, false, true); break;
            case XARTag::TAG_PATH_RELATIVE_STROKED: ParsePathRecord(record, false, true, true); break;
            case XARTag::TAG_PATH_RELATIVE_FILLED_STROKED: ParsePathRecord(record, true, true, true); break;
            case XARTag::TAG_PATH_FLAGS: ParsePathFlagsRecord(record); break;
            case XARTag::TAG_PATHREF_TRANSFORM: ParsePathRefTransformRecord(record); break;

            // QuickShapes
            case XARTag::TAG_RECTANGLE_SIMPLE:
            case XARTag::TAG_RECTANGLE_SIMPLE_REFORMED:
            case XARTag::TAG_RECTANGLE_SIMPLE_STELLATED:
            case XARTag::TAG_RECTANGLE_SIMPLE_STELLATED_REFORMED:
            case XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED:
            case XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED:
            case XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED:
            case XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED:
            case XARTag::TAG_RECTANGLE_COMPLEX:
            case XARTag::TAG_RECTANGLE_COMPLEX_REFORMED:
            case XARTag::TAG_RECTANGLE_COMPLEX_STELLATED:
            case XARTag::TAG_RECTANGLE_COMPLEX_STELLATED_REFORMED:
            case XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED:
            case XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_REFORMED:
            case XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED:
            case XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED:
                ParseRectangleRecord(record); break;
            case XARTag::TAG_ELLIPSE_SIMPLE:
            case XARTag::TAG_ELLIPSE_COMPLEX:
                ParseEllipseRecord(record); break;
            case XARTag::TAG_POLYGON_COMPLEX:
            case XARTag::TAG_POLYGON_COMPLEX_REFORMED:
            case XARTag::TAG_POLYGON_COMPLEX_STELLATED:
            case XARTag::TAG_POLYGON_COMPLEX_STELLATED_REFORMED:
            case XARTag::TAG_POLYGON_COMPLEX_ROUNDED:
            case XARTag::TAG_POLYGON_COMPLEX_ROUNDED_REFORMED:
            case XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED:
            case XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED:
                ParsePolygonRecord(record); break;
            case XARTag::TAG_REGULAR_SHAPE_PHASE_1:
                ParseRegularShapeRecord(record, true); break;
            case XARTag::TAG_REGULAR_SHAPE_PHASE_2:
                ParseRegularShapeRecord(record, false); break;

            // Group
            case XARTag::TAG_GROUP: ParseGroupRecord(record); break;

            // Fills
            case XARTag::TAG_FLATFILL: ParseFlatFillRecord(record); break;
            case XARTag::TAG_FLATFILL_NONE:
                currentContext.fill.type = XARFillType::NoneFill;
                currentContext.hasFill = true;
                break;
            case XARTag::TAG_FLATFILL_BLACK:
                currentContext.fill.type = XARFillType::Flat;
                currentContext.fill.startColor = Color(0, 0, 0, 255);
                currentContext.hasFill = true;
                break;
            case XARTag::TAG_FLATFILL_WHITE:
                currentContext.fill.type = XARFillType::Flat;
                currentContext.fill.startColor = Color(255, 255, 255, 255);
                currentContext.hasFill = true;
                break;
            case XARTag::TAG_LINEARFILL: ParseLinearFillRecord(record, false, false); break;
            case XARTag::TAG_LINEARFILL3POINT: ParseLinearFillRecord(record, true, false); break;
            case XARTag::TAG_LINEARFILLMULTISTAGE: ParseLinearFillRecord(record, false, true); break;
            case XARTag::TAG_LINEARFILLMULTISTAGE3POINT: ParseLinearFillRecord(record, true, true); break;
            case XARTag::TAG_CIRCULARFILL:
                ParseRadialFillRecord(record, XARFillType::CircularGradient, false, false); break;
            case XARTag::TAG_CIRCULARFILLMULTISTAGE:
                ParseRadialFillRecord(record, XARFillType::CircularGradient, false, true); break;
            case XARTag::TAG_ELLIPTICALFILL:
                ParseRadialFillRecord(record, XARFillType::EllipticalGradient, true, false); break;
            case XARTag::TAG_ELLIPTICALFILLMULTISTAGE:
                ParseRadialFillRecord(record, XARFillType::EllipticalGradient, true, true); break;
            case XARTag::TAG_CONICALFILL: ParseConicalFillRecord(record, false); break;
            case XARTag::TAG_CONICALFILLMULTISTAGE: ParseConicalFillRecord(record, true); break;
            case XARTag::TAG_BITMAPFILL: ParseBitmapFillRecord(record, false); break;
            case XARTag::TAG_CONTONEBITMAPFILL: ParseBitmapFillRecord(record, true); break;
            case XARTag::TAG_FRACTALFILL: ParseFractalFillRecord(record, false); break;
            case XARTag::TAG_NOISEFILL: ParseFractalFillRecord(record, true); break;
            case XARTag::TAG_DIAMONDFILL: ParseDiamondFillRecord(record); break;
            case XARTag::TAG_THREECOLFILL: ParseThreeFourColFillRecord(record, false); break;
            case XARTag::TAG_FOURCOLFILL: ParseThreeFourColFillRecord(record, true); break;
            case XARTag::TAG_SQUAREFILLMULTISTAGE:
                ParseRadialFillRecord(record, XARFillType::EllipticalGradient, true, true); break;

            // Fill repeat
            case XARTag::TAG_FILL_REPEATING:
                currentContext.fill.repeat = XARFillRepeat::Repeating; break;
            case XARTag::TAG_FILL_NONREPEATING:
                currentContext.fill.repeat = XARFillRepeat::NonRepeating; break;
            case XARTag::TAG_FILL_REPEATINGINVERTED:
                currentContext.fill.repeat = XARFillRepeat::RepeatingInverted; break;
            case XARTag::TAG_FILL_REPEATING_EXTRA:
                currentContext.fill.repeat = XARFillRepeat::RepeatingExtra; break;

            // Fill effect
            case XARTag::TAG_FILLEFFECT_FADE:
                currentContext.fill.effect = XARFillEffect::Fade; break;
            case XARTag::TAG_FILLEFFECT_RAINBOW:
                currentContext.fill.effect = XARFillEffect::Rainbow; break;
            case XARTag::TAG_FILLEFFECT_ALTRAINBOW:
                currentContext.fill.effect = XARFillEffect::AltRainbow; break;

            // Transparency
            case XARTag::TAG_FLATTRANSPARENTFILL: ParseFlatTransparentFillRecord(record); break;
            case XARTag::TAG_LINEARTRANSPARENTFILL: ParseLinearTransparentFillRecord(record, false); break;
            case XARTag::TAG_LINEARTRANSPARENTFILL3POINT: ParseLinearTransparentFillRecord(record, true); break;
            case XARTag::TAG_CIRCULARTRANSPARENTFILL:
                ParseRadialTransparentFillRecord(record, XARTransparencyType::CircularGradient, false); break;
            case XARTag::TAG_ELLIPTICALTRANSPARENTFILL:
                ParseRadialTransparentFillRecord(record, XARTransparencyType::EllipticalGradient, true); break;
            case XARTag::TAG_CONICALTRANSPARENTFILL: ParseConicalTransparentFillRecord(record); break;
            case XARTag::TAG_BITMAPTRANSPARENTFILL: ParseBitmapTransparentFillRecord(record); break;
            case XARTag::TAG_FRACTALTRANSPARENTFILL: ParseFractalTransparentFillRecord(record, false); break;
            case XARTag::TAG_NOISETRANSPARENTFILL: ParseFractalTransparentFillRecord(record, true); break;
            case XARTag::TAG_DIAMONDTRANSPARENTFILL: ParseDiamondTransparentFillRecord(record); break;
            case XARTag::TAG_THREECOLTRANSPARENTFILL: ParseThreeFourColTransparentFillRecord(record, false); break;
            case XARTag::TAG_FOURCOLTRANSPARENTFILL: ParseThreeFourColTransparentFillRecord(record, true); break;
            case XARTag::TAG_TRANSPARENTFILL_REPEATING:
            case XARTag::TAG_TRANSPARENTFILL_NONREPEATING:
            case XARTag::TAG_TRANSPARENTFILL_REPEATINGINVERTED:
            case XARTag::TAG_TRANSPARENTFILL_REPEATING_EXTRA:
                // Affects transparency tiling; treated as flat by current renderer
                break;

            // Line attributes
            case XARTag::TAG_LINECOLOUR: ParseLineColourRecord(record); break;
            case XARTag::TAG_LINECOLOUR_NONE:
                currentContext.line.color.a = 0;
                currentContext.line.hasColor = false;
                currentContext.hasLine = false;
                break;
            case XARTag::TAG_LINECOLOUR_BLACK:
                currentContext.line.color = Color(0, 0, 0, 255);
                currentContext.line.hasColor = true;
                currentContext.hasLine = true;
                break;
            case XARTag::TAG_LINECOLOUR_WHITE:
                currentContext.line.color = Color(255, 255, 255, 255);
                currentContext.line.hasColor = true;
                currentContext.hasLine = true;
                break;
            case XARTag::TAG_LINEWIDTH: ParseLineWidthRecord(record); break;
            case XARTag::TAG_STARTCAP: ParseLineCapRecord(record, true); break;
            case XARTag::TAG_ENDCAP: ParseLineCapRecord(record, false); break;
            case XARTag::TAG_JOINSTYLE: ParseLineJoinRecord(record); break;
            case XARTag::TAG_MITRELIMIT: ParseLineMitreLimitRecord(record); break;
            case XARTag::TAG_LINETRANSPARENCY: ParseLineTransparencyRecord(record); break;
            case XARTag::TAG_DASHSTYLE: ParseDashStyleRecord(record); break;
            case XARTag::TAG_DEFINEDASH: ParseDefineDashRecord(record, false); break;
            case XARTag::TAG_DEFINEDASH_SCALED: ParseDefineDashRecord(record, true); break;
            case XARTag::TAG_ARROWHEAD: ParseArrowRecord(record, false); break;
            case XARTag::TAG_ARROWTAIL: ParseArrowRecord(record, true); break;
            case XARTag::TAG_DEFINEARROW: ParseDefineArrowRecord(record); break;
            case XARTag::TAG_WINDINGRULE: ParseWindingRuleRecord(record); break;

            // Text
            case XARTag::TAG_TEXT_STORY_SIMPLE:
            case XARTag::TAG_TEXT_STORY_COMPLEX:
            case XARTag::TAG_TEXT_STORY_SIMPLE_START_LEFT:
            case XARTag::TAG_TEXT_STORY_SIMPLE_START_RIGHT:
            case XARTag::TAG_TEXT_STORY_SIMPLE_END_LEFT:
            case XARTag::TAG_TEXT_STORY_SIMPLE_END_RIGHT:
            case XARTag::TAG_TEXT_STORY_COMPLEX_START_LEFT:
            case XARTag::TAG_TEXT_STORY_COMPLEX_START_RIGHT:
            case XARTag::TAG_TEXT_STORY_COMPLEX_END_LEFT:
            case XARTag::TAG_TEXT_STORY_COMPLEX_END_RIGHT:
                ParseTextStoryRecord(record); break;
            case XARTag::TAG_TEXT_STORY_WORD_WRAP_INFO:
            case XARTag::TAG_TEXT_STORY_INDENT_INFO:
            case XARTag::TAG_TEXT_STORY_HEIGHT_INFO:
            case XARTag::TAG_TEXT_STORY_LINK_INFO:
            case XARTag::TAG_TEXT_STORY_TRANSLATION_INFO:
                break;
            case XARTag::TAG_TEXT_LINE: ParseTextLineRecord(record); break;
            case XARTag::TAG_TEXT_STRING: ParseTextStringRecord(record); break;
            case XARTag::TAG_TEXT_CHAR: ParseTextCharRecord(record); break;
            case XARTag::TAG_TEXT_KERN: ParseTextKernRecord(record); break;
            case XARTag::TAG_TEXT_LINE_INFO: ParseTextLineInfoRecord(record); break;
            case XARTag::TAG_TEXT_EOL:
                textListActive = false;
                if (auto line = std::dynamic_pointer_cast<XARTextLineNode>(CurrentNode())) {
                    line->endsParagraph = true;
                }
                break;
            case XARTag::TAG_TEXT_LIST_BULLET:
            case XARTag::TAG_TEXT_LIST_NUMBERED:
                if (auto line = std::dynamic_pointer_cast<XARTextLineNode>(CurrentNode())) {
                    line->isListItem = true;
                }
                textListActive = true;
                break;
            case XARTag::TAG_TEXT_LIST_SEPARATOR:
                // Field boundary inside a list marker; the layout keys off
                // the marker-to-text transition instead.
                break;
            case XARTag::TAG_TEXT_CARET:
            case XARTag::TAG_TEXT_STRING_POS:
            case XARTag::TAG_TEXT_TAB:
            case XARTag::TAG_TEXT_LEFT_INDENT:
            case XARTag::TAG_TEXT_FIRST_INDENT:
            case XARTag::TAG_TEXT_RIGHT_INDENT:
            case XARTag::TAG_TEXT_RULER:
            case XARTag::TAG_TEXT_SPACE_BEFORE:
            case XARTag::TAG_TEXT_SPACE_AFTER:
            case XARTag::TAG_TEXT_SPECIAL_HYPHEN:
            case XARTag::TAG_TEXT_SOFT_RETURN:
            case XARTag::TAG_TEXT_EXTRA_FONT_INFO:
            case XARTag::TAG_TEXT_LINESPACE_LEADING:
                // Layout attributes — not rendered by current minimal text path
                break;
            case XARTag::TAG_TEXT_BOLD_ON: currentContext.text.bold = true; break;
            case XARTag::TAG_TEXT_BOLD_OFF: currentContext.text.bold = false; break;
            case XARTag::TAG_TEXT_ITALIC_ON: currentContext.text.italic = true; break;
            case XARTag::TAG_TEXT_ITALIC_OFF: currentContext.text.italic = false; break;
            case XARTag::TAG_TEXT_UNDERLINE_ON: currentContext.text.underline = true; break;
            case XARTag::TAG_TEXT_UNDERLINE_OFF: currentContext.text.underline = false; break;
            case XARTag::TAG_TEXT_SCRIPT_ON:
            case XARTag::TAG_TEXT_SCRIPT_OFF:
            case XARTag::TAG_TEXT_SUPERSCRIPT_ON:
            case XARTag::TAG_TEXT_SUBSCRIPT_ON:
                break;
            case XARTag::TAG_TEXT_FONT_SIZE: ParseTextAttrRecord(record); break;
            case XARTag::TAG_TEXT_FONT_TYPEFACE: ParseTextAttrRecord(record); break;
            case XARTag::TAG_TEXT_TRACKING:
            case XARTag::TAG_TEXT_ASPECT_RATIO:
            case XARTag::TAG_TEXT_BASELINE:
            case XARTag::TAG_TEXT_LINESPACE_RATIO:
            case XARTag::TAG_TEXT_LINESPACE_ABSOLUTE:
                ParseTextAttrRecord(record); break;
            case XARTag::TAG_TEXT_JUSTIFICATION_LEFT:
                currentContext.text.justification = XARTextAttribute::Justification::Left; break;
            case XARTag::TAG_TEXT_JUSTIFICATION_CENTRE:
                currentContext.text.justification = XARTextAttribute::Justification::Centre; break;
            case XARTag::TAG_TEXT_JUSTIFICATION_RIGHT:
                currentContext.text.justification = XARTextAttribute::Justification::Right; break;
            case XARTag::TAG_TEXT_JUSTIFICATION_FULL:
                currentContext.text.justification = XARTextAttribute::Justification::Full; break;
            case XARTag::TAG_FONT_DEF_TRUETYPE:
            case XARTag::TAG_TEXT_EXTRA_TT_FONT_DEF:
                ParseFontDefRecord(record, true); break;
            case XARTag::TAG_FONT_DEF_ATM:
            case XARTag::TAG_TEXT_EXTRA_ATM_FONT_DEF:
                ParseFontDefRecord(record, false); break;

            // Effects
            case XARTag::TAG_SHADOWCONTROLLER: ParseShadowRecord(record); break;
            case XARTag::TAG_SHADOW:
                // The shadow atom inside the controller; the silhouette is
                // derived from the shadowed siblings, so nothing to keep.
                break;
            case XARTag::TAG_BEVEL: ParseBevelRecord(record); break;
            case XARTag::TAG_BEVATTR_INDENT:
            case XARTag::TAG_BEVATTR_LIGHTANGLE:
            case XARTag::TAG_BEVATTR_CONTRAST:
            case XARTag::TAG_BEVATTR_TYPE:
            case XARTag::TAG_BEVELINK:
                // Bevel sub-attributes; consumed silently
                break;
            case XARTag::TAG_CONTOUR: ParseContourRecord(record); break;
            case XARTag::TAG_CONTOURCONTROLLER:
                break;
            case XARTag::TAG_BLEND: ParseBlendRecord(record); break;
            case XARTag::TAG_BLENDER:
            case XARTag::TAG_BLENDER_CURVEPROP:
            case XARTag::TAG_BLENDER_CURVEANGLES:
            case XARTag::TAG_BLENDPROFILES:
            case XARTag::TAG_BLENDERADDITIONAL:
            case XARTag::TAG_NODEBLENDPATH_FILLED:
            case XARTag::TAG_BLEND_PATH:
                break;
            case XARTag::TAG_MOULD_ENVELOPE: ParseMouldRecord(record, false); break;
            case XARTag::TAG_MOULD_PERSPECTIVE: ParseMouldRecord(record, true); break;
            case XARTag::TAG_MOULD_GROUP:
            case XARTag::TAG_MOULD_PATH:
            case XARTag::TAG_MOULD_BOUNDS:
                break;
            case XARTag::TAG_CLIPVIEW: ParseClipViewRecord(record); break;
            case XARTag::TAG_CLIPVIEWCONTROLLER:
            case XARTag::TAG_CLIPVIEW_PATH:
                break;
            case XARTag::TAG_FEATHER:
            case XARTag::TAG_FEATHER_EFFECT:
                ParseFeatherRecord(record); break;
            case XARTag::TAG_LIVE_EFFECT:
            case XARTag::TAG_LOCKED_EFFECT:
                ParseLiveEffectRecord(record); break;

            // Brushes (parse but render falls back to plain stroke via line attrs)
            case XARTag::TAG_BRUSHATTR:
            case XARTag::TAG_BRUSHDEFINITION:
            case XARTag::TAG_BRUSHDATA:
            case XARTag::TAG_MOREBRUSHDATA:
            case XARTag::TAG_MOREBRUSHATTR:
            case XARTag::TAG_EVENMOREBRUSHDATA:
            case XARTag::TAG_EVENMOREBRUSHATTR:
            case XARTag::TAG_TIMESTAMPBRUSHDATA:
            case XARTag::TAG_BRUSHPRESSUREINFO:
            case XARTag::TAG_BRUSHPRESSUREDATA:
            case XARTag::TAG_BRUSHATTRPRESSUREINFO:
            case XARTag::TAG_BRUSHCOLOURDATA:
            case XARTag::TAG_BRUSHPRESSURESAMPLEDATA:
            case XARTag::TAG_BRUSHTIMESAMPLEDATA:
            case XARTag::TAG_BRUSHATTRFILLFLAGS:
            case XARTag::TAG_BRUSHTRANSPINFO:
            case XARTag::TAG_BRUSHATTRTRANSPINFO:
                break;

            // Misc / skip
            case XARTag::TAG_OBJECTBOUNDS: ParseObjectBoundsRecord(record); break;
            case XARTag::TAG_COMPOUNDRENDER:
            case XARTag::TAG_CURRENTATTRIBUTES:
            case XARTag::TAG_CURRENTATTRIBUTEBOUNDS:
            case XARTag::TAG_CURRENTATTRIBUTES_PHASE2:
            case XARTag::TAG_PRINTERSETTINGS:
            case XARTag::TAG_PRINTERSETTINGS_PHASE2:
            case XARTag::TAG_IMAGESETTING:
            case XARTag::TAG_COLOURPLATE:
            case XARTag::TAG_PRINTMARKDEFAULT:
            case XARTag::TAG_OVERPRINTLINEON:
            case XARTag::TAG_OVERPRINTLINEOFF:
            case XARTag::TAG_OVERPRINTFILLON:
            case XARTag::TAG_OVERPRINTFILLOFF:
            case XARTag::TAG_PRINTONALLPLATESON:
            case XARTag::TAG_PRINTONALLPLATESOFF:
            case XARTag::TAG_USERVALUE:
            case XARTag::TAG_EXPORT_HINT:
            case XARTag::TAG_WEBADDRESS:
            case XARTag::TAG_WEBADDRESS_BOUNDINGBOX:
            case XARTag::TAG_WIZOP:
            case XARTag::TAG_WIZOP_STYLE:
            case XARTag::TAG_WIZOP_STYLEREF:
            case XARTag::TAG_SETSENTINEL:
            case XARTag::TAG_SETPROPERTY:
            case XARTag::TAG_BARPROPERTY:
            case XARTag::TAG_DUPLICATIONOFFSET:
            case XARTag::TAG_VARIABLEWIDTHFUNC:
            case XARTag::TAG_VARIABLEWIDTHTABLE:
            case XARTag::TAG_STROKETYPE:
            case XARTag::TAG_STROKEDEFINITION:
            case XARTag::TAG_STROKEAIRBRUSH:
                break;

            default:
                // Unknown / not yet implemented; consumed safely but counted,
                // so a file that leans on a record type this parser skips can
                // be triaged (see GetDiagnostics / Tests/XARProbeTest.cpp).
                diagnostics.unhandledTags[static_cast<uint32_t>(record.tag)]++;
                break;
        }

        // An object's attribute records arrive as its children (object,
        // TAG_DOWN, attributes, TAG_UP) — after they run, the object owning
        // the current scope re-snapshots the running context so those
        // attributes actually land on it. Re-snapshotting a container or
        // after a no-op record is harmless: the snapshot equals the scope's
        // context either way.
        if (!nodeStack.empty()) {
            ApplyCurrentAttributesTo(nodeStack.top());
        }
    }

// ===== HEADER PARSING =====

    void XARDocument::ParseFileHeaderRecord(const XARRecord& record) {
        if (record.data.size() < 15) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        char ft[4] = {0};
        ft[0] = static_cast<char>(d[off++]);
        ft[1] = static_cast<char>(d[off++]);
        ft[2] = static_cast<char>(d[off++]);
        fileType = ft;
        fileSizeHint = ReadUInt32(d, off);
        ReadUInt32(d, off);                 // WebLink (always 0)
        refinementFlags = ReadUInt32(d, off);
        producer = ReadASCIIString(d, off, record.data.size() - off);
        producerVersion = ReadASCIIString(d, off, record.data.size() - off);
        producerBuild = ReadASCIIString(d, off, record.data.size() - off);
    }

// ===== DOCUMENT STRUCTURE =====

    void XARDocument::ParseSpreadRecord(const XARRecord&) {
        auto spread = std::make_shared<XARSpreadNode>();
        AttachNode(spread);
        spreadNodes.push_back(spread);
    }

    void XARDocument::ParseSpreadInfoRecord(const XARRecord& record) {
        if (record.data.size() < 17) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        spreadWidthMP = ReadInt32(d, off);
        spreadHeightMP = ReadInt32(d, off);
        int32_t margin = ReadInt32(d, off);
        int32_t bleed = ReadInt32(d, off);
        uint8_t flags = ReadByte(d, off);
        haveSpreadInfo = true;
        if (auto sp = std::dynamic_pointer_cast<XARSpreadNode>(CurrentNode())) {
            sp->width = spreadWidthMP;
            sp->height = spreadHeightMP;
            sp->margin = margin;
            sp->bleed = bleed;
            sp->flags = flags;
        }
    }

    void XARDocument::ParseLayerRecord(const XARRecord&) {
        AttachNode(std::make_shared<XARLayerNode>());
    }

    void XARDocument::ParseLayerDetailsRecord(const XARRecord& record, bool isGuide) {
        if (record.data.empty()) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        uint8_t flags = ReadByte(d, off);
        std::string name = ReadUTF16String(d, off, record.data.size() - off);
        if (auto layer = std::dynamic_pointer_cast<XARLayerNode>(CurrentNode())) {
            layer->visible = (flags & 0x1) != 0;
            layer->locked = (flags & 0x2) != 0;
            layer->printable = (flags & 0x4) != 0;
            layer->name = name;
            layer->isGuide = isGuide;
        }
    }

    void XARDocument::ParsePageRecord(const XARRecord& record) {
        auto page = std::make_shared<XARPageNode>();
        if (record.data.size() >= 20) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            page->bottomLeft = ReadCoord(d, off);
            page->topRight = ReadCoord(d, off);
            page->colourRef = ReadInt32(d, off);
        }
        // If we don't have spread info yet, derive size from page coords
        if (!haveSpreadInfo) {
            spreadWidthMP = page->topRight.x - page->bottomLeft.x;
            spreadHeightMP = page->topRight.y - page->bottomLeft.y;
            haveSpreadInfo = true;
            if (auto sp = std::dynamic_pointer_cast<XARSpreadNode>(CurrentNode())) {
                sp->width = spreadWidthMP;
                sp->height = spreadHeightMP;
            }
        }
        AttachNode(page);
    }

    void XARDocument::ParseViewportRecord(const XARRecord& record) {
        if (record.data.size() < 16) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        Point2Di bl = ReadCoord(d, off);
        Point2Di tr = ReadCoord(d, off);
        viewport.x = static_cast<float>(bl.x) * XARConstants::MILLIPOINTS_TO_PIXELS;
        viewport.y = static_cast<float>(bl.y) * XARConstants::MILLIPOINTS_TO_PIXELS;
        viewport.width = static_cast<float>(tr.x - bl.x) * XARConstants::MILLIPOINTS_TO_PIXELS;
        viewport.height = static_cast<float>(tr.y - bl.y) * XARConstants::MILLIPOINTS_TO_PIXELS;
        haveViewport = true;
    }

// ===== PATHS =====

    void XARDocument::ParsePathRecord(const XARRecord& record, bool filled, bool stroked, bool relative) {
        if (record.data.empty()) return;
        auto path = std::make_shared<XARPathNode>();
        ApplyCurrentAttributesTo(path);
        path->isFilled = filled;
        path->isStroked = stroked;

        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();

        if (relative) {
            // Relative format: <verb><coord>+ where each entry is 9 bytes.
            // Coordinates are byte-interleaved MSB-first (X = b0,b2,b4,b6;
            // Y = b1,b3,b5,b7). The first coordinate is absolute; every
            // following one stores the REVERSE delta (previous minus current),
            // so decoding subtracts. Verified against the renderings embedded
            // in Xara Designer Pro X19 files (see PR #317): adding instead of
            // subtracting keeps each subpath's shape but scatters the pieces.
            Point2Di last(0, 0);
            bool firstCoord = true;
            while (off + 9 <= total) {
                uint8_t verb = ReadByte(d, off);
                Point2Di p = ReadRelativeCoord(d, off, last, firstCoord);
                firstCoord = false;
                uint8_t base = verb & 0x06;
                bool close = (verb & 0x01) != 0;
                if (base == 0x06) {
                    XARPathCommand mv(XARPathVerb::MoveTo);
                    mv.points.push_back(p);
                    path->commands.push_back(std::move(mv));
                } else if (base == 0x02) {
                    XARPathCommand ln(XARPathVerb::LineTo);
                    ln.points.push_back(p);
                    path->commands.push_back(std::move(ln));
                    if (close) path->commands.push_back(XARPathCommand(XARPathVerb::ClosePath));
                } else if (base == 0x04) {
                    // Read 2 more coords for the bezier
                    if (off + 9 + 9 > total) break;
                    uint8_t v2 = ReadByte(d, off);
                    Point2Di c2 = ReadRelativeCoord(d, off, last, false);
                    uint8_t v3 = ReadByte(d, off);
                    Point2Di e = ReadRelativeCoord(d, off, last, false);
                    XARPathCommand bz(XARPathVerb::BezierTo);
                    bz.points.push_back(p);
                    bz.points.push_back(c2);
                    bz.points.push_back(e);
                    path->commands.push_back(std::move(bz));
                    if ((v3 & 0x01) || close) path->commands.push_back(XARPathCommand(XARPathVerb::ClosePath));
                    (void)v2;
                }
            }
        } else {
            // Absolute format: numCoords:UINT32, verbs[numCoords] (4-byte aligned), coords[numCoords]
            if (off + 4 > total) { RegisterRenderableNode(path); return; }
            int32_t numCoords = ReadInt32(d, off);
            if (numCoords <= 0 || off + static_cast<size_t>(numCoords) > total) {
                RegisterRenderableNode(path); return;
            }
            std::vector<uint8_t> verbs(numCoords);
            for (int32_t i = 0; i < numCoords; ++i) verbs[i] = ReadByte(d, off);
            // Align to 4-byte boundary
            while ((off % 4) != 0 && off < total) ++off;
            // Read coords
            std::vector<Point2Di> coords(numCoords);
            for (int32_t i = 0; i < numCoords; ++i) {
                if (off + 8 > total) { coords.resize(i); break; }
                coords[i] = ReadCoord(d, off);
            }

            int i = 0;
            int n = static_cast<int>(coords.size());
            while (i < n) {
                uint8_t verb = verbs[i];
                uint8_t base = verb & 0x06;
                bool close = (verb & 0x01) != 0;
                if (base == 0x06) {
                    XARPathCommand mv(XARPathVerb::MoveTo);
                    mv.points.push_back(coords[i]);
                    path->commands.push_back(std::move(mv));
                    ++i;
                } else if (base == 0x02) {
                    XARPathCommand ln(XARPathVerb::LineTo);
                    ln.points.push_back(coords[i]);
                    path->commands.push_back(std::move(ln));
                    if (close) path->commands.push_back(XARPathCommand(XARPathVerb::ClosePath));
                    ++i;
                } else if (base == 0x04) {
                    if (i + 2 >= n) break;
                    XARPathCommand bz(XARPathVerb::BezierTo);
                    bz.points.push_back(coords[i]);
                    bz.points.push_back(coords[i + 1]);
                    bz.points.push_back(coords[i + 2]);
                    path->commands.push_back(std::move(bz));
                    bool closeOn = ((verbs[i + 2] & 0x01) != 0) || close;
                    if (closeOn) path->commands.push_back(XARPathCommand(XARPathVerb::ClosePath));
                    i += 3;
                } else {
                    ++i; // unknown verb; skip
                }
            }
        }

        pathsBySequence[currentSequenceNumber] = path;
        RegisterRenderableNode(path);
    }

    void XARDocument::ParsePathFlagsRecord(const XARRecord&) {
        // Per-point edit flags; not needed for rendering.
    }

    void XARDocument::ParsePathRefTransformRecord(const XARRecord& record) {
        if (record.data.size() < 28) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        int32_t pathRef = ReadInt32(d, off);
        // FIXED16: 32-bit fixed-point with 16 fractional bits
        auto readFixed16 = [&](size_t& o) -> double {
            int32_t v = ReadInt32(d, o);
            return static_cast<double>(v) / 65536.0;
        };
        XARMatrix m;
        m.a = readFixed16(off);
        m.b = readFixed16(off);
        m.c = readFixed16(off);
        m.d = readFixed16(off);
        m.e = static_cast<double>(ReadInt32(d, off));
        m.f = static_cast<double>(ReadInt32(d, off));

        auto it = pathsBySequence.find(pathRef);
        if (it == pathsBySequence.end()) return;
        auto src = it->second;

        // Clone path with transformed coords
        auto clone = std::make_shared<XARPathNode>();
        ApplyCurrentAttributesTo(clone);
        clone->isFilled = src->isFilled;
        clone->isStroked = src->isStroked;
        for (const auto& cmd : src->commands) {
            XARPathCommand nc(cmd.verb);
            for (const auto& p : cmd.points) {
                nc.points.push_back(m.Transform(p));
            }
            clone->commands.push_back(std::move(nc));
        }
        RegisterRenderableNode(clone);
    }

// ===== QUICKSHAPES =====

    void XARDocument::ParseRectangleRecord(const XARRecord& record) {
        auto rect = std::make_shared<XARRectangleNode>();
        ApplyCurrentAttributesTo(rect);
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();

        bool isSimple =
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_STELLATED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_STELLATED_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED;

        bool isRounded =
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED ||
            record.tag == XARTag::TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED ||
            record.tag == XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_REFORMED ||
            record.tag == XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED ||
            record.tag == XARTag::TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED;

        rect->isSimple = isSimple;
        rect->isRounded = isRounded;

        if (isSimple) {
            // Simple: Centre + MajorAxis + MinorAxis (24 bytes), opt cornerRadius (4 bytes)
            if (off + 24 <= total) {
                rect->centre = ReadCoord(d, off);
                rect->majorAxis = ReadCoord(d, off);
                rect->minorAxis = ReadCoord(d, off);
                if (isRounded && off + 4 <= total) {
                    rect->cornerRadius = ReadInt32(d, off);
                }
            }
        } else {
            // Complex: Centre + Matrix(48) + halfWidth + halfHeight + opt cornerRadius
            if (off + 8 <= total) rect->centre = ReadCoord(d, off);
            if (off + 24 <= total) rect->transform = ReadMatrix(d, off);
            if (off + 8 <= total) {
                rect->halfWidth = ReadInt32(d, off);
                rect->halfHeight = ReadInt32(d, off);
            }
            if (isRounded && off + 4 <= total) {
                rect->cornerRadius = ReadInt32(d, off);
            }
            // Use halfWidth/halfHeight for axis vectors
            rect->majorAxis = Point2Di(rect->halfWidth, 0);
            rect->minorAxis = Point2Di(0, rect->halfHeight);
        }
        RegisterRenderableNode(rect);
    }

    void XARDocument::ParseEllipseRecord(const XARRecord& record) {
        auto ell = std::make_shared<XAREllipseNode>();
        ApplyCurrentAttributesTo(ell);
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();

        if (record.tag == XARTag::TAG_ELLIPSE_SIMPLE) {
            ell->isSimple = true;
            if (off + 24 <= total) {
                ell->centre = ReadCoord(d, off);
                ell->majorAxis = ReadCoord(d, off);
                ell->minorAxis = ReadCoord(d, off);
            }
        } else {
            ell->isSimple = false;
            if (off + 8 <= total) ell->centre = ReadCoord(d, off);
            if (off + 24 <= total) ell->transform = ReadMatrix(d, off);
            int32_t hMajor = 0, hMinor = 0;
            if (off + 8 <= total) {
                hMajor = ReadInt32(d, off);
                hMinor = ReadInt32(d, off);
            }
            ell->majorAxis = Point2Di(hMajor, 0);
            ell->minorAxis = Point2Di(0, hMinor);
        }
        RegisterRenderableNode(ell);
    }

    void XARDocument::ParsePolygonRecord(const XARRecord& record) {
        auto poly = std::make_shared<XARPolygonNode>();
        ApplyCurrentAttributesTo(poly);
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();

        bool stellated =
            record.tag == XARTag::TAG_POLYGON_COMPLEX_STELLATED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_STELLATED_REFORMED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED;
        bool rounded =
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED_REFORMED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED ||
            record.tag == XARTag::TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED;
        poly->isStellated = stellated;
        poly->isRounded = rounded;

        if (off + 4 <= total) poly->numSides = ReadInt32(d, off);
        if (off + 8 <= total) poly->centre = ReadCoord(d, off);
        if (off + 24 <= total) poly->transform = ReadMatrix(d, off);
        int32_t hMajor = 0, hMinor = 0;
        if (off + 8 <= total) {
            hMajor = ReadInt32(d, off);
            hMinor = ReadInt32(d, off);
        }
        poly->majorAxis = Point2Di(hMajor, 0);
        poly->minorAxis = Point2Di(0, hMinor);
        if (rounded && off + 4 <= total) {
            int32_t cv = ReadInt32(d, off);
            poly->curvature = static_cast<float>(cv) / 65536.0f;
        }
        if (stellated && off + 8 <= total) {
            int32_t sr = ReadInt32(d, off);
            int32_t so = ReadInt32(d, off);
            poly->stellationRadius = static_cast<float>(sr) / 65536.0f;
            poly->stellationOffset = static_cast<float>(so) / 65536.0f;
        }
        RegisterRenderableNode(poly);
    }

    void XARDocument::ParseRegularShapeRecord(const XARRecord& record, bool phase1) {
        // Layout per Xara LX's RegularShapeRecordHandler (rechrshp.cpp):
        //   BYTE flags; UINT16 numSides; [COORD centre — phase 1 only];
        //   COORD majorAxis; COORD minorAxis; MATRIX transform;
        //   DOUBLE stellRadius, stellOffset, curvature1, curvature2;
        //   PATH edge1, edge2 (reformed edges — not rendered yet).
        // The axes point from the centre to the middles of two adjacent
        // edges, so a 4-sided shape with axis-aligned axes is an
        // axis-aligned rectangle.
        auto poly = std::make_shared<XARPolygonNode>();
        ApplyCurrentAttributesTo(poly);
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 3 > total) { RegisterRenderableNode(poly); return; }

        uint8_t flags = ReadByte(d, off);
        poly->isCircular = (flags & 0x01) != 0;
        poly->isStellated = (flags & 0x02) != 0;
        poly->isRounded = (flags & 0x04) != 0;
        poly->numSides = ReadUInt16(d, off);

        if (phase1 && off + 8 <= total) poly->centre = ReadCoord(d, off);
        if (off + 8 <= total) poly->majorAxis = ReadCoord(d, off);
        if (off + 8 <= total) poly->minorAxis = ReadCoord(d, off);
        if (off + 24 <= total) poly->transform = ReadMatrix(d, off);
        if (off + 16 <= total) {
            poly->stellationRadius = static_cast<float>(ReadDouble(d, off));
            poly->stellationOffset = static_cast<float>(ReadDouble(d, off));
        }
        if (off + 16 <= total) {
            poly->curvature = static_cast<float>(ReadDouble(d, off));
            ReadDouble(d, off);   // secondary curvature — unused
        }
        // Edge paths (reformed shapes) follow; not consumed.
        RegisterRenderableNode(poly);
    }

// ===== GROUPS / EFFECTS =====

    void XARDocument::ParseGroupRecord(const XARRecord&) {
        auto g = std::make_shared<XARGroupNode>();
        ApplyCurrentAttributesTo(g);
        AttachNode(g);
    }

    // TAG_SHADOWCONTROLLER: the group holding a shadowed object. Layout
    // (verified against MAGIX Designer 16 output): BYTE type, INT32 penumbra
    // width (mp), INT32 offsetX (mp), INT32 offsetY (mp), INT32 wall angle
    // (microradians), INT32 darkness (percent), then scale/height fields.
    void XARDocument::ParseShadowRecord(const XARRecord& record) {
        auto sh = std::make_shared<XARShadowNode>();
        const uint8_t* d = record.data.data();
        size_t off = 0;
        if (record.data.size() >= 21) {
            sh->shadowType = ReadByte(d, off);
            sh->blurRadius = ReadInt32(d, off);
            sh->offsetX = ReadInt32(d, off);
            sh->offsetY = ReadInt32(d, off);
            ReadInt32(d, off);                                  // wall angle
            int32_t darkness = ReadInt32(d, off);
            if (darkness < 0) darkness = 0;
            if (darkness > 100) darkness = 100;
            sh->shadowColor = Color(0, 0, 0,
                    static_cast<uint8_t>(255 * darkness / 100));
        }
        AttachNode(sh);
    }

    void XARDocument::ParseBevelRecord(const XARRecord& record) {
        (void)record;
        AttachNode(std::make_shared<XARBevelNode>());
    }

    void XARDocument::ParseContourRecord(const XARRecord& record) {
        (void)record;
        AttachNode(std::make_shared<XARContourNode>());
    }

    void XARDocument::ParseBlendRecord(const XARRecord& record) {
        (void)record;
        AttachNode(std::make_shared<XARBlendNode>());
    }

    void XARDocument::ParseMouldRecord(const XARRecord& record, bool perspective) {
        (void)record;
        auto m = std::make_shared<XARMouldNode>();
        m->isPerspective = perspective;
        AttachNode(m);
    }

    void XARDocument::ParseClipViewRecord(const XARRecord&) {
        AttachNode(std::make_shared<XARClipViewNode>());
    }

    void XARDocument::ParseFeatherRecord(const XARRecord& record) {
        auto f = std::make_shared<XARFeatherNode>();
        if (record.data.size() >= 4) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            f->featherRadius = ReadInt32(d, off);
        }
        AttachNode(f);
    }

    void XARDocument::ParseLiveEffectRecord(const XARRecord& record) {
        (void)record;
        AttachNode(std::make_shared<XARLiveEffectNode>());
    }

    void XARDocument::ParseBrushRecord(const XARRecord&) {
        AttachNode(std::make_shared<XARBrushNode>());
    }

// ===== TEXT =====

    void XARDocument::ParseTextStoryRecord(const XARRecord& record) {
        textListActive = false;
        storyLineOffsetMP = 0;
        auto story = std::make_shared<XARTextStoryNode>();
        ApplyCurrentAttributesTo(story);
        // Text stories have variable layouts; try to read a position/matrix prefix
        if (record.data.size() >= 8) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            story->position = ReadCoord(d, off);
            if (record.data.size() >= 8 + 24) {
                story->transform = ReadMatrix(d, off);
                story->hasTransform = true;
            }
        }
        AttachNode(story);
    }

    void XARDocument::ParseTextLineRecord(const XARRecord&) {
        auto line = std::make_shared<XARTextLineNode>();
        ApplyCurrentAttributesTo(line);
        // Step successive lines of a story down one leading each.
        int existing = 0;
        for (const auto& sibling : CurrentNode()->children) {
            if (sibling->type == XARNodeType::TextLine) ++existing;
        }
        line->lineIndex = existing;
        // A line opened while a list item's paragraph is still running (no
        // TAG_TEXT_EOL yet) wraps that item: it starts at the hanging indent.
        line->hangingIndent = textListActive;
        AttachNode(line);
    }

    void XARDocument::ParseTextStringRecord(const XARRecord& record) {
        auto str = std::make_shared<XARTextStringNode>();
        ApplyCurrentAttributesTo(str);
        if (!record.data.empty()) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            str->text = ReadUTF16String(d, off, record.data.size());
        }
        AttachNode(str);
    }

    void XARDocument::ParseTextCharRecord(const XARRecord& record) {
        // A single UTF-16 code unit as its own object — used for characters
        // whose attributes differ from their neighbours (dingbats, the big
        // numerals in the samples). Modelled as a one-character string.
        if (record.data.size() < 2) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        uint16_t code = ReadUInt16(d, off);
        auto str = std::make_shared<XARTextStringNode>();
        str->fromCharRecord = true;
        ApplyCurrentAttributesTo(str);
        // Encode the BMP code point as UTF-8.
        if (code < 0x80) {
            str->text.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            str->text.push_back(static_cast<char>(0xC0 | (code >> 6)));
            str->text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            str->text.push_back(static_cast<char>(0xE0 | (code >> 12)));
            str->text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            str->text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        AttachNode(str);
    }

    void XARDocument::ParseTextKernRecord(const XARRecord& record) {
        if (record.data.size() < 8) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        auto kern = std::make_shared<XARTextKernNode>();
        kern->offset = ReadCoord(d, off);
        AttachNode(kern);
    }

    void XARDocument::ParseTextLineInfoRecord(const XARRecord& record) {
        // Layout: INT32 line width as Xara computed it, INT32 ascent, INT32
        // step from the previous line's baseline (negative = down, 0 on the
        // story's first line). Accumulating the steps gives each line's
        // exact baseline offset, replacing the leading heuristic.
        if (record.data.size() < 4) return;
        if (auto line = std::dynamic_pointer_cast<XARTextLineNode>(CurrentNode())) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            line->lineWidthMP = ReadInt32(d, off);
            if (record.data.size() >= 12) {
                ReadInt32(d, off);                          // ascent
                storyLineOffsetMP += ReadInt32(d, off);
                line->yOffsetMP = static_cast<int32_t>(storyLineOffsetMP);
                line->hasYOffset = true;
            }
        }
    }

    void XARDocument::ParseTextAttrRecord(const XARRecord& record) {
        if (record.data.empty()) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        switch (record.tag) {
            case XARTag::TAG_TEXT_FONT_SIZE:
                if (record.data.size() >= 4) currentContext.text.fontSize = ReadInt32(d, off);
                break;
            case XARTag::TAG_TEXT_FONT_TYPEFACE:
                if (record.data.size() >= 4) {
                    currentContext.text.fontRef = ReadInt32(d, off);
                    if (auto* fd = GetFont(currentContext.text.fontRef)) {
                        currentContext.text.fontName = fd->fontName;
                    }
                }
                break;
            case XARTag::TAG_TEXT_TRACKING:
                if (record.data.size() >= 4) currentContext.text.tracking = ReadInt32(d, off);
                break;
            case XARTag::TAG_TEXT_ASPECT_RATIO:
                if (record.data.size() >= 4) {
                    int32_t v = ReadInt32(d, off);
                    currentContext.text.aspectRatio = static_cast<float>(v) / 65536.0f;
                }
                break;
            case XARTag::TAG_TEXT_BASELINE:
                if (record.data.size() >= 4) currentContext.text.baselineShift = ReadInt32(d, off);
                break;
            case XARTag::TAG_TEXT_LINESPACE_RATIO:
                if (record.data.size() >= 4) {
                    int32_t v = ReadInt32(d, off);
                    currentContext.text.lineSpaceRatio = static_cast<float>(v) / 65536.0f;
                }
                break;
            case XARTag::TAG_TEXT_LINESPACE_ABSOLUTE:
                if (record.data.size() >= 4) currentContext.text.lineSpaceAbsolute = ReadInt32(d, off);
                break;
            default: break;
        }
    }

    void XARDocument::ParseFontDefRecord(const XARRecord& record, bool isTrueType) {
        XARFontDefinition fd;
        fd.sequenceNumber = currentSequenceNumber;
        fd.isTrueType = isTrueType;
        if (record.data.size() >= 2) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            // Layout (verified against Designer Pro X19 output): the typeface
            // name as a null-terminated UTF-16 string, then the full font
            // name, then the panose classification bytes.
            fd.fontName = ReadUTF16String(d, off, record.data.size() - off);
            fd.familyName = ReadUTF16String(d, off, record.data.size() - off);
            if (fd.familyName.empty()) fd.familyName = fd.fontName;
            for (int i = 0; i < 10 && off < record.data.size(); ++i) {
                fd.panose[i] = d[off++];
            }
        }
        fonts[currentSequenceNumber] = std::move(fd);
    }

// ===== BITMAPS =====

    void XARDocument::ParseBitmapDefRecord(const XARRecord& record, XARBitmapDefinition::Format fmt) {
        XARBitmapDefinition bd;
        bd.sequenceNumber = currentSequenceNumber;
        bd.format = fmt;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        // Layout is name (Unicode) + dimensions (later spec) + raw image bytes.
        // For best-effort: try to read a Unicode name terminated by 0x0000, then
        // the rest is raw image data.
        bd.name = ReadUTF16String(d, off, record.data.size());
        if (off < record.data.size()) {
            bd.data.assign(record.data.begin() + off, record.data.end());
        }
        bitmaps[currentSequenceNumber] = std::move(bd);
    }

    void XARDocument::ParseNodeBitmapRecord(const XARRecord& record, bool contoned) {
        auto bm = std::make_shared<XARBitmapNode>();
        ApplyCurrentAttributesTo(bm);
        bm->isContoned = contoned;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        if (record.data.size() >= 24) {
            bm->bottomLeft = ReadCoord(d, off);
            bm->bottomRight = ReadCoord(d, off);
            bm->topLeft = ReadCoord(d, off);
        }
        if (contoned && record.data.size() >= 24 + 8) {
            bm->startColourRef = ReadInt32(d, off);
            bm->endColourRef = ReadInt32(d, off);
        }
        if (off + 4 <= record.data.size()) {
            bm->bitmapRef = ReadInt32(d, off);
        }
        RegisterRenderableNode(bm);
    }

// ===== COLOURS =====

    void XARDocument::ParseColorRecord(const XARRecord& record) {
        XARColorDefinition cd;
        cd.sequenceNumber = currentSequenceNumber;
        if (record.data.size() >= 3) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            cd.color = ReadColor3(d, off);
        }
        colors[currentSequenceNumber] = std::move(cd);
    }

    void XARDocument::ParseComplexColorRecord(const XARRecord& record) {
        // Wire layout per XAR spec (XARFormatDocument.pdf p.123):
        //   <Simple RGBColour>     3 BYTE  (R, G, B) — fallback for screen rendering
        //   <ColourModel>          BYTE    (2=RGB, 3=CMYK, 4=HSV, 5=Greyscale)
        //   <ColourType>           BYTE    (0=Normal, 1=Spot, 2=Tint, 3=Linked, 4=Shade)
        //   <EntryIndex>           UINT32
        //   <ParentColour>         INT32 COLOURREF
        //   <ColourDescription>    4 × UINT32 fixed24 (1.0 == 0x01000000)
        //   <ColourName>           UTF-16LE, terminated by 0x00 0x00

        XARColorDefinition cd;
        cd.sequenceNumber = currentSequenceNumber;

        const uint8_t* d  = record.data.data();
        const size_t total = record.data.size();
        size_t off = 0;

        // 1) Simple RGB fallback. Always populate cd.color from this so screen
        //    rendering is correct even if we can't decode the rest.
        if (off + 3 > total) { colors[currentSequenceNumber] = std::move(cd); return; }
        uint8_t r = ReadByte(d, off);
        uint8_t g = ReadByte(d, off);
        uint8_t b = ReadByte(d, off);
        cd.simpleRGB = Color(r, g, b, 255);
        cd.color     = cd.simpleRGB;

        // 2) ColourModel + ColourType. In-memory enum ordinals don't match the
        //    wire values, so map explicitly.
        if (off + 2 > total) { colors[currentSequenceNumber] = std::move(cd); return; }
        uint8_t modelByte = ReadByte(d, off);
        uint8_t typeByte  = ReadByte(d, off);
        switch (modelByte) {
            case 2:  cd.model = XARColorDefinition::Model::RGB;       break;
            case 3:  cd.model = XARColorDefinition::Model::CMYK;      break;
            case 4:  cd.model = XARColorDefinition::Model::HSV;       break;
            case 5:  cd.model = XARColorDefinition::Model::Greyscale; break;
            default: cd.model = XARColorDefinition::Model::RGB;       break;
        }
        switch (typeByte) {
            case 0: cd.colorType = XARColorDefinition::Type::Normal; break;
            case 1: cd.colorType = XARColorDefinition::Type::Spot;   break;
            case 2: cd.colorType = XARColorDefinition::Type::Tint;   break;
            case 3: cd.colorType = XARColorDefinition::Type::Linked; break;
            case 4: cd.colorType = XARColorDefinition::Type::Shaded; break;
            default: cd.colorType = XARColorDefinition::Type::Normal; break;
        }

        // 3) EntryIndex + ParentColour
        if (off + 8 > total) { colors[currentSequenceNumber] = std::move(cd); return; }
        cd.entryIndex = ReadUInt32(d, off);
        cd.parentRef  = ReadInt32(d, off);

        // 4) Four UINT32 fixed24 components (binary point between bits 23 and 24,
        //    so 1.0 == 0x01000000). 0xF8000000 in a Linked component means
        //    "inherit from parent" — leave as 0 here, current renderer doesn't
        //    consume it.
        bool componentsOk = true;
        for (int i = 0; i < 4; ++i) {
            if (off + 4 > total) { componentsOk = false; break; }
            uint32_t v = ReadUInt32(d, off);
            if (v == 0xF8000000u) { cd.components[i] = 0.0f; continue; }
            cd.components[i] = std::min(1.0f,
                static_cast<float>(v) / static_cast<float>(0x01000000));
        }

        // 5) ColourName — last field.
        if (off + 2 <= total) {
            cd.name = ReadUTF16String(d, off, total - off);
        }

        // 6) Resolve cd.color from the full definition where possible. If the
        //    components weren't fully present, keep cd.color = simpleRGB fallback.
        if (componentsOk) {
            switch (cd.model) {
                case XARColorDefinition::Model::RGB:
                    cd.color = Color(
                        static_cast<uint8_t>(cd.components[0] * 255),
                        static_cast<uint8_t>(cd.components[1] * 255),
                        static_cast<uint8_t>(cd.components[2] * 255), 255);
                    break;
                case XARColorDefinition::Model::Greyscale: {
                    uint8_t gv = static_cast<uint8_t>(cd.components[0] * 255);
                    cd.color = Color(gv, gv, gv, 255);
                    break;
                }
                case XARColorDefinition::Model::HSV: {
                    float h = cd.components[0] * 360.0f;
                    float s = cd.components[1];
                    float v = cd.components[2];
                    float c = v * s;
                    float hh = h / 60.0f;
                    float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
                    float r1 = 0, g1 = 0, b1 = 0;
                    if (hh < 1) { r1 = c; g1 = x; }
                    else if (hh < 2) { r1 = x; g1 = c; }
                    else if (hh < 3) { g1 = c; b1 = x; }
                    else if (hh < 4) { g1 = x; b1 = c; }
                    else if (hh < 5) { r1 = x; b1 = c; }
                    else { r1 = c; b1 = x; }
                    float mm = v - c;
                    cd.color = Color(
                        static_cast<uint8_t>((r1 + mm) * 255),
                        static_cast<uint8_t>((g1 + mm) * 255),
                        static_cast<uint8_t>((b1 + mm) * 255), 255);
                    break;
                }
                case XARColorDefinition::Model::CMYK: {
                    float cc = cd.components[0], mm = cd.components[1];
                    float yy = cd.components[2], kk = cd.components[3];
                    float rr = (1 - cc) * (1 - kk);
                    float gg = (1 - mm) * (1 - kk);
                    float bb = (1 - yy) * (1 - kk);
                    cd.color = Color(
                        static_cast<uint8_t>(rr * 255),
                        static_cast<uint8_t>(gg * 255),
                        static_cast<uint8_t>(bb * 255), 255);
                    break;
                }
            }
        }

        if (cd.colorType == XARColorDefinition::Type::Linked) {
            if (auto* parent = GetColor(cd.parentRef)) cd.color = parent->color;
        } else if (cd.colorType == XARColorDefinition::Type::Tint ||
                   cd.colorType == XARColorDefinition::Type::Shaded) {
            cd.tintValue = cd.components[0];
            if (auto* parent = GetColor(cd.parentRef)) {
                Color target = (cd.colorType == XARColorDefinition::Type::Tint)
                    ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
                cd.color = BlendColors(parent->color, target, 1.0f - cd.tintValue);
            }
        }
        colors[currentSequenceNumber] = std::move(cd);
    }

// ===== FILLS =====

    void XARDocument::ParseFlatFillRecord(const XARRecord& record) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        int32_t ref = ReadInt32(d, off);
        currentContext.fill.type = XARFillType::Flat;
        currentContext.fill.startColor = ResolveColorRef(ref);
        currentContext.hasFill = true;
    }

    void XARDocument::ParseLinearFillRecord(const XARRecord& record, bool threePoint, bool multistage) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (total < 16) return;
        currentContext.fill.type = XARFillType::LinearGradient;
        currentContext.fill.startPoint = ReadCoord(d, off);
        currentContext.fill.endPoint = ReadCoord(d, off);
        if (threePoint && off + 8 <= total) currentContext.fill.endPoint2 = ReadCoord(d, off);
        if (off + 4 <= total) currentContext.fill.startColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.endColor = ResolveColorRef(ReadInt32(d, off));
        if (multistage) {
            if (off + 4 > total) { currentContext.hasFill = true; return; }
            int32_t numCols = ReadInt32(d, off);
            currentContext.fill.stops.clear();
            currentContext.fill.stops.push_back({0.0, currentContext.fill.startColor, 0});
            for (int32_t i = 0; i < numCols && off + 12 <= total; ++i) {
                XARFillStop st;
                st.position = ReadDouble(d, off);
                st.colourRef = ReadInt32(d, off);
                st.color = ResolveColorRef(st.colourRef);
                currentContext.fill.stops.push_back(st);
            }
            currentContext.fill.stops.push_back({1.0, currentContext.fill.endColor, 0});
        }
        // Optional profile (16 bytes: bias, gain doubles)
        if (off + 16 <= total) {
            currentContext.fill.profileBias = ReadDouble(d, off);
            currentContext.fill.profileGain = ReadDouble(d, off);
        }
        currentContext.hasFill = true;
    }

    void XARDocument::ParseRadialFillRecord(const XARRecord& record, XARFillType ft, bool elliptical, bool multistage) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        currentContext.fill.type = ft;
        if (off + 16 > total) return;
        currentContext.fill.startPoint = ReadCoord(d, off);
        currentContext.fill.endPoint = ReadCoord(d, off);
        if (elliptical && off + 8 <= total) currentContext.fill.endPoint2 = ReadCoord(d, off);
        if (off + 4 <= total) currentContext.fill.startColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.endColor = ResolveColorRef(ReadInt32(d, off));
        if (multistage && off + 4 <= total) {
            int32_t numCols = ReadInt32(d, off);
            currentContext.fill.stops.clear();
            currentContext.fill.stops.push_back({0.0, currentContext.fill.startColor, 0});
            for (int32_t i = 0; i < numCols && off + 12 <= total; ++i) {
                XARFillStop st;
                st.position = ReadDouble(d, off);
                st.colourRef = ReadInt32(d, off);
                st.color = ResolveColorRef(st.colourRef);
                currentContext.fill.stops.push_back(st);
            }
            currentContext.fill.stops.push_back({1.0, currentContext.fill.endColor, 0});
        }
        if (off + 16 <= total) {
            currentContext.fill.profileBias = ReadDouble(d, off);
            currentContext.fill.profileGain = ReadDouble(d, off);
        }
        currentContext.hasFill = true;
    }

    void XARDocument::ParseConicalFillRecord(const XARRecord& record, bool multistage) {
        ParseRadialFillRecord(record, XARFillType::ConicalGradient, false, multistage);
    }

    void XARDocument::ParseBitmapFillRecord(const XARRecord& record, bool contoned) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        currentContext.fill.type = contoned ? XARFillType::ContoneBitmap : XARFillType::Bitmap;
        if (off + 24 > total) return;
        currentContext.fill.startPoint = ReadCoord(d, off);    // bottom-left
        currentContext.fill.endPoint = ReadCoord(d, off);      // bottom-right
        currentContext.fill.endPoint2 = ReadCoord(d, off);     // top-left
        if (contoned && off + 8 <= total) {
            int32_t s = ReadInt32(d, off);
            int32_t e = ReadInt32(d, off);
            currentContext.fill.startColor = ResolveColorRef(s);
            currentContext.fill.endColor = ResolveColorRef(e);
        }
        if (off + 4 <= total) currentContext.fill.bitmapRef = ReadInt32(d, off);
        if (off + 16 <= total) {
            currentContext.fill.profileBias = ReadDouble(d, off);
            currentContext.fill.profileGain = ReadDouble(d, off);
        }
        // The bitmap definition always precedes its use, so resolve the
        // reference now; the table entry outlives every node snapshot.
        currentContext.fill.bitmapDef = GetBitmap(currentContext.fill.bitmapRef);
        currentContext.hasFill = true;
    }

    void XARDocument::ParseFractalFillRecord(const XARRecord& record, bool noise) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        currentContext.fill.type = noise ? XARFillType::Noise : XARFillType::Fractal;
        if (off + 24 > total) return;
        currentContext.fill.startPoint = ReadCoord(d, off);
        currentContext.fill.endPoint = ReadCoord(d, off);
        currentContext.fill.endPoint2 = ReadCoord(d, off);
        if (off + 4 <= total) currentContext.fill.startColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.endColor = ResolveColorRef(ReadInt32(d, off));
        // Best-effort: skip fractal-specific parameters
        currentContext.hasFill = true;
    }

    void XARDocument::ParseDiamondFillRecord(const XARRecord& record) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        currentContext.fill.type = XARFillType::Diamond;
        if (off + 24 > total) return;
        currentContext.fill.startPoint = ReadCoord(d, off);
        currentContext.fill.endPoint = ReadCoord(d, off);
        currentContext.fill.endPoint2 = ReadCoord(d, off);
        if (off + 4 <= total) currentContext.fill.startColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.endColor = ResolveColorRef(ReadInt32(d, off));
        currentContext.hasFill = true;
    }

    void XARDocument::ParseThreeFourColFillRecord(const XARRecord& record, bool four) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        currentContext.fill.type = four ? XARFillType::FourColour : XARFillType::ThreeColour;
        if (off + 24 > total) return;
        currentContext.fill.startPoint = ReadCoord(d, off);
        currentContext.fill.endPoint = ReadCoord(d, off);
        currentContext.fill.endPoint2 = ReadCoord(d, off);
        if (off + 4 <= total) currentContext.fill.startColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.endColor = ResolveColorRef(ReadInt32(d, off));
        if (off + 4 <= total) currentContext.fill.thirdColor = ResolveColorRef(ReadInt32(d, off));
        if (four && off + 4 <= total) currentContext.fill.fourthColor = ResolveColorRef(ReadInt32(d, off));
        currentContext.hasFill = true;
    }

// ===== TRANSPARENCY =====

    void XARDocument::ParseFlatTransparentFillRecord(const XARRecord& record) {
        if (record.data.size() < 2) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        currentContext.transparency.type = XARTransparencyType::Flat;
        currentContext.transparency.startTransparency = ReadByte(d, off);
        uint8_t mix = ReadByte(d, off);
        currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseLinearTransparentFillRecord(const XARRecord& record, bool threePoint) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 16 > total) return;
        currentContext.transparency.type = XARTransparencyType::LinearGradient;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        if (threePoint && off + 8 <= total) currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseRadialTransparentFillRecord(const XARRecord& record, XARTransparencyType tt, bool elliptical) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 16 > total) return;
        currentContext.transparency.type = tt;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        if (elliptical && off + 8 <= total) currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseConicalTransparentFillRecord(const XARRecord& record) {
        ParseRadialTransparentFillRecord(record, XARTransparencyType::ConicalGradient, false);
    }

    void XARDocument::ParseBitmapTransparentFillRecord(const XARRecord& record) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 24 > total) return;
        currentContext.transparency.type = XARTransparencyType::Bitmap;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        if (off + 4 <= total) currentContext.transparency.bitmapRef = ReadInt32(d, off);
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseFractalTransparentFillRecord(const XARRecord& record, bool noise) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 24 > total) return;
        currentContext.transparency.type = noise ? XARTransparencyType::Noise : XARTransparencyType::Fractal;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseDiamondTransparentFillRecord(const XARRecord& record) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 24 > total) return;
        currentContext.transparency.type = XARTransparencyType::Diamond;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        currentContext.hasTransparency = true;
    }

    void XARDocument::ParseThreeFourColTransparentFillRecord(const XARRecord& record, bool four) {
        const uint8_t* d = record.data.data();
        size_t off = 0;
        size_t total = record.data.size();
        if (off + 24 > total) return;
        currentContext.transparency.type = four ? XARTransparencyType::FourColour : XARTransparencyType::ThreeColour;
        currentContext.transparency.startPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint = ReadCoord(d, off);
        currentContext.transparency.endPoint2 = ReadCoord(d, off);
        if (off + 1 <= total) currentContext.transparency.startTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.endTransparency = ReadByte(d, off);
        if (off + 1 <= total) currentContext.transparency.thirdTransparency = ReadByte(d, off);
        if (four && off + 1 <= total) currentContext.transparency.fourthTransparency = ReadByte(d, off);
        if (off + 1 <= total) {
            uint8_t mix = ReadByte(d, off);
            currentContext.transparency.mix = static_cast<XARTransparencyMix>(std::min<uint8_t>(mix, 10));
        }
        currentContext.hasTransparency = true;
    }

// ===== LINE ATTRIBUTES =====

    void XARDocument::ParseLineColourRecord(const XARRecord& record) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        currentContext.line.color = ResolveColorRef(ReadInt32(d, off));
        currentContext.line.hasColor = true;
        currentContext.hasLine = true;
    }

    void XARDocument::ParseLineWidthRecord(const XARRecord& record) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        currentContext.line.width = ReadInt32(d, off);
        currentContext.hasLine = true;
    }

    void XARDocument::ParseLineCapRecord(const XARRecord& record, bool /*isStart*/) {
        if (record.data.empty()) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        uint8_t v = ReadByte(d, off);
        currentContext.line.cap = (v == 1) ? LineCap::Round
                                : (v == 2) ? LineCap::Square
                                : LineCap::Butt;
    }

    void XARDocument::ParseLineJoinRecord(const XARRecord& record) {
        if (record.data.empty()) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        uint8_t v = ReadByte(d, off);
        currentContext.line.join = (v == 1) ? LineJoin::Round
                                 : (v == 2) ? LineJoin::Bevel
                                 : LineJoin::Miter;
    }

    void XARDocument::ParseLineMitreLimitRecord(const XARRecord& record) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        currentContext.line.mitreLimit = static_cast<float>(ReadInt32(d, off)) / 65536.0f;
    }

    void XARDocument::ParseLineTransparencyRecord(const XARRecord& record) {
        if (record.data.size() < 2) return;
        currentContext.line.lineTransparency = record.data[0];
        currentContext.line.lineTransparencyMix =
            static_cast<XARTransparencyMix>(std::min<uint8_t>(record.data[1], 10));
    }

    void XARDocument::ParseDashStyleRecord(const XARRecord& record) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        int32_t ref = ReadInt32(d, off);
        if (auto* dd = GetDash(ref)) currentContext.line.dashPattern = dd->pattern;
        else currentContext.line.dashPattern.clear();
    }

    void XARDocument::ParseDefineDashRecord(const XARRecord& record, bool scaled) {
        XARDashDefinition dd;
        dd.sequenceNumber = currentSequenceNumber;
        dd.scaled = scaled;
        if (record.data.size() >= 4) {
            const uint8_t* d = record.data.data();
            size_t off = 0;
            int32_t numEntries = ReadInt32(d, off);
            for (int32_t i = 0; i < numEntries && off + 4 <= record.data.size(); ++i) {
                int32_t mp = ReadInt32(d, off);
                dd.pattern.push_back(static_cast<double>(MPtoPx(mp)));
            }
        }
        dashes[currentSequenceNumber] = std::move(dd);
    }

    void XARDocument::ParseArrowRecord(const XARRecord& record, bool isStart) {
        if (record.data.size() < 4) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        int32_t ref = ReadInt32(d, off);
        if (isStart) currentContext.line.startArrowRef = ref;
        else currentContext.line.endArrowRef = ref;
    }

    void XARDocument::ParseDefineArrowRecord(const XARRecord&) {
        XARArrowDefinition ad;
        ad.sequenceNumber = currentSequenceNumber;
        // Full arrow definition rendering is out of scope; store an empty stub.
        arrows[currentSequenceNumber] = std::move(ad);
    }

    void XARDocument::ParseWindingRuleRecord(const XARRecord& record) {
        if (record.data.empty()) return;
        currentContext.windingRule = (record.data[0] == 2) ? XARWindingRule::EvenOdd
                                                            : XARWindingRule::NonZero;
    }

// ===== MISC =====

    void XARDocument::ParseObjectBoundsRecord(const XARRecord& record) {
        if (record.data.size() < 16) return;
        const uint8_t* d = record.data.data();
        size_t off = 0;
        Point2Di bl = ReadCoord(d, off);
        Point2Di tr = ReadCoord(d, off);
        pendingObjectBounds = Rect2Dd(
            static_cast<float>(bl.x) * XARConstants::MILLIPOINTS_TO_PIXELS,
            static_cast<float>(bl.y) * XARConstants::MILLIPOINTS_TO_PIXELS,
            static_cast<float>(tr.x - bl.x) * XARConstants::MILLIPOINTS_TO_PIXELS,
            static_cast<float>(tr.y - bl.y) * XARConstants::MILLIPOINTS_TO_PIXELS);
        havePendingBounds = true;
    }

    void XARDocument::ParseDocumentInfoRecord(const XARRecord&) {
        // Document metadata; not needed for rendering
    }

// ===== COLOR REF RESOLUTION =====

    Color XARDocument::ResolveColorRef(int32_t ref) {
        // Negative refs are default reusable items; positive refs index into colors map
        if (ref >= 1) {
            if (auto* c = GetColor(ref)) return c->color;
            return Color(0, 0, 0, 255);
        }
        // Defaults (Appendix B): -1 black, -2 white, -3 25% grey, -4 50% grey, -5 75% grey, etc.
        switch (ref) {
            case -1: return Color(0, 0, 0, 255);
            case -2: return Color(255, 255, 255, 255);
            case -3: return Color(192, 192, 192, 255);
            case -4: return Color(128, 128, 128, 255);
            case -5: return Color(64, 64, 64, 255);
            default: return Color(0, 0, 0, 255);
        }
    }

    XARColorDefinition* XARDocument::GetColor(int32_t ref) {
        auto it = colors.find(ref);
        return (it != colors.end()) ? &it->second : nullptr;
    }
    XARBitmapDefinition* XARDocument::GetBitmap(int32_t ref) {
        auto it = bitmaps.find(ref);
        return (it != bitmaps.end()) ? &it->second : nullptr;
    }
    XARFontDefinition* XARDocument::GetFont(int32_t ref) {
        auto it = fonts.find(ref);
        return (it != fonts.end()) ? &it->second : nullptr;
    }
    XARArrowDefinition* XARDocument::GetArrow(int32_t ref) {
        auto it = arrows.find(ref);
        return (it != arrows.end()) ? &it->second : nullptr;
    }
    XARDashDefinition* XARDocument::GetDash(int32_t ref) {
        auto it = dashes.find(ref);
        return (it != dashes.end()) ? &it->second : nullptr;
    }

// ===== NODE STACK =====

    void XARDocument::AttachNode(XARNodePtr node) {
        CurrentNode()->AddChild(node);
        lastNode = node;
    }

    void XARDocument::PopNode() {
        if (!nodeStack.empty()) nodeStack.pop();
    }

    XARNodePtr XARDocument::CurrentNode() {
        return nodeStack.empty() ? root : nodeStack.top();
    }

    void XARDocument::RegisterRenderableNode(XARNodePtr node) {
        if (havePendingBounds) {
            node->bounds = pendingObjectBounds;
            node->boundsCached = true;
            havePendingBounds = false;
        }
        CurrentNode()->AddChild(node);
        lastNode = node;
    }

    void XARDocument::ApplyCurrentAttributesTo(XARNodePtr node) {
        node->fill = currentContext.fill;
        node->transparency = currentContext.transparency;
        node->line = currentContext.line;
        node->windingRule = currentContext.windingRule;
        node->textAttr = currentContext.text;
        node->hasFill = currentContext.hasFill;
        node->hasLine = currentContext.hasLine;
        node->hasTransparency = currentContext.hasTransparency;
    }

// ===== BINARY READERS =====

    uint8_t XARDocument::ReadByte(const uint8_t* d, size_t& o) { return d[o++]; }
    uint16_t XARDocument::ReadUInt16(const uint8_t* d, size_t& o) {
        uint16_t v = static_cast<uint16_t>(d[o] | (d[o + 1] << 8)); o += 2; return v;
    }
    int16_t XARDocument::ReadInt16(const uint8_t* d, size_t& o) {
        return static_cast<int16_t>(ReadUInt16(d, o));
    }
    uint32_t XARDocument::ReadUInt32(const uint8_t* d, size_t& o) {
        uint32_t v = static_cast<uint32_t>(d[o]) |
                     (static_cast<uint32_t>(d[o + 1]) << 8) |
                     (static_cast<uint32_t>(d[o + 2]) << 16) |
                     (static_cast<uint32_t>(d[o + 3]) << 24);
        o += 4;
        return v;
    }
    int32_t XARDocument::ReadInt32(const uint8_t* d, size_t& o) {
        return static_cast<int32_t>(ReadUInt32(d, o));
    }
    double XARDocument::ReadDouble(const uint8_t* d, size_t& o) {
        double v;
        std::memcpy(&v, d + o, 8);
        o += 8;
        return v;
    }
    float XARDocument::ReadFloat(const uint8_t* d, size_t& o) {
        float v;
        std::memcpy(&v, d + o, 4);
        o += 4;
        return v;
    }

    std::string XARDocument::ReadUTF16String(const uint8_t* d, size_t& o, size_t maxBytes) {
        // UTF-16LE null-terminated; convert to UTF-8 (BMP only — surrogate pairs
        // are passed through as replacement chars for simplicity).
        std::string out;
        size_t end = o + maxBytes;
        while (o + 2 <= end) {
            uint16_t ch = static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
            o += 2;
            if (ch == 0) break;
            if (ch < 0x80) {
                out.push_back(static_cast<char>(ch));
            } else if (ch < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }
        return out;
    }

    std::string XARDocument::ReadASCIIString(const uint8_t* d, size_t& o, size_t maxBytes) {
        std::string out;
        size_t end = o + maxBytes;
        while (o < end) {
            uint8_t c = d[o++];
            if (c == 0) break;
            out.push_back(static_cast<char>(c));
        }
        return out;
    }

    Point2Di XARDocument::ReadCoord(const uint8_t* d, size_t& o) {
        int32_t x = ReadInt32(d, o);
        int32_t y = ReadInt32(d, o);
        return Point2Di(x, y);
    }

    XARMatrix XARDocument::ReadMatrix(const uint8_t* d, size_t& o) {
        // On-disk MATRIX per the Xar spec: a,b,c,d as signed 16.16 fixed
        // point, e,f as INT32 millipoints — 24 bytes total (matches Xara
        // LX's CXaraFileRecord::ReadMatrix).
        XARMatrix m;
        m.a = static_cast<double>(ReadInt32(d, o)) / 65536.0;
        m.b = static_cast<double>(ReadInt32(d, o)) / 65536.0;
        m.c = static_cast<double>(ReadInt32(d, o)) / 65536.0;
        m.d = static_cast<double>(ReadInt32(d, o)) / 65536.0;
        m.e = static_cast<double>(ReadInt32(d, o));
        m.f = static_cast<double>(ReadInt32(d, o));
        return m;
    }

    Color XARDocument::ReadColor3(const uint8_t* d, size_t& o) {
        uint8_t r = ReadByte(d, o);
        uint8_t g = ReadByte(d, o);
        uint8_t b = ReadByte(d, o);
        return Color(r, g, b, 255);
    }

    Point2Di XARDocument::ReadRelativeCoord(const uint8_t* d, size_t& o, Point2Di& last,
                                            bool firstCoord) {
        // 8 bytes, byte-interleaved MSB-first (X = b0,b2,b4,b6 ;
        // Y = b1,b3,b5,b7). The first coordinate of a record is absolute;
        // subsequent ones store the reverse delta (previous minus current).
        uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = d[o + i];
        o += 8;
        uint32_t xRaw = (uint32_t(b[0]) << 24) | (uint32_t(b[2]) << 16)
                      | (uint32_t(b[4]) << 8)  | uint32_t(b[6]);
        uint32_t yRaw = (uint32_t(b[1]) << 24) | (uint32_t(b[3]) << 16)
                      | (uint32_t(b[5]) << 8)  | uint32_t(b[7]);
        int32_t vx = static_cast<int32_t>(xRaw);
        int32_t vy = static_cast<int32_t>(yRaw);
        Point2Di abs = firstCoord ? Point2Di(vx, vy)
                                  : Point2Di(last.x - vx, last.y - vy);
        last = abs;
        return abs;
    }

// ===== RENDER =====

    void XARDocument::Render(IRenderContext* ctx, float scale) {
        // Multi-spread documents render one page at a time — every spread's
        // coordinates restart at its own origin, so drawing them all at once
        // would overlay every page.
        if (!spreadNodes.empty()) {
            RenderPage(ctx, 0, scale);
            return;
        }
        if (!root) return;
        ctx->PushState();
        // XAR uses Y-up coordinates; flip to Y-down screen space
        ctx->Translate(0, height);
        ctx->Scale(1.0f, -1.0f);
        root->Render(ctx, scale);
        ctx->PopState();
    }

    int XARDocument::GetPageCount() const {
        return spreadNodes.empty() ? 1 : static_cast<int>(spreadNodes.size());
    }

    float XARDocument::GetPageWidth(int page) const {
        if (page >= 0 && page < static_cast<int>(spreadNodes.size()) &&
            spreadNodes[page]->width > 0) {
            return spreadNodes[page]->GetWidthInPixels();
        }
        return width;
    }

    float XARDocument::GetPageHeight(int page) const {
        if (page >= 0 && page < static_cast<int>(spreadNodes.size()) &&
            spreadNodes[page]->height > 0) {
            return spreadNodes[page]->GetHeightInPixels();
        }
        return height;
    }

    void XARDocument::RenderPage(IRenderContext* ctx, int page, float scale) {
        if (spreadNodes.empty()) { Render(ctx, scale); return; }
        if (page < 0 || page >= static_cast<int>(spreadNodes.size())) return;
        ctx->PushState();
        // XAR uses Y-up coordinates; flip against this spread's own height.
        ctx->Translate(0, GetPageHeight(page) * scale);
        ctx->Scale(1.0f, -1.0f);
        spreadNodes[page]->Render(ctx, scale);
        ctx->PopState();
    }

// ===== UI ELEMENT =====

    UltraCanvasXARElement::UltraCanvasXARElement(const std::string& identifier,
                                                 float x, float y, float w, float h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {}

    bool UltraCanvasXARElement::LoadFromFile(const std::string& filepath) {
        lastError.clear();
        document = std::make_unique<XARDocument>();
        if (!document->LoadFromFile(filepath)) {
            // Distinguish a file-access problem (missing / locked / no permission)
            // from a valid-but-unparseable XAR file.
            std::string access = DescribeFileReadError(filepath);
            lastError = !access.empty()
                ? access
                : ("The file is not a valid Xara (.xar) drawing: " + filepath);
            document.reset();
            return false;
        }
        return true;
    }

    bool UltraCanvasXARElement::LoadFromMemory(const uint8_t* data, size_t size) {
        lastError.clear();
        document = std::make_unique<XARDocument>();
        if (!document->LoadFromMemory(data, size)) {
            lastError = "The data is not a valid Xara (.xar) drawing.";
            document.reset();
            return false;
        }
        return true;
    }

    void UltraCanvasXARElement::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!document || !ctx) return;
        Rect2Di bounds = GetBounds();
        ctx->PushState();
        float docW = document->GetPageWidth(currentPage);
        float docH = document->GetPageHeight(currentPage);
        if (docW > 0 && docH > 0) {
            float scaleX = static_cast<float>(bounds.width) / docW;
            float scaleY = static_cast<float>(bounds.height) / docH;
            float renderScale = scale;
            if (preserveAspectRatio) {
                if (docW / docH > static_cast<float>(bounds.width) / bounds.height) {
                    renderScale = scaleX * scale;
                    ctx->Translate(bounds.x, bounds.y + (bounds.height - docH * renderScale) / 2);
                } else {
                    renderScale = scaleY * scale;
                    ctx->Translate(bounds.x + (bounds.width - docW * renderScale) / 2, bounds.y);
                }
            } else {
                ctx->Translate(bounds.x, bounds.y);
                ctx->Scale(scaleX * scale, scaleY * scale);
                renderScale = 1.0f;
            }
            // White page behind the drawing, like Xara's own canvas — the
            // host surface (e.g. the fullscreen viewer) may be dark.
            ctx->SetFillPaint(Color(255, 255, 255, 255));
            ctx->FillRectangle(Rect2Dd(0, 0, docW * renderScale, docH * renderScale));
            document->RenderPage(ctx, currentPage, renderScale);
        }
        ctx->PopState();
    }

    void UltraCanvasXARElement::SetCurrentPage(int page) {
        if (!document) return;
        int clamped = std::clamp(page, 0, document->GetPageCount() - 1);
        if (clamped == currentPage) return;
        currentPage = clamped;
        RequestRedraw();
        if (onPageChanged) onPageChanged(currentPage);
    }

// ===== PLUGIN =====

    bool UltraCanvasXARPlugin::CanHandle(const std::string& filePath) const {
        std::string ext = GetFileExtension(filePath);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == "xar" || ext == "web" || ext == "wix";
    }

    bool UltraCanvasXARPlugin::CanHandle(const GraphicsFileInfo& fileInfo) const {
        return fileInfo.formatType == GraphicsFormatType::Vector && CanHandle(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::LoadGraphics(const std::string& filePath) {
        auto element = std::make_shared<UltraCanvasXARElement>("XARElement", 0, 0, 400, 400);
        if (element->LoadFromFile(filePath)) {
            if (element->GetDocument()) {
                element->SetElementSize(Size2Df (element->GetDocument()->GetWidth(), element->GetDocument()->GetHeight()));
            }
            return element;
        }
        return nullptr;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::LoadGraphics(const GraphicsFileInfo& fileInfo) {
        return LoadGraphics(fileInfo.filename);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasXARPlugin::CreateGraphics(int, int, GraphicsFormatType) {
        return nullptr;
    }

    GraphicsFileInfo UltraCanvasXARPlugin::GetFileInfo(const std::string& filePath) {
        GraphicsFileInfo info(filePath);
        XARDocument doc;
        if (doc.LoadFromFile(filePath)) {
            info.width = static_cast<int>(doc.GetWidth());
            info.height = static_cast<int>(doc.GetHeight());
        }
        info.formatType = GraphicsFormatType::Vector;
        info.supportedManipulations = GetSupportedManipulations();
        return info;
    }

    bool UltraCanvasXARPlugin::ValidateFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;
        uint8_t header[8];
        file.read(reinterpret_cast<char*>(header), 8);
        if (file.gcount() < 8) return false;
        uint32_t m1 = static_cast<uint32_t>(header[0]) |
                      (static_cast<uint32_t>(header[1]) << 8) |
                      (static_cast<uint32_t>(header[2]) << 16) |
                      (static_cast<uint32_t>(header[3]) << 24);
        uint32_t m2 = static_cast<uint32_t>(header[4]) |
                      (static_cast<uint32_t>(header[5]) << 8) |
                      (static_cast<uint32_t>(header[6]) << 16) |
                      (static_cast<uint32_t>(header[7]) << 24);
        return m1 == XARConstants::MAGIC_XARA && m2 == XARConstants::MAGIC_SIGNATURE;
    }

    std::string UltraCanvasXARPlugin::GetFileExtension(const std::string& filePath) const {
        size_t p = filePath.find_last_of('.');
        return p == std::string::npos ? "" : filePath.substr(p + 1);
    }

} // namespace UltraCanvas
