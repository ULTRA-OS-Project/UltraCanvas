// UltraCanvasVectorRenderer.cpp
// Vector Graphics Rendering for UltraCanvas
// Version: 2.1.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
//
// Draws a VectorStorage::VectorDocument into any IRenderContext. Every
// shape goes through one pipeline: BuildElementPath() puts the element's
// outline on the context (paths are normalised through PathOps, so arcs,
// smooth curves and quadratics all arrive as cubics), the outline's own
// extents are the box object-bounding-box gradients resolve against, and
// FillAndStroke() paints it with the element's fill and stroke opacities
// folded into the paint. A clip-path reference on the style is honoured by
// building the clip elements' outlines and clipping to them before the
// element is drawn.

#include "DataFormats/UltraCanvasVectorRenderer.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace UltraCanvas {

    using namespace VectorStorage;

    namespace {
        bool HasFill(const VectorStyle &style) {
            return style.Fill.has_value() && !std::holds_alternative<std::monostate>(style.Fill.value());
        }

        bool HasStroke(const VectorStyle &style) {
            return style.Stroke.has_value() && style.Stroke->Width > 0 &&
                   !std::holds_alternative<std::monostate>(style.Stroke->Fill);
        }

        Color WithOpacity(Color c, float opacity) {
            if (opacity >= 1.0f) return c;
            if (opacity <= 0.0f) { c.a = 0; return c; }
            c.a = static_cast<uint8_t>(std::lround(c.a * opacity));
            return c;
        }

        std::vector<GradientStop> StopsWithOpacity(const std::vector<GradientStop> &stops, float opacity) {
            if (opacity >= 1.0f) return stops;
            std::vector<GradientStop> out = stops;
            for (auto &s: out) s.color = WithOpacity(s.color, opacity);
            return out;
        }

        // A box with no area and no position is what GetBoundingBox() returns
        // for an element without geometry.
        bool EmptyBox(const Rect2Dd &b) {
            return b.width <= 0 && b.height <= 0 && b.x == 0 && b.y == 0;
        }
    }

    VectorRenderer::VectorRenderer() = default;

    VectorRenderer::~VectorRenderer() = default;

    void VectorRenderer::RenderDocument(IRenderContext *context, const VectorDocument &document) {
        auto startTime = std::chrono::high_resolution_clock::now();
        ctx = context;
        currentDocument = &document;
        stats.Reset();

        ctx->PushState();

        // Setup viewport transform
        if (document.ViewBox.width > 0 && document.ViewBox.height > 0 &&
            options.ViewportBounds.width > 0 && options.ViewportBounds.height > 0) {
            float scaleX = options.ViewportBounds.width / document.ViewBox.width;
            float scaleY = options.ViewportBounds.height / document.ViewBox.height;
            if (document.PreserveAspectRatio != "none") {
                float scale = std::min(scaleX, scaleY);
                float dx = (options.ViewportBounds.width - document.ViewBox.width * scale) / 2;
                float dy = (options.ViewportBounds.height - document.ViewBox.height * scale) / 2;
                ctx->Translate(dx, dy);
                ctx->Scale(scale, scale);
            } else {
                ctx->Scale(scaleX, scaleY);
            }
            ctx->Translate(-document.ViewBox.x, -document.ViewBox.y);
        }

        if (options.PixelRatio != 1.0f) ctx->Scale(options.PixelRatio, options.PixelRatio);

        if (document.BackgroundColor.has_value()) {
            ctx->SetFillPaint(document.BackgroundColor.value());
            ctx->FillRectangle(document.ViewBox);
        }

        for (const auto &layer: document.Layers) {
            if (layer->Visible || options.RenderInvisibleElements) RenderLayer(ctx, *layer);
        }

        ctx->PopState();
        currentDocument = nullptr;

        auto endTime = std::chrono::high_resolution_clock::now();
        stats.RenderTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    }

    void VectorRenderer::RenderLayer(IRenderContext *context, const VectorLayer &layer) {
        ctx = context;
        ctx->PushState();

        float layerOpacity = silhouetteMode ? 1.0f : layer.Opacity * currentOpacity;
        opacityStack.push(currentOpacity);
        currentOpacity = layerOpacity;
        ctx->SetAlpha(currentOpacity);

        for (const auto &child: layer.Children) {
            if (child) RenderElement(ctx, *child);
        }

        currentOpacity = opacityStack.top();
        opacityStack.pop();
        ctx->PopState();
    }

    void VectorRenderer::RenderElement(IRenderContext *context, const VectorElement &element) {
        ctx = context;
        if (!IsVisible(element)) return;

        if (options.EnableCulling && options.ClipToViewport && !IsInViewport(element.GetBoundingBox())) {
            stats.ElementsCulled++;
            return;
        }

        ctx->PushState();
        if (element.Transform.has_value()) ApplyTransform(element.Transform.value());
        ApplyStyle(element.Style);
        if (element.Style.ClipPath.has_value() && !element.Style.ClipPath->empty())
            ApplyClip(*element.Style.ClipPath);

        const TransparencyData *tr = element.Style.Transparency.has_value() ? &*element.Style.Transparency : nullptr;
        const bool effects = !silhouetteMode &&
                             (element.Effects.Any() ||
                              (tr && (tr->IsGradient() || tr->Mix != TransparencyMix::Mix || tr->Level > 0.0f)));
        if (effects) RenderWithEffects(element);
        else DrawElementBody(element);

        if (options.ShowBoundingBoxes) RenderDebugBounds(element.GetBoundingBox());
        stats.ElementsRendered++;
        ctx->PopState();
    }

    // The element itself, in the space its Transform and clip have set up.
    void VectorRenderer::DrawElementBody(const VectorElement &element) {
        switch (element.Type) {
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle:
            case VectorElementType::Circle:
            case VectorElementType::Ellipse:
            case VectorElementType::Polyline:
            case VectorElementType::Polygon:
            case VectorElementType::Path:
                RenderShape(element);
                break;
            case VectorElementType::Line:
                RenderLine(static_cast<const VectorLine &>(element));
                break;
            case VectorElementType::Text:
                RenderText(static_cast<const VectorText &>(element));
                break;
            case VectorElementType::Image:
                RenderImage(static_cast<const VectorImage &>(element));
                break;
            case VectorElementType::Group:
            case VectorElementType::Symbol:
                RenderGroup(static_cast<const VectorGroup &>(element));
                break;
            case VectorElementType::Layer:
                RenderLayer(ctx, static_cast<const VectorLayer &>(element));
                break;
            case VectorElementType::Use:
                RenderUse(static_cast<const VectorUse &>(element));
                break;
            default:
                break;
        }
    }

    // Puts the element's outline on the context in the element's own
    // coordinate space (its Transform is already applied by the caller).
    // Returns false for element kinds that have no outline.
    bool VectorRenderer::BuildElementPath(const VectorElement &element) {
        switch (element.Type) {
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle: {
                const auto &rect = static_cast<const VectorRect &>(element);
                if (rect.RadiusX > 0 || rect.RadiusY > 0)
                    ctx->RoundedRect(rect.Bounds.x, rect.Bounds.y, rect.Bounds.width, rect.Bounds.height,
                                     std::max(rect.RadiusX, rect.RadiusY));
                else
                    ctx->Rect(rect.Bounds.x, rect.Bounds.y, rect.Bounds.width, rect.Bounds.height);
                return true;
            }
            case VectorElementType::Circle: {
                const auto &circle = static_cast<const VectorCircle &>(element);
                ctx->Circle(circle.Center.x, circle.Center.y, circle.Radius);
                return true;
            }
            case VectorElementType::Ellipse: {
                const auto &ellipse = static_cast<const VectorEllipse &>(element);
                ctx->Ellipse(ellipse.Center.x, ellipse.Center.y, ellipse.RadiusX, ellipse.RadiusY, 0);
                return true;
            }
            case VectorElementType::Line: {
                const auto &line = static_cast<const VectorLine &>(element);
                ctx->MoveTo(line.Start.x, line.Start.y);
                ctx->LineTo(line.End.x, line.End.y);
                return true;
            }
            case VectorElementType::Polyline: {
                const auto &polyline = static_cast<const VectorPolyline &>(element);
                if (polyline.Points.size() < 2) return false;
                ctx->MoveTo(polyline.Points[0].x, polyline.Points[0].y);
                for (size_t i = 1; i < polyline.Points.size(); i++)
                    ctx->LineTo(polyline.Points[i].x, polyline.Points[i].y);
                return true;
            }
            case VectorElementType::Polygon: {
                const auto &polygon = static_cast<const VectorPolygon &>(element);
                if (polygon.Points.size() < 3) return false;
                ctx->MoveTo(polygon.Points[0].x, polygon.Points[0].y);
                for (size_t i = 1; i < polygon.Points.size(); i++)
                    ctx->LineTo(polygon.Points[i].x, polygon.Points[i].y);
                ctx->ClosePath();
                return true;
            }
            case VectorElementType::Path: {
                const auto &path = static_cast<const VectorPath &>(element);
                if (path.Path.commands.empty()) return false;
                BuildPath(path.Path);
                return true;
            }
            default:
                return false;
        }
    }

    void VectorRenderer::RenderShape(const VectorElement &element) {
        if (!BuildElementPath(element)) return;
        const VectorStyle &style = element.Style;
        const StrokeData *st = HasStroke(style) ? &*style.Stroke : nullptr;
        const bool gallery = st && (st->HasArrowheads() || st->HasWidthProfile() || st->HasBrush());
        if (!gallery) {
            FillAndStroke(style);
            return;
        }
        // A line-gallery stroke: the fill as usual, the plain stroke only
        // when neither a width profile nor a brush replaces it, then the
        // decorations from the outline.
        const Rect2Dd bounds = ctx->GetPathExtents();
        if (HasFill(style)) {
            ApplyFill(style.Fill.value(), bounds, style.FillOpacity);
            ctx->FillPathPreserve();
        }
        if (!st->HasWidthProfile() && !st->HasBrush()) {
            ApplyStroke(*st, bounds, style.StrokeOpacity);
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
        RenderLineGallery(element, *st, bounds, style.StrokeOpacity);
    }

    // Fills and strokes the current path. Object-bounding-box gradients
    // resolve against the path's own extents, which are the element's
    // untransformed bounds because the element's Transform is on the CTM.
    void VectorRenderer::FillAndStroke(const VectorStyle &style) {
        const bool fill = HasFill(style);
        const bool stroke = HasStroke(style);
        if (!fill && !stroke) {
            ctx->ClearPath();
            return;
        }
        const Rect2Dd bounds = ctx->GetPathExtents();
        if (fill) {
            ApplyFill(style.Fill.value(), bounds, style.FillOpacity);
            ctx->FillPathPreserve();
        }
        if (stroke) {
            ApplyStroke(style.Stroke.value(), bounds, style.StrokeOpacity);
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderLine(const VectorLine &line) {
        if (!HasStroke(line.Style)) return;
        RenderShape(line);
    }

    void VectorRenderer::RenderText(const VectorText &text) {
        const float opacity = silhouetteMode ? 1.0f : text.Style.FillOpacity;
        if (silhouetteMode) ctx->SetTextPaint(Colors::Black);
        else if (text.Style.Fill.has_value()) {
            if (auto *color = std::get_if<Color>(&text.Style.Fill.value()))
                ctx->SetTextPaint(WithOpacity(*color, opacity));
            else ctx->SetTextPaint(WithOpacity(Colors::Black, opacity));
        } else ctx->SetTextPaint(WithOpacity(Colors::Black, opacity));

        // Each span is set in its own font (a span with no family or the
        // default size inherits the base style's), measured with the
        // context's font engine, and advanced by its real width; the anchor
        // shifts the whole run once the widths are known.
        struct Run { const TextSpanData *span; FontStyle font; int width; };
        std::vector<Run> runs;
        int totalWidth = 0;
        for (const auto &span: text.Spans) {
            VectorTextStyle st = span.Style;
            if (st.FontFamily.empty()) st.FontFamily = text.BaseStyle.FontFamily;
            if (st.FontSize <= 0) st.FontSize = text.BaseStyle.FontSize;
            FontStyle fs = st.ToFontStyle();
            ctx->SetFontFace(fs.fontFamily, fs.fontWeight, fs.fontSlant);
            ctx->SetFontSize(fs.fontSize);
            int w = span.Text.empty() ? 0 : ctx->GetTextLineWidth(span.Text);
            runs.push_back({&span, fs, w});
            totalWidth += w;
        }

        double anchorShift = 0;
        if (text.BaseStyle.Anchor == TextAnchor::Middle) anchorShift = -totalWidth / 2.0;
        else if (text.BaseStyle.Anchor == TextAnchor::End) anchorShift = -totalWidth;

        Point2Dd pos = text.Position;
        pos.x += anchorShift;
        for (const auto &run: runs) {
            if (run.span->Position.has_value()) {
                pos = run.span->Position.value();
                pos.x += anchorShift;
            }
            ctx->SetFontFace(run.font.fontFamily, run.font.fontWeight, run.font.fontSlant);
            ctx->SetFontSize(run.font.fontSize);
            ctx->DrawText(run.span->Text, Point2Dd(pos.x, pos.y));
            pos.x += run.width;
        }
    }

    void VectorRenderer::RenderImage(const VectorImage &image) {
        if (silhouetteMode) {
            ctx->SetFillPaint(Colors::Black);
            ctx->FillRectangle(image.Bounds);
            return;
        }
        if (!image.Source.empty())
            ctx->DrawImage(image.Source,
                           Rect2Dd(image.Bounds.x, image.Bounds.y, image.Bounds.width, image.Bounds.height),
                           ImageFitMode::Contain);
    }

    void VectorRenderer::RenderGroup(const VectorGroup &group) {
        opacityStack.push(currentOpacity);
        if (!silhouetteMode) currentOpacity *= group.Style.Opacity;
        ctx->SetAlpha(currentOpacity);
        for (const auto &child: group.Children) if (child) RenderElement(ctx, *child);
        currentOpacity = opacityStack.top();
        opacityStack.pop();
    }

    void VectorRenderer::RenderUse(const VectorUse &use) {
        if (!currentDocument || use.Reference.empty()) return;
        auto ref = currentDocument->GetDefinition(use.Reference);
        if (!ref) return;
        ctx->PushState();
        ctx->Translate(use.Position.x, use.Position.y);
        Rect2Dd rb = ref->GetBoundingBox();
        if (use.Size.width > 0 && use.Size.height > 0 && rb.width > 0 && rb.height > 0)
            ctx->Scale(use.Size.width / rb.width, use.Size.height / rb.height);
        RenderElement(ctx, *ref);
        ctx->PopState();
    }

    void VectorRenderer::ApplyStyle(const VectorStyle &style) {
        ctx->SetAlpha(silhouetteMode ? 1.0 : style.Opacity * currentOpacity);
    }

    // Clips to the outlines of a <clipPath> definition. The clip is set in
    // the clipped element's space; a clip child with its own Transform is
    // built under it and the transform is undone afterwards (the context
    // keeps the path in device space, so the outline stays where it was).
    void VectorRenderer::ApplyClip(const std::string &clipId) {
        if (!currentDocument) return;
        auto def = currentDocument->GetDefinition(clipId);
        if (!def) return;
        auto *clip = dynamic_cast<const VectorClipPath *>(def.get());
        if (!clip) return;
        bool any = false;
        for (const auto &child: clip->Data.Elements) {
            if (!child) continue;
            if (child->Transform.has_value()) ApplyTransform(child->Transform.value());
            if (BuildElementPath(*child)) any = true;
            if (child->Transform.has_value()) ApplyTransform(child->Transform->Inverse());
        }
        if (!any) return;
        ctx->SetFillRule(clip->Data.ClipRule == VectorStorage::FillRule::EvenOdd
                         ? UltraCanvas::FillRule::EvenOdd : UltraCanvas::FillRule::NonZero);
        ctx->ClipPath();
        ctx->ClearPath();
        ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
    }

    void VectorRenderer::ApplyFill(const FillData &fill, const Rect2Dd &bounds, float opacity) {
        if (silhouetteMode) { ctx->SetFillPaint(Colors::Black); return; }
        if (auto *c = std::get_if<Color>(&fill)) ctx->SetFillPaint(WithOpacity(*c, opacity));
        else if (auto *g = std::get_if<GradientData>(&fill)) SetupGradient(*g, bounds, opacity, false);
        else if (auto *id = std::get_if<std::string>(&fill)) {
            if (currentDocument)
                if (auto d = currentDocument->GetDefinition(*id))
                    if (auto *gd = dynamic_cast<VectorGradient *>(d.get()))
                        SetupGradient(gd->Data, bounds, opacity, false);
        }
    }

    void VectorRenderer::ApplyStroke(const StrokeData &stroke, const Rect2Dd &bounds, float opacity) {
        const float strokeOpacity = opacity * stroke.Opacity;
        if (silhouetteMode) ctx->SetStrokePaint(Colors::Black);
        else if (auto *c = std::get_if<Color>(&stroke.Fill)) ctx->SetStrokePaint(WithOpacity(*c, strokeOpacity));
        else if (auto *g = std::get_if<GradientData>(&stroke.Fill)) SetupGradient(*g, bounds, strokeOpacity, true);
        else if (auto *id = std::get_if<std::string>(&stroke.Fill)) {
            if (currentDocument)
                if (auto d = currentDocument->GetDefinition(*id))
                    if (auto *gd = dynamic_cast<VectorGradient *>(d.get()))
                        SetupGradient(gd->Data, bounds, strokeOpacity, true);
        }
        ctx->SetStrokeWidth(stroke.Width);
        LineCap cap = stroke.LineCap == StrokeLineCap::Round ? LineCap::Round : (stroke.LineCap == StrokeLineCap::Square
                                                                                 ? LineCap::Square : LineCap::Butt);
        LineJoin join =
                stroke.LineJoin == StrokeLineJoin::Round ? LineJoin::Round : (stroke.LineJoin == StrokeLineJoin::Bevel
                                                                              ? LineJoin::Bevel : LineJoin::Miter);
        ctx->SetLineCap(cap);
        ctx->SetLineJoin(join);
        ctx->SetMiterLimit(stroke.MiterLimit);
        if (!stroke.DashArray.empty()) ctx->SetLineDash(UCDashPattern(stroke.DashArray, stroke.DashOffset));
    }

    void VectorRenderer::ApplyTransform(const Matrix3x3 &t) {
        ctx->Transform(t.m[0][0], t.m[1][0], t.m[0][1], t.m[1][1], t.m[0][2], t.m[1][2]);
    }

    void VectorRenderer::SetupGradient(const GradientData &gradient, const Rect2Dd &bounds, float opacity,
                                       bool forStroke) {
        std::shared_ptr<IPaintPattern> pattern;
        if (auto *l = std::get_if<LinearGradientData>(&gradient)) pattern = MakeLinearGradient(*l, bounds, opacity);
        else if (auto *r = std::get_if<RadialGradientData>(&gradient)) pattern = MakeRadialGradient(*r, bounds, opacity);
        else if (auto *c = std::get_if<ConicalGradientData>(&gradient)) {
            // No conic pattern in the render context yet: the average of the
            // stops is the honest flat substitute, and better than nothing.
            Color avg = Colors::Black;
            if (!c->Stops.empty()) {
                double r = 0, g = 0, b = 0, a = 0;
                for (const auto &s: c->Stops) { r += s.color.r; g += s.color.g; b += s.color.b; a += s.color.a; }
                double n = static_cast<double>(c->Stops.size());
                avg = Color(static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n),
                            static_cast<uint8_t>(b / n), static_cast<uint8_t>(a / n));
            }
            if (forStroke) ctx->SetStrokePaint(WithOpacity(avg, opacity));
            else ctx->SetFillPaint(WithOpacity(avg, opacity));
            return;
        }
        if (!pattern) return;
        if (forStroke) ctx->SetStrokePaint(pattern);
        else ctx->SetFillPaint(pattern);
    }

    std::shared_ptr<IPaintPattern> VectorRenderer::MakeLinearGradient(const LinearGradientData &g, const Rect2Dd &b,
                                                                      float opacity) {
        Point2Dd start, end;
        if (g.Units == GradientUnits::ObjectBoundingBox) {
            start = {b.x + g.Start.x * b.width, b.y + g.Start.y * b.height};
            end = {b.x + g.End.x * b.width, b.y + g.End.y * b.height};
        } else {
            start = g.Start;
            end = g.End;
        }
        return ctx->CreateLinearGradientPattern(start.x, start.y, end.x, end.y, StopsWithOpacity(g.Stops, opacity));
    }

    std::shared_ptr<IPaintPattern> VectorRenderer::MakeRadialGradient(const RadialGradientData &g, const Rect2Dd &b,
                                                                      float opacity) {
        Point2Dd center, focal;
        double radius, focalRadius;
        if (g.Units == GradientUnits::ObjectBoundingBox) {
            center = {b.x + g.Center.x * b.width, b.y + g.Center.y * b.height};
            focal = {b.x + g.FocalPoint.x * b.width, b.y + g.FocalPoint.y * b.height};
            const double k = std::max(b.width, b.height);
            radius = g.Radius * k;
            focalRadius = g.FocalRadius * k;
        } else {
            center = g.Center;
            focal = g.FocalPoint;
            radius = g.Radius;
            focalRadius = g.FocalRadius;
        }
        return ctx->CreateRadialGradientPattern(focal.x, focal.y, focalRadius, center.x, center.y, radius,
                                                StopsWithOpacity(g.Stops, opacity));
    }

    // Every command kind - relative forms, H/V lines, smooth cubics and
    // quadratics, SVG arcs - is normalised by PathOps to absolute move /
    // line / cubic segments, so nothing is approximated here.
    void VectorRenderer::BuildPath(const PathData &pathData) {
        const auto segs = VectorConverter::PathOps::NormalizePath(pathData);
        stats.PathCommandsProcessed += static_cast<uint32_t>(pathData.commands.size());
        for (const auto &s: segs) {
            switch (s.kind) {
                case VectorConverter::PathOps::FlatSeg::Move:
                    ctx->MoveTo(s.p[0].x, s.p[0].y);
                    break;
                case VectorConverter::PathOps::FlatSeg::Line:
                    ctx->LineTo(s.p[0].x, s.p[0].y);
                    break;
                case VectorConverter::PathOps::FlatSeg::Cubic:
                    ctx->BezierCurveTo(s.p[0].x, s.p[0].y, s.p[1].x, s.p[1].y, s.p[2].x, s.p[2].y);
                    break;
            }
            if (s.closeAfter) ctx->ClosePath();
        }
    }


    // ===========================================================================
    // EFFECTS: shadow, feather, transparency ramps and mixes
    // ===========================================================================

    namespace {
        void HashMix(size_t &h, size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); }
        void HashDouble(size_t &h, double d) { HashMix(h, std::hash<double>()(d)); }
        void HashRect(size_t &h, const Rect2Dd &r) {
            HashDouble(h, r.x); HashDouble(h, r.y); HashDouble(h, r.width); HashDouble(h, r.height);
        }

        // A fingerprint of what an element's silhouette depends on, so a
        // cached raster is reused across frames and replaced after an edit.
        size_t GeometryHash(const VectorElement &e) {
            size_t h = static_cast<size_t>(e.Type) * 31 + 7;
            HashRect(h, e.GetBoundingBox());
            if (e.Transform.has_value())
                for (int r = 0; r < 2; ++r)
                    for (int c = 0; c < 3; ++c) HashDouble(h, e.Transform->m[r][c]);
            HashMix(h, e.Style.Fill.has_value() ? 1u : 0u);
            if (e.Style.Stroke.has_value()) {
                HashDouble(h, e.Style.Stroke->Width);
                HashMix(h, e.Style.Stroke->WidthProfile.size());
                HashMix(h, static_cast<size_t>(e.Style.Stroke->StartArrow.Kind) * 8 +
                           static_cast<size_t>(e.Style.Stroke->EndArrow.Kind));
            }
            switch (e.Type) {
                case VectorElementType::Path:
                    for (const auto &c: static_cast<const VectorPath &>(e).Path.commands) {
                        HashMix(h, static_cast<size_t>(c.Type));
                        for (float v: c.Parameters) HashDouble(h, v);
                    }
                    break;
                case VectorElementType::Polyline:
                    for (const auto &p: static_cast<const VectorPolyline &>(e).Points) { HashDouble(h, p.x); HashDouble(h, p.y); }
                    break;
                case VectorElementType::Polygon:
                    for (const auto &p: static_cast<const VectorPolygon &>(e).Points) { HashDouble(h, p.x); HashDouble(h, p.y); }
                    break;
                case VectorElementType::Text: {
                    const auto &t = static_cast<const VectorText &>(e);
                    HashDouble(h, t.Position.x); HashDouble(h, t.Position.y);
                    for (const auto &sp: t.Spans) { HashMix(h, std::hash<std::string>()(sp.Text)); HashDouble(h, sp.Style.FontSize); }
                    break;
                }
                case VectorElementType::Group:
                case VectorElementType::Symbol:
                case VectorElementType::Layer:
                    for (const auto &c: static_cast<const VectorGroup &>(e).Children) if (c) HashMix(h, GeometryHash(*c));
                    break;
                default:
                    break;
            }
            return h;
        }

        // Three box passes approximate a gaussian of `sigma` pixels on the
        // alpha of a premultiplied ARGB32 buffer whose colour is black, so
        // alpha is the whole pixel. Outside the buffer counts as clear.
        void BlurAlpha(uint32_t *px, int w, int h, double sigma) {
            if (!px || w <= 0 || h <= 0 || sigma < 0.3) return;
            const int r = std::max(1, static_cast<int>(std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)));
            const size_t n = static_cast<size_t>(w) * h;
            std::vector<float> a(n), tmp(n);
            for (size_t i = 0; i < n; ++i) a[i] = static_cast<float>(px[i] >> 24) / 255.0f;
            const float norm = 1.0f / (2 * r + 1);
            for (int pass = 0; pass < 3; ++pass) {
                for (int y = 0; y < h; ++y) {                 // horizontal
                    const float *row = &a[static_cast<size_t>(y) * w];
                    float *out = &tmp[static_cast<size_t>(y) * w];
                    float sum = 0;
                    for (int x = 0; x <= std::min(r, w - 1); ++x) sum += row[x];
                    for (int x = 0; x < w; ++x) {
                        out[x] = sum * norm;
                        const int add = x + r + 1, sub = x - r;
                        if (add < w) sum += row[add];
                        if (sub >= 0) sum -= row[sub];
                    }
                }
                for (int x = 0; x < w; ++x) {                 // vertical
                    float sum = 0;
                    for (int y = 0; y <= std::min(r, h - 1); ++y) sum += tmp[static_cast<size_t>(y) * w + x];
                    for (int y = 0; y < h; ++y) {
                        a[static_cast<size_t>(y) * w + x] = sum * norm;
                        const int add = y + r + 1, sub = y - r;
                        if (add < h) sum += tmp[static_cast<size_t>(add) * w + x];
                        if (sub >= 0) sum -= tmp[static_cast<size_t>(sub) * w + x];
                    }
                }
            }
            for (size_t i = 0; i < n; ++i) {
                const float v = std::min(1.0f, std::max(0.0f, a[i]));
                px[i] = static_cast<uint32_t>(std::lround(v * 255.0f)) << 24;
            }
        }

        UltraCanvas::BlendMode ToBlendMode(TransparencyMix mix) {
            switch (mix) {
                case TransparencyMix::StainedGlass: return UltraCanvas::BlendMode::Multiply;
                case TransparencyMix::Bleach: return UltraCanvas::BlendMode::Screen;
                case TransparencyMix::Contrast: return UltraCanvas::BlendMode::Overlay;
                case TransparencyMix::Saturation: return UltraCanvas::BlendMode::Saturation;
                case TransparencyMix::Darken: return UltraCanvas::BlendMode::Darken;
                case TransparencyMix::Lighten: return UltraCanvas::BlendMode::Lighten;
                case TransparencyMix::Brightness: return UltraCanvas::BlendMode::HardLight;
                case TransparencyMix::Luminosity: return UltraCanvas::BlendMode::Luminosity;
                case TransparencyMix::Hue: return UltraCanvas::BlendMode::Hue;
                case TransparencyMix::Mix:
                default: return UltraCanvas::BlendMode::Normal;
            }
        }

        // The widest stroke the element or its children draw, for the
        // raster's padding.
        double StrokePadOf(const VectorElement &e) {
            double pad = 0;
            if (e.Style.Stroke.has_value()) {
                pad = e.Style.Stroke->Width;
                if (e.Style.Stroke->HasArrowheads()) pad *= 4.0 * std::max(e.Style.Stroke->StartArrow.Scale, e.Style.Stroke->EndArrow.Scale);
            }
            if (e.Type == VectorElementType::Group || e.Type == VectorElementType::Layer || e.Type == VectorElementType::Symbol)
                for (const auto &c: static_cast<const VectorGroup &>(e).Children) if (c) pad = std::max(pad, StrokePadOf(*c));
            return pad;
        }
    }

    void VectorRenderer::RenderWithEffects(const VectorElement &element) {
        const TransparencyData *t = element.Style.Transparency.has_value() ? &*element.Style.Transparency : nullptr;
        const bool tGroup = t && (t->IsGradient() || t->Mix != TransparencyMix::Mix || t->Level > 0.0f);
        const bool feather = element.Effects.Feather.has_value() && element.Effects.Feather->Radius > 0;

        if (element.Effects.Shadow.has_value()) RenderShadow(element, *element.Effects.Shadow);

        // The mix is the operator the finished group is painted with, so it
        // has to be in force before the group is pushed (popping the group
        // restores it); inside the group the parts composite normally.
        if (tGroup) {
            ctx->SetBlendMode(ToBlendMode(t->Mix));
            ctx->BeginGroup();
            ctx->SetBlendMode(UltraCanvas::BlendMode::Normal);
        }
        if (feather) ctx->BeginGroup();

        DrawElementBody(element);

        if (feather) {
            const EffectRaster *r = SilhouetteOf(element, element.Effects.Feather->Radius);
            std::shared_ptr<IPaintPattern> mask;
            if (r && r->alpha) mask = ctx->CreatePixmapPattern(*r->alpha, r->rect, PatternExtend::NoExtend);
            ctx->EndGroupMasked(mask);
        }
        if (tGroup) {
            auto mask = TransparencyMask(*t);
            if (mask) ctx->EndGroupMasked(mask);
            else ctx->EndGroup(1.0 - std::min(1.0f, std::max(0.0f, t->Level)));
            ctx->SetBlendMode(UltraCanvas::BlendMode::Normal);
        }
    }

    const VectorRenderer::EffectRaster *VectorRenderer::SilhouetteOf(const VectorElement &element, float blur) {
        const Rect2Dd box = element.GetBoundingBox();
        if (box.width < 0 || box.height < 0) return nullptr;
        blur = std::max(0.0f, blur);

        // Device pixels per element unit under the current transform; the
        // raster is isotropic at the larger axis and capped in size.
        const Point2Dd o = ctx->UserToDevice(Point2Dd(0, 0));
        const Point2Dd ux = ctx->UserToDevice(Point2Dd(1, 0));
        const Point2Dd uy = ctx->UserToDevice(Point2Dd(0, 1));
        double s = std::max(std::hypot(ux.x - o.x, ux.y - o.y), std::hypot(uy.x - o.x, uy.y - o.y));
        if (!(s > 1e-6) || !std::isfinite(s)) s = 1.0;
        const double padU = blur * 2.0 + StrokePadOf(element) + 2.0 / s;
        const double fullW = box.width + 2 * padU, fullH = box.height + 2 * padU;
        constexpr double kMaxPixels = 4096.0;
        if (fullW * s > kMaxPixels) s = kMaxPixels / fullW;
        if (fullH * s > kMaxPixels) s = kMaxPixels / fullH;
        const int w = std::max(1, static_cast<int>(std::ceil(fullW * s)));
        const int h = std::max(1, static_cast<int>(std::ceil(fullH * s)));

        size_t key = GeometryHash(element);
        HashDouble(key, s);
        HashDouble(key, blur);
        auto it = effectCache.find(&element);
        if (it != effectCache.end() && it->second.key == key && it->second.alpha) return &it->second;

        std::unique_ptr<IRenderContext> off = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!off) return nullptr;
        off->Clear(Color(0, 0, 0, 0));
        off->Translate(padU * s, padU * s);
        off->Scale(s, s);
        off->Translate(-box.x, -box.y);
        {
            IRenderContext *savedCtx = ctx;
            const float savedOpacity = currentOpacity;
            const bool savedMode = silhouetteMode;
            const VectorRenderOptions savedOptions = options;
            ctx = off.get();
            silhouetteMode = true;
            currentOpacity = 1.0f;
            options.EnableCulling = false;
            options.ShowBoundingBoxes = false;
            ctx->SetAlpha(1.0);
            DrawElementBody(element);
            ctx = savedCtx;
            silhouetteMode = savedMode;
            currentOpacity = savedOpacity;
            options = savedOptions;
        }
        auto pm = std::make_shared<UCPixmap>();
        if (!pm->Init(w, h)) return nullptr;
        off->FlushToSurface(pm->GetSurface(), Point2Dd(0, 0));
        pm->MarkDirty();
        pm->Flush();
        // The penumbra spans about 3.3 sigma (5 % to 95 % of the ramp).
        BlurAlpha(pm->GetPixelData(), w, h, blur * s / 3.3);
        pm->MarkDirty();

        if (effectCache.size() > 256) effectCache.clear();
        EffectRaster &r = effectCache[&element];
        r.alpha = pm;
        r.key = key;
        r.rect = Rect2Dd(box.x - padU, box.y - padU, w / s, h / s);
        return &r;
    }

    void VectorRenderer::RenderShadow(const VectorElement &element, const ShadowEffect &sh) {
        if (sh.Darkness <= 0) return;
        const EffectRaster *r = SilhouetteOf(element, sh.Blur);
        if (!r || !r->alpha) return;
        Color c = sh.Colour;
        c.a = static_cast<uint8_t>(std::lround(std::min(1.0f, std::max(0.0f, sh.Darkness)) * 255.0f));
        ctx->PushState();
        ctx->SetImageSmoothing(true);
        switch (sh.Kind) {
            case ShadowKind::Glow:
                ctx->DrawMask(c, *r->alpha, r->rect, ImageFitMode::Fill);
                break;
            case ShadowKind::Floor: {
                // Squash toward the element's bottom edge and shear sideways:
                // x' = x + shear * (bottom - y), y' = bottom - squash * (bottom - y).
                const Rect2Dd box = element.GetBoundingBox();
                const double bottom = box.y + box.height;
                ctx->Translate(sh.Offset.x, bottom);
                ctx->Transform(1.0, 0.0, -sh.FloorShear, std::max(0.05f, sh.FloorSquash), 0.0, 0.0);
                ctx->Translate(0.0, -bottom);
                ctx->DrawMask(c, *r->alpha, r->rect, ImageFitMode::Fill);
                break;
            }
            case ShadowKind::Wall:
            default:
                ctx->DrawMask(c, *r->alpha,
                              Rect2Dd(r->rect.x + sh.Offset.x, r->rect.y + sh.Offset.y, r->rect.width, r->rect.height),
                              ImageFitMode::Fill);
                break;
        }
        ctx->PopState();
    }

    std::shared_ptr<IPaintPattern> VectorRenderer::TransparencyMask(const TransparencyData &t) {
        if (!t.IsGradient()) return nullptr;
        std::vector<GradientStop> stops;
        stops.reserve(t.Stops.size());
        for (const auto &st: t.Stops) {
            const float alpha = 1.0f - std::min(1.0f, std::max(0.0f, st.Level));
            stops.emplace_back(st.Position, Color(0, 0, 0, static_cast<uint8_t>(std::lround(alpha * 255.0f))));
        }
        switch (t.Shape) {
            case TransparencyShape::Linear:
                return ctx->CreateLinearGradientPattern(t.Start.x, t.Start.y, t.End.x, t.End.y, stops);
            case TransparencyShape::Radial: {
                const double r = std::max(1e-3, std::hypot(t.End.x - t.Start.x, t.End.y - t.Start.y));
                return ctx->CreateRadialGradientPattern(t.Start.x, t.Start.y, 0.0, t.Start.x, t.Start.y, r, stops);
            }
            case TransparencyShape::Conical: {
                const double a0 = std::atan2(t.End.y - t.Start.y, t.End.x - t.Start.x);
                return ctx->CreateConicGradientPattern(t.Start.x, t.Start.y, a0, a0 + 2.0 * M_PI, stops);
            }
            case TransparencyShape::Flat:
            default:
                return nullptr;
        }
    }

    // ===========================================================================
    // LINE GALLERY: arrowheads, variable width, brushes
    // ===========================================================================

    namespace {
        struct FlatPolyline {
            std::vector<Point2Dd> pts;
            bool closed = false;
        };

        Point2Dd Unit(const Point2Dd &v) {
            const double l = std::hypot(v.x, v.y);
            return l > 1e-12 ? Point2Dd(v.x / l, v.y / l) : Point2Dd(1, 0);
        }

        // Flattens path data to polylines in element units, one per
        // subpath, cubics subdivided by their control-polygon length.
        std::vector<FlatPolyline> FlattenOutline(const PathData &pd) {
            using namespace VectorConverter::PathOps;
            std::vector<FlatPolyline> out;
            Point2Dd cur{0, 0};
            for (const auto &s: NormalizePath(pd)) {
                switch (s.kind) {
                    case FlatSeg::Move:
                        out.push_back({});
                        out.back().pts.push_back(s.p[0]);
                        cur = s.p[0];
                        break;
                    case FlatSeg::Line:
                        if (out.empty()) { out.push_back({}); out.back().pts.push_back(cur); }
                        out.back().pts.push_back(s.p[0]);
                        cur = s.p[0];
                        break;
                    case FlatSeg::Cubic: {
                        if (out.empty()) { out.push_back({}); out.back().pts.push_back(cur); }
                        const double len = std::hypot(s.p[0].x - cur.x, s.p[0].y - cur.y) +
                                           std::hypot(s.p[1].x - s.p[0].x, s.p[1].y - s.p[0].y) +
                                           std::hypot(s.p[2].x - s.p[1].x, s.p[2].y - s.p[1].y);
                        const int n = std::min(64, std::max(4, static_cast<int>(std::ceil(len / 3.0))));
                        for (int i = 1; i <= n; ++i) {
                            const double u = static_cast<double>(i) / n, v = 1.0 - u;
                            out.back().pts.emplace_back(
                                    v * v * v * cur.x + 3 * v * v * u * s.p[0].x + 3 * v * u * u * s.p[1].x + u * u * u * s.p[2].x,
                                    v * v * v * cur.y + 3 * v * v * u * s.p[0].y + 3 * v * u * u * s.p[1].y + u * u * u * s.p[2].y);
                        }
                        cur = s.p[2];
                        break;
                    }
                }
                if (s.closeAfter && !out.empty()) {
                    out.back().closed = true;
                    if (!out.back().pts.empty()) cur = out.back().pts.front();
                }
            }
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [](const FlatPolyline &l) { return l.pts.size() < 2; }), out.end());
            return out;
        }

        std::vector<double> CumulativeLengths(const std::vector<Point2Dd> &pts) {
            std::vector<double> cum(pts.size(), 0.0);
            for (size_t i = 1; i < pts.size(); ++i)
                cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
            return cum;
        }
    }

    void VectorRenderer::RenderLineGallery(const VectorElement &element, const StrokeData &stroke,
                                           const Rect2Dd &bounds, float opacity) {
        PathData pd;
        if (!BuildOutlinePath(element, pd)) return;
        const auto lines = FlattenOutline(pd);
        if (lines.empty()) return;
        SetGalleryPaint(stroke, bounds, opacity);
        if (stroke.HasWidthProfile()) {
            for (const auto &l: lines) FillVariableWidth(l.pts, l.closed, stroke);
        } else if (stroke.HasBrush()) {
            for (const auto &l: lines) StampBrush(l.pts, stroke);
        }
        if (stroke.HasArrowheads()) {
            SetGalleryPaint(stroke, bounds, opacity);
            const FlatPolyline &first = lines.front();
            const FlatPolyline &last = lines.back();
            if (stroke.StartArrow.IsSet() && !first.closed && first.pts.size() >= 2) {
                size_t k = 1;
                while (k + 1 < first.pts.size() && std::hypot(first.pts[k].x - first.pts[0].x, first.pts[k].y - first.pts[0].y) < 1e-9) ++k;
                DrawArrowhead(stroke.StartArrow, first.pts[0],
                              Unit(Point2Dd(first.pts[0].x - first.pts[k].x, first.pts[0].y - first.pts[k].y)), stroke);
            }
            if (stroke.EndArrow.IsSet() && !last.closed && last.pts.size() >= 2) {
                const size_t n = last.pts.size();
                size_t k = n - 2;
                while (k > 0 && std::hypot(last.pts[n - 1].x - last.pts[k].x, last.pts[n - 1].y - last.pts[k].y) < 1e-9) --k;
                DrawArrowhead(stroke.EndArrow, last.pts[n - 1],
                              Unit(Point2Dd(last.pts[n - 1].x - last.pts[k].x, last.pts[n - 1].y - last.pts[k].y)), stroke);
            }
        }
    }

    // The stroke's paint as both fill and stroke source, for the filled
    // decorations and the open ones.
    void VectorRenderer::SetGalleryPaint(const StrokeData &stroke, const Rect2Dd &bounds, float opacity) {
        ctx->SetLineDash(UCDashPattern());
        ctx->SetStrokeWidth(stroke.Width);
        ctx->SetLineCap(LineCap::Round);
        ctx->SetLineJoin(LineJoin::Round);
        if (silhouetteMode) {
            ctx->SetFillPaint(Colors::Black);
            ctx->SetStrokePaint(Colors::Black);
            return;
        }
        const float a = opacity * stroke.Opacity;
        if (auto *g = std::get_if<GradientData>(&stroke.Fill)) {
            SetupGradient(*g, bounds, a, false);
            SetupGradient(*g, bounds, a, true);
            return;
        }
        Color c = Colors::Black;
        if (auto *col = std::get_if<Color>(&stroke.Fill)) c = *col;
        ctx->SetFillPaint(WithOpacity(c, a));
        ctx->SetStrokePaint(WithOpacity(c, a));
    }

    void VectorRenderer::DrawArrowhead(const ArrowheadData &arrow, const Point2Dd &tip, const Point2Dd &d,
                                       const StrokeData &stroke) {
        const double W = std::max(0.5f, stroke.Width);
        const double L = 4.0 * W * arrow.Scale, H = 2.0 * W * arrow.Scale;
        const Point2Dd n(-d.y, d.x);
        // `along` back from the tip, `across` to the side.
        auto P = [&](double along, double across) {
            return Point2Dd(tip.x - d.x * along + n.x * across, tip.y - d.y * along + n.y * across);
        };
        auto poly = [&](std::initializer_list<Point2Dd> pts) {
            bool first = true;
            for (const auto &p: pts) {
                if (first) ctx->MoveTo(p.x, p.y); else ctx->LineTo(p.x, p.y);
                first = false;
            }
            ctx->ClosePath();
        };
        ctx->ClearPath();
        switch (arrow.Kind) {
            case ArrowheadKind::Triangle:
                poly({tip, P(L, H / 2), P(L, -H / 2)});
                ctx->FillPathPreserve();
                break;
            case ArrowheadKind::OpenArrow: {
                const Point2Dd a = P(L, H / 2), b = P(L, -H / 2);
                ctx->MoveTo(a.x, a.y);
                ctx->LineTo(tip.x, tip.y);
                ctx->LineTo(b.x, b.y);
                ctx->StrokePathPreserve();
                break;
            }
            case ArrowheadKind::Circle: {
                const Point2Dd c = P(H / 2, 0);
                ctx->Circle(c.x, c.y, H / 2);
                ctx->FillPathPreserve();
                break;
            }
            case ArrowheadKind::Square:
                poly({P(0, H / 2), P(H, H / 2), P(H, -H / 2), P(0, -H / 2)});
                ctx->FillPathPreserve();
                break;
            case ArrowheadKind::Diamond:
                poly({tip, P(L / 2, H / 2), P(L, 0), P(L / 2, -H / 2)});
                ctx->FillPathPreserve();
                break;
            case ArrowheadKind::Bar: {
                const Point2Dd a = P(0, H / 2), b = P(0, -H / 2);
                ctx->MoveTo(a.x, a.y);
                ctx->LineTo(b.x, b.y);
                ctx->StrokePathPreserve();
                break;
            }
            case ArrowheadKind::NoArrowhead:
            default:
                break;
        }
        ctx->ClearPath();
    }

    // The band between the left and right offsets of the polyline, its
    // half-width following the profile; a closed polyline gives a ring.
    void VectorRenderer::FillVariableWidth(const std::vector<Point2Dd> &input, bool closed, const StrokeData &stroke) {
        std::vector<Point2Dd> pts = input;
        if (closed && pts.size() > 2 &&
            std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) < 1e-9)
            pts.pop_back();
        const size_t n = pts.size();
        if (n < 2) return;
        const std::vector<double> cum = CumulativeLengths(pts);
        const double total = closed ? cum.back() + std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y)
                                    : cum.back();
        if (total <= 1e-9) return;
        std::vector<Point2Dd> left(n), right(n);
        Point2Dd lastT(1, 0);
        for (size_t i = 0; i < n; ++i) {
            const Point2Dd &prev = i > 0 ? pts[i - 1] : (closed ? pts[n - 1] : pts[i]);
            const Point2Dd &next = i + 1 < n ? pts[i + 1] : (closed ? pts[0] : pts[i]);
            Point2Dd t(next.x - prev.x, next.y - prev.y);
            if (std::hypot(t.x, t.y) < 1e-12) t = lastT; else t = Unit(t);
            lastT = t;
            const double half = stroke.WidthAt(static_cast<float>(cum[i] / total)) / 2.0;
            left[i] = Point2Dd(pts[i].x - t.y * half, pts[i].y + t.x * half);
            right[i] = Point2Dd(pts[i].x + t.y * half, pts[i].y - t.x * half);
        }
        ctx->ClearPath();
        if (closed) {
            ctx->MoveTo(left[0].x, left[0].y);
            for (size_t i = 1; i < n; ++i) ctx->LineTo(left[i].x, left[i].y);
            ctx->ClosePath();
            ctx->MoveTo(right[0].x, right[0].y);
            for (size_t i = 1; i < n; ++i) ctx->LineTo(right[i].x, right[i].y);
            ctx->ClosePath();
            ctx->SetFillRule(UltraCanvas::FillRule::EvenOdd);
        } else {
            ctx->MoveTo(left[0].x, left[0].y);
            for (size_t i = 1; i < n; ++i) ctx->LineTo(left[i].x, left[i].y);
            for (size_t i = n; i-- > 0;) ctx->LineTo(right[i].x, right[i].y);
            ctx->ClosePath();
            ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
        }
        ctx->FillPathPreserve();
        ctx->ClearPath();
        ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
    }

    void VectorRenderer::StampBrush(const std::vector<Point2Dd> &pts, const StrokeData &stroke) {
        const BrushData &b = *stroke.Brush;
        if (!b.Stamp || pts.size() < 2) return;
        const Rect2Dd sb = b.Stamp->GetBoundingBox();
        if (sb.width <= 0 || sb.height <= 0) return;
        const double k = (std::max(0.5f, stroke.Width) * std::max(0.01f, b.Scale)) / sb.height;
        const double step = std::max(0.25, sb.width * k * std::max(0.05f, b.Spacing));
        const std::vector<double> cum = CumulativeLengths(pts);
        const double total = cum.back();
        if (total <= 1e-9) return;
        const VectorRenderOptions savedOptions = options;
        options.EnableCulling = false;
        size_t seg = 1;
        int stamps = 0;
        for (double dist = 0; dist <= total + 1e-9 && stamps < 4000; dist += step, ++stamps) {
            while (seg + 1 < pts.size() && cum[seg] < dist) ++seg;
            const double segLen = cum[seg] - cum[seg - 1];
            const double u = segLen > 1e-12 ? std::min(1.0, std::max(0.0, (dist - cum[seg - 1]) / segLen)) : 0.0;
            const Point2Dd p(pts[seg - 1].x + (pts[seg].x - pts[seg - 1].x) * u,
                             pts[seg - 1].y + (pts[seg].y - pts[seg - 1].y) * u);
            const Point2Dd t = Unit(Point2Dd(pts[seg].x - pts[seg - 1].x, pts[seg].y - pts[seg - 1].y));
            ctx->PushState();
            ctx->Translate(p.x, p.y);
            if (b.Rotate) ctx->Rotate(std::atan2(t.y, t.x));
            ctx->Scale(k, k);
            ctx->Translate(-(sb.x + sb.width / 2), -(sb.y + sb.height / 2));
            RenderElement(ctx, *b.Stamp);
            ctx->PopState();
        }
        options = savedOptions;
    }

    bool VectorRenderer::IsVisible(const VectorElement &e) const {
        return e.Style.Visible && e.Style.Display && e.Style.Opacity > 0;
    }

    bool VectorRenderer::IsInViewport(const Rect2Dd &b) const {
        if (options.ViewportBounds.width <= 0 || options.ViewportBounds.height <= 0) return true;
        if (EmptyBox(b)) return true;   // an element without bounds (a group of definitions) is never culled
        return !(b.x + b.width < options.ViewportBounds.x || b.y + b.height < options.ViewportBounds.y ||
                 b.x > options.ViewportBounds.x + options.ViewportBounds.width ||
                 b.y > options.ViewportBounds.y + options.ViewportBounds.height);
    }

    void VectorRenderer::RenderDebugBounds(const Rect2Dd &b) {
        ctx->PushState();
        ctx->SetStrokePaint(options.DebugColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(b);
        ctx->PopState();
    }

    void VectorRenderer::ClearCaches() { effectCache.clear(); }

    bool BuildVectorElementOutline(IRenderContext *context, const VectorElement &element) {
        if (!context) return false;
        VectorRenderer r;
        r.ctx = context;
        return r.BuildElementPath(element);
    }

    // `p` is in the coordinate space the element's bounding box is expressed
    // in: the parent's space (the box already includes the element's own
    // Transform). An element without bounds never hits.
    bool HitTestElement(const VectorElement &e, const Point2Dd &p) {
        Rect2Dd b = e.GetBoundingBox();
        if (EmptyBox(b)) return false;
        return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
    }

    // The document point is carried down the tree through the inverse of each
    // group's Transform, so children of a transformed group (a block insert,
    // a mirrored entity) are tested in the space their geometry is stored in.
    std::vector<const VectorElement *> HitTestDocument(const VectorDocument &doc, const Point2Dd &pt) {
        std::vector<const VectorElement *> hits;
        std::function<void(const VectorGroup &, const Point2Dd &)> test =
                [&](const VectorGroup &g, const Point2Dd &local) {
            for (auto it = g.Children.rbegin(); it != g.Children.rend(); ++it) {
                if (!*it || !(*it)->Style.Visible) continue;
                if (HitTestElement(**it, local)) hits.push_back(it->get());
                if (auto *gg = dynamic_cast<const VectorGroup *>(it->get())) {
                    Point2Dd inner = gg->Transform.has_value()
                                     ? gg->Transform->Inverse().Transform(local) : local;
                    test(*gg, inner);
                }
            }
        };
        for (auto it = doc.Layers.rbegin(); it != doc.Layers.rend(); ++it) {
            if (!(*it)->Visible) continue;
            Point2Dd local = (*it)->Transform.has_value()
                             ? (*it)->Transform->Inverse().Transform(pt) : pt;
            test(**it, local);
        }
        return hits;
    }

    Rect2Dd CalculateDocumentBounds(const VectorDocument &doc) { return doc.GetBoundingBox(); }

} // namespace UltraCanvas
