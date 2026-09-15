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

        float layerOpacity = layer.Opacity * currentOpacity;
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

        if (options.ShowBoundingBoxes) RenderDebugBounds(element.GetBoundingBox());
        stats.ElementsRendered++;
        ctx->PopState();
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
        FillAndStroke(element.Style);
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
        BuildElementPath(line);
        const Rect2Dd bounds = ctx->GetPathExtents();
        ApplyStroke(line.Style.Stroke.value(), bounds, line.Style.StrokeOpacity);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
    }

    void VectorRenderer::RenderText(const VectorText &text) {
        const float opacity = text.Style.FillOpacity;
        if (text.Style.Fill.has_value()) {
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
        if (!image.Source.empty())
            ctx->DrawImage(image.Source,
                           Rect2Dd(image.Bounds.x, image.Bounds.y, image.Bounds.width, image.Bounds.height),
                           ImageFitMode::Contain);
    }

    void VectorRenderer::RenderGroup(const VectorGroup &group) {
        opacityStack.push(currentOpacity);
        currentOpacity *= group.Style.Opacity;
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
        ctx->SetAlpha(style.Opacity * currentOpacity);
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
        if (auto *c = std::get_if<Color>(&stroke.Fill)) ctx->SetStrokePaint(WithOpacity(*c, strokeOpacity));
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

    void VectorRenderer::ClearCaches() {}

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
