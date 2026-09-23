// UltraCanvasVectorRenderer.cpp
// Vector Graphics Rendering for UltraCanvas
// Version: 2.1.1
// Last Modified: 2026-09-22
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
#include "DataFormats/UltraCanvasVectorGeometry.h"
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

        // A transform that collapses the plane onto a line or a point. It
        // arrives from readers rather than from bad arithmetic here: a DWG
        // block standing in a vertical plane projects to plan view with one
        // axis scaled to zero (the bathroom sample's 'cornice' layer does
        // exactly this). Such an element has no area to draw, and handing the
        // matrix to the backend is worse than skipping it - Cairo latches a
        // non-invertible matrix as a permanent error on the context, after
        // which NOTHING renders: the rest of the drawing, the rest of the
        // page, and every later frame drawn into it.
        bool Singular(const Matrix3x3 &m) {
            return !(std::fabs(m.Determinant()) > 1e-12);   // false for NaN too
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
        if (element.Transform.has_value() && Singular(element.Transform.value())) {
            stats.ElementsCulled++;
            return;
        }

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
        else {
            // A silhouette (for a shadow or feather) takes an outward
            // contour's outermost ring with it.
            if (silhouetteMode && element.Effects.Contour.has_value() && element.Effects.Contour->Width > 0)
                RenderContour(element, *element.Effects.Contour, true);
            DrawElementBody(element);
        }

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
            case VectorElementType::ClipView:
                RenderClipView(static_cast<const VectorClipView &>(element));
                break;
            case VectorElementType::Blend:
                RenderBlend(static_cast<const VectorBlend &>(element));
                break;
            case VectorElementType::Mould:
                RenderMould(static_cast<const VectorMould &>(element));
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
                case VectorElementType::Mould:
                    for (const auto &c: static_cast<const VectorMould &>(e).Shape.commands)
                        for (float v: c.Parameters) HashDouble(h, v);
                    [[fallthrough]];
                case VectorElementType::Group:
                case VectorElementType::Symbol:
                case VectorElementType::Layer:
                case VectorElementType::ClipView:
                case VectorElementType::Blend:
                    for (const auto &c: static_cast<const VectorGroup &>(e).Children) if (c) HashMix(h, GeometryHash(*c));
                    break;
                default:
                    break;
            }
            if (e.Effects.Contour.has_value()) { HashDouble(h, e.Effects.Contour->Width); HashMix(h, e.Effects.Contour->Steps); }
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
            if (IsGroupType(e.Type))
                for (const auto &c: static_cast<const VectorGroup &>(e).Children) if (c) pad = std::max(pad, StrokePadOf(*c));
            if (e.Effects.Contour.has_value() && e.Effects.Contour->Width > 0) pad += e.Effects.Contour->Width;
            if (e.Effects.Bevel.has_value() && e.Effects.Bevel->Outer) pad += e.Effects.Bevel->Indent;
            return pad;
        }

        // The colour an element paints with, for colour runs: a flat fill,
        // a gradient's first stop, else the stroke's colour, else black.
        Color FlatColourOf(const VectorStyle &style) {
            if (style.Fill.has_value()) {
                if (auto *c = std::get_if<Color>(&*style.Fill)) return *c;
                if (auto *g = std::get_if<GradientData>(&*style.Fill)) {
                    if (auto *l = std::get_if<LinearGradientData>(g)) { if (!l->Stops.empty()) return l->Stops.front().color; }
                    else if (auto *r = std::get_if<RadialGradientData>(g)) { if (!r->Stops.empty()) return r->Stops.front().color; }
                }
            }
            if (style.Stroke.has_value())
                if (auto *c = std::get_if<Color>(&style.Stroke->Fill)) return *c;
            return Color(0, 0, 0, 255);
        }

        void RgbToHsv(const Color &c, double &h, double &s, double &v) {
            const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
            const double mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
            v = mx;
            const double d = mx - mn;
            s = mx > 0 ? d / mx : 0;
            if (d < 1e-9) { h = 0; return; }
            if (mx == r) h = std::fmod((g - b) / d, 6.0);
            else if (mx == g) h = (b - r) / d + 2;
            else h = (r - g) / d + 4;
            h *= 60;
            if (h < 0) h += 360;
        }
        Color HsvToRgb(double h, double s, double v, uint8_t a) {
            h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
            const double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
            double r = 0, g = 0, b = 0;
            if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
            else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
            auto ch = [](double q) { return static_cast<uint8_t>(std::lround(std::min(1.0, std::max(0.0, q)) * 255.0)); };
            return Color(ch(r + m), ch(g + m), ch(b + m), a);
        }

        // The colour `t` of the way from `a` to `b` along the run.
        Color RunColour(const Color &a, const Color &b, double t, VectorStorage::ColourBlendKind kind) {
            t = std::min(1.0, std::max(0.0, t));
            auto ch = [](double q) { return static_cast<uint8_t>(std::lround(std::min(255.0, std::max(0.0, q)))); };
            const uint8_t alpha = ch(a.a + (b.a - a.a) * t);
            switch (kind) {
                case VectorStorage::ColourBlendKind::Constant:
                    return a;
                case VectorStorage::ColourBlendKind::Rainbow:
                case VectorStorage::ColourBlendKind::AltRainbow: {
                    double ha, sa, va, hb, sb, vb;
                    RgbToHsv(a, ha, sa, va);
                    RgbToHsv(b, hb, sb, vb);
                    if (sa < 1e-6) ha = hb;
                    if (sb < 1e-6) hb = ha;
                    double dh = hb - ha;
                    if (dh > 180) dh -= 360;
                    if (dh < -180) dh += 360;
                    if (kind == VectorStorage::ColourBlendKind::AltRainbow) dh = dh > 0 ? dh - 360 : dh + 360;
                    return HsvToRgb(ha + dh * t, sa + (sb - sa) * t, va + (vb - va) * t, alpha);
                }
                case VectorStorage::ColourBlendKind::Fade:
                default:
                    return Color(ch(a.r + (b.r - a.r) * t), ch(a.g + (b.g - a.g) * t), ch(a.b + (b.b - a.b) * t), alpha);
            }
        }

        // The element's outline flattened into its own space, groups as
        // the union of their children's (transforms applied).
        PolygonSet OutlinePolygonsOf(const VectorElement &e) {
            if (IsGroupType(e.Type)) {
                PolygonSet all;
                for (const auto &c: static_cast<const VectorGroup &>(e).Children) {
                    if (!c) continue;
                    PolygonSet part = OutlinePolygonsOf(*c);
                    if (c->Transform.has_value())
                        for (auto &ring: part)
                            for (auto &p: ring) p = c->Transform->Transform(p);
                    all.insert(all.end(), part.begin(), part.end());
                }
                return PolygonBoolean(all, VectorStorage::FillRule::NonZero, PolygonSet(), VectorStorage::FillRule::NonZero,
                                      VectorStorage::PathBooleanOp::Union);
            }
            PathData pd;
            if (!BuildOutlinePath(e, pd)) return {};
            PolygonSet polys = FlattenToPolygons(pd);
            // An unfilled stroked shape contours around its stroke.
            if (!HasFill(e.Style) && HasStroke(e.Style))
                polys = OffsetPolygons(polys, VectorStorage::FillRule::NonZero, e.Style.Stroke->Width / 2.0);
            return polys;
        }

        // Felzenszwalb / Huttenlocher squared distance transform, one
        // dimension; `f` holds 0 on the sources and +inf elsewhere.
        void DistanceTransform1D(const float *f, float *d, int n, int *v, float *z) {
            int k = 0;
            v[0] = 0;
            z[0] = -1e20f;
            z[1] = 1e20f;
            for (int q = 1; q < n; ++q) {
                float s;
                while (true) {
                    s = ((f[q] + q * static_cast<float>(q)) - (f[v[k]] + v[k] * static_cast<float>(v[k]))) / (2.0f * (q - v[k]));
                    if (s <= z[k] && k > 0) --k; else break;
                }
                ++k;
                v[k] = q;
                z[k] = s;
                z[k + 1] = 1e20f;
            }
            k = 0;
            for (int q = 0; q < n; ++q) {
                while (z[k + 1] < q) ++k;
                d[q] = (q - v[k]) * static_cast<float>(q - v[k]) + f[v[k]];
            }
        }
        // Euclidean distance (pixels) of every pixel to the nearest source
        // pixel (`inside` true), 0 on the sources.
        std::vector<float> DistanceTo(const std::vector<char> &inside, int w, int h) {
            std::vector<float> f(static_cast<size_t>(w) * h);
            for (size_t i = 0; i < f.size(); ++i) f[i] = inside[i] ? 0.0f : 1e20f;
            const int n = std::max(w, h);
            std::vector<float> row(n), out(n), z(n + 1);
            std::vector<int> v(n);
            for (int x = 0; x < w; ++x) {
                for (int y = 0; y < h; ++y) row[y] = f[static_cast<size_t>(y) * w + x];
                DistanceTransform1D(row.data(), out.data(), h, v.data(), z.data());
                for (int y = 0; y < h; ++y) f[static_cast<size_t>(y) * w + x] = out[y];
            }
            for (int y = 0; y < h; ++y) {
                float *r = &f[static_cast<size_t>(y) * w];
                DistanceTransform1D(r, out.data(), w, v.data(), z.data());
                for (int x = 0; x < w; ++x) r[x] = std::sqrt(out[x]);
            }
            return f;
        }

        // The rim's height at `t` (0 at the edge, 1 a full indent in) for
        // each of Xara's bevel profiles, approximated.
        double BevelProfile(VectorStorage::BevelKind kind, double t) {
            t = std::min(1.0, std::max(0.0, t));
            auto smooth = [](double q) { return q * q * (3 - 2 * q); };
            auto ripple = [](double q, double k) { return (1 - std::cos(2 * M_PI * k * q)) / 2; };
            switch (kind) {
                case VectorStorage::BevelKind::Round: return std::sin(t * M_PI / 2);
                case VectorStorage::BevelKind::HalfRound: return 1 - std::sqrt(std::max(0.0, 1 - t * t));
                case VectorStorage::BevelKind::Frame: return t < 0.5 ? 2 * t : 2 - 2 * t;
                case VectorStorage::BevelKind::Mesa1: return std::min(1.0, 1.5 * t);
                case VectorStorage::BevelKind::Mesa2: return std::min(1.0, 3.0 * t);
                case VectorStorage::BevelKind::Smooth1: return smooth(t);
                case VectorStorage::BevelKind::Smooth2: return smooth(smooth(t));
                case VectorStorage::BevelKind::Point1: return 1 - std::fabs(2 * t - 1);
                case VectorStorage::BevelKind::Point2a: return 1 - std::fabs(std::fmod(4 * t, 2.0) - 1);
                case VectorStorage::BevelKind::Point2b: return t < 0.5 ? 1 - std::fabs(4 * t - 1) : std::min(1.0, 2 * t - 1) * 0.5 + 0.5;
                case VectorStorage::BevelKind::Ruffle2a: return ripple(t, 2);
                case VectorStorage::BevelKind::Ruffle2b: return ripple(t, 2) * 0.5 + t * 0.5;
                case VectorStorage::BevelKind::Ruffle3a: return ripple(t, 3);
                case VectorStorage::BevelKind::Ruffle3b: return ripple(t, 3) * 0.5 + t * 0.5;
                case VectorStorage::BevelKind::Flat:
                default: return t;
            }
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

        // An outward contour lies behind the object, an inward one over it;
        // the bevel is lit over whatever the object drew.
        const ContourEffect *contour = element.Effects.Contour.has_value() ? &*element.Effects.Contour : nullptr;
        if (contour) RenderContour(element, *contour, true);
        DrawElementBody(element);
        if (contour) RenderContour(element, *contour, false);
        if (element.Effects.Bevel.has_value()) RenderBevel(element, *element.Effects.Bevel);

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
        PaintSilhouette(off.get(), element);
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

    void VectorRenderer::PaintSilhouette(IRenderContext *off, const VectorElement &element) {
        IRenderContext *savedCtx = ctx;
        const float savedOpacity = currentOpacity;
        const bool savedMode = silhouetteMode;
        const VectorRenderOptions savedOptions = options;
        ctx = off;
        silhouetteMode = true;
        currentOpacity = 1.0f;
        options.EnableCulling = false;
        options.ShowBoundingBoxes = false;
        ctx->SetAlpha(1.0);
        if (element.Effects.Contour.has_value() && element.Effects.Contour->Width > 0)
            RenderContour(element, *element.Effects.Contour, true);
        DrawElementBody(element);
        ctx = savedCtx;
        silhouetteMode = savedMode;
        currentOpacity = savedOpacity;
        options = savedOptions;
    }

    // ===========================================================================
    // CONTOUR
    // ===========================================================================

    const VectorRenderer::ContourRings *VectorRenderer::ContourRingsOf(const VectorElement &element, const ContourEffect &c) {
        size_t key = GeometryHash(element);
        HashDouble(key, c.Width);
        HashMix(key, static_cast<size_t>(c.Steps));
        auto it = contourCache.find(&element);
        if (it != contourCache.end() && it->second.key == key) return &it->second;
        if (contourCache.size() > 256) contourCache.clear();
        ContourRings &r = contourCache[&element];
        r.key = key;
        r.rings.clear();
        const PolygonSet base = OutlinePolygonsOf(element);
        if (base.empty()) return &r;
        const int steps = std::max(1, std::min(c.Steps, 64));
        for (int i = 1; i <= steps; ++i) {
            const double d = c.Width * static_cast<double>(i) / steps;
            r.rings.push_back(OffsetPolygons(base, VectorStorage::FillRule::NonZero, d, StrokeLineJoin::Round));
        }
        return &r;
    }

    void VectorRenderer::RenderContour(const VectorElement &element, const ContourEffect &c, bool behind) {
        if (c.Steps <= 0 || std::fabs(c.Width) < 1e-3) return;
        const bool outward = c.Width > 0;
        if (behind != outward) return;
        const ContourRings *rings = ContourRingsOf(element, c);
        if (!rings || rings->rings.empty()) return;
        const int steps = static_cast<int>(rings->rings.size());
        const Color base = FlatColourOf(element.Style);
        const bool strokeOnly = !HasFill(element.Style) && HasStroke(element.Style);
        ctx->PushState();
        ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
        // Outward: the outermost ring first, the object over the innermost.
        // Inward: the largest inset first, the smallest (the outermost
        // colour) on top.
        for (int n = 0; n < steps; ++n) {
            const int i = outward ? steps - n : n + 1;
            const PolygonSet &ring = rings->rings[i - 1];
            if (ring.empty()) continue;
            const Color col = silhouetteMode ? Colors::Black
                                             : WithOpacity(RunColour(base, c.Colour, static_cast<double>(i) / steps, c.Blend), 1.0f);
            ctx->ClearPath();
            BuildPath(PolygonsToPath(ring));
            if (strokeOnly) {
                ctx->SetStrokePaint(col);
                ctx->SetStrokeWidth(element.Style.Stroke->Width);
                ctx->StrokePathPreserve();
            } else {
                ctx->SetFillPaint(col);
                ctx->FillPathPreserve();
            }
            ctx->ClearPath();
        }
        ctx->PopState();
    }

    // ===========================================================================
    // BEVEL
    // ===========================================================================

    const VectorRenderer::BevelRaster *VectorRenderer::BevelRasterOf(const VectorElement &element, const BevelEffect &b) {
        const Rect2Dd box = element.GetBoundingBox();
        if (box.width < 0 || box.height < 0) return nullptr;
        const double indent = std::max(0.5f, b.Indent);
        const Point2Dd o = ctx->UserToDevice(Point2Dd(0, 0));
        const Point2Dd ux = ctx->UserToDevice(Point2Dd(1, 0));
        const Point2Dd uy = ctx->UserToDevice(Point2Dd(0, 1));
        double s = std::max(std::hypot(ux.x - o.x, ux.y - o.y), std::hypot(uy.x - o.x, uy.y - o.y));
        if (!(s > 1e-6) || !std::isfinite(s)) s = 1.0;
        const double padU = (b.Outer ? indent : 0.0) + StrokePadOf(element) + 2.0 / s;
        const double fullW = box.width + 2 * padU, fullH = box.height + 2 * padU;
        constexpr double kMaxPixels = 2048.0;
        if (fullW * s > kMaxPixels) s = kMaxPixels / fullW;
        if (fullH * s > kMaxPixels) s = kMaxPixels / fullH;
        const int w = std::max(1, static_cast<int>(std::ceil(fullW * s)));
        const int h = std::max(1, static_cast<int>(std::ceil(fullH * s)));

        size_t key = GeometryHash(element);
        HashDouble(key, s);
        HashDouble(key, indent);
        HashDouble(key, b.LightAngle);
        HashDouble(key, b.Tilt);
        HashDouble(key, b.Contrast);
        HashMix(key, static_cast<size_t>(b.Kind) * 2 + (b.Outer ? 1 : 0));
        auto it = bevelCache.find(&element);
        if (it != bevelCache.end() && it->second.key == key && it->second.light) return &it->second;

        std::unique_ptr<IRenderContext> off = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!off) return nullptr;
        off->Clear(Color(0, 0, 0, 0));
        off->Translate(padU * s, padU * s);
        off->Scale(s, s);
        off->Translate(-box.x, -box.y);
        PaintSilhouette(off.get(), element);
        UCPixmap cov;
        if (!cov.Init(w, h)) return nullptr;
        off->FlushToSurface(cov.GetSurface(), Point2Dd(0, 0));
        cov.MarkDirty();
        cov.Flush();
        const uint32_t *px = cov.GetPixelData();
        const size_t n = static_cast<size_t>(w) * h;
        std::vector<char> inside(n);
        std::vector<float> coverage(n);
        for (size_t i = 0; i < n; ++i) {
            coverage[i] = static_cast<float>(px[i] >> 24) / 255.0f;
            inside[i] = coverage[i] >= 0.5f;
        }
        // Distance from the rim's outer edge: for an inner bevel the
        // distance of inside pixels to the outside, for an outer one the
        // distance of outside pixels to the inside.
        std::vector<char> source(n);
        for (size_t i = 0; i < n; ++i) source[i] = b.Outer ? inside[i] : !inside[i];
        const std::vector<float> dist = DistanceTo(source, w, h);
        const double rim = indent * s;
        std::vector<float> height(n);
        std::vector<float> band(n);
        for (size_t i = 0; i < n; ++i) {
            const bool inBand = b.Outer ? (!inside[i] && dist[i] <= rim) : inside[i];
            const double t = std::min(1.0, dist[i] / rim);
            height[i] = inBand ? static_cast<float>(BevelProfile(b.Kind, b.Outer ? 1.0 - t : t)) : (b.Outer ? 0.0f : 1.0f);
            band[i] = inBand ? (b.Outer ? 1.0f : coverage[i]) : 0.0f;
        }
        // A little smoothing keeps the pixel steps of the silhouette out of
        // the normals.
        {
            std::vector<float> tmp(n);
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        float sum = 0; int cnt = 0;
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int xx = x + dx;
                            if (xx < 0 || xx >= w) continue;
                            sum += height[static_cast<size_t>(y) * w + xx]; ++cnt;
                        }
                        tmp[static_cast<size_t>(y) * w + x] = sum / cnt;
                    }
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        float sum = 0; int cnt = 0;
                        for (int dy = -1; dy <= 1; ++dy) {
                            const int yy = y + dy;
                            if (yy < 0 || yy >= h) continue;
                            sum += tmp[static_cast<size_t>(yy) * w + x]; ++cnt;
                        }
                        height[static_cast<size_t>(y) * w + x] = sum / cnt;
                    }
            }
        }
        // Lighting: the surface normal from the height gradient (the rim
        // rises by its own width), against the light's direction.
        const double angle = b.LightAngle * M_PI / 180.0, tilt = std::max(5.0, std::min(85.0, static_cast<double>(b.Tilt))) * M_PI / 180.0;
        const double lx = std::cos(angle) * std::cos(tilt), ly = -std::sin(angle) * std::cos(tilt), lz = std::sin(tilt);
        const double strength = 2.0 * std::max(0.0f, std::min(1.0f, b.Contrast));
        auto light = std::make_shared<UCPixmap>(), dark = std::make_shared<UCPixmap>();
        if (!light->Init(w, h) || !dark->Init(w, h)) return nullptr;
        uint32_t *lp = light->GetPixelData(), *dp = dark->GetPixelData();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t i = static_cast<size_t>(y) * w + x;
                lp[i] = dp[i] = 0;
                if (band[i] <= 0) continue;
                const float hl = height[static_cast<size_t>(y) * w + std::max(0, x - 1)], hr = height[static_cast<size_t>(y) * w + std::min(w - 1, x + 1)];
                const float hu = height[static_cast<size_t>(std::max(0, y - 1)) * w + x], hd = height[static_cast<size_t>(std::min(h - 1, y + 1)) * w + x];
                const double gx = (hr - hl) / 2.0 * rim, gy = (hd - hu) / 2.0 * rim;
                const double nl = std::sqrt(gx * gx + gy * gy + 1.0);
                const double nx = -gx / nl, ny = -gy / nl, nz = 1.0 / nl;
                const double shade = (nx * lx + ny * ly + nz * lz - lz) * strength;
                const double a = std::min(1.0, std::fabs(shade)) * band[i];
                const uint32_t alpha = static_cast<uint32_t>(std::lround(a * 255.0));
                if (shade > 0) lp[i] = (alpha << 24) | (alpha << 16) | (alpha << 8) | alpha;   // premultiplied white
                else dp[i] = alpha << 24;
            }
        }
        light->MarkDirty();
        dark->MarkDirty();
        if (bevelCache.size() > 128) bevelCache.clear();
        BevelRaster &r = bevelCache[&element];
        r.light = light;
        r.dark = dark;
        r.key = key;
        r.rect = Rect2Dd(box.x - padU, box.y - padU, w / s, h / s);
        return &r;
    }

    void VectorRenderer::RenderBevel(const VectorElement &element, const BevelEffect &b) {
        if (silhouetteMode) {
            if (!b.Outer) return;
            // An outer bevel widens the silhouette by its indent.
            const ContourEffect ring{1, b.Indent, VectorStorage::ColourBlendKind::Constant, Colors::Black, false, 0, 0, 0, 0};
            RenderContour(element, ring, true);
            return;
        }
        const BevelRaster *r = BevelRasterOf(element, b);
        if (!r || !r->light || !r->dark) return;
        ctx->PushState();
        ctx->SetImageSmoothing(true);
        if (b.Outer) {
            // The outer rim is the object's colour grown by the indent,
            // under the object, lit like the rest.
            const ContourEffect ring{1, b.Indent, VectorStorage::ColourBlendKind::Constant, FlatColourOf(element.Style), false, 0, 0, 0, 0};
            RenderContour(element, ring, true);
            DrawElementBody(element);
        }
        ctx->DrawMask(Color(255, 255, 255, static_cast<uint8_t>(std::lround(255 * currentOpacity))), *r->light, r->rect, ImageFitMode::Fill);
        ctx->DrawMask(Color(0, 0, 0, static_cast<uint8_t>(std::lround(255 * currentOpacity))), *r->dark, r->rect, ImageFitMode::Fill);
        ctx->PopState();
    }

    // ===========================================================================
    // CLIPVIEW, BLEND, MOULD
    // ===========================================================================

    void VectorRenderer::RenderClipView(const VectorClipView &clip) {
        const auto keyholes = clip.KeyholeShapes();
        bool any = false;
        ctx->ClearPath();
        for (const auto &k: keyholes) {
            if (!k) continue;
            if (k->Transform.has_value()) ApplyTransform(*k->Transform);
            if (BuildElementPath(*k)) any = true;
            if (k->Transform.has_value()) ApplyTransform(k->Transform->Inverse());
        }
        if (!any) { ctx->ClearPath(); return; }
        ctx->PushState();
        ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
        ctx->ClipPath();
        ctx->ClearPath();
        opacityStack.push(currentOpacity);
        if (!silhouetteMode) currentOpacity *= clip.Style.Opacity;
        ctx->SetAlpha(currentOpacity);
        for (const auto &child: clip.Contents()) if (child) RenderElement(ctx, *child);
        currentOpacity = opacityStack.top();
        opacityStack.pop();
        ctx->PopState();
    }

    void VectorRenderer::RenderBlend(const VectorBlend &blend) {
        opacityStack.push(currentOpacity);
        if (!silhouetteMode) currentOpacity *= blend.Style.Opacity;
        ctx->SetAlpha(currentOpacity);
        const auto &kids = blend.Children;
        for (size_t i = 0; i < kids.size(); ++i) {
            if (!kids[i]) continue;
            RenderElement(ctx, *kids[i]);
            if (i + 1 < kids.size() && kids[i + 1]) RenderBlendSteps(*kids[i], *kids[i + 1], blend);
        }
        currentOpacity = opacityStack.top();
        opacityStack.pop();
    }

    namespace {
        // A polyline resampled to `n` points spaced evenly by arc length.
        std::vector<Point2Dd> Resample(const std::vector<Point2Dd> &pts, bool closed, int n) {
            std::vector<Point2Dd> src = pts;
            if (closed && !src.empty()) src.push_back(src.front());
            std::vector<Point2Dd> out;
            if (src.size() < 2 || n < 2) return out;
            std::vector<double> cum(src.size(), 0.0);
            for (size_t i = 1; i < src.size(); ++i) cum[i] = cum[i - 1] + std::hypot(src[i].x - src[i - 1].x, src[i].y - src[i - 1].y);
            const double total = cum.back();
            if (total < 1e-9) { out.assign(n, src.front()); return out; }
            size_t seg = 1;
            const int count = closed ? n : n - 1;
            for (int k = 0; k <= count; ++k) {
                if (closed && k == count) break;
                const double d = total * k / count;
                while (seg + 1 < src.size() && cum[seg] < d) ++seg;
                const double span = cum[seg] - cum[seg - 1];
                const double t = span > 1e-12 ? (d - cum[seg - 1]) / span : 0.0;
                out.emplace_back(src[seg - 1].x + (src[seg].x - src[seg - 1].x) * t, src[seg - 1].y + (src[seg].y - src[seg - 1].y) * t);
            }
            return out;
        }
        double SignedArea(const std::vector<Point2Dd> &r) {
            double a = 0;
            for (size_t i = 0, n = r.size(); i < n; ++i) a += r[i].x * r[(i + 1) % n].y - r[(i + 1) % n].x * r[i].y;
            return a / 2;
        }
    }

    void VectorRenderer::RenderBlendSteps(const VectorElement &a, const VectorElement &b, const VectorBlend &blend) {
        const int steps = std::max(0, std::min(blend.Steps, 500));
        if (steps == 0) return;
        PathData pa, pb;
        if (!BuildOutlinePath(a, pa) || !BuildOutlinePath(b, pb)) return;
        auto flat = [](const PathData &pd, const VectorElement &e) {
            std::vector<FlatSubpath> subs = FlattenPathData(pd);
            if (e.Transform.has_value())
                for (auto &s: subs)
                    for (auto &p: s.Points) p = e.Transform->Transform(p);
            return subs;
        };
        const std::vector<FlatSubpath> fa = flat(pa, a), fb = flat(pb, b);
        const size_t pairs = std::min(fa.size(), fb.size());
        if (pairs == 0) return;

        // Each pair of subpaths resampled to the same count, the second
        // wound like the first and started at its nearest point.
        struct Pair { std::vector<Point2Dd> from, to; bool closed; };
        std::vector<Pair> matched;
        for (size_t i = 0; i < pairs; ++i) {
            const bool closed = fa[i].Closed || fb[i].Closed;
            int n = blend.OneToOne ? static_cast<int>(std::max(fa[i].Points.size(), fb[i].Points.size()))
                                   : static_cast<int>(std::max<size_t>(64, std::max(fa[i].Points.size(), fb[i].Points.size())));
            n = std::min(n, 2048);
            std::vector<Point2Dd> from = Resample(fa[i].Points, closed, n), to = Resample(fb[i].Points, closed, n);
            if (from.size() != to.size() || from.size() < 2) continue;
            if (closed) {
                if ((SignedArea(from) < 0) != (SignedArea(to) < 0)) std::reverse(to.begin(), to.end());
                size_t bestShift = 0;
                double best = 1e300;
                for (size_t sh = 0; sh < to.size(); ++sh) {
                    double d = 0;
                    for (size_t k = 0; k < to.size() && d < best; ++k) {
                        const Point2Dd &q = to[(k + sh) % to.size()];
                        d += (q.x - from[k].x) * (q.x - from[k].x) + (q.y - from[k].y) * (q.y - from[k].y);
                    }
                    if (d < best) { best = d; bestShift = sh; }
                }
                std::rotate(to.begin(), to.begin() + bestShift, to.end());
            }
            matched.push_back({from, to, closed});
        }
        if (matched.empty()) return;

        const Color fillA = FlatColourOf(a.Style), fillB = FlatColourOf(b.Style);
        const bool fills = HasFill(a.Style) || HasFill(b.Style);
        const bool strokes = HasStroke(a.Style) || HasStroke(b.Style);
        const float wA = HasStroke(a.Style) ? a.Style.Stroke->Width : 0.0f, wB = HasStroke(b.Style) ? b.Style.Stroke->Width : 0.0f;
        Color sA = Colors::Black, sB = Colors::Black;
        if (HasStroke(a.Style)) if (auto *c = std::get_if<Color>(&a.Style.Stroke->Fill)) sA = *c;
        if (HasStroke(b.Style)) if (auto *c = std::get_if<Color>(&b.Style.Stroke->Fill)) sB = *c;
        if (!HasStroke(a.Style)) sA = sB;
        if (!HasStroke(b.Style)) sB = sA;

        ctx->PushState();
        for (int st = 1; st <= steps; ++st) {
            const double t = static_cast<double>(st) / (steps + 1);
            const float opacity = static_cast<float>(a.Style.Opacity + (b.Style.Opacity - a.Style.Opacity) * t);
            ctx->SetAlpha(silhouetteMode ? 1.0 : opacity * currentOpacity);
            ctx->ClearPath();
            for (const Pair &pr : matched) {
                for (size_t k = 0; k < pr.from.size(); ++k) {
                    const double x = pr.from[k].x + (pr.to[k].x - pr.from[k].x) * t;
                    const double y = pr.from[k].y + (pr.to[k].y - pr.from[k].y) * t;
                    if (k == 0) ctx->MoveTo(x, y); else ctx->LineTo(x, y);
                }
                if (pr.closed) ctx->ClosePath();
            }
            if (fills) {
                ctx->SetFillPaint(silhouetteMode ? Colors::Black : RunColour(fillA, fillB, t, blend.ColourEffect));
                ctx->FillPathPreserve();
            }
            if (strokes) {
                const float w = static_cast<float>(wA + (wB - wA) * t);
                if (w > 0) {
                    ctx->SetStrokePaint(silhouetteMode ? Colors::Black : RunColour(sA, sB, t, blend.ColourEffect));
                    ctx->SetStrokeWidth(w);
                    ctx->StrokePathPreserve();
                }
            }
            ctx->ClearPath();
        }
        ctx->PopState();
    }

    void VectorRenderer::RenderMould(const VectorMould &mould) {
        Point2Dd corners[4];
        const Rect2Dd src = mould.EffectiveSourceBounds();
        if (!mould.ShapeCorners(corners) || src.width <= 0 || src.height <= 0) {
            RenderGroup(mould);
            return;
        }
        opacityStack.push(currentOpacity);
        if (!silhouetteMode) currentOpacity *= mould.Style.Opacity;
        ctx->SetAlpha(currentOpacity);
        for (const auto &child: mould.Children) if (child) RenderMoulded(*child, mould, Matrix3x3::Identity());
        currentOpacity = opacityStack.top();
        opacityStack.pop();
    }

    void VectorRenderer::RenderMoulded(const VectorElement &e, const VectorMould &mould, const Matrix3x3 &parentToMould) {
        if (!IsVisible(e)) return;
        const Matrix3x3 M = e.Transform.has_value() ? parentToMould * (*e.Transform) : parentToMould;
        if (IsGroupType(e.Type)) {
            opacityStack.push(currentOpacity);
            if (!silhouetteMode) currentOpacity *= e.Style.Opacity;
            ctx->SetAlpha(currentOpacity);
            for (const auto &c: static_cast<const VectorGroup &>(e).Children) if (c) RenderMoulded(*c, mould, M);
            currentOpacity = opacityStack.top();
            opacityStack.pop();
            return;
        }
        PathData pd;
        if (!BuildOutlinePath(e, pd)) {
            // Text and images are not warped: they move to where the mould
            // takes their anchor and draw as they are.
            const Rect2Dd box = e.GetBoundingBox();
            const Point2Dd anchor = M.Transform(Point2Dd(box.x, box.y + box.height));
            const Point2Dd moved = mould.Warp(anchor);
            ctx->PushState();
            ctx->Translate(moved.x - anchor.x, moved.y - anchor.y);
            ApplyTransform(parentToMould);
            RenderElement(ctx, e);
            ctx->PopState();
            return;
        }
        const Rect2Dd src = mould.EffectiveSourceBounds();
        const double maxSeg = std::max(0.5, std::min(src.width, src.height) / 40.0);
        ctx->PushState();
        ApplyStyle(e.Style);
        ctx->ClearPath();
        for (const FlatSubpath &sub: FlattenPathData(pd)) {
            std::vector<Point2Dd> pts = sub.Points;
            if (pts.empty()) continue;
            if (sub.Closed) pts.push_back(pts.front());
            bool first = true;
            for (size_t i = 0; i < pts.size(); ++i) {
                const Point2Dd p = M.Transform(pts[i]);
                if (first) {
                    const Point2Dd w = mould.Warp(p);
                    ctx->MoveTo(w.x, w.y);
                    first = false;
                    continue;
                }
                const Point2Dd prev = M.Transform(pts[i - 1]);
                const double len = std::hypot(p.x - prev.x, p.y - prev.y);
                const int pieces = std::max(1, static_cast<int>(std::ceil(len / maxSeg)));
                for (int k = 1; k <= pieces; ++k) {
                    const double t = static_cast<double>(k) / pieces;
                    const Point2Dd w = mould.Warp(Point2Dd(prev.x + (p.x - prev.x) * t, prev.y + (p.y - prev.y) * t));
                    ctx->LineTo(w.x, w.y);
                }
            }
            if (sub.Closed) ctx->ClosePath();
        }
        FillAndStroke(e.Style);
        ctx->PopState();
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
    // The geometry comes from the model (FlattenPathData, PathEndpoints,
    // ArrowheadOutline, VariableWidthOutline) so the XAR writer bakes the
    // same shapes this draws.

    namespace {
        Point2Dd Unit(const Point2Dd &v) {
            const double l = std::hypot(v.x, v.y);
            return l > 1e-12 ? Point2Dd(v.x / l, v.y / l) : Point2Dd(1, 0);
        }
    }

    void VectorRenderer::RenderLineGallery(const VectorElement &element, const StrokeData &stroke,
                                           const Rect2Dd &bounds, float opacity) {
        PathData pd;
        if (!BuildOutlinePath(element, pd)) return;
        SetGalleryPaint(stroke, bounds, opacity);
        if (stroke.HasWidthProfile()) {
            const PathData band = VariableWidthOutline(pd, stroke);
            if (!band.commands.empty()) {
                ctx->ClearPath();
                BuildPath(band);
                ctx->SetFillRule(UltraCanvas::FillRule::EvenOdd);
                ctx->FillPathPreserve();
                ctx->ClearPath();
                ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
            }
        } else if (stroke.HasBrush()) {
            for (const auto &sub: FlattenPathData(pd)) StampBrush(sub.Points, stroke);
        }
        if (stroke.HasArrowheads()) {
            Point2Dd start, startDir, end, endDir;
            if (PathEndpoints(pd, start, startDir, end, endDir)) {
                SetGalleryPaint(stroke, bounds, opacity);
                if (stroke.StartArrow.IsSet()) DrawArrowhead(stroke.StartArrow, start, startDir, stroke);
                if (stroke.EndArrow.IsSet()) DrawArrowhead(stroke.EndArrow, end, endDir, stroke);
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
        bool stroked = false;
        const PathData outline = ArrowheadOutline(arrow, tip, d, stroke.Width, stroked);
        if (outline.commands.empty()) return;
        ctx->ClearPath();
        BuildPath(outline);
        if (stroked) ctx->StrokePathPreserve();
        else ctx->FillPathPreserve();
        ctx->ClearPath();
    }

    void VectorRenderer::FillVariableWidth(const std::vector<Point2Dd> &pts, bool closed, const StrokeData &stroke) {
        // Kept for the header's sake; RenderLineGallery uses the model's
        // VariableWidthOutline over the whole path instead.
        PathData pd;
        for (size_t i = 0; i < pts.size(); ++i) {
            PathCommand c;
            c.Type = i == 0 ? PathCommandType::MoveTo : PathCommandType::LineTo;
            c.Parameters = {static_cast<float>(pts[i].x), static_cast<float>(pts[i].y)};
            pd.commands.push_back(c);
        }
        if (closed) { PathCommand z; z.Type = PathCommandType::ClosePath; pd.commands.push_back(z); pd.Closed = true; }
        const PathData band = VariableWidthOutline(pd, stroke);
        if (band.commands.empty()) return;
        ctx->ClearPath();
        BuildPath(band);
        ctx->SetFillRule(UltraCanvas::FillRule::EvenOdd);
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
        std::vector<double> cum(pts.size(), 0.0);
        for (size_t i = 1; i < pts.size(); ++i)
            cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
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

    void VectorRenderer::ClearCaches() { effectCache.clear(); bevelCache.clear(); contourCache.clear(); }

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
