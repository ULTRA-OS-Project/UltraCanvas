// UltraCanvasVectorizerPlugin.h
// Raster → SVG vector tracing plugin for UltraCanvas (VTracer-backed).
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>

#ifdef ULTRACANVAS_VECTORIZER_SUPPORT

#include "UltraCanvasCommonTypes.h"

namespace PixelFX { class PFXImage; }

namespace UltraCanvas {

// ===== CONFIGURATION =====
enum class VectorizerColorMode {
    Binary,     // black & white silhouette
    Color       // colour-clustered (default)
};

enum class VectorizerHierarchy {
    Stacked,    // shapes painted on top of each other (default)
    Cutout      // shapes cut from each other (no overlap)
};

enum class VectorizerPathMode {
    Pixel,      // pixel-perfect, no simplification
    Polygon,    // straight-line simplification
    Spline      // Bezier curve fitting (default)
};

struct VectorizerConfig {
    VectorizerColorMode colorMode       = VectorizerColorMode::Color;
    VectorizerHierarchy hierarchy       = VectorizerHierarchy::Stacked;
    VectorizerPathMode  pathMode        = VectorizerPathMode::Spline;
    int                 filterSpeckle   = 4;     // drop blobs smaller than N pixels
    int                 colorPrecision  = 6;     // significant bits per colour channel (1..8)
    int                 layerDifference = 16;    // cluster merge threshold (0..255)
    int                 cornerThreshold = 60;    // degrees; smaller = more corners detected
    double              lengthThreshold = 4.0;   // minimum path length in pixels
    int                 spliceThreshold = 45;    // degrees; controls curve splicing
    int                 maxIterations   = 10;    // path-fitting iterations
};

// ===== RESULT =====
struct VectorizerResult {
    std::string svg;          // complete <svg>…</svg> document on success
    int         width  = 0;
    int         height = 0;
    double      elapsedMs = 0.0;
    std::string error;        // empty on success
    bool Ok() const { return error.empty(); }
};

// ===== FACADE =====
class UltraCanvasVectorizer {
public:
    explicit UltraCanvasVectorizer(const VectorizerConfig& cfg = {});
    ~UltraCanvasVectorizer();

    UltraCanvasVectorizer(const UltraCanvasVectorizer&) = delete;
    UltraCanvasVectorizer& operator=(const UltraCanvasVectorizer&) = delete;
    UltraCanvasVectorizer(UltraCanvasVectorizer&&) noexcept;
    UltraCanvasVectorizer& operator=(UltraCanvasVectorizer&&) noexcept;

    VectorizerResult VectorizeFile  (const std::string& path);
    VectorizerResult VectorizeBuffer(const void* data, size_t bytes,
                                     const std::string& formatHint = "");
    VectorizerResult VectorizeImage (const PixelFX::PFXImage& img);

    std::future<VectorizerResult> VectorizeImageAsync(
        const PixelFX::PFXImage& img);
    std::future<VectorizerResult> VectorizeFileAsync(
        const std::string& path);

    void                     SetConfig(const VectorizerConfig& cfg);
    const VectorizerConfig&  Config() const;

    // True when the FFI to the underlying tracer is linked and reachable.
    static bool        IsAvailable();
    // Human-readable backend version string (e.g. "vtracer 0.6.11 / vtracer_c 0.1.0").
    static const char* BackendVersion();

private:
    VectorizerConfig config;
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_VECTORIZER_SUPPORT
