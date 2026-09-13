// core/UltraCanvasVectorRaster.cpp
// Vector artwork -> pixels. Two rasterizers behind one call: the libvips
// image pipeline (librsvg for SVG, poppler/pdfium for PDF, a PostScript
// delegate for EPS/PS) and the graphics plugin registry (any registered
// IGraphicsPlugin element, rendered into an offscreen render context).
//
// Both render *at* the requested size rather than rendering small and
// scaling up: the whole point of a vector source is that the resolution is
// chosen at rasterization time.
// Version: 1.0.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRaster.h"

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasImage.h"            // UCPixmap, VipsCanLoad
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasDebug.h"

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    std::string LowerExt(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        const size_t slash = path.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
        std::string e = path.substr(dot + 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
    }

    std::string StemOf(const std::string& path) {
        std::error_code ec;
        std::string stem = fs::path(path).stem().string();
        (void)ec;
        return stem.empty() ? std::string("Vector") : stem;
    }

    // The three families the image pipeline can rasterize, each with its own
    // resolution knob. SVGZ is plain SVG through the same loader; AI files
    // written this century are PDF and load through pdfload.
    enum class PipelineKind { NotPipeline, Svg, Pdf, PostScript };

    PipelineKind PipelineKindOf(const std::string& ext) {
        if (ext == "svg" || ext == "svgz") return PipelineKind::Svg;
        if (ext == "pdf" || ext == "ai")   return PipelineKind::Pdf;
        if (ext == "eps" || ext == "epsf" || ext == "epsi" || ext == "ps") return PipelineKind::PostScript;
        return PipelineKind::NotPipeline;
    }

#ifdef HAS_LIBVIPS
    // libvips only knows its loaders once it has been initialised, so the
    // probes below are worthless before this has run. Same gate the format
    // inventory uses.
    bool EnsureImageSubsystem() {
        static const bool ok = UCImageRaster::InitializeImageSubsysterm(nullptr);
        return ok;
    }
#endif

    // True when this libvips build actually carries the loader. Without
    // libvips there is no image pipeline at all.
    bool PipelineCanLoad(const std::string& ext) {
#ifdef HAS_LIBVIPS
        if (!EnsureImageSubsystem()) return false;
        return VipsCanLoad("." + ext);
#else
        (void)ext;
        return false;
#endif
    }

    const char* PipelineProvider(PipelineKind kind) {
        switch (kind) {
            case PipelineKind::Svg:        return "librsvg (via libvips)";
            case PipelineKind::Pdf:        return "libvips pdfload";
            case PipelineKind::PostScript: return "libvips PostScript delegate";
            default:                       return "";
        }
    }

    // A registered graphics plugin that claims this extension *and* whose
    // extension is a vector format. The registry also answers for bitmaps,
    // which the image pipeline handles far better.
    std::shared_ptr<IGraphicsPlugin> VectorPluginFor(const std::string& path) {
        const std::string ext = LowerExt(path);
        if (ext.empty()) return nullptr;
        if (GraphicsFormatDetector::DetectFromExtension(ext) != GraphicsFormatType::Vector) return nullptr;
        for (const auto& plugin : UltraCanvasGraphicsPluginRegistry::GetAllPlugins()) {
            if (!plugin) continue;
            for (const auto& supported : plugin->GetSupportedExtensions()) {
                std::string s = supported;
                if (!s.empty() && s[0] == '.') s = s.substr(1);
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                if (s == ext) return plugin;
            }
        }
        return nullptr;
    }

    // Fits the wanted size to the natural aspect ratio: either dimension may
    // be left at 0, and both at 0 means "natural size".
    void ResolveTargetSize(int naturalW, int naturalH, int wantW, int wantH, int& outW, int& outH) {
        const bool haveNatural = naturalW > 0 && naturalH > 0;
        if (wantW <= 0 && wantH <= 0) {
            outW = haveNatural ? naturalW : 0;
            outH = haveNatural ? naturalH : 0;
        } else if (wantW > 0 && wantH > 0) {
            outW = wantW;
            outH = wantH;
        } else if (wantW > 0) {
            outW = wantW;
            outH = haveNatural ? std::max(1, static_cast<int>(std::lround(
                    static_cast<double>(wantW) * naturalH / naturalW))) : wantW;
        } else {
            outH = wantH;
            outW = haveNatural ? std::max(1, static_cast<int>(std::lround(
                    static_cast<double>(wantH) * naturalW / naturalH))) : wantH;
        }
    }

    bool SizeIsSane(int w, int h, size_t maxPixels, std::string& error) {
        if (w <= 0 || h <= 0) {
            error = "The raster size must be at least 1 x 1 pixel";
            return false;
        }
        const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        if (maxPixels > 0 && pixels > maxPixels) {
            error = "The requested raster size (" + std::to_string(w) + " x " + std::to_string(h) +
                    ") is larger than this rasterization allows";
            return false;
        }
        return true;
    }

    // Paints `drawing` over `options.background` when the background is not
    // fully transparent; otherwise hands the drawing straight back.
    std::shared_ptr<UCRasterLayer> ApplyBackground(std::shared_ptr<UCRasterLayer> drawing,
                                                   const RasterPixel& background) {
        if (!drawing || background.a == 0) return drawing;
        auto out = std::make_shared<UCRasterLayer>(drawing->GetWidth(), drawing->GetHeight(),
                                                   background, drawing->name);
        out->BlendFrom(*drawing, 0, 0);
        return out;
    }

#ifdef HAS_LIBVIPS
    // Loads the vector source at the resolution that produces `targetW` x
    // `targetH` directly, falling back to a resample when the loader lands a
    // pixel or two off (rounding) or cannot be steered at all.
    PixelFX::PFXImage LoadAtSize(const std::string& path, PipelineKind kind, int page,
                                 int naturalW, int naturalH, int targetW, int targetH) {
        const double sx = naturalW > 0 ? static_cast<double>(targetW) / naturalW : 1.0;
        const double sy = naturalH > 0 ? static_cast<double>(targetH) / naturalH : 1.0;
        // One scale for the render — non-uniform scaling is finished by the
        // resample below, which keeps the drawing from being rendered twice.
        const double s = std::max(0.0001, std::min(sx, sy));

        PixelFX::PFXImage img;
        switch (kind) {
            case PipelineKind::Svg:
                // dpi stays at the libvips default so physical units (mm, in)
                // measure the same as everywhere else in the framework; the
                // size comes from the scale.
                img = PixelFX::FileIO::LoadSvg(path, 72.0, s);
                break;
            case PipelineKind::Pdf:
                img = PixelFX::FileIO::LoadPdf(path, page, 72.0 * s);
                break;
            default:
                // EPS / PS: whichever delegate the build has, at its own idea
                // of the size.
                img = PixelFX::FileIO::Load(path);
                break;
        }
        if (img.Width() != targetW || img.Height() != targetH) {
            const double rx = img.Width()  > 0 ? static_cast<double>(targetW) / img.Width()  : 1.0;
            const double ry = img.Height() > 0 ? static_cast<double>(targetH) / img.Height() : 1.0;
            img = PixelFX::Resample::Resize(img, rx, ry, PixelFX::Kernel::Lanczos3);
        }
        return img;
    }
#endif

    // ===== THE OFFSCREEN PATH =====
    // A plugin element knows how to draw itself into an IRenderContext, so an
    // offscreen context of the target size plus a read-back is all a
    // rasterizer needs. The pixels come back premultiplied ARGB32 (the
    // backend's surface format) and are un-premultiplied into the layer's
    // straight RGBA.
    std::shared_ptr<UCRasterLayer> RasterizeThroughPlugin(const std::string& path,
                                                          const std::shared_ptr<IGraphicsPlugin>& plugin,
                                                          int w, int h,
                                                          const RasterPixel& background,
                                                          std::string& error) {
        std::shared_ptr<UltraCanvasUIElement> element;
        try {
            element = plugin->LoadGraphics(path);
        } catch (const std::exception& e) {
            error = e.what();
            return nullptr;
        }
        if (!element) {
            error = "The " + plugin->GetPluginName() + " plugin could not read " + path;
            return nullptr;
        }

        UCPixmap pixmap;
        if (!pixmap.Init(w, h)) {
            error = "Could not allocate a " + std::to_string(w) + " x " + std::to_string(h) + " pixel buffer";
            return nullptr;
        }
        std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) {
            error = "Could not create an offscreen render context";
            return nullptr;
        }

        ctx->Clear(background.ToColor());
        try {
            element->SetBounds(Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
            element->Render(ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
        } catch (const std::exception& e) {
            error = std::string("Rendering ") + path + " failed: " + e.what();
            return nullptr;
        }
        ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
        pixmap.MarkDirty();
        pixmap.Flush();

        const uint32_t* src = pixmap.GetPixelData();
        if (!src) {
            error = "The offscreen surface exposed no pixels";
            return nullptr;
        }

        auto layer = std::make_shared<UCRasterLayer>(w, h, StemOf(path));
        if (!layer->IsValid()) {
            error = "Could not allocate the raster layer";
            return nullptr;
        }
        for (int y = 0; y < h; ++y) {
            const uint32_t* srcRow = src + static_cast<size_t>(y) * w;
            uint8_t* dstRow = layer->Row(y);
            for (int x = 0; x < w; ++x) {
                const uint32_t p = srcRow[x];
                const uint8_t a = static_cast<uint8_t>((p >> 24) & 0xFF);
                uint8_t r = static_cast<uint8_t>((p >> 16) & 0xFF);
                uint8_t g = static_cast<uint8_t>((p >>  8) & 0xFF);
                uint8_t b = static_cast<uint8_t>( p        & 0xFF);
                if (a != 0 && a != 255) {
                    r = static_cast<uint8_t>(std::min(255, (r * 255 + a / 2) / a));
                    g = static_cast<uint8_t>(std::min(255, (g * 255 + a / 2) / a));
                    b = static_cast<uint8_t>(std::min(255, (b * 255 + a / 2) / a));
                } else if (a == 0) {
                    r = g = b = 0;
                }
                dstRow[4 * x + 0] = r;
                dstRow[4 * x + 1] = g;
                dstRow[4 * x + 2] = b;
                dstRow[4 * x + 3] = a;
            }
        }
        return layer;
    }

} // namespace

// ===========================================================================
// PUBLIC API
// ===========================================================================

const char* VectorRasterSourceName(VectorRasterSource source) {
    switch (source) {
        case VectorRasterSource::ImagePipeline:  return "image pipeline";
        case VectorRasterSource::GraphicsPlugin: return "graphics plugin";
        default:                                 return "none";
    }
}

bool IsVectorGraphicsPath(const std::string& path) {
    const std::string ext = LowerExt(path);
    if (ext.empty()) return false;
    const PipelineKind kind = PipelineKindOf(ext);
    if (kind != PipelineKind::NotPipeline && PipelineCanLoad(ext)) return true;
    return VectorPluginFor(path) != nullptr;
}

std::vector<std::string> GetVectorRasterExtensions() {
    std::vector<std::string> out;
    for (const char* ext : { "svg", "svgz", "pdf", "ai", "eps", "epsf", "epsi", "ps" }) {
        if (PipelineCanLoad(ext)) out.emplace_back(ext);
    }
    for (const auto& plugin : UltraCanvasGraphicsPluginRegistry::GetAllPlugins()) {
        if (!plugin) continue;
        for (const auto& supported : plugin->GetSupportedExtensions()) {
            std::string s = supported;
            if (!s.empty() && s[0] == '.') s = s.substr(1);
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            if (s.empty()) continue;
            if (GraphicsFormatDetector::DetectFromExtension(s) != GraphicsFormatType::Vector) continue;
            if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        }
    }
    return out;
}

VectorSourceInfo InspectVectorFile(const std::string& path) {
    VectorSourceInfo info;
    const std::string ext = LowerExt(path);
    if (ext.empty()) {
        info.error = "No file extension to identify " + path + " by";
        return info;
    }

    const PipelineKind kind = PipelineKindOf(ext);
    if (kind != PipelineKind::NotPipeline && PipelineCanLoad(ext)) {
#ifdef HAS_LIBVIPS
        try {
            // libvips is lazy: this reads the header and the page count, not
            // the pixels.
            PixelFX::PFXImage probe = (kind == PipelineKind::Svg)
                    ? PixelFX::FileIO::LoadSvg(path, 72.0, 1.0)
                    : PixelFX::FileIO::Load(path);
            info.ok = true;
            info.source = VectorRasterSource::ImagePipeline;
            info.naturalWidth = probe.Width();
            info.naturalHeight = probe.Height();
            info.provider = PipelineProvider(kind);
            if (kind == PipelineKind::Pdf) {
                try { info.pageCount = std::max(1, probe.get_int("n-pages")); }
                catch (...) { info.pageCount = 1; }
            }
            return info;
        } catch (const std::exception& e) {
            info.error = e.what();
            return info;
        }
#endif
    }

    if (auto plugin = VectorPluginFor(path)) {
        GraphicsFileInfo fileInfo;
        try { fileInfo = plugin->GetFileInfo(path); }
        catch (...) { fileInfo = GraphicsFileInfo(path); }
        info.ok = true;
        info.source = VectorRasterSource::GraphicsPlugin;
        info.naturalWidth = std::max(0, fileInfo.width);
        info.naturalHeight = std::max(0, fileInfo.height);
        info.provider = plugin->GetPluginName();
        return info;
    }

    info.error = "No rasterizer in this build reads ." + ext;
    return info;
}

std::shared_ptr<UCRasterLayer> RasterizeVectorFile(const std::string& path,
                                                   const VectorRasterOptions& options,
                                                   std::string& error) {
    error.clear();
    const VectorSourceInfo info = InspectVectorFile(path);
    if (!info.ok) {
        error = info.error.empty() ? ("Cannot rasterize " + path) : info.error;
        return nullptr;
    }

    int w = 0, h = 0;
    ResolveTargetSize(info.naturalWidth, info.naturalHeight, options.width, options.height, w, h);
    if (w <= 0 || h <= 0) {
        error = path + " does not declare a size; choose the raster width and height";
        return nullptr;
    }
    if (!SizeIsSane(w, h, options.maxPixels, error)) return nullptr;

    std::string name = StemOf(path);

    if (info.source == VectorRasterSource::GraphicsPlugin) {
        auto plugin = VectorPluginFor(path);
        if (!plugin) {
            error = "The plugin that claimed " + path + " is no longer registered";
            return nullptr;
        }
        auto layer = RasterizeThroughPlugin(path, plugin, w, h, options.background, error);
        if (layer) layer->name = name;
        return layer;
    }

#ifdef HAS_LIBVIPS
    const PipelineKind kind = PipelineKindOf(LowerExt(path));
    const int page = std::max(0, std::min(options.page, std::max(0, info.pageCount - 1)));
    if (info.pageCount > 1) name += " page " + std::to_string(page + 1);
    try {
        PixelFX::PFXImage img = LoadAtSize(path, kind, page, info.naturalWidth, info.naturalHeight, w, h);
        auto layer = std::make_shared<UCRasterLayer>();
        if (!layer->FromPixelFX(img)) {
            error = "Could not convert the rendered " + path + " into a raster layer";
            return nullptr;
        }
        layer->name = name;
        return ApplyBackground(layer, options.background);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
#else
    error = "Rasterizing " + path + " needs libvips (HAS_LIBVIPS)";
    return nullptr;
#endif
}

} // namespace UltraCanvas
