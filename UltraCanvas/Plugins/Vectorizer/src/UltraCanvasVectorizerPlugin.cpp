// UltraCanvasVectorizerPlugin.cpp
// Facade implementation for the VTracer-backed Vectorizer plugin.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#include "UltraCanvasVectorizerPlugin.h"

#ifdef ULTRACANVAS_VECTORIZER_SUPPORT

#include "PixelFX/PixelFX.h"
#include "ultracanvas_vtracer.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace UltraCanvas {

namespace {

UCVectorizerConfig ToFFI(const VectorizerConfig& cfg) {
    UCVectorizerConfig out{};
    out.color_mode      = static_cast<uint8_t>(cfg.colorMode);
    out.hierarchy       = static_cast<uint8_t>(cfg.hierarchy);
    out.path_mode       = static_cast<uint8_t>(cfg.pathMode);
    out._reserved       = 0;
    out.filter_speckle  = cfg.filterSpeckle;
    out.color_precision = cfg.colorPrecision;
    out.layer_difference= cfg.layerDifference;
    out.corner_threshold= cfg.cornerThreshold;
    out.length_threshold= cfg.lengthThreshold;
    out.splice_threshold= cfg.spliceThreshold;
    out.max_iterations  = cfg.maxIterations;
    return out;
}

// Convert any PFXImage to 8-bit RGBA, row-major, tightly packed.
bool PFXToRGBA(const PixelFX::PFXImage& src,
               std::vector<uint8_t>& out,
               int& outW, int& outH) {
    try {
        vips::VImage img = src; // PFXImage inherits from vips::VImage
        if (img.format() != VIPS_FORMAT_UCHAR) {
            img = img.cast(VIPS_FORMAT_UCHAR);
        }
        img = img.colourspace(VIPS_INTERPRETATION_sRGB);
        if (img.bands() == 3) {
            // Add a fully-opaque alpha channel — VTracer expects RGBA.
            img = img.bandjoin(255);
        } else if (img.bands() == 1) {
            img = img.bandjoin(img).bandjoin(img); // greyscale → RGB
            img = img.bandjoin(255);
        } else if (img.bands() == 2) {
            // grey + alpha — expand grey to RGB, keep alpha
            vips::VImage grey  = img.extract_band(0);
            vips::VImage alpha = img.extract_band(1);
            img = grey.bandjoin(grey).bandjoin(grey).bandjoin(alpha);
        }
        // Anything else (4+ bands) is assumed to already be RGBA-compatible.

        outW = img.width();
        outH = img.height();
        if (outW <= 0 || outH <= 0) return false;

        size_t bytes = 0;
        void* mem = img.write_to_memory(&bytes);
        const size_t expected = static_cast<size_t>(outW) * outH * 4u;
        if (!mem || bytes < expected) {
            if (mem) g_free(mem);
            return false;
        }
        out.assign(static_cast<uint8_t*>(mem), static_cast<uint8_t*>(mem) + expected);
        g_free(mem);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

VectorizerResult RunFFI(const std::vector<uint8_t>& rgba, int w, int h,
                        const VectorizerConfig& cfg) {
    VectorizerResult result;
    result.width  = w;
    result.height = h;

    const auto t0 = std::chrono::steady_clock::now();
    UCVectorizerConfig ffiCfg = ToFFI(cfg);
    UCVectorizerOutput out{};
    const int32_t rc = ultracanvas_vtracer_from_rgba(
        rgba.data(), w, h, &ffiCfg, &out);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    result.elapsedMs = ms;

    if (rc == 0 && out.svg_utf8) {
        result.svg.assign(out.svg_utf8, out.svg_len);
    } else {
        result.error = out.error_utf8 ? out.error_utf8 : "unknown vectorizer error";
    }
    ultracanvas_vtracer_free(&out);
    return result;
}

} // namespace

// ===== FACADE =====
UltraCanvasVectorizer::UltraCanvasVectorizer(const VectorizerConfig& cfg)
    : config(cfg) {}
UltraCanvasVectorizer::~UltraCanvasVectorizer() = default;
UltraCanvasVectorizer::UltraCanvasVectorizer(UltraCanvasVectorizer&&) noexcept = default;
UltraCanvasVectorizer& UltraCanvasVectorizer::operator=(UltraCanvasVectorizer&&) noexcept = default;

void UltraCanvasVectorizer::SetConfig(const VectorizerConfig& cfg) { config = cfg; }
const VectorizerConfig& UltraCanvasVectorizer::Config() const     { return config; }

VectorizerResult UltraCanvasVectorizer::VectorizeImage(const PixelFX::PFXImage& img) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!PFXToRGBA(img, rgba, w, h)) {
        VectorizerResult r;
        r.error = "failed to convert PFXImage to RGBA";
        return r;
    }
    return RunFFI(rgba, w, h, config);
}

VectorizerResult UltraCanvasVectorizer::VectorizeFile(const std::string& path) {
    try {
        PixelFX::PFXImage img = PixelFX::PFXImage::FromFile(path);
        return VectorizeImage(img);
    } catch (const std::exception& e) {
        VectorizerResult r;
        r.error = std::string("failed to load image: ") + e.what();
        return r;
    }
}

VectorizerResult UltraCanvasVectorizer::VectorizeBuffer(const void* data, size_t bytes,
                                                        const std::string& formatHint) {
    try {
        PixelFX::PFXImage img = PixelFX::PFXImage::FromBuffer(
            const_cast<void*>(data), bytes, formatHint);
        return VectorizeImage(img);
    } catch (const std::exception& e) {
        VectorizerResult r;
        r.error = std::string("failed to decode image buffer: ") + e.what();
        return r;
    }
}

std::future<VectorizerResult> UltraCanvasVectorizer::VectorizeImageAsync(
    const PixelFX::PFXImage& img) {
    // VTracer doesn't expose mid-flight cancellation; the stop_token is
    // accepted now so callers can be written against the final shape.
    return std::async(std::launch::async,
                      [this, img]() { return VectorizeImage(img); });
}

std::future<VectorizerResult> UltraCanvasVectorizer::VectorizeFileAsync(
    const std::string& path) {
    return std::async(std::launch::async,
                      [this, path]() { return VectorizeFile(path); });
}

bool UltraCanvasVectorizer::IsAvailable() {
    return ultracanvas_vtracer_version() != nullptr;
}

const char* UltraCanvasVectorizer::BackendVersion() {
    const char* v = ultracanvas_vtracer_version();
    return v ? v : "unavailable";
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_VECTORIZER_SUPPORT
