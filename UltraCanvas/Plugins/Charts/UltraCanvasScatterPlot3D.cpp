// Plugins/Charts/UltraCanvasScatterPlot3D.cpp
// Software-rendered 3D scatter plot with correlation line.
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasScatterPlot3D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {

        std::string FormatFixedScatter3D(double v, int decimals) {
            if (std::isnan(v)) return "NaN";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 12), v);
            std::string s(buf);
            if (s.find_first_not_of("-0.") == std::string::npos && !s.empty() && s[0] == '-') {
                s.erase(s.begin());
            }
            return s;
        }

        // Keep text from rendering upside-down when an axis points leftwards.
        double UprightAngleScatter3D(double angle) {
            if (angle > M_PI / 2.0) angle -= M_PI;
            else if (angle < -M_PI / 2.0) angle += M_PI;
            return angle;
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasScatterPlot3DElement::UltraCanvasScatterPlot3DElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        // The 2D grid/axes machinery of the base class does not apply here.
        showGrid = false;
        showAxes = false;
        enableTooltips = true;
    }

// =============================================================================
// SETTERS
// =============================================================================

    void UltraCanvasScatterPlot3DElement::SetPointColor(const Color& color) {
        pointColor = color;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetPointSize(double size) {
        pointSize = std::clamp(size, 0.5, 60.0);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetPointShape(PointShape3D shape) {
        pointShape = shape;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetDepthFade(bool enabled, double strength) {
        depthFade = enabled;
        depthFadeStrength = std::clamp(strength, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetShowCorrelationLine(bool show) {
        showCorrelationLine = show;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetCorrelationLineColor(const Color& color) {
        correlationLineColor = color;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetCorrelationLineWidth(float width) {
        correlationLineWidth = std::max(0.1f, width);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetCamera(double yawRadians, double pitchRadians,
                                                    double cameraDistance) {
        yaw = yawRadians;
        const double limit = M_PI / 2.0 - 0.02;
        pitch = std::clamp(pitchRadians, -limit, limit);
        distance = std::clamp(cameraDistance, 0.6, 40.0);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetFieldOfView(double radians) {
        fieldOfView = std::clamp(radians, 0.1, 2.0);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetEnableOrbit(bool on) { enableOrbit = on; }

    // The data box spans [-1,1] in every direction, so its corners sit at
    // radius ~1.73; at the default field of view a distance of about 4.6 keeps
    // the whole cloud plus its axis labels in frame.
    void UltraCanvasScatterPlot3DElement::ViewIsometric() { SetCamera(-0.62, 0.42, 4.6); }
    void UltraCanvasScatterPlot3DElement::ViewTop()       { SetCamera(0.0, M_PI / 2.0 - 0.03, 4.6); }
    void UltraCanvasScatterPlot3DElement::ViewFront()     { SetCamera(0.0, 0.06, 4.6); }

    void UltraCanvasScatterPlot3DElement::SetShowAxes3D(bool on) {
        showAxes3D = on;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetShowGroundGrid(bool on, const Color& c) {
        showGroundGrid = on;
        groundGridColor = c;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetAxisTitles(const std::string& x,
                                                        const std::string& y,
                                                        const std::string& z) {
        axisTitleX = x; axisTitleY = y; axisTitleZ = z;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetTickCounts(int xTicks, int yTicks, int zTicks) {
        xTickCount = std::max(2, xTicks);
        yTickCount = std::max(2, yTicks);
        zTickCount = std::max(2, zTicks);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetXTickFormatter(ValueFormatter fn) {
        xTickFormatter = std::move(fn); RequestRedraw();
    }
    void UltraCanvasScatterPlot3DElement::SetYTickFormatter(ValueFormatter fn) {
        yTickFormatter = std::move(fn); RequestRedraw();
    }
    void UltraCanvasScatterPlot3DElement::SetZTickFormatter(ValueFormatter fn) {
        zTickFormatter = std::move(fn); RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetAxisColors(const Color& line, const Color& label) {
        axisColor = line;
        axisLabelColor = label;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetAxisLabelFontSize(float size) {
        axisLabelFontSize = std::max(1.0f, size);
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetRotateAxisLabels(bool on) {
        rotateAxisLabels = on;
        RequestRedraw();
    }

    void UltraCanvasScatterPlot3DElement::SetTitleColor(const Color& c) {
        titleColor = c;
        RequestRedraw();
    }

// =============================================================================
// DATA BOUNDS
// =============================================================================

    ChartDataBounds UltraCanvasScatterPlot3DElement::CalculateDataBounds() {
        ChartDataBounds bounds;
        if (!dataSource || dataSource->GetPointCount() == 0) {
            return bounds;
        }

        auto first = dataSource->GetPoint(0);
        bounds = ChartDataBounds(first.x, first.x, first.y, first.y, first.z, first.z);

        for (size_t i = 1; i < dataSource->GetPointCount(); ++i) {
            auto point = dataSource->GetPoint(i);
            bounds.Expand(point.x, point.y, point.z);
        }

        bounds.AddMargin(0.05);
        return bounds;
    }

// =============================================================================
// PROJECTION
// =============================================================================

    Vec3 UltraCanvasScatterPlot3DElement::WorldFromData(double x, double y, double z) const {
        auto normalize = [](double v, double lo, double hi) {
            double range = hi - lo;
            if (range <= 0.0) return 0.0;
            return (v - lo) / range * 2.0 - 1.0;
        };
        // Data z is the vertical axis on screen (world up); data y recedes
        // into the scene, matching the usual mathematical 3D plot layout.
        return Vec3(static_cast<float>(normalize(x, cachedDataBounds.minX, cachedDataBounds.maxX)),
                    static_cast<float>(normalize(z, cachedDataBounds.minZ, cachedDataBounds.maxZ)),
                    static_cast<float>(normalize(y, cachedDataBounds.minY, cachedDataBounds.maxY)));
    }

    UltraCanvasScatterPlot3DElement::Camera
    UltraCanvasScatterPlot3DElement::BuildCamera(const ChartPlotArea& area) const {
        Camera cam;
        Vec3 target(0.0f, 0.0f, 0.0f);
        double cp = std::cos(pitch), sp = std::sin(pitch);
        double cy = std::cos(yaw), sy = std::sin(yaw);

        Vec3 dir(static_cast<float>(cp * sy), static_cast<float>(sp), static_cast<float>(cp * cy));
        cam.eye = target + dir * static_cast<float>(distance);
        cam.forward = (target - cam.eye).Normalized();

        Vec3 worldUp(0.0f, 1.0f, 0.0f);
        cam.right = cam.forward.Cross(worldUp).Normalized();
        if (cam.right.Length() < 1e-4f) {
            // Looking straight down: pick any horizontal reference.
            cam.right = Vec3(1.0f, 0.0f, 0.0f);
        }
        cam.up = cam.right.Cross(cam.forward).Normalized();

        double minDim = std::min(area.width, area.height);
        cam.focal = (minDim * 0.5) / std::tan(fieldOfView * 0.5);
        cam.cx = area.x + area.width * 0.5;
        cam.cy = area.y + area.height * 0.5;
        return cam;
    }

    bool UltraCanvasScatterPlot3DElement::Project(const Camera& cam, const Vec3& p,
                                                  Point2Dd& out, double& depth) const {
        Vec3 d = p - cam.eye;
        depth = d.Dot(cam.forward);
        if (depth < 0.05) return false;             // behind (or on) the camera
        double scale = cam.focal / depth;
        out = Point2Dd(cam.cx + d.Dot(cam.right) * scale,
                       cam.cy - d.Dot(cam.up) * scale);
        return true;
    }

// =============================================================================
// CORRELATION FIT
// =============================================================================

    // Principal axis of the cloud (orthogonal least squares): dominant
    // eigenvector of the 3x3 covariance matrix, found by power iteration.
    void UltraCanvasScatterPlot3DElement::RebuildFit() const {
        fitValid = true;
        fitExists = false;
        if (!dataSource) return;

        size_t n = dataSource->GetPointCount();
        if (n < 2) return;

        double mx = 0.0, my = 0.0, mz = 0.0;
        for (size_t i = 0; i < n; ++i) {
            auto p = dataSource->GetPoint(i);
            mx += p.x; my += p.y; mz += p.z;
        }
        mx /= n; my /= n; mz /= n;

        double cxx = 0, cxy = 0, cxz = 0, cyy = 0, cyz = 0, czz = 0;
        for (size_t i = 0; i < n; ++i) {
            auto p = dataSource->GetPoint(i);
            double dx = p.x - mx, dy = p.y - my, dz = p.z - mz;
            cxx += dx * dx; cxy += dx * dy; cxz += dx * dz;
            cyy += dy * dy; cyz += dy * dz; czz += dz * dz;
        }

        double trace = cxx + cyy + czz;
        if (trace <= 0.0) return;   // all points coincide

        // Power iteration on the covariance matrix. It converges to the
        // dominant eigenvector for any start not orthogonal to it; the
        // slightly asymmetric seed avoids the unlucky orthogonal start.
        double vx = 1.0, vy = 0.8, vz = 0.6;
        for (int iter = 0; iter < 64; ++iter) {
            double nx = cxx * vx + cxy * vy + cxz * vz;
            double ny = cxy * vx + cyy * vy + cyz * vz;
            double nz = cxz * vx + cyz * vy + czz * vz;
            double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 1e-30) return;
            vx = nx / len; vy = ny / len; vz = nz / len;
        }

        fitCentroid = Vec3(static_cast<float>(mx), static_cast<float>(my), static_cast<float>(mz));
        fitDirection = Vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz));

        // Span the data extent along the axis.
        fitTMin = std::numeric_limits<double>::infinity();
        fitTMax = -std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < n; ++i) {
            auto p = dataSource->GetPoint(i);
            double t = (p.x - mx) * vx + (p.y - my) * vy + (p.z - mz) * vz;
            fitTMin = std::min(fitTMin, t);
            fitTMax = std::max(fitTMax, t);
        }
        fitExists = fitTMax > fitTMin;
    }

    bool UltraCanvasScatterPlot3DElement::GetCorrelationLine(Vec3& centroid, Vec3& direction) const {
        RebuildFit();
        if (!fitExists) return false;
        centroid = fitCentroid;
        direction = fitDirection;
        return true;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    ChartPlotArea UltraCanvasScatterPlot3DElement::ComputePlotArea3D(IRenderContext* ctx) {
        double left = 8.0, right = 8.0, top = 8.0, bottom = 8.0;
        if (!chartTitle.empty()) {
            ctx->SetFontSize(11.0);
            top += ctx->GetTextLineHeight("0") + 12.0;
        }
        double w = std::max(1.0, static_cast<double>(GetWidth()) - left - right);
        double h = std::max(1.0, static_cast<double>(GetHeight()) - top - bottom);
        return ChartPlotArea(left, top, w, h);
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasScatterPlot3DElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()), backgroundColor);
        }
        if (!dataSource || dataSource->GetPointCount() == 0) {
            DrawEmptyState(ctx);
            return;
        }

        cachedPlotArea = ComputePlotArea3D(ctx);
        cachedDataBounds = CalculateDataBounds();

        if (!chartTitle.empty()) {
            ctx->SetFontSize(15.0);
            ctx->SetTextPaint(titleColor);
            Size2Di sz = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - sz.width / 2.0, 6));
        }

        RenderChart(ctx);
    }

    void UltraCanvasScatterPlot3DElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !dataSource || dataSource->GetPointCount() == 0) return;

        const ChartPlotArea& area = cachedPlotArea;
        Camera cam = BuildCamera(area);

        ctx->PushState();
        ctx->ClipRect(area.ToRect2D());
        if (showGroundGrid) RenderGroundGrid(ctx, cam);
        if (showAxes3D) RenderAxes3D(ctx, cam);
        RenderCloud(ctx, cam);
        ctx->PopState();
    }

    void UltraCanvasScatterPlot3DElement::RenderGroundGrid(IRenderContext* ctx, const Camera& cam) {
        ctx->SetStrokePaint(groundGridColor);
        ctx->SetStrokeWidth(0.8f);

        auto drawWorldLine = [&](const Vec3& a, const Vec3& b) {
            Point2Dd sa, sb;
            double da, db;
            if (Project(cam, a, sa, da) && Project(cam, b, sb, db)) {
                ctx->DrawLine(sa, sb);
            }
        };

        // Grid lines on the floor of the data box, one per tick.
        for (int i = 0; i < xTickCount; ++i) {
            float x = -1.0f + 2.0f * i / (xTickCount - 1);
            drawWorldLine(Vec3(x, -1.0f, -1.0f), Vec3(x, -1.0f, 1.0f));
        }
        for (int i = 0; i < yTickCount; ++i) {
            float z = -1.0f + 2.0f * i / (yTickCount - 1);
            drawWorldLine(Vec3(-1.0f, -1.0f, z), Vec3(1.0f, -1.0f, z));
        }
    }

    void UltraCanvasScatterPlot3DElement::RenderCloud(IRenderContext* ctx, const Camera& cam) {
        size_t n = dataSource->GetPointCount();

        // Points and correlation-line segments share one depth-sorted list so
        // the line weaves through the cloud with correct occlusion.
        struct DrawItem {
            double depth = 0.0;
            bool isSegment = false;
            Point2Dd a, b;          // segment endpoints (isSegment)
            double radius = 0.0;    // point radius (!isSegment)
            Color color;
            size_t index = 0;
        };
        std::vector<DrawItem> items;
        items.reserve(n + 64);

        projectedPoints.clear();
        projectedPoints.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            auto point = dataSource->GetPoint(i);
            Vec3 world = WorldFromData(point.x, point.y, point.z);

            DrawItem item;
            double depth;
            if (!Project(cam, world, item.a, depth)) continue;

            // Perspective size: a point at the orbit distance renders at
            // pointSize; nearer points grow, farther points shrink.
            item.depth = depth;
            item.radius = std::clamp(pointSize * distance / depth, 0.5, pointSize * 4.0);
            item.color = point.color.a > 0 ? point.color : pointColor;
            item.index = i;
            items.push_back(item);
        }

        if (showCorrelationLine) {
            RebuildFit();
            if (fitExists) {
                // Subdivide so segments sort into the cloud by depth.
                const int segments = 48;
                Vec3 prevWorld = WorldFromData(
                        fitCentroid.x + fitDirection.x * fitTMin,
                        fitCentroid.y + fitDirection.y * fitTMin,
                        fitCentroid.z + fitDirection.z * fitTMin);
                for (int s = 1; s <= segments; ++s) {
                    double t = fitTMin + (fitTMax - fitTMin) * s / segments;
                    Vec3 world = WorldFromData(fitCentroid.x + fitDirection.x * t,
                                               fitCentroid.y + fitDirection.y * t,
                                               fitCentroid.z + fitDirection.z * t);
                    Point2Dd sa, sb;
                    double da, db;
                    bool okA = Project(cam, prevWorld, sa, da);
                    bool okB = Project(cam, world, sb, db);
                    prevWorld = world;
                    if (!okA || !okB) continue;

                    DrawItem item;
                    item.isSegment = true;
                    item.a = sa;
                    item.b = sb;
                    item.depth = (da + db) * 0.5;
                    item.color = correlationLineColor;
                    items.push_back(item);
                }
            }
        }

        // Painter's algorithm: farthest first.
        std::sort(items.begin(), items.end(),
                  [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });

        double minDepth = std::numeric_limits<double>::infinity();
        double maxDepth = -std::numeric_limits<double>::infinity();
        for (const auto& item : items) {
            minDepth = std::min(minDepth, item.depth);
            maxDepth = std::max(maxDepth, item.depth);
        }
        double depthRange = (maxDepth > minDepth) ? maxDepth - minDepth : 1.0;

        for (const auto& item : items) {
            if (item.isSegment) {
                ctx->SetStrokePaint(item.color);
                ctx->SetStrokeWidth(correlationLineWidth);
                ctx->DrawLine(item.a, item.b);
                continue;
            }

            Color c = item.color;
            if (depthFade) {
                // Fade towards the background colour with distance.
                double t = (item.depth - minDepth) / depthRange * depthFadeStrength;
                auto mix = [t](uint8_t from, uint8_t to) {
                    return static_cast<uint8_t>(from + (to - from) * t);
                };
                c = Color(mix(c.r, backgroundColor.r),
                          mix(c.g, backgroundColor.g),
                          mix(c.b, backgroundColor.b), c.a);
            }
            DrawPointSprite(ctx, item.a, item.radius, c);
            projectedPoints.push_back({item.a, item.radius, item.index});
        }
    }

    void UltraCanvasScatterPlot3DElement::DrawPointSprite(IRenderContext* ctx, const Point2Dd& at,
                                                          double radius, const Color& color) {
        ctx->SetFillPaint(color);
        switch (pointShape) {
            case PointShape3D::Circle:
                ctx->FillCircle(at, radius);
                break;
            case PointShape3D::Square:
                ctx->FillRectangle(Rect2Dd(at.x - radius, at.y - radius,
                                           radius * 2.0, radius * 2.0));
                break;
            case PointShape3D::Diamond: {
                std::vector<Point2Dd> diamond = {
                        Point2Dd(at.x, at.y - radius),
                        Point2Dd(at.x + radius, at.y),
                        Point2Dd(at.x, at.y + radius),
                        Point2Dd(at.x - radius, at.y)
                };
                ctx->FillLinePath(diamond);
                break;
            }
        }
    }

    void UltraCanvasScatterPlot3DElement::RenderAxes3D(IRenderContext* ctx, const Camera& cam) {
        // The two ground axes are drawn along the edges meeting at the ground
        // corner nearest the camera, so they stay in front of the cloud and
        // stay readable as the view is orbited.
        Vec3 corners[4] = {
                Vec3(-1.0f, -1.0f, -1.0f),
                Vec3( 1.0f, -1.0f, -1.0f),
                Vec3( 1.0f, -1.0f,  1.0f),
                Vec3(-1.0f, -1.0f,  1.0f),
        };

        int nearest = 0;
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 4; ++i) {
            double d = (corners[i] - cam.eye).Dot(cam.forward);
            if (d < best) { best = d; nearest = i; }
        }

        Vec3 origin = corners[nearest];
        Vec3 alongX = origin;    // vary x, keep z
        Vec3 alongZ = origin;    // vary z, keep x
        alongX.x = -origin.x;
        alongZ.z = -origin.z;

        auto buildTicks = [&](bool isX) {
            std::vector<std::string> out;
            int n = isX ? xTickCount : yTickCount;
            double lo = isX ? cachedDataBounds.minX : cachedDataBounds.minY;
            double hi = isX ? cachedDataBounds.maxX : cachedDataBounds.maxY;
            const ValueFormatter& fmt = isX ? xTickFormatter : yTickFormatter;
            for (int i = 0; i < n; ++i) {
                out.push_back(FormatTick(lo + (hi - lo) * i / (n - 1), fmt));
            }
            return out;
        };

        // The axis runs from the near corner outward; when the near corner sits
        // at +1 the data runs backwards along it, so the labels are reversed.
        std::vector<std::string> xTicks = buildTicks(true);
        std::vector<std::string> yTicks = buildTicks(false);
        if (origin.x > 0.0f) std::reverse(xTicks.begin(), xTicks.end());
        if (origin.z > 0.0f) std::reverse(yTicks.begin(), yTicks.end());

        RenderAxisLine(ctx, cam, origin, alongX, xTicks, axisTitleX);
        RenderAxisLine(ctx, cam, origin, alongZ, yTicks, axisTitleY);

        // ---- vertical (data z) axis, rising from the far end of the X axis ----
        std::vector<std::string> zTicks;
        for (int i = 0; i < zTickCount; ++i) {
            zTicks.push_back(FormatTick(cachedDataBounds.minZ +
                                        (cachedDataBounds.maxZ - cachedDataBounds.minZ) * i / (zTickCount - 1),
                                        zTickFormatter));
        }
        Vec3 zBase = alongX;
        Vec3 zTop = zBase;
        zTop.y = 1.0f;
        RenderAxisLine(ctx, cam, zBase, zTop, zTicks, axisTitleZ);
    }

    void UltraCanvasScatterPlot3DElement::RenderAxisLine(
            IRenderContext* ctx, const Camera& cam,
            const Vec3& from, const Vec3& to,
            const std::vector<std::string>& tickTexts,
            const std::string& title) {
        Point2Dd sFrom, sTo;
        double dFrom, dTo;
        if (!Project(cam, from, sFrom, dFrom) || !Project(cam, to, sTo, dTo)) return;

        ctx->SetStrokePaint(axisColor);
        ctx->SetStrokeWidth(1.2);
        ctx->DrawLine(sFrom, sTo);

        double dx = sTo.x - sFrom.x;
        double dy = sTo.y - sFrom.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0) return;

        // Push labels away from the plot centre, perpendicular to the axis.
        double nx = -dy / len, ny = dx / len;
        double toCentreX = cam.cx - (sFrom.x + sTo.x) * 0.5;
        double toCentreY = cam.cy - (sFrom.y + sTo.y) * 0.5;
        if (nx * toCentreX + ny * toCentreY > 0.0) { nx = -nx; ny = -ny; }

        double angle = rotateAxisLabels ? UprightAngleScatter3D(std::atan2(dy, dx)) : 0.0;

        ctx->SetFontSize(axisLabelFontSize);
        ctx->SetTextPaint(axisLabelColor);

        const size_t n = tickTexts.size();
        for (size_t i = 0; i < n; ++i) {
            double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.5;
            Point2Dd at(sFrom.x + dx * t, sFrom.y + dy * t);

            // Small tick mark.
            ctx->SetStrokePaint(axisColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(at, Point2Dd(at.x + nx * 4.0, at.y + ny * 4.0));

            Size2Di sz = ctx->GetTextLineDimensions(tickTexts[i]);
            double offset = 8.0 + sz.height * 0.5;
            ctx->PushState();
            ctx->Translate(at.x + nx * offset, at.y + ny * offset);
            ctx->Rotate(angle);
            ctx->SetTextPaint(axisLabelColor);
            ctx->DrawText(tickTexts[i], Point2Dd(-sz.width / 2.0, -sz.height / 2.0));
            ctx->PopState();
        }

        if (!title.empty()) {
            Size2Di sz = ctx->GetTextLineDimensions(title);
            double titleOffset = 8.0 + sz.height * 0.5 + 16.0;
            Point2Dd mid(sFrom.x + dx * 0.5, sFrom.y + dy * 0.5);
            ctx->PushState();
            ctx->Translate(mid.x + nx * titleOffset, mid.y + ny * titleOffset);
            ctx->Rotate(angle);
            ctx->SetTextPaint(axisLabelColor);
            ctx->DrawText(title, Point2Dd(-sz.width / 2.0, -sz.height / 2.0));
            ctx->PopState();
        }
    }

    std::string UltraCanvasScatterPlot3DElement::FormatTick(double v, const ValueFormatter& fn) const {
        if (fn) return fn(v);
        double mag = std::max(std::fabs(v), 1e-12);
        int decimals = (mag >= 1000.0) ? 0 : (mag >= 10.0 ? 1 : 2);
        return FormatFixedScatter3D(v, decimals);
    }

// =============================================================================
// INTERACTION
// =============================================================================

    bool UltraCanvasScatterPlot3DElement::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!dataSource || !enableTooltips) return false;

        // Scan near-to-far so the visually frontmost point wins the tooltip.
        for (auto it = projectedPoints.rbegin(); it != projectedPoints.rend(); ++it) {
            double dx = mousePos.x - it->screen.x;
            double dy = mousePos.y - it->screen.y;
            if (dx * dx + dy * dy <= (it->radius + 3.0) * (it->radius + 3.0)) {
                auto point = dataSource->GetPoint(it->index);
                ShowChartPointTooltip(mousePos, point, it->index);
                return true;
            }
        }

        if (isTooltipActive) {
            HideTooltip();
        }
        return false;
    }

    std::string UltraCanvasScatterPlot3DElement::GenerateTooltipContent(const ChartDataPoint& point,
                                                                        size_t index) {
        if (customTooltipGenerator) {
            return customTooltipGenerator(point, index);
        }

        std::string content;
        if (!seriesName.empty()) content += seriesName + "\n";
        if (!point.label.empty()) content += point.label + "\n";
        content += "X: " + FormatAxisLabel(point.x) + "\n";
        content += "Y: " + FormatAxisLabel(point.y) + "\n";
        content += "Z: " + FormatAxisLabel(point.z);
        return content;
    }

    bool UltraCanvasScatterPlot3DElement::OnEvent(const UCEvent& event) {
        if (enableOrbit) {
            switch (event.type) {
                case UCEventType::MouseDown:
                    if (event.button == UCMouseButton::Left) {
                        dragging = true;
                        lastMouseX = event.pointer.x;
                        lastMouseY = event.pointer.y;
                        return true;
                    }
                    break;
                case UCEventType::MouseUp:
                    if (event.button == UCMouseButton::Left && dragging) {
                        dragging = false;
                        return true;
                    }
                    break;
                case UCEventType::MouseMove:
                    if (dragging) {
                        int dx = event.pointer.x - lastMouseX;
                        int dy = event.pointer.y - lastMouseY;
                        lastMouseX = event.pointer.x;
                        lastMouseY = event.pointer.y;
                        SetCamera(yaw - dx * 0.01, pitch + dy * 0.01, distance);
                        return true;
                    }
                    break;
                case UCEventType::MouseWheel:
                    SetCamera(yaw, pitch, distance * (event.wheelDelta > 0 ? 0.9 : 1.1));
                    return true;
                default:
                    break;
            }
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas
