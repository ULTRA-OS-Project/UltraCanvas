// Plugins/Charts/UltraCanvasContourSurfaceGL.cpp
// GL-backed 3D contour surface (roadmap item T6). Compiles to nothing extra
// without ULTRACANVAS_ENABLE_GL - the header then aliases the class onto the
// software renderer.
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend ColorBar mode.
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasContourSurfaceGL.h"

#ifdef ULTRACANVAS_ENABLE_GL

#include "UltraCanvasDebug.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

// GL headers must live at global scope (not inside namespace UltraCanvas).
// Include strategy matches Plugins/Models/STL/UltraCanvasSTLElement.cpp.
#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
    #include <OpenGL/gl3ext.h>
#elif defined(_WIN32)
    #include <GL/glew.h>
#else
    #ifndef GL_GLEXT_PROTOTYPES
    #define GL_GLEXT_PROTOTYPES
    #endif
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

    namespace {

        // Interleaved vertex layout: position(3) + normal(3) + colour(4).
        constexpr int kFloatsPerVertex = 10;

        const char* kSurfaceVertexShader =
                "#version 330 core\n"
                "layout(location = 0) in vec3 aPos;\n"
                "layout(location = 1) in vec3 aNormal;\n"
                "layout(location = 2) in vec4 aColor;\n"
                "uniform mat4 uMVP;\n"
                "out vec3 vNormal;\n"
                "out vec4 vColor;\n"
                "void main() {\n"
                "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
                "    vNormal = aNormal;\n"
                "    vColor = aColor;\n"
                "}\n";

        const char* kSurfaceFragmentShader =
                "#version 330 core\n"
                "in vec3 vNormal;\n"
                "in vec4 vColor;\n"
                "uniform vec3 uLightDir;\n"
                "uniform float uAmbient;\n"
                "uniform int uLightingOn;\n"
                "uniform int uOverrideOn;\n"
                "uniform vec4 uOverrideColor;\n"
                "out vec4 FragColor;\n"
                "void main() {\n"
                "    if (uOverrideOn == 1) { FragColor = uOverrideColor; return; }\n"
                "    float k = 1.0;\n"
                "    if (uLightingOn == 1) {\n"
                "        vec3 n = normalize(vNormal);\n"
                "        if (n.y < 0.0) n = -n;\n"           // light both faces equally
                "        float lambert = max(dot(n, normalize(uLightDir)), 0.0);\n"
                "        k = uAmbient + (1.0 - uAmbient) * lambert;\n"
                "    }\n"
                "    FragColor = vec4(vColor.rgb * k, vColor.a);\n"
                "}\n";

        unsigned int CompileContourShader(unsigned int type, const char* src) {
            unsigned int shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            int ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024] = {0};
                glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
                debugOutput << "[ContourGL] Shader compile failed: " << log << std::endl;
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        unsigned int LinkContourProgram(unsigned int vs, unsigned int fs) {
            unsigned int prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glLinkProgram(prog);
            int ok = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[1024] = {0};
                glGetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
                debugOutput << "[ContourGL] Program link failed: " << log << std::endl;
                glDeleteProgram(prog);
                return 0;
            }
            return prog;
        }

        std::string FormatFixedGL(double v, int decimals) {
            if (std::isnan(v)) return "NaN";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 12), v);
            std::string s(buf);
            if (s.find_first_not_of("-0.") == std::string::npos && !s.empty() && s[0] == '-') {
                s.erase(s.begin());
            }
            return s;
        }

        double UprightAngleGL(double angle) {
            if (angle > M_PI / 2.0) angle -= M_PI;
            else if (angle < -M_PI / 2.0) angle += M_PI;
            return angle;
        }

        void PushVertex(std::vector<float>& out, const Vec3& p, const Vec3& n, const Color& c) {
            out.push_back(p.x); out.push_back(p.y); out.push_back(p.z);
            out.push_back(n.x); out.push_back(n.y); out.push_back(n.z);
            out.push_back(c.r / 255.0f); out.push_back(c.g / 255.0f);
            out.push_back(c.b / 255.0f); out.push_back(c.a / 255.0f);
        }

    } // namespace

// =============================================================================
// CONSTRUCTION / DATA
// =============================================================================

    UltraCanvasContourSurfaceGLElement::UltraCanvasContourSurfaceGLElement(
            const std::string& id, float x, float y, float width, float height)
            : UltraCanvasGLSurface(id, x, y, width, height) {
        // 3.3 core with a depth buffer (the GLSurface default) is what we need.
        SetRenderMode(RenderMode::OnDemand);
    }

    UltraCanvasContourSurfaceGLElement::~UltraCanvasContourSurfaceGLElement() = default;

    void UltraCanvasContourSurfaceGLElement::SetData(const std::vector<double>& flat,
                                                     int cols, int rows) {
        if (cols < 2 || rows < 2) { ClearData(); return; }
        field.Resize(cols, rows);
        field.values = flat;
        field.values.resize(static_cast<size_t>(cols) * rows, 0.0);
        fieldValid = true;
        RecomputeValueRange();
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetData(const std::vector<std::vector<double>>& matrix) {
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

    void UltraCanvasContourSurfaceGLElement::SetFunction(
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

    void UltraCanvasContourSurfaceGLElement::SetDataRange(double xMin, double xMax,
                                                          double yMin, double yMax) {
        field.SetExtents(xMin, xMax, yMin, yMax);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetValueRange(double lo, double hi) {
        valueMin = lo;
        valueMax = hi;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetAutoValueRange() {
        RecomputeValueRange();
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::ClearData() {
        field = ContourGrid();
        fieldValid = false;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::RecomputeValueRange() {
        double lo, hi;
        if (!field.ValueRange(lo, hi) || !(hi > lo)) {
            if (hi == lo) { lo -= 0.5; hi += 0.5; }
            else { lo = 0.0; hi = 1.0; }
        }
        valueMin = lo;
        valueMax = hi;
    }

    void UltraCanvasContourSurfaceGLElement::InvalidateMesh() {
        levelsValid = false;
        meshDirty = true;
        RequestRender();
    }

// =============================================================================
// LEVELS / COLOUR SETTERS
// =============================================================================

    void UltraCanvasContourSurfaceGLElement::SetLevelCount(int count) {
        levelMode = ContourLevelMode::Count;
        levelCount = std::max(1, count);
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetLevelStep(double step, double origin) {
        levelMode = ContourLevelMode::Step;
        levelStep = step;
        levelOrigin = origin;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetLevels(const std::vector<double>& values) {
        levelMode = ContourLevelMode::Explicit;
        explicitLevels = values;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetNiceLevels(bool on) {
        niceLevels = on;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::RebuildLevels() const {
        if (levelsValid) return;
        levelsValid = true;
        levels = GenerateContourLevels(field, levelMode, levelCount, levelStep,
                                       levelOrigin, explicitLevels, niceLevels);
    }

    const std::vector<double>& UltraCanvasContourSurfaceGLElement::GetLevels() const {
        RebuildLevels();
        return levels;
    }

    void UltraCanvasContourSurfaceGLElement::SetColormap(HeatmapColormap c) {
        colormap = c;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetCustomColormap(const std::vector<Color>& colors) {
        customColormap = colors;
        colormap = HeatmapColormap::Custom;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetReverseColormap(bool on) {
        reverseColormap = on;
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetSurfaceColorMode(SurfaceColorMode mode) {
        colorMode = mode;
        InvalidateMesh();
    }

// =============================================================================
// APPEARANCE / CAMERA / AXES / LEGEND SETTERS
// =============================================================================

    void UltraCanvasContourSurfaceGLElement::SetShowWireframe(bool on, const Color& c, float width) {
        showWireframe = on;
        wireframeColor = c;
        wireframeWidth = std::max(0.1f, width);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetShowSurfaceIsolines(bool on, const Color& c,
                                                                    float width) {
        showSurfaceIsolines = on;
        isolineColor = c;
        isolineWidth = std::max(0.1f, width);
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetZExaggeration(double factor) {
        zExaggeration = std::clamp(factor, 0.01, 20.0);
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetHeightSpan(double span) {
        heightSpan = std::clamp(span, 0.01, 5.0);
        InvalidateMesh();
    }

    void UltraCanvasContourSurfaceGLElement::SetLighting(bool enabled, double ambientLevel) {
        lightingEnabled = enabled;
        ambient = std::clamp(ambientLevel, 0.0, 1.0);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetLightDirection(const Vec3& dir) {
        lightDirection = dir.Normalized();
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetCamera(double yawRadians, double pitchRadians,
                                                       double cameraDistance) {
        yaw = yawRadians;
        const double limit = M_PI / 2.0 - 0.02;
        pitch = std::clamp(pitchRadians, -limit, limit);
        distance = std::clamp(cameraDistance, 0.6, 40.0);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetFieldOfView(double radians) {
        fieldOfView = std::clamp(radians, 0.1, 2.0);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetAutoRotate(bool on) {
        autoRotate = on;
        SetRenderMode(on ? RenderMode::Continuous : RenderMode::OnDemand);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::ViewIsometric() { SetCamera(-0.62, 0.42, 4.6); }
    void UltraCanvasContourSurfaceGLElement::ViewTop()       { SetCamera(0.0, M_PI / 2.0 - 0.03, 4.6); }
    void UltraCanvasContourSurfaceGLElement::ViewFront()     { SetCamera(0.0, 0.06, 4.6); }

    void UltraCanvasContourSurfaceGLElement::SetShowAxes3D(bool on) {
        showAxes3D = on;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetAxisTitles(const std::string& x,
                                                           const std::string& y,
                                                           const std::string& z) {
        axisTitleX = x; axisTitleY = y; axisTitleZ = z;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetCategoryLabelsX(const std::vector<std::string>& labels) {
        categoryLabelsX = labels;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetCategoryLabelsY(const std::vector<std::string>& labels) {
        categoryLabelsY = labels;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetTickCounts(int xTicks, int yTicks, int zTicks) {
        xTickCount = std::max(2, xTicks);
        yTickCount = std::max(2, yTicks);
        zTickCount = std::max(2, zTicks);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetXTickFormatter(ValueFormatter fn) {
        xTickFormatter = std::move(fn); RequestRender();
    }
    void UltraCanvasContourSurfaceGLElement::SetYTickFormatter(ValueFormatter fn) {
        yTickFormatter = std::move(fn); RequestRender();
    }
    void UltraCanvasContourSurfaceGLElement::SetZTickFormatter(ValueFormatter fn) {
        zTickFormatter = std::move(fn); RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetAxisColors(const Color& line, const Color& label) {
        axisColor = line;
        axisLabelColor = label;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetAxisLabelFontSize(float size) {
        axisLabelFontSize = std::max(1.0f, size);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetRotateAxisLabels(bool on) {
        rotateAxisLabels = on;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetLegendMode(SurfaceLegendMode mode) {
        legendMode = mode;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetLegendTitle(const std::string& title) {
        legendTitle = title;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetLegendDecimals(int decimals) {
        legendDecimals = std::clamp(decimals, 0, 12);
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetChartTitle(const std::string& title) {
        chartTitle = title;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetTitleColor(const Color& c) {
        titleColor = c;
        RequestRender();
    }

    void UltraCanvasContourSurfaceGLElement::SetBackgroundColor(const Color& c) {
        backgroundColor = c;
        RequestRender();
    }

// =============================================================================
// COLOUR / GEOMETRY HELPERS
// =============================================================================

    double UltraCanvasContourSurfaceGLElement::NormalizeValue(double v) const {
        if (std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();
        if (valueMax <= valueMin) return 0.0;
        return std::clamp((v - valueMin) / (valueMax - valueMin), 0.0, 1.0);
    }

    Color UltraCanvasContourSurfaceGLElement::ColorForValue(double v) const {
        if (colorMode == SurfaceColorMode::Banded) {
            RebuildLevels();
            size_t band = static_cast<size_t>(
                    std::upper_bound(levels.begin(), levels.end(), v) - levels.begin());
            return BandColor(band);
        }
        return SampleColormap(colormap, customColormap, NormalizeValue(v), reverseColormap);
    }

    size_t UltraCanvasContourSurfaceGLElement::BandCount() const {
        RebuildLevels();
        return levels.size() + 1;
    }

    void UltraCanvasContourSurfaceGLElement::BandRange(size_t band, double& lo, double& hi) const {
        RebuildLevels();
        lo = (band == 0) ? valueMin : levels[band - 1];
        hi = (band >= levels.size()) ? valueMax : levels[band];
    }

    Color UltraCanvasContourSurfaceGLElement::BandColor(size_t band) const {
        double lo, hi;
        BandRange(band, lo, hi);
        return SampleColormap(colormap, customColormap,
                              NormalizeValue((lo + hi) * 0.5), reverseColormap);
    }

    std::string UltraCanvasContourSurfaceGLElement::FormatTick(double v,
                                                               const ValueFormatter& fn) const {
        if (fn) return fn(v);
        double mag = std::max(std::fabs(v), 1e-12);
        int decimals = (mag >= 1000.0) ? 0 : (mag >= 10.0 ? 1 : 2);
        return FormatFixedGL(v, decimals);
    }

    Vec3 UltraCanvasContourSurfaceGLElement::WorldAt(double col, double row, double value) const {
        double u = (field.cols > 1) ? col / (field.cols - 1) : 0.5;
        double v = (field.rows > 1) ? row / (field.rows - 1) : 0.5;
        double h = NormalizeValue(value);
        if (std::isnan(h)) h = 0.0;
        double y = (h - 0.5) * heightSpan * zExaggeration;
        return Vec3(static_cast<float>(u * 2.0 - 1.0),
                    static_cast<float>(y),
                    static_cast<float>(v * 2.0 - 1.0));
    }

    Vec3 UltraCanvasContourSurfaceGLElement::NodeNormal(int c, int r) const {
        // Central differences over the world-space height field. A NaN or
        // out-of-range neighbour falls back to the node itself, which flattens
        // the slope contribution instead of poisoning it.
        auto heightAt = [this](int cc, int rr, double fallback) {
            double v = field.At(std::clamp(cc, 0, field.cols - 1),
                                std::clamp(rr, 0, field.rows - 1));
            double h = NormalizeValue(v);
            if (std::isnan(h)) return fallback;
            return (h - 0.5) * heightSpan * zExaggeration;
        };

        double h0n = NormalizeValue(field.At(c, r));
        double h0 = std::isnan(h0n) ? 0.0 : (h0n - 0.5) * heightSpan * zExaggeration;
        double stepX = 2.0 / std::max(1, field.cols - 1);
        double stepZ = 2.0 / std::max(1, field.rows - 1);

        double dhdx = (heightAt(c + 1, r, h0) - heightAt(c - 1, r, h0)) /
                      ((c > 0 && c < field.cols - 1) ? 2.0 * stepX : stepX);
        double dhdz = (heightAt(c, r + 1, h0) - heightAt(c, r - 1, h0)) /
                      ((r > 0 && r < field.rows - 1) ? 2.0 * stepZ : stepZ);

        return Vec3(static_cast<float>(-dhdx), 1.0f, static_cast<float>(-dhdz)).Normalized();
    }

// =============================================================================
// GL PIPELINE
// =============================================================================

    void UltraCanvasContourSurfaceGLElement::OnGLInit() {
        unsigned int vs = CompileContourShader(GL_VERTEX_SHADER, kSurfaceVertexShader);
        unsigned int fs = CompileContourShader(GL_FRAGMENT_SHADER, kSurfaceFragmentShader);
        if (vs && fs) {
            program = LinkContourProgram(vs, fs);
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);

        if (program) {
            uMVP = glGetUniformLocation(program, "uMVP");
            uLightDir = glGetUniformLocation(program, "uLightDir");
            uAmbient = glGetUniformLocation(program, "uAmbient");
            uLightingOn = glGetUniformLocation(program, "uLightingOn");
            uOverrideOn = glGetUniformLocation(program, "uOverrideOn");
            uOverrideColor = glGetUniformLocation(program, "uOverrideColor");
        }

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glGenBuffers(1, &wireEbo);
        glGenVertexArrays(1, &lineVao);
        glGenBuffers(1, &lineVbo);

        glReady = (program != 0);
        meshDirty = true;
    }

    void UltraCanvasContourSurfaceGLElement::UploadMesh() {
        meshDirty = false;
        triangleIndexCount = 0;
        wireIndexCount = 0;
        isolineVertexCount = 0;
        if (!glReady || !fieldValid || !field.Valid()) return;

        RebuildLevels();

        const int cellCols = field.cols - 1;
        const int cellRows = field.rows - 1;

        // Decimate extremely fine grids: more quads than pixels costs GPU and
        // CPU build time and shows nothing extra.
        int stride = 1;
        while (static_cast<long long>(cellCols / stride) * (cellRows / stride) > 300000) ++stride;

        std::vector<float> verts;
        std::vector<uint32_t> triIndices;
        std::vector<uint32_t> wireIndices;
        verts.reserve(static_cast<size_t>(cellCols / stride + 1) *
                      (cellRows / stride + 1) * 4 * kFloatsPerVertex);

        for (int r = 0; r < cellRows; r += stride) {
            int r1 = std::min(r + stride, field.rows - 1);
            for (int c = 0; c < cellCols; c += stride) {
                int c1 = std::min(c + stride, field.cols - 1);

                double v00 = field.At(c, r),   v10 = field.At(c1, r);
                double v11 = field.At(c1, r1), v01 = field.At(c, r1);
                if (std::isnan(v00) || std::isnan(v10) || std::isnan(v11) || std::isnan(v01)) {
                    continue;
                }

                Vec3 p0 = WorldAt(c,  r,  v00);
                Vec3 p1 = WorldAt(c1, r,  v10);
                Vec3 p2 = WorldAt(c1, r1, v11);
                Vec3 p3 = WorldAt(c,  r1, v01);

                uint32_t base = static_cast<uint32_t>(verts.size() / kFloatsPerVertex);

                if (colorMode == SurfaceColorMode::Banded) {
                    // The Excel look: one flat colour and one flat normal per
                    // quad, which is why the four vertices are not shared.
                    Vec3 n = (p1 - p0).Cross(p3 - p0).Normalized();
                    Color col = ColorForValue((v00 + v10 + v11 + v01) * 0.25);
                    PushVertex(verts, p0, n, col);
                    PushVertex(verts, p1, n, col);
                    PushVertex(verts, p2, n, col);
                    PushVertex(verts, p3, n, col);
                } else {
                    PushVertex(verts, p0, NodeNormal(c,  r),  ColorForValue(v00));
                    PushVertex(verts, p1, NodeNormal(c1, r),  ColorForValue(v10));
                    PushVertex(verts, p2, NodeNormal(c1, r1), ColorForValue(v11));
                    PushVertex(verts, p3, NodeNormal(c,  r1), ColorForValue(v01));
                }

                triIndices.push_back(base);     triIndices.push_back(base + 1);
                triIndices.push_back(base + 2);
                triIndices.push_back(base);     triIndices.push_back(base + 2);
                triIndices.push_back(base + 3);

                wireIndices.push_back(base);     wireIndices.push_back(base + 1);
                wireIndices.push_back(base + 1); wireIndices.push_back(base + 2);
                wireIndices.push_back(base + 2); wireIndices.push_back(base + 3);
                wireIndices.push_back(base + 3); wireIndices.push_back(base);
            }
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);
        const GLsizei strideBytes = kFloatsPerVertex * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideBytes, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, strideBytes, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, strideBytes, (void*)(6 * sizeof(float)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(triIndices.size() * sizeof(uint32_t)),
                     triIndices.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);

        // The wireframe shares the vertex buffer but not the element buffer, so
        // it is bound on demand in OnGLRender.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wireEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(wireIndices.size() * sizeof(uint32_t)),
                     wireIndices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        triangleIndexCount = static_cast<int>(triIndices.size());
        wireIndexCount = static_cast<int>(wireIndices.size());

        // ---- isolines lifted onto the surface, depth-tested by GL ----
        if (showSurfaceIsolines && !levels.empty()) {
            std::vector<float> linePts;
            for (double level : levels) {
                std::vector<ContourSegment> segs = ExtractContourSegments(field, level);
                for (const auto& s : segs) {
                    Vec3 a = WorldAt(s.a.x, s.a.y, level);
                    Vec3 b = WorldAt(s.b.x, s.b.y, level);
                    linePts.push_back(a.x); linePts.push_back(a.y); linePts.push_back(a.z);
                    linePts.push_back(b.x); linePts.push_back(b.y); linePts.push_back(b.z);
                }
            }
            glBindVertexArray(lineVao);
            glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(linePts.size() * sizeof(float)),
                         linePts.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glBindVertexArray(0);
            isolineVertexCount = static_cast<int>(linePts.size() / 3);
        }
    }

    void UltraCanvasContourSurfaceGLElement::OnGLRender(const RenderSurfaceInfo& info) {
        if (meshDirty) UploadMesh();

        glViewport(0, 0, info.width, info.height);
        glClearColor(backgroundColor.r / 255.0f, backgroundColor.g / 255.0f,
                     backgroundColor.b / 255.0f, backgroundColor.a / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!glReady || triangleIndexCount == 0) return;

        if (autoRotate) {
            yaw -= info.deltaTime * 0.5;
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Camera: the same spherical orbit as the software renderer, so the 2D
        // axis overlay can reproduce the projection exactly.
        double cp = std::cos(pitch), sp = std::sin(pitch);
        double cy = std::cos(yaw), sy = std::sin(yaw);
        Vec3 eye(static_cast<float>(cp * sy * distance),
                 static_cast<float>(sp * distance),
                 static_cast<float>(cp * cy * distance));

        float aspect = (info.height > 0)
                       ? static_cast<float>(info.width) / static_cast<float>(info.height)
                       : 1.0f;
        Mat4 proj = Mat4::Perspective(static_cast<float>(fieldOfView), aspect, 0.05f, 100.0f);
        Mat4 view = Mat4::LookAt(eye, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
        Mat4 mvp = proj * view;

        glUseProgram(program);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
        glUniform3f(uLightDir, lightDirection.x, lightDirection.y, lightDirection.z);
        glUniform1f(uAmbient, static_cast<float>(ambient));
        glUniform1i(uLightingOn, lightingEnabled ? 1 : 0);

        // Fill pushed slightly back in depth so the wireframe and the isolines
        // win the depth test where they lie exactly on the surface.
        glUniform1i(uOverrideOn, 0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, triangleIndexCount, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_POLYGON_OFFSET_FILL);

        glUniform1i(uOverrideOn, 1);
        if (showWireframe && wireIndexCount > 0) {
            glUniform4f(uOverrideColor, wireframeColor.r / 255.0f, wireframeColor.g / 255.0f,
                        wireframeColor.b / 255.0f, wireframeColor.a / 255.0f);
            glLineWidth(std::clamp(wireframeWidth, 0.5f, 4.0f));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wireEbo);
            glDrawElements(GL_LINES, wireIndexCount, GL_UNSIGNED_INT, nullptr);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        }
        glBindVertexArray(0);

        if (showSurfaceIsolines && isolineVertexCount > 0) {
            glUniform4f(uOverrideColor, isolineColor.r / 255.0f, isolineColor.g / 255.0f,
                        isolineColor.b / 255.0f, isolineColor.a / 255.0f);
            glLineWidth(std::clamp(isolineWidth, 0.5f, 4.0f));
            glBindVertexArray(lineVao);
            glDrawArrays(GL_LINES, 0, isolineVertexCount);
            glBindVertexArray(0);
        }

        glLineWidth(1.0f);
        glUseProgram(0);
    }

    void UltraCanvasContourSurfaceGLElement::OnGLCleanup() {
        if (lineVbo) { glDeleteBuffers(1, &lineVbo); lineVbo = 0; }
        if (lineVao) { glDeleteVertexArrays(1, &lineVao); lineVao = 0; }
        if (wireEbo) { glDeleteBuffers(1, &wireEbo); wireEbo = 0; }
        if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (program) { glDeleteProgram(program); program = 0; }
        glReady = false;
    }

// =============================================================================
// 2D OVERLAY (title, axes, legend)
// =============================================================================

    UltraCanvasContourSurfaceGLElement::OverlayCamera
    UltraCanvasContourSurfaceGLElement::BuildOverlayCamera() const {
        OverlayCamera cam;
        double cp = std::cos(pitch), sp = std::sin(pitch);
        double cy = std::cos(yaw), sy = std::sin(yaw);
        Vec3 dir(static_cast<float>(cp * sy), static_cast<float>(sp), static_cast<float>(cp * cy));
        cam.eye = dir * static_cast<float>(distance);
        cam.forward = (Vec3(0, 0, 0) - cam.eye).Normalized();

        Vec3 worldUp(0.0f, 1.0f, 0.0f);
        cam.right = cam.forward.Cross(worldUp).Normalized();
        if (cam.right.Length() < 1e-4f) cam.right = Vec3(1.0f, 0.0f, 0.0f);
        cam.up = cam.right.Cross(cam.forward).Normalized();

        // GL projects with Perspective(fov, aspect = w/h): the pixel scale then
        // works out to (height/2) / tan(fov/2) on both screen axes, which is
        // what makes this CPU projection land exactly on the GL image.
        cam.focal = (GetHeight() * 0.5) / std::tan(fieldOfView * 0.5);
        cam.cx = GetWidth() * 0.5;
        cam.cy = GetHeight() * 0.5;
        return cam;
    }

    bool UltraCanvasContourSurfaceGLElement::Project(const OverlayCamera& cam, const Vec3& p,
                                                     Point2Dd& out, double& depth) const {
        Vec3 d = p - cam.eye;
        depth = d.Dot(cam.forward);
        if (depth < 0.05) return false;
        double scale = cam.focal / depth;
        out = Point2Dd(cam.cx + d.Dot(cam.right) * scale,
                       cam.cy - d.Dot(cam.up) * scale);
        return true;
    }

    void UltraCanvasContourSurfaceGLElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasGLSurface::Render(ctx, dirtyRect);
        if (ctx) RenderOverlay(ctx);
    }

    void UltraCanvasContourSurfaceGLElement::RenderOverlay(IRenderContext* ctx) {
        if (!fieldValid || !field.Valid()) return;

        if (!chartTitle.empty()) {
            ctx->SetFontSize(15.0);
            ctx->SetTextPaint(titleColor);
            Size2Di sz = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - sz.width / 2.0, 6));
        }

        if (showAxes3D) {
            OverlayCamera cam = BuildOverlayCamera();
            RenderAxes3D(ctx, cam);
        }

        // The legend floats over the GL image: the viewport is not shrunk, the
        // shared component just paints its colour bar at the chosen edge.
        SyncLegend();
        double top = chartTitle.empty() ? 8.0 : 30.0;
        Rect2Dd overlayArea(8.0, top,
                            std::max(1.0, static_cast<double>(GetWidth()) - 16.0),
                            std::max(1.0, static_cast<double>(GetHeight()) - top - 8.0));
        legend.Render(ctx, overlayArea);
    }

    void UltraCanvasContourSurfaceGLElement::RenderAxes3D(IRenderContext* ctx,
                                                          const OverlayCamera& cam) {
        double floorY = -0.5 * heightSpan * zExaggeration;

        // Same edge selection as the software renderer: the two ground axes run
        // along the edges meeting at the corner nearest the camera.
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
        Vec3 alongX = origin;
        Vec3 alongZ = origin;
        alongX.x = -origin.x;
        alongZ.z = -origin.z;

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

        std::vector<std::string> xTicks = buildTicks(true);
        std::vector<std::string> yTicks = buildTicks(false);
        if (origin.x > 0.0f) std::reverse(xTicks.begin(), xTicks.end());
        if (origin.z > 0.0f) std::reverse(yTicks.begin(), yTicks.end());

        RenderAxisLine(ctx, cam, origin, alongX, xTicks, axisTitleX);
        RenderAxisLine(ctx, cam, origin, alongZ, yTicks, axisTitleY);

        std::vector<std::string> zTicks;
        for (int i = 0; i < zTickCount; ++i) {
            zTicks.push_back(FormatTick(valueMin + (valueMax - valueMin) * i / (zTickCount - 1),
                                        zTickFormatter));
        }
        Vec3 zBase = alongX;
        Vec3 zTop = zBase;
        zTop.y = static_cast<float>(floorY + heightSpan * zExaggeration);
        RenderAxisLine(ctx, cam, zBase, zTop, zTicks, axisTitleZ);
    }

    void UltraCanvasContourSurfaceGLElement::RenderAxisLine(
            IRenderContext* ctx, const OverlayCamera& cam,
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

        double nx = -dy / len, ny = dx / len;
        double toCentreX = cam.cx - (sFrom.x + sTo.x) * 0.5;
        double toCentreY = cam.cy - (sFrom.y + sTo.y) * 0.5;
        if (nx * toCentreX + ny * toCentreY > 0.0) { nx = -nx; ny = -ny; }

        double angle = rotateAxisLabels ? UprightAngleGL(std::atan2(dy, dx)) : 0.0;

        ctx->SetFontSize(axisLabelFontSize);
        ctx->SetTextPaint(axisLabelColor);

        const size_t n = tickTexts.size();
        for (size_t i = 0; i < n; ++i) {
            double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.5;
            Point2Dd at(sFrom.x + dx * t, sFrom.y + dy * t);

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

    void UltraCanvasContourSurfaceGLElement::SyncLegend() {
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
        bar.formatter = [decimals](double v) { return FormatFixedGL(v, decimals); };
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

    bool UltraCanvasContourSurfaceGLElement::OnEvent(const UCEvent& event) {
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
        return UltraCanvasGLSurface::OnEvent(event);
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
