// Plugins/Charts/UltraCanvasContourSurface3D.cpp
// Software-rendered 3D contour surface.
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend ColorBar mode.
// Author: UltraCanvas Framework
#include "Plugins/Charts/UltraCanvasContourSurface3D.h"
#include "UltraCanvasTooltipManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {

        std::string FormatFixed3D(double v, int decimals) {
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
        double UprightAngle(double angle) {
            if (angle > M_PI / 2.0) angle -= M_PI;
            else if (angle < -M_PI / 2.0) angle += M_PI;
            return angle;
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasContourSurface3DElement::UltraCanvasContourSurface3DElement(
            const std::string& id, int x, int y, int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        // The 2D grid/axes machinery of the base class does not apply here.
        showGrid = false;
        showAxes = false;
        enableTooltips = true;
    }

// =============================================================================
// DATA
// =============================================================================

    void UltraCanvasContourSurface3DElement::SetData(const std::vector<double>& flat,
                                                      int cols, int rows) {
        if (cols < 2 || rows < 2) { ClearData(); return; }
        field.Resize(cols, rows);
        field.values = flat;
        field.values.resize(static_cast<size_t>(cols) * rows, 0.0);
        fieldValid = true;
        RecomputeValueRange();
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetData(const std::vector<std::vector<double>>& matrix) {
        int rows = static_cast<int>(matrix.size());
        int cols = 0;
        for (const auto& row : matrix) cols = std::max(cols, static_cast<int>(row.size()));
        if (cols < 2 || rows < 2) { ClearData(); return; }

        std::vector<double> flat(static_cast<size_t>(cols) * rows, 0.0);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < static_cast<int>(matrix[r].size()); ++c) {
                flat[static_cast<size_t>(r) * cols + c] = matrix[r][c];
            }
        }
        SetData(flat, cols, rows);
    }

    void UltraCanvasContourSurface3DElement::SetFunction(
            const std::function<double(double, double)>& fn,
            int cols, int rows, double xMin, double xMax, double yMin, double yMax) {
        if (!fn || cols < 2 || rows < 2) return;
        std::vector<double> v(static_cast<size_t>(cols) * rows, 0.0);
        for (int r = 0; r < rows; ++r) {
            double y = yMin + (yMax - yMin) * r / (rows - 1);
            for (int c = 0; c < cols; ++c) {
                double x = xMin + (xMax - xMin) * c / (cols - 1);
                v[static_cast<size_t>(r) * cols + c] = fn(x, y);
            }
        }
        SetData(v, cols, rows);
        SetDataRange(xMin, xMax, yMin, yMax);
    }

    void UltraCanvasContourSurface3DElement::SetDataRange(double xMin, double xMax,
                                                          double yMin, double yMax) {
        field.SetExtents(xMin, xMax, yMin, yMax);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetValueRange(double lo, double hi) {
        valueMin = lo;
        valueMax = hi;
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetAutoValueRange() {
        RecomputeValueRange();
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::ClearData() {
        field = ContourGrid();
        fieldValid = false;
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::RecomputeValueRange() {
        double lo, hi;
        if (!field.ValueRange(lo, hi) || !(hi > lo)) {
            if (hi == lo) { lo -= 0.5; hi += 0.5; }
            else { lo = 0.0; hi = 1.0; }
        }
        valueMin = lo;
        valueMax = hi;
    }

    void UltraCanvasContourSurface3DElement::InvalidateDerived() {
        levelsValid = false;
        segmentsValid = false;
    }

// =============================================================================
// LEVELS / COLOUR
// =============================================================================

    void UltraCanvasContourSurface3DElement::SetLevelCount(int count) {
        levelMode = ContourLevelMode::Count;
        levelCount = std::max(1, count);
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLevelStep(double step, double origin) {
        levelMode = ContourLevelMode::Step;
        levelStep = step;
        levelOrigin = origin;
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLevels(const std::vector<double>& values) {
        levelMode = ContourLevelMode::Explicit;
        explicitLevels = values;
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetNiceLevels(bool on) {
        niceLevels = on;
        InvalidateDerived();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::RebuildLevels() const {
        if (levelsValid) return;
        levelsValid = true;
        levels = GenerateContourLevels(field, levelMode, levelCount, levelStep,
                                       levelOrigin, explicitLevels, niceLevels);
    }

    const std::vector<double>& UltraCanvasContourSurface3DElement::GetLevels() const {
        RebuildLevels();
        return levels;
    }

    void UltraCanvasContourSurface3DElement::RebuildSegments() const {
        if (segmentsValid) return;
        segmentsValid = true;
        surfaceSegments.clear();
        segmentCellStart.clear();
        if (!showSurfaceIsolines || !field.Valid()) return;

        RebuildLevels();
        for (double level : levels) {
            std::vector<ContourSegment> segs = ExtractContourSegments(field, level);
            surfaceSegments.insert(surfaceSegments.end(),
                                   std::make_move_iterator(segs.begin()),
                                   std::make_move_iterator(segs.end()));
        }

        // Bucket the segments by cell so the render pass can find a quad's
        // isolines in constant time. Scanning the whole segment list per quad
        // instead would be quadratic - tens of millions of comparisons a frame
        // on a moderate grid, which is far too slow to orbit.
        const int cellCols = field.cols - 1;
        const int cellRows = field.rows - 1;
        if (cellCols < 1 || cellRows < 1) return;

        std::sort(surfaceSegments.begin(), surfaceSegments.end(),
                  [cellCols](const ContourSegment& a, const ContourSegment& b) {
                      return (a.cellRow * cellCols + a.cellCol) <
                             (b.cellRow * cellCols + b.cellCol);
                  });

        const size_t cellCount = static_cast<size_t>(cellCols) * cellRows;
        segmentCellStart.assign(cellCount + 1, 0);
        for (const auto& s : surfaceSegments) {
            size_t cell = static_cast<size_t>(s.cellRow) * cellCols + s.cellCol;
            if (cell < cellCount) ++segmentCellStart[cell + 1];
        }
        for (size_t i = 1; i <= cellCount; ++i) {
            segmentCellStart[i] += segmentCellStart[i - 1];
        }
    }

    void UltraCanvasContourSurface3DElement::SetColormap(HeatmapColormap c) {
        colormap = c;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetCustomColormap(const std::vector<Color>& colors) {
        customColormap = colors;
        colormap = HeatmapColormap::Custom;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetReverseColormap(bool on) {
        reverseColormap = on;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetSurfaceColorMode(SurfaceColorMode mode) {
        colorMode = mode;
        RequestRedraw();
    }

// =============================================================================
// APPEARANCE / CAMERA / AXES / LEGEND SETTERS
// =============================================================================

    void UltraCanvasContourSurface3DElement::SetShowWireframe(bool on, const Color& c, float width) {
        showWireframe = on;
        wireframeColor = c;
        wireframeWidth = std::max(0.1f, width);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetShowSurfaceIsolines(bool on, const Color& c, float width) {
        showSurfaceIsolines = on;
        isolineColor = c;
        isolineWidth = std::max(0.1f, width);
        segmentsValid = false;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetZExaggeration(double factor) {
        zExaggeration = std::clamp(factor, 0.01, 20.0);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetHeightSpan(double span) {
        heightSpan = std::clamp(span, 0.01, 5.0);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLighting(bool enabled, double ambientLevel) {
        lightingEnabled = enabled;
        ambient = std::clamp(ambientLevel, 0.0, 1.0);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLightDirection(const Vec3& dir) {
        lightDirection = dir.Normalized();
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetCamera(double yawRadians, double pitchRadians,
                                                        double cameraDistance) {
        yaw = yawRadians;
        const double limit = M_PI / 2.0 - 0.02;
        pitch = std::clamp(pitchRadians, -limit, limit);
        distance = std::clamp(cameraDistance, 0.6, 40.0);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetFieldOfView(double radians) {
        fieldOfView = std::clamp(radians, 0.1, 2.0);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetEnableOrbit(bool on) { enableOrbit = on; }

    // The ground box spans [-1,1] in x and z, so its corners sit at radius
    // ~1.41; at the default field of view that needs a camera distance of about
    // 4.6 for the whole surface plus its axis labels to stay in frame.
    void UltraCanvasContourSurface3DElement::ViewIsometric() { SetCamera(-0.62, 0.42, 4.6); }
    void UltraCanvasContourSurface3DElement::ViewTop()       { SetCamera(0.0, M_PI / 2.0 - 0.03, 4.6); }
    void UltraCanvasContourSurface3DElement::ViewFront()     { SetCamera(0.0, 0.06, 4.6); }

    void UltraCanvasContourSurface3DElement::SetShowAxes3D(bool on) {
        showAxes3D = on;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetAxisTitles(const std::string& x,
                                                            const std::string& y,
                                                            const std::string& z) {
        axisTitleX = x; axisTitleY = y; axisTitleZ = z;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetCategoryLabelsX(const std::vector<std::string>& labels) {
        categoryLabelsX = labels;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetCategoryLabelsY(const std::vector<std::string>& labels) {
        categoryLabelsY = labels;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetTickCounts(int xTicks, int yTicks, int zTicks) {
        xTickCount = std::max(2, xTicks);
        yTickCount = std::max(2, yTicks);
        zTickCount = std::max(2, zTicks);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetXTickFormatter(ValueFormatter fn) {
        xTickFormatter = std::move(fn); RequestRedraw();
    }
    void UltraCanvasContourSurface3DElement::SetYTickFormatter(ValueFormatter fn) {
        yTickFormatter = std::move(fn); RequestRedraw();
    }
    void UltraCanvasContourSurface3DElement::SetZTickFormatter(ValueFormatter fn) {
        zTickFormatter = std::move(fn); RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetAxisColors(const Color& line, const Color& label) {
        axisColor = line;
        axisLabelColor = label;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetAxisLabelFontSize(float size) {
        axisLabelFontSize = std::max(1.0f, size);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetRotateAxisLabels(bool on) {
        rotateAxisLabels = on;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLegendMode(SurfaceLegendMode mode) {
        legendMode = mode;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLegendTitle(const std::string& title) {
        legendTitle = title;
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetLegendDecimals(int decimals) {
        legendDecimals = std::clamp(decimals, 0, 12);
        RequestRedraw();
    }

    void UltraCanvasContourSurface3DElement::SetTitleColor(const Color& c) {
        titleColor = c;
        RequestRedraw();
    }

// =============================================================================
// COLOUR
// =============================================================================

    double UltraCanvasContourSurface3DElement::NormalizeValue(double v) const {
        if (std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();
        if (valueMax <= valueMin) return 0.0;
        return std::clamp((v - valueMin) / (valueMax - valueMin), 0.0, 1.0);
    }

    Color UltraCanvasContourSurface3DElement::ColorForValue(double v) const {
        if (colorMode == SurfaceColorMode::Banded) {
            RebuildLevels();
            size_t band = static_cast<size_t>(
                    std::upper_bound(levels.begin(), levels.end(), v) - levels.begin());
            return BandColor(band);
        }
        return SampleColormap(colormap, customColormap, NormalizeValue(v), reverseColormap);
    }

    Color UltraCanvasContourSurface3DElement::ShadeColor(const Color& base, const Vec3& normal) const {
        if (!lightingEnabled) return base;
        Vec3 n = normal;
        if (n.y < 0.0f) n = Vec3(-n.x, -n.y, -n.z);   // light both faces equally
        double lambert = std::max(0.0, static_cast<double>(n.Dot(lightDirection)));
        double k = ambient + (1.0 - ambient) * lambert;
        auto ch = [k](uint8_t c) {
            return static_cast<uint8_t>(std::clamp(c * k, 0.0, 255.0));
        };
        return Color(ch(base.r), ch(base.g), ch(base.b), base.a);
    }

    size_t UltraCanvasContourSurface3DElement::BandCount() const {
        RebuildLevels();
        return levels.size() + 1;
    }

    void UltraCanvasContourSurface3DElement::BandRange(size_t band, double& lo, double& hi) const {
        RebuildLevels();
        lo = (band == 0) ? valueMin : levels[band - 1];
        hi = (band >= levels.size()) ? valueMax : levels[band];
    }

    Color UltraCanvasContourSurface3DElement::BandColor(size_t band) const {
        double lo, hi;
        BandRange(band, lo, hi);
        return SampleColormap(colormap, customColormap,
                              NormalizeValue((lo + hi) * 0.5), reverseColormap);
    }

    std::string UltraCanvasContourSurface3DElement::FormatTick(double v, const ValueFormatter& fn) const {
        if (fn) return fn(v);
        double mag = std::max(std::fabs(v), 1e-12);
        int decimals = (mag >= 1000.0) ? 0 : (mag >= 10.0 ? 1 : 2);
        return FormatFixed3D(v, decimals);
    }

// =============================================================================
// PROJECTION
// =============================================================================

    Vec3 UltraCanvasContourSurface3DElement::WorldAt(double col, double row, double value) const {
        double u = (field.cols > 1) ? col / (field.cols - 1) : 0.5;
        double v = (field.rows > 1) ? row / (field.rows - 1) : 0.5;
        double h = NormalizeValue(value);
        if (std::isnan(h)) h = 0.0;
        double y = (h - 0.5) * heightSpan * zExaggeration;
        return Vec3(static_cast<float>(u * 2.0 - 1.0),
                    static_cast<float>(y),
                    static_cast<float>(v * 2.0 - 1.0));
    }

    Vec3 UltraCanvasContourSurface3DElement::WorldAtNode(int c, int r) const {
        return WorldAt(c, r, field.At(c, r));
    }

    UltraCanvasContourSurface3DElement::Camera
    UltraCanvasContourSurface3DElement::BuildCamera(const ChartPlotArea& area) const {
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

    bool UltraCanvasContourSurface3DElement::Project(const Camera& cam, const Vec3& p,
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
// LAYOUT
// =============================================================================

    ChartPlotArea UltraCanvasContourSurface3DElement::ComputeSurfaceArea(IRenderContext* ctx) {
        double left = 8.0, right = 8.0, top = 8.0, bottom = 8.0;
        ctx->SetFontSize(11.0);
        int lineH = ctx->GetTextLineHeight("0");

        if (!chartTitle.empty()) top += lineH + 12.0;

        double w = std::max(1.0, static_cast<double>(GetWidth()) - left - right);
        double h = std::max(1.0, static_cast<double>(GetHeight()) - top - bottom);
        legendContentArea = Rect2Dd(left, top, w, h);

        // The shared legend measures itself and hands back the plot area.
        SyncLegend();
        legend.Measure(ctx, legendContentArea);
        Rect2Dd plot = legend.RemainingArea(legendContentArea);
        return ChartPlotArea(plot.x, plot.y,
                             std::max(1.0, plot.width), std::max(1.0, plot.height));
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasContourSurface3DElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        if (showBackground) {
            ctx->DrawFilledRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()), backgroundColor);
        }
        if (!fieldValid || !field.Valid()) {
            DrawEmptyState(ctx);
            return;
        }

        cachedPlotArea = ComputeSurfaceArea(ctx);

        if (!chartTitle.empty()) {
            ctx->SetFontSize(15.0);
            ctx->SetTextPaint(titleColor);
            Size2Di sz = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - sz.width / 2.0, 6));
        }

        RenderChart(ctx);
    }

    void UltraCanvasContourSurface3DElement::RenderChart(IRenderContext* ctx) {
        if (!ctx || !field.Valid()) return;

        RebuildLevels();
        RebuildSegments();

        const ChartPlotArea& area = cachedPlotArea;
        Camera cam = BuildCamera(area);

        ctx->PushState();
        ctx->ClipRect(area.ToRect2D());
        if (showAxes3D) RenderAxes3D(ctx, cam);
        RenderSurface(ctx, cam);
        ctx->PopState();

        legend.Render(ctx, legendContentArea);
    }

    void UltraCanvasContourSurface3DElement::RenderSurface(IRenderContext* ctx, const Camera& cam) {
        const int cellCols = field.cols - 1;
        const int cellRows = field.rows - 1;
        if (cellCols < 1 || cellRows < 1) return;

        // Very fine grids are decimated for display: a heightfield with more
        // quads than screen pixels costs a lot and shows nothing extra.
        int stride = 1;
        while ((cellCols / stride) * (cellRows / stride) > 40000) ++stride;

        quadBuffer.clear();
        quadBuffer.reserve(static_cast<size_t>(cellCols / stride + 1) * (cellRows / stride + 1));

        for (int r = 0; r < cellRows; r += stride) {
            int r1 = std::min(r + stride, field.rows - 1);
            for (int c = 0; c < cellCols; c += stride) {
                int c1 = std::min(c + stride, field.cols - 1);

                double v00 = field.At(c, r),  v10 = field.At(c1, r);
                double v11 = field.At(c1, r1), v01 = field.At(c, r1);
                if (std::isnan(v00) || std::isnan(v10) || std::isnan(v11) || std::isnan(v01)) {
                    continue;
                }

                Vec3 p0 = WorldAt(c,  r,  v00);
                Vec3 p1 = WorldAt(c1, r,  v10);
                Vec3 p2 = WorldAt(c1, r1, v11);
                Vec3 p3 = WorldAt(c,  r1, v01);

                SurfaceQuad q;
                double d0, d1, d2, d3;
                if (!Project(cam, p0, q.screen[0], d0) || !Project(cam, p1, q.screen[1], d1) ||
                    !Project(cam, p2, q.screen[2], d2) || !Project(cam, p3, q.screen[3], d3)) {
                    continue;
                }
                q.depth = (d0 + d1 + d2 + d3) * 0.25;

                Vec3 normal = (p1 - p0).Cross(p3 - p0).Normalized();
                double mid = (v00 + v10 + v11 + v01) * 0.25;
                q.color = ShadeColor(ColorForValue(mid), normal);
                q.cellCol = c;
                q.cellRow = r;
                q.visible = true;
                quadBuffer.push_back(q);
            }
        }

        // Painter's algorithm: farthest first. A height field has no cyclic
        // overlaps, so a centroid-depth sort resolves it correctly.
        std::sort(quadBuffer.begin(), quadBuffer.end(),
                  [](const SurfaceQuad& a, const SurfaceQuad& b) { return a.depth > b.depth; });

        std::vector<Point2Dd> poly(4);
        std::vector<Point2Dd> seg(2);

        for (const auto& q : quadBuffer) {
            poly[0] = q.screen[0];
            poly[1] = q.screen[1];
            poly[2] = q.screen[2];
            poly[3] = q.screen[3];

            ctx->SetFillPaint(q.color);
            ctx->FillLinePath(poly);

            if (showWireframe && wireframeColor.a > 0) {
                ctx->SetStrokePaint(wireframeColor);
                ctx->SetStrokeWidth(wireframeWidth);
                ctx->DrawLinePath(poly, true);
            }

            // Isolines belonging to this quad are drawn immediately after it,
            // so the painter's ordering occludes them exactly like the surface.
            if (showSurfaceIsolines && !segmentCellStart.empty() && stride == 1) {
                size_t cell = static_cast<size_t>(q.cellRow) * (field.cols - 1) + q.cellCol;
                if (cell + 1 < segmentCellStart.size()) {
                    size_t from = segmentCellStart[cell];
                    size_t to = segmentCellStart[cell + 1];
                    if (from < to) {
                        ctx->SetStrokePaint(isolineColor);
                        ctx->SetStrokeWidth(isolineWidth);
                        for (size_t i = from; i < to; ++i) {
                            const ContourSegment& s = surfaceSegments[i];
                            Point2Dd sa, sb;
                            double da, db;
                            if (!Project(cam, WorldAt(s.a.x, s.a.y, s.level), sa, da)) continue;
                            if (!Project(cam, WorldAt(s.b.x, s.b.y, s.level), sb, db)) continue;
                            seg[0] = sa;
                            seg[1] = sb;
                            ctx->DrawLinePath(seg, false);
                        }
                    }
                }
            }
        }
    }

    void UltraCanvasContourSurface3DElement::RenderAxes3D(IRenderContext* ctx, const Camera& cam) {
        double floorY = -0.5 * heightSpan * zExaggeration;

        // The two ground axes are drawn along the edges meeting at the ground
        // corner nearest the camera, so they stay in front of the surface and
        // stay readable as the view is orbited.
        Vec3 corners[4] = {
                Vec3(-1.0f, static_cast<float>(floorY), -1.0f),
                Vec3( 1.0f, static_cast<float>(floorY), -1.0f),
                Vec3( 1.0f, static_cast<float>(floorY),  1.0f),
                Vec3(-1.0f, static_cast<float>(floorY),  1.0f),
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

        // ---- tick label text ----
        auto buildTicks = [&](bool isX) {
            std::vector<std::string> out;
            const std::vector<std::string>& cats = isX ? categoryLabelsX : categoryLabelsY;
            if (!cats.empty()) return cats;
            int n = isX ? xTickCount : yTickCount;
            double lo = isX ? field.xMin : field.yMin;
            double hi = isX ? field.xMax : field.yMax;
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

        RenderAxisLine(ctx, cam, origin, alongX, xTicks, axisTitleX, true);
        RenderAxisLine(ctx, cam, origin, alongZ, yTicks, axisTitleY, false);

        // ---- vertical (value) axis, rising from the far end of the X axis ----
        std::vector<std::string> zTicks;
        for (int i = 0; i < zTickCount; ++i) {
            zTicks.push_back(FormatTick(valueMin + (valueMax - valueMin) * i / (zTickCount - 1),
                                        zTickFormatter));
        }
        Vec3 zBase = alongX;
        Vec3 zTop = zBase;
        zTop.y = static_cast<float>(floorY + heightSpan * zExaggeration);
        RenderAxisLine(ctx, cam, zBase, zTop, zTicks, axisTitleZ, true);
    }

    void UltraCanvasContourSurface3DElement::RenderAxisLine(
            IRenderContext* ctx, const Camera& cam,
            const Vec3& from, const Vec3& to,
            const std::vector<std::string>& tickTexts,
            const std::string& title, bool outwardLeft) {
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

        double angle = rotateAxisLabels ? UprightAngle(std::atan2(dy, dx)) : 0.0;

        ctx->SetFontSize(axisLabelFontSize);
        ctx->SetTextPaint(axisLabelColor);

        const size_t n = tickTexts.size();
        for (size_t i = 0; i < n; ++i) {
            // Categorical ticks sit at slot centres; numeric ticks at the ends.
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
        (void)outwardLeft;
    }

    void UltraCanvasContourSurface3DElement::SyncLegend() {
        legend.SetVisible(legendMode != SurfaceLegendMode::NoLegend && fieldValid);
        legend.SetMode(ChartLegendMode::ColorBar);
        legend.SetPosition(legendMode == SurfaceLegendMode::Vertical
                                   ? ChartLegendPosition::RightStart
                                   : ChartLegendPosition::BottomCenter);
        legend.SetTitle(legendTitle);

        LegendColorBar bar;
        bar.colormap = colormap;
        bar.customColormap = customColormap;
        bar.reverse = reverseColormap;
        bar.minValue = valueMin;
        bar.maxValue = valueMax;
        if (colorMode == SurfaceColorMode::Banded) {
            // Quantized bands mirror the banded surface colouring; ticks sit
            // on the band boundaries.
            bar.quantizeLevels = static_cast<int>(BandCount());
            bar.tickCount = std::clamp(bar.quantizeLevels + 1, 2, 11);
        } else {
            bar.quantizeLevels = 0;   // continuous ramp
            bar.tickCount = 5;
        }
        bar.barLength = 200.0;        // clamped to the available span by Measure()
        int decimals = legendDecimals;
        bar.formatter = [decimals](double v) { return FormatFixed3D(v, decimals); };
        legend.SetColorBar(bar);

        ChartLegendStyle ls;
        ls.fontSize = 10.0f;
        ls.textColor = axisLabelColor;
        ls.titleFontSize = 10.0f;
        ls.titleColor = axisLabelColor;
        legend.SetStyle(ls);
    }

// =============================================================================
// INTERACTION
// =============================================================================

    bool UltraCanvasContourSurface3DElement::HandleChartMouseMove(const Point2Di& mousePos) {
        return false;   // orbiting is handled directly in OnEvent
    }

    bool UltraCanvasContourSurface3DElement::OnEvent(const UCEvent& event) {
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
                    // Ease the dolly in rather than stepping it (see
                    // UltraCanvasSmoothScroll.h); each step is the same
                    // SetCamera call a single one made.
                    if (!dollyAnim.IsBound()) {
                        // SetCamera already asks for a repaint, so the animator
                        // needs no separate one.
                        dollyAnim.Bind(
                            [this](double f) { SetCamera(yaw, pitch, distance * f); },
                            [] {});
                    }
                    // The same limits SetCamera clamps the distance to.
                    dollyAnim.ZoomBy(event.wheelDelta > 0 ? 0.9 : 1.1,
                                     distance, 0.6, 40.0);
                    return true;
                default:
                    break;
            }
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

} // namespace UltraCanvas
