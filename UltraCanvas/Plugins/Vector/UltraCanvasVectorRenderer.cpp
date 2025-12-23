// UltraCanvasVectorRenderer.cpp
// Vector Graphics Rendering for UltraCanvas
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRenderer.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace UltraCanvas {

    using namespace VectorStorage;

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
            ctx->FillRectangle(document.ViewBox.x, document.ViewBox.y, document.ViewBox.width, document.ViewBox.height);
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

        switch (element.Type) {
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle:
                RenderRect(static_cast<const VectorRect &>(element));
                break;
            case VectorElementType::Circle:
                RenderCircle(static_cast<const VectorCircle &>(element));
                break;
            case VectorElementType::Ellipse:
                RenderEllipse(static_cast<const VectorEllipse &>(element));
                break;
            case VectorElementType::Line:
                RenderLine(static_cast<const VectorLine &>(element));
                break;
            case VectorElementType::Polyline:
                RenderPolyline(static_cast<const VectorPolyline &>(element));
                break;
            case VectorElementType::Polygon:
                RenderPolygon(static_cast<const VectorPolygon &>(element));
                break;
            case VectorElementType::Path:
                RenderPath(static_cast<const VectorPath &>(element));
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

    void VectorRenderer::RenderRect(const VectorRect &rect) {
        bool hasFill = rect.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(rect.Style.Fill.value());
        bool hasStroke = rect.Style.Stroke.has_value();

        if (rect.RadiusX > 0 || rect.RadiusY > 0)
            ctx->RoundedRect(rect.Bounds.x, rect.Bounds.y, rect.Bounds.width, rect.Bounds.height,
                             std::max(rect.RadiusX, rect.RadiusY));
        else
            ctx->Rect(rect.Bounds.x, rect.Bounds.y, rect.Bounds.width, rect.Bounds.height);

        if (hasFill) {
            ApplyFill(rect.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(rect.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderCircle(const VectorCircle &circle) {
        bool hasFill =
                circle.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(circle.Style.Fill.value());
        bool hasStroke = circle.Style.Stroke.has_value();
        ctx->Circle(circle.Center.x, circle.Center.y, circle.Radius);
        if (hasFill) {
            ApplyFill(circle.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(circle.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderEllipse(const VectorEllipse &ellipse) {
        bool hasFill =
                ellipse.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(ellipse.Style.Fill.value());
        bool hasStroke = ellipse.Style.Stroke.has_value();
        ctx->Ellipse(ellipse.Center.x, ellipse.Center.y, ellipse.RadiusX, ellipse.RadiusY, 0);
        if (hasFill) {
            ApplyFill(ellipse.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(ellipse.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderLine(const VectorLine &line) {
        if (!line.Style.Stroke.has_value()) return;
        ApplyStroke(line.Style.Stroke.value());
        ctx->DrawLine(line.Start.x, line.Start.y, line.End.x, line.End.y);
    }

    void VectorRenderer::RenderPolyline(const VectorPolyline &polyline) {
        if (polyline.Points.size() < 2) return;
        bool hasFill =
                polyline.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(polyline.Style.Fill.value());
        bool hasStroke = polyline.Style.Stroke.has_value();
        ctx->MoveTo(polyline.Points[0].x, polyline.Points[0].y);
        for (size_t i = 1; i < polyline.Points.size(); i++) ctx->LineTo(polyline.Points[i].x, polyline.Points[i].y);
        if (hasFill) {
            ApplyFill(polyline.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(polyline.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderPolygon(const VectorPolygon &polygon) {
        if (polygon.Points.size() < 3) return;
        bool hasFill =
                polygon.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(polygon.Style.Fill.value());
        bool hasStroke = polygon.Style.Stroke.has_value();
        ctx->MoveTo(polygon.Points[0].x, polygon.Points[0].y);
        for (size_t i = 1; i < polygon.Points.size(); i++) ctx->LineTo(polygon.Points[i].x, polygon.Points[i].y);
        ctx->ClosePath();
        if (hasFill) {
            ApplyFill(polygon.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(polygon.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderPath(const VectorPath &path) {
        if (path.Path.Commands.empty()) return;
        bool hasFill = path.Style.Fill.has_value() && !std::holds_alternative<std::monostate>(path.Style.Fill.value());
        bool hasStroke = path.Style.Stroke.has_value();
        BuildPath(path.Path);
        if (hasFill) {
            ApplyFill(path.Style.Fill.value());
            ctx->FillPathPreserve();
        }
        if (hasStroke) {
            ApplyStroke(path.Style.Stroke.value());
            ctx->StrokePathPreserve();
        }
        ctx->ClearPath();
    }

    void VectorRenderer::RenderText(const VectorText &text) {
        FontStyle fs = text.BaseStyle.ToFontStyle();
        ctx->SetFontFace(fs.fontFamily, fs.fontWeight, fs.fontSlant);
        ctx->SetFontSize(fs.fontSize);
        if (text.Style.Fill.has_value()) {
            if (auto *color = std::get_if<Color>(&text.Style.Fill.value())) ctx->SetTextPaint(*color);
        } else ctx->SetTextPaint(Colors::Black);

        Point2Df pos = text.Position;
        for (const auto &span: text.Spans) {
            if (span.Position.has_value()) pos = span.Position.value();
            ctx->DrawText(span.Text, pos.x, pos.y);
            pos.x += span.Text.length() * span.Style.FontSize * 0.6f;
        }
    }

    void VectorRenderer::RenderImage(const VectorImage &image) {
        if (!image.Source.empty())
            ctx->DrawImage(image.Source, image.Bounds.x, image.Bounds.y, image.Bounds.width, image.Bounds.height,
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
        Rect2Df rb = ref->GetBoundingBox();
        if (use.Size.width > 0 && use.Size.height > 0 && rb.width > 0 && rb.height > 0)
            ctx->Scale(use.Size.width / rb.width, use.Size.height / rb.height);
        RenderElement(ctx, *ref);
        ctx->PopState();
    }

    void VectorRenderer::ApplyStyle(const VectorStyle &style) {
        ctx->SetAlpha(style.Opacity * currentOpacity);
    }

    void VectorRenderer::ApplyFill(const FillData &fill) {
        if (auto *c = std::get_if<Color>(&fill)) ctx->SetFillPaint(*c);
        else if (auto *g = std::get_if<GradientData>(&fill)) SetupGradient(*g, {0, 0, 100, 100});
        else if (auto *id = std::get_if<std::string>(&fill)) {
            if (currentDocument)
                if (auto d = currentDocument->GetDefinition(*id))
                    if (auto *gd = dynamic_cast<VectorGradient *>(d.get())) SetupGradient(gd->Data, {0, 0, 100, 100});
        }
    }

    void VectorRenderer::ApplyStroke(const StrokeData &stroke) {
        if (auto *c = std::get_if<Color>(&stroke.Fill)) ctx->SetStrokePaint(*c);
        else if (auto *g = std::get_if<GradientData>(&stroke.Fill)) SetupGradient(*g, {0, 0, 100, 100});
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

    void VectorRenderer::SetupGradient(const GradientData &gradient, const Rect2Df &bounds) {
        if (auto *l = std::get_if<LinearGradientData>(&gradient)) SetupLinearGradient(*l, bounds);
        else if (auto *r = std::get_if<RadialGradientData>(&gradient)) SetupRadialGradient(*r, bounds);
    }

    void VectorRenderer::SetupLinearGradient(const LinearGradientData &g, const Rect2Df &b) {
        Gradient gr(GradientType::Linear);
        if (g.Units == GradientUnits::ObjectBoundingBox) {
            gr.startPoint = {b.x + g.Start.x * b.width, b.y + g.Start.y * b.height};
            gr.endPoint = {b.x + g.End.x * b.width, b.y + g.End.y * b.height};
        } else {
            gr.startPoint = g.Start;
            gr.endPoint = g.End;
        }
        gr.stops = g.Stops;
        ctx->SetFillGradient(gr);
    }

    void VectorRenderer::SetupRadialGradient(const RadialGradientData &g, const Rect2Df &b) {
        Gradient gr(GradientType::Radial);
        if (g.Units == GradientUnits::ObjectBoundingBox) {
            gr.startPoint = {b.x + g.Center.x * b.width, b.y + g.Center.y * b.height};
            gr.radius1 = g.Radius * std::max(b.width, b.height);
        } else {
            gr.startPoint = g.Center;
            gr.radius1 = g.Radius;
        }
        gr.endPoint = gr.startPoint;
        gr.radius2 = gr.radius1;
        gr.stops = g.Stops;
        ctx->SetFillGradient(gr);
    }

    void VectorRenderer::BuildPath(const PathData &pathData) {
        Point2Df cur{0, 0}, subStart{0, 0}, lastCtrl{0, 0};
        for (const auto &cmd: pathData.commands) {
            stats.PathCommandsProcessed++;
            switch (cmd.Type) {
                case PathCommandType::MoveTo:
                    if (cmd.Parameters.size() >= 2) {
                        float x = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[1] : cmd.Parameters[1];
                        ctx->MoveTo(x, y);
                        cur = subStart = {x, y};
                    }
                    break;
                case PathCommandType::LineTo:
                    if (cmd.Parameters.size() >= 2) {
                        float x = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[1] : cmd.Parameters[1];
                        ctx->LineTo(x, y);
                        cur = {x, y};
                    }
                    break;
                case PathCommandType::HorizontalLineTo:
                    if (cmd.Parameters.size() >= 1) {
                        float x = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        ctx->LineTo(x, cur.y);
                        cur.x = x;
                    }
                    break;
                case PathCommandType::VerticalLineTo:
                    if (cmd.Parameters.size() >= 1) {
                        float y = cmd.Relative ? cur.y + cmd.Parameters[0] : cmd.Parameters[0];
                        ctx->LineTo(cur.x, y);
                        cur.y = y;
                    }
                    break;
                case PathCommandType::CurveTo:
                    if (cmd.Parameters.size() >= 6) {
                        float x1 = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        float y1 = cmd.Relative ? cur.y + cmd.Parameters[1] : cmd.Parameters[1];
                        float x2 = cmd.Relative ? cur.x + cmd.Parameters[2] : cmd.Parameters[2];
                        float y2 = cmd.Relative ? cur.y + cmd.Parameters[3] : cmd.Parameters[3];
                        float x = cmd.Relative ? cur.x + cmd.Parameters[4] : cmd.Parameters[4];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[5] : cmd.Parameters[5];
                        ctx->BezierCurveTo(x1, y1, x2, y2, x, y);
                        lastCtrl = {x2, y2};
                        cur = {x, y};
                    }
                    break;
                case PathCommandType::SmoothCurveTo:
                    if (cmd.Parameters.size() >= 4) {
                        float x1 = 2 * cur.x - lastCtrl.x, y1 = 2 * cur.y - lastCtrl.y;
                        float x2 = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        float y2 = cmd.Relative ? cur.y + cmd.Parameters[1] : cmd.Parameters[1];
                        float x = cmd.Relative ? cur.x + cmd.Parameters[2] : cmd.Parameters[2];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[3] : cmd.Parameters[3];
                        ctx->BezierCurveTo(x1, y1, x2, y2, x, y);
                        lastCtrl = {x2, y2};
                        cur = {x, y};
                    }
                    break;
                case PathCommandType::QuadraticTo:
                    if (cmd.Parameters.size() >= 4) {
                        float qx = cmd.Relative ? cur.x + cmd.Parameters[0] : cmd.Parameters[0];
                        float qy = cmd.Relative ? cur.y + cmd.Parameters[1] : cmd.Parameters[1];
                        float x = cmd.Relative ? cur.x + cmd.Parameters[2] : cmd.Parameters[2];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[3] : cmd.Parameters[3];
                        float cx1 = cur.x + 2.0f / 3.0f * (qx - cur.x), cy1 = cur.y + 2.0f / 3.0f * (qy - cur.y);
                        float cx2 = x + 2.0f / 3.0f * (qx - x), cy2 = y + 2.0f / 3.0f * (qy - y);
                        ctx->BezierCurveTo(cx1, cy1, cx2, cy2, x, y);
                        lastCtrl = {qx, qy};
                        cur = {x, y};
                    }
                    break;
                case PathCommandType::ArcTo:
                    if (cmd.Parameters.size() >= 7) {
                        float x = cmd.Relative ? cur.x + cmd.Parameters[5] : cmd.Parameters[5];
                        float y = cmd.Relative ? cur.y + cmd.Parameters[6] : cmd.Parameters[6];
                        ctx->LineTo(x, y);
                        cur = {x, y}; // Simplified
                    }
                    break;
                case PathCommandType::ClosePath:
                    ctx->ClosePath();
                    cur = subStart;
                    break;
                default:
                    break;
            }
        }
    }

    bool VectorRenderer::IsVisible(const VectorElement &e) const {
        return e.Style.Visible && e.Style.Display && e.Style.Opacity > 0;
    }

    bool VectorRenderer::IsInViewport(const Rect2Df &b) const {
        if (options.ViewportBounds.width <= 0 || options.ViewportBounds.height <= 0) return true;
        return !(b.x + b.width < options.ViewportBounds.x || b.y + b.height < options.ViewportBounds.y ||
                 b.x > options.ViewportBounds.x + options.ViewportBounds.width ||
                 b.y > options.ViewportBounds.y + options.ViewportBounds.height);
    }

    void VectorRenderer::RenderDebugBounds(const Rect2Df &b) {
        ctx->PushState();
        ctx->SetStrokePaint(options.DebugColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(b.x, b.y, b.width, b.height);
        ctx->PopState();
    }

    void VectorRenderer::ClearCaches() {}

    bool HitTestElement(const VectorElement &e, const Point2Df &p) {
        Rect2Df b = e.GetBoundingBox();
        return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
    }

    std::vector<const VectorElement *> HitTestDocument(const VectorDocument &doc, const Point2Df &pt) {
        std::vector<const VectorElement *> hits;
        std::function<void(const VectorGroup &)> test = [&](const VectorGroup &g) {
            for (auto it = g.Children.rbegin(); it != g.Children.rend(); ++it) {
                if (!*it || !(*it)->Style.Visible) continue;
                if (HitTestElement(**it, pt)) hits.push_back(it->get());
                if (auto *gg = dynamic_cast<const VectorGroup *>(it->get())) test(*gg);
            }
        };
        for (auto it = doc.Layers.rbegin(); it != doc.Layers.rend(); ++it) if ((*it)->Visible) test(**it);
        return hits;
    }

    Rect2Df CalculateDocumentBounds(const VectorDocument &doc) { return doc.GetBoundingBox(); }

} // namespace UltraCanvas