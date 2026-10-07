// libspecific/Cairo/RenderContextCairo.cpp
// Cairo support implementation for UltraCanvas Framework
// Version: 1.0.13 - borders drawn as filled wedges of the border ring: sides meet on
//                  the corner's diagonal (mitred), each in its own colour, and rounded
//                  corners are shared the same way; dashed sides stay strokes
// Version: 1.0.12 - per-side borders: each side strokes in its own colour, dashed
//                  or solid on its own (a dashed side took the previous colour and
//                  passed its dash on to the sides after it)
// Version: 1.0.11 - A non-invertible matrix is refused instead of killing the context
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasUtilsUtf8.h"
#include "UCTextLayout.h"
#include "../libspecific/Cairo/RenderContextCairo.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
// ===== GLOBAL TEXT LAYOUTS CACHE =====

    // 20 MB cache for text layouts
    struct UCTextLayoutCacheEntry {
        std::shared_ptr<UCTextLayout> payload;
        std::chrono::steady_clock::time_point lastAccess;
        size_t GetEntrySize() {
            return sizeof(UCTextLayoutCacheEntry) + sizeof(UCTextLayout) + 256;
        }
    };
    static UCCache<UCTextLayout, UCTextLayoutCacheEntry> g_TextLayoutsCache(20 * 1024 * 1024);

    // Configurable text rendering font options (global, matching cache scope).
    // Defaults are chosen for layout stability across platforms/DPI: fractional
    // advance widths (HINT_METRICS_OFF) keep paragraph widths identical to the
    // font designer's metrics, and HINT_STYLE_SLIGHT keeps glyph outlines crisp
    // at small sizes without snapping advances to integer pixels.
    static cairo_antialias_t g_TextAntialias = CAIRO_ANTIALIAS_SUBPIXEL;
    static cairo_hint_style_t g_TextHintStyle = CAIRO_HINT_STYLE_SLIGHT;
    static cairo_hint_metrics_t g_TextHintMetrics = CAIRO_HINT_METRICS_OFF;

    // Pin Pango point->pixel conversion to a known DPI on every platform. 96
    // matches the CSS reference and what every modern UI toolkit assumes; fixing
    // it here removes drift from Cairo backends that report different defaults.
    static double g_PangoResolution = 96.0;

    std::vector<RenderContextCairo*> RenderContextCairo::g_Instances;

    // Font options as words, for the text-render diagnostic: the enum numbers
    // mean nothing in a log without the cairo headers open beside it.
    static std::string DescribeFontOptions(const cairo_font_options_t* fo) {
        if (!fo) return "none";
        auto antialias = [](cairo_antialias_t a) -> const char* {
            switch (a) {
                case CAIRO_ANTIALIAS_DEFAULT:  return "default";
                case CAIRO_ANTIALIAS_NONE:     return "none";
                case CAIRO_ANTIALIAS_GRAY:     return "gray";
                case CAIRO_ANTIALIAS_SUBPIXEL: return "subpixel";
                case CAIRO_ANTIALIAS_FAST:     return "fast";
                case CAIRO_ANTIALIAS_GOOD:     return "good";
                case CAIRO_ANTIALIAS_BEST:     return "best";
            }
            return "?";
        };
        auto hintStyle = [](cairo_hint_style_t h) -> const char* {
            switch (h) {
                case CAIRO_HINT_STYLE_DEFAULT: return "default";
                case CAIRO_HINT_STYLE_NONE:    return "none";
                case CAIRO_HINT_STYLE_SLIGHT:  return "slight";
                case CAIRO_HINT_STYLE_MEDIUM:  return "medium";
                case CAIRO_HINT_STYLE_FULL:    return "full";
            }
            return "?";
        };
        auto hintMetrics = [](cairo_hint_metrics_t m) -> const char* {
            switch (m) {
                case CAIRO_HINT_METRICS_DEFAULT: return "default";
                case CAIRO_HINT_METRICS_OFF:     return "off";
                case CAIRO_HINT_METRICS_ON:      return "on";
            }
            return "?";
        };
        auto subpixel = [](cairo_subpixel_order_t o) -> const char* {
            switch (o) {
                case CAIRO_SUBPIXEL_ORDER_DEFAULT: return "default";
                case CAIRO_SUBPIXEL_ORDER_RGB:     return "rgb";
                case CAIRO_SUBPIXEL_ORDER_BGR:     return "bgr";
                case CAIRO_SUBPIXEL_ORDER_VRGB:    return "vrgb";
                case CAIRO_SUBPIXEL_ORDER_VBGR:    return "vbgr";
            }
            return "?";
        };
        std::string out = "antialias:";
        out += antialias(cairo_font_options_get_antialias(fo));
        out += "/hint_style:";
        out += hintStyle(cairo_font_options_get_hint_style(fo));
        out += "/hint_metrics:";
        out += hintMetrics(cairo_font_options_get_hint_metrics(fo));
        out += "/subpixel_order:";
        out += subpixel(cairo_font_options_get_subpixel_order(fo));
        return out;
    }


    // The process-wide text font options as one cairo object, for applying
    // to a Pango context and for the log. The caller destroys it.
    static cairo_font_options_t* CreateTextFontOptions() {
        cairo_font_options_t *opts = cairo_font_options_create();
        cairo_font_options_set_antialias(opts, g_TextAntialias);
        cairo_font_options_set_hint_style(opts, g_TextHintStyle);
        cairo_font_options_set_hint_metrics(opts, g_TextHintMetrics);
        return opts;
    }

    void RenderContextCairo::ApplyPangoFontOptions() {
        cairo_font_options_t *opts = CreateTextFontOptions();
        pango_cairo_context_set_font_options(pangoContext, opts);
        cairo_font_options_destroy(opts);
        // Hinting and antialiasing change a glyph's ink, so every cap height
        // measured so far is measured again under the new options.
        InvalidateFontMetricsCache();
    }

    void RenderContextCairo::InvalidateFontMetricsCache() {
        IRenderContext::InvalidateFontMetricsCache();
        UCTextLayout::InvalidateFontMetricsCache(pangoContext);
    }

    // The one place that knows what a change to the process-wide text font
    // options invalidates: the shared layout cache, whose layouts keep their
    // extents, and then every context, which takes the new options and drops
    // its own font measurements (ApplyPangoFontOptions ends in
    // InvalidateFontMetricsCache). The three setters below all come here.
    void RenderContextCairo::InvalidateAllFontMetricsCaches() {
        // The first-surface diagnostic logged the options text started with;
        // this line keeps the log true after a runtime change to them.
        {
            cairo_font_options_t* opts = CreateTextFontOptions();
            debugOutput << "UC text-render diag: font options changed, contexts=" << g_Instances.size()
                        << " pango_font_options=" << DescribeFontOptions(opts)
                        << " (layout and font-metrics caches cleared)" << std::endl;
            cairo_font_options_destroy(opts);
        }
        g_TextLayoutsCache.ClearCache();
        for (auto* instance : g_Instances) {
            instance->ApplyPangoFontOptions();
        }
    }

    void RenderContextCairo::SetTextAntialias(cairo_antialias_t mode) {
        if (g_TextAntialias != mode) {
            g_TextAntialias = mode;
            InvalidateAllFontMetricsCaches();
        }
    }

    cairo_antialias_t RenderContextCairo::GetTextAntialias() {
        return g_TextAntialias;
    }

    void RenderContextCairo::SetTextHintStyle(cairo_hint_style_t style) {
        if (g_TextHintStyle != style) {
            g_TextHintStyle = style;
            InvalidateAllFontMetricsCaches();
        }
    }

    cairo_hint_style_t RenderContextCairo::GetTextHintStyle() {
        return g_TextHintStyle;
    }

    void RenderContextCairo::SetTextHintMetrics(cairo_hint_metrics_t metrics) {
        if (g_TextHintMetrics != metrics) {
            g_TextHintMetrics = metrics;
            InvalidateAllFontMetricsCaches();
        }
    }

    cairo_hint_metrics_t RenderContextCairo::GetTextHintMetrics() {
        return g_TextHintMetrics;
    }

    // ===== TEXT SURFACE CACHING IMPLEMENTATION =====
    void ApplySourceToCairo(cairo_t* cairo, const Color& sourceColor, std::shared_ptr<IPaintPattern> sourcePattern) {
        if (sourceColor.a > 0) {
            cairo_set_source_rgba(cairo,
                                  (float)sourceColor.r / 255.0f,
                                  (float)sourceColor.g / 255.0f,
                                  (float)sourceColor.b / 255.0f,
                                  (float)sourceColor.a / 255.0f);
        } else if (sourcePattern != nullptr) {
            auto handle = static_cast<cairo_pattern_t*>(sourcePattern->GetHandle());
            if (handle) {
                cairo_set_source(cairo, handle);
            } else {
                debugOutput << "ERROR: ApplySourceToCairo no pattern handle";
            }
        } else {
            // A fully transparent colour paints nothing. Setting no source
            // at all left cairo's previous one in place - black by default -
            // so a fill-opacity="0" shape (CorelDRAW writes stripes of them)
            // came out as a solid black bar.
            cairo_set_source_rgba(cairo, 0.0, 0.0, 0.0, 0.0);
        }
    }

    std::string RenderContextCairo::GenerateTextCacheKey(const std::string& text, const Size2Di &sz, bool isMarkup) {
        // Generate a unique cache key based on all parameters that affect text rendering.
        //
        // Two things a layout depends on are deliberately NOT in the key:
        //
        // - The device scale. Cached layouts are shared by every context in
        //   the process, and a context on a 2x display and one on a 1x display
        //   can ask for the same text. That is safe because a layout's extents
        //   are in user (logical) units, and with hint metrics OFF (the
        //   framework default, g_TextHintMetrics) Pango does not round glyph
        //   advances or line heights to device pixels, so the same font
        //   measures the same at any scale. Only the paint differs, and the
        //   paint happens at draw time on the drawing context. If hint metrics
        //   were ever turned on, measurements would depend on the scale and
        //   this key would need it - see the font-options invalidation below.
        // - The text font options (antialias, hint style, hint metrics). They
        //   are process-wide, and changing one goes through
        //   InvalidateAllFontMetricsCaches, which clears this whole cache, so
        //   a layout made under old options never survives them; keying on
        //   them would only make the old entries unreachable rather than gone.
        //
        // The pinned Pango resolution IS in the key ("|r" below), so a layout
        // made at one point-to-pixel ratio is never reused at another.
        std::ostringstream keyStream;

        keyStream << sz.width << "x" << sz.height << "|"
                  << currentState.fontStyle.fontFamily
                  << static_cast<int>(currentState.fontStyle.fontSize*10)
                  << static_cast<int>(currentState.fontStyle.fontWeight)
                  << static_cast<int>(currentState.fontStyle.fontSlant)
                  << static_cast<int>(currentState.textStyle.alignment)
                  << static_cast<int>(currentState.textStyle.verticalAlignment)
                  << currentState.textStyle.indent
                  << static_cast<int>(currentState.textStyle.wrap)
//                  << currentState.textSourceColor.ToARGB()
                  << static_cast<int>(currentState.textStyle.lineHeight * 100)
                  // The same string laid out as markup and as literal text
                  // produces different layouts, so it must not share a cache
                  // entry. This tracks the caller's request, not the render
                  // state, because GetOrCreateTextLayout takes it as an argument.
                  << (isMarkup ? "1" : "0")
//                  << static_cast<int>(g_TextAntialias)
//                  << static_cast<int>(g_TextHintStyle)
//                  << static_cast<int>(g_TextHintMetrics) << "|"
                  << "|r" << static_cast<int>(g_PangoResolution * 100)
                  << text.substr(0,300);

        return keyStream.str();
    }


    bool RenderContextCairo::CreateSurface(const Size2Di & sz, NativeSurfacePtr createSimilarToSurface) {
        auto oldCairoSurface = surface;

        // If we are creating sub-surface (e.g. popup) similar to a HiDPI parent
        // (Retina backing scale 2.0+), inherit that scale so the sub-surface
        // also rasterizes at backing pixel resolution and composes back onto
        // the parent without any upscale blur. Default 1.0 keeps standard
        // displays untouched.
        double parentSx = 1.0, parentSy = 1.0;
        if (createSimilarToSurface) {
            cairo_surface_get_device_scale(static_cast<cairo_surface_t*>(createSimilarToSurface),
                                           &parentSx, &parentSy);
            if (parentSx <= 0.0) parentSx = 1.0;
            if (parentSy <= 0.0) parentSy = 1.0;
        }

        if (createSimilarToSurface) {
            // cairo_surface_create_similar takes raw (device-pixel) dimensions.
            // We want the new surface to present `sz.width × sz.height` user
            // units at the parent's scale — so allocate sz * scale raw pixels.
            const int rawW = static_cast<int>(sz.width * parentSx);
            const int rawH = static_cast<int>(sz.height * parentSy);
            surface = cairo_surface_create_similar(static_cast<cairo_surface_t *>(createSimilarToSurface),
                                                   CAIRO_CONTENT_COLOR_ALPHA, rawW, rawH);
        } else {
            surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz.width, sz.height);
        }

        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "RenderContextCairo::CreateSurface: Can't create surface" << std::endl;
            return false;
        }

        // Cairo versions differ on whether create_similar inherits device_scale;
        // set it explicitly so the new surface always presents `sz` user units.
        if (createSimilarToSurface && (parentSx != 1.0 || parentSy != 1.0)) {
            cairo_surface_set_device_scale(surface, parentSx, parentSy);
        }

        surfaceSize = sz;
        return InitializeForSurface(oldCairoSurface);
    }

    bool RenderContextCairo::AttachSurface(cairo_surface_t* target, const Size2Di& sz) {
        if (!target || cairo_surface_status(target) != CAIRO_STATUS_SUCCESS) return false;
        auto oldCairoSurface = surface;
        surface = target;
        surfaceSize = sz;
        return InitializeForSurface(oldCairoSurface);
    }

    // The cairo and Pango contexts for `surface`, replacing any earlier ones
    // (and releasing `oldCairoSurface`).
    bool RenderContextCairo::InitializeForSurface(cairo_surface_t* oldCairoSurface) {
        if (pangoContext) {
            g_object_unref(pangoContext);
            pangoContext = nullptr;
        }
        if (cairo) {
            cairo_destroy(cairo);
            cairo = nullptr;
        }
        if (oldCairoSurface) {
            cairo_surface_destroy(oldCairoSurface);
        }

        cairo = cairo_create(surface);
        if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            debugOutput << "RenderContextCairo::CreateSurface: Can't create cairo context" << std::endl;
            return false;
        }

        pangoContext = pango_cairo_create_context(cairo);
        if (!pangoContext) {
            cairo_destroy(cairo);
            cairo_surface_destroy(surface);
            debugOutput << "RenderContextCairo::CreateSurface: Can't create Pango context" << std::endl;
            return false;

        }

        // Pin Pango DPI before any layout uses this context.
        pango_cairo_context_set_resolution(pangoContext, g_PangoResolution);
        // A new surface may carry a new device scale, and this is a new
        // PangoContext: measurements made on the old one no longer apply.
        InvalidateFontMetricsCache();

        // Belt-and-braces: also pin the cairo surface's fallback DPI and the
        // default Pango font map's resolution. Pango's draw-time
        // pango_cairo_update_layout() can resync metrics from cairo's
        // font_options or the font map; both must agree with our context pin
        // or text widths drift between platforms.
        cairo_surface_set_fallback_resolution(surface, g_PangoResolution, g_PangoResolution);
        if (PangoFontMap* fm = pango_cairo_font_map_get_default()) {
            pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(fm), g_PangoResolution);
        }

        // Apply configurable text rendering font options
        ApplyPangoFontOptions();

        // One-shot diagnostic, on the first surface: everything a text
        // measurement depends on, so a "why is this label 1px off on that
        // machine" question is answerable from the log alone. It is logged
        // after ApplyPangoFontOptions so the font options shown are the ones
        // text is shaped with, not cairo's defaults; the Pango context's are
        // the ones that matter for layout, cairo's for the final paint. The
        // sample line is the default FontStyle measured on this context:
        // compare it across machines before suspecting the caller.
        static bool s_diagLogged = false;
        if (!s_diagLogged) {
            s_diagLogged = true;
            double ctxRes = pango_cairo_context_get_resolution(pangoContext);
            PangoFontMap* fmDiag = pango_cairo_font_map_get_default();
            double fmRes = fmDiag
                ? pango_cairo_font_map_get_resolution(PANGO_CAIRO_FONT_MAP(fmDiag))
                : -1.0;
            double sxFb = 0, syFb = 0;
            cairo_surface_get_fallback_resolution(surface, &sxFb, &syFb);
            double devSx = 0, devSy = 0;
            cairo_surface_get_device_scale(surface, &devSx, &devSy);

            cairo_font_options_t* cairoFo = cairo_font_options_create();
            cairo_get_font_options(cairo, cairoFo);
            const cairo_font_options_t* pangoFo = pango_cairo_context_get_font_options(pangoContext);

            debugOutput << "UC text-render diag (first surface):"
                        << " surface=" << surfaceSize.width << "x" << surfaceSize.height
                        << " pinned_res=" << g_PangoResolution
                        << " pango_ctx_res=" << ctxRes
                        << " fontmap_res=" << fmRes
                        << " surface_fallback_res=" << sxFb << "x" << syFb
                        << " surface_device_scale=" << devSx << "x" << devSy
                        << " pango_font_options=" << DescribeFontOptions(pangoFo)
                        << " cairo_font_options=" << DescribeFontOptions(cairoFo)
                        << std::endl;
            cairo_font_options_destroy(cairoFo);

            try {
                FontStyle sample;   // the framework default: family "", 12pt, regular
                UCTextLayout probe(pangoContext);
                probe.SetFontStyle(sample);
                probe.SetText("H");
                debugOutput << "UC text-render diag sample: font=\"" << sample.ToFontDesc() << "\""
                            << " line_height=" << probe.GetLayoutHeight()
                            << " baseline=" << probe.GetBaseline()
                            << " cap_height=" << probe.GetCapHeight()
                            << " H_width=" << probe.GetLayoutWidth()
                            << " (px, before device scale)"
                            << std::endl;
            } catch (const std::exception& e) {
                debugOutput << "UC text-render diag sample: not measured (" << e.what() << ")" << std::endl;
            }
        }

        g_Instances.push_back(this);

        // Initialize default state
        debugOutput << "RenderContextCairo: Initialization complete" << std::endl;
        return true;
    }

    RenderContextCairo::~RenderContextCairo() {
        debugOutput << "RenderContextCairo: Destroying..." << std::endl;

        g_Instances.erase(std::remove(g_Instances.begin(), g_Instances.end(), this), g_Instances.end());

        // Clear the state stack to prevent any pending cairo operations
        stateStack.clear();

        // Clean up Pango context
        if (pangoContext) {
            g_object_unref(pangoContext);
            pangoContext = nullptr;
        }

        // Null the cairo pointer to prevent any accidental access
        // Note: We don't own the cairo context, so don't destroy it
        if (cairo) {
            cairo_destroy(cairo);
            cairo = nullptr;
        }
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }

        debugOutput << "RenderContextCairo: Destruction complete" << std::endl;
    }

//    void RenderContextCairo::SetTargetSurface(cairo_surface_t* surf, int w, int h) {
//        cairo_status_t status = cairo_surface_status(surf);
//        if (status != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "ERROR: RenderContextCairo: Cairo target surface is invalid: " << cairo_status_to_string(status)
//                      << std::endl;
//            throw std::runtime_error("RenderContextCairo: Invalid target Cairo surface");
//        }
//
//        if (targetContext) {
//            cairo_destroy(targetContext);
//        }
//        if (cairo) {
//            cairo_destroy(cairo);
//        }
//
//        surfaceWidth = w;
//        surfaceHeight = h;
//        targetSurface = surf;
//        targetContext = cairo_create(targetSurface);
//
//        // Check Cairo context status
//        status = cairo_status(targetContext);
//        if (status != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "ERROR: RenderContextCairo: Cairo target context is invalid: " << cairo_status_to_string(status)
//                      << std::endl;
//            throw std::runtime_error("RenderContextCairo: Invalid target Cairo context");
//        }
//
//        cairo = cairo_create(targetSurface);
//        status = cairo_status(cairo);
//        if (status != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "ERROR: RenderContextCairo: Cairo context is invalid: " << cairo_status_to_string(status)
//                      << std::endl;
//            throw std::runtime_error("RenderContextCairo: Invalid Cairo context");
//        }
//    }

//    bool RenderContextCairo::CreateStagingSurface() {
//        // Create image surface for staging (back buffer)
//        stagingSurface = cairo_surface_create_similar(targetSurface, CAIRO_CONTENT_COLOR_ALPHA, surfaceWidth, surfaceHeight);
//
//        if (cairo_surface_status(stagingSurface) != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "RenderContextCairo: Failed to create staging surface" << std::endl;
//            return false;
//        }
//
//        return true;
//    }

    bool RenderContextCairo::ResizeSurface(const Size2Di& sz) {
        if (sz.width <= 0 || sz.height <= 0 || !surface) {
            return false;
        }

        // Update dimensions

//        int oldSurfaceWidth = cairo_image_surface_get_width(surface);
//        int oldSurfaceHeight = cairo_image_surface_get_height(surface);

        // Recreate surface with new dimensions
        if (!CreateSurface(sz, surface)) {
            return false;
        }

//        int copyWidth = std::min(surfaceWidth, oldSurfaceWidth);
//        int copyHeight = std::min(surfaceHeight, oldSurfaceHeight);
//        if (copyWidth > 0 && copyHeight > 0) {
//            // Actually perform the copy operation
//            cairo_save(cairo);
//            cairo_set_source_surface(cairo, oldStagingSurface, 0, 0);
//            cairo_rectangle(cairo, 0, 0, copyWidth, copyHeight);
//            cairo_clip(cairo);
//            cairo_paint(cairo);
//            cairo_restore(cairo);
//
//        }
//
//        cairo_surface_destroy(oldStagingSurface);
//
        debugOutput << "ResizeSurface: Resized to " << sz.width << "x" << sz.height << std::endl;
        return true;
    }

//    void RenderContextCairo::SwitchToSurface(cairo_surface_t* surf) {
//        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "SwitchToSurface: Invalid surface" << std::endl;
//            return;
//        }
//
//        if (cairo) {
//            cairo_destroy(cairo);
//        }
//        cairo = cairo_create(surf);
//
//        if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
//            debugOutput << "SwitchToSurface: Invalid context" << std::endl;
//        }
//        ResetState();
//        pango_cairo_update_context(cairo, pangoContext);
//    }

// ===== STATE MANAGEMENT =====
    void RenderContextCairo::PushState() {
        stateStack.push_back(currentState);
        cairo_save(cairo);
    }

    void RenderContextCairo::PopState() {
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
            cairo_restore(cairo);
        } else {
            debugOutput << "RenderContextCairo::PopState() stateStack empty!" << std::endl;
        }
    }

    void RenderContextCairo::ResetState() {
        currentState = RenderState();
        stateStack.clear();
        if (cairo) {
            cairo_identity_matrix(cairo);
            cairo_reset_clip(cairo);
        }
    }

// ===== TRANSFORMATION =====
    // Cairo latches a non-invertible matrix (a zero scale, a NaN) as
    // CAIRO_STATUS_INVALID_MATRIX on the cairo_t, and that status is
    // permanent: every later fill, stroke, text and image on this context is
    // silently dropped, for the rest of the frame and every frame after it.
    // One degenerate transform in one widget's content would take the whole
    // window's painting with it, so the matrix is checked before it is
    // applied and a bad one is refused (the caller's drawing lands
    // untransformed, which is visible and recoverable, instead of ending all
    // drawing). A CAD drawing supplies these for real: a block standing in a
    // vertical plane projects to plan view with one axis scaled to zero.
    bool RenderContextCairo::UsableMatrix(const cairo_matrix_t& m, const char* where) {
        const double det = m.xx * m.yy - m.xy * m.yx;
        if (std::isfinite(det) && std::fabs(det) > 1e-12 &&
            std::isfinite(m.x0) && std::isfinite(m.y0)) {
            return true;
        }
        static bool reported = false;
        if (!reported) {
            reported = true;
            debugOutput << "RenderContextCairo::" << where
                        << " refused a non-invertible matrix ["
                        << m.xx << " " << m.xy << " " << m.x0 << "; "
                        << m.yx << " " << m.yy << " " << m.y0 << "]" << std::endl;
        }
        return false;
    }

    void RenderContextCairo::Translate(double x, double y) {
        if (x != 0 || y != 0) {
            cairo_translate(cairo, x, y);
        }
    }

    void RenderContextCairo::Rotate(double angle) {
        cairo_rotate(cairo, angle);
    }

    void RenderContextCairo::Scale(double sx, double sy) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, sx, 0, 0, sy, 0, 0);
        if (!UsableMatrix(matrix, "Scale")) return;
        cairo_scale(cairo, sx, sy);
    }

    void RenderContextCairo::SetTransform(double a, double b, double c, double d, double e, double f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        if (!UsableMatrix(matrix, "SetTransform")) return;
        cairo_set_matrix(cairo, &matrix);
    }

    void RenderContextCairo::Transform(double a, double b, double c, double d, double e, double f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        if (!UsableMatrix(matrix, "Transform")) return;
        cairo_transform(cairo, &matrix);
    }

    void RenderContextCairo::ResetTransform() {
        cairo_identity_matrix(cairo);
    }

    void RenderContextCairo::ClearClipRect() {
        debugOutput << "RenderContextCairo::ClearClipRect - clearing clip region" << std::endl;

        // Reset the clip region to cover the entire surface
        cairo_reset_clip(cairo);
//        debugOutput << "RenderContextCairo::ClearClipRect - clip region cleared successfully" << std::endl;
    }

    void RenderContextCairo::ClipRect(const Rect2Dd& rect) {
//        debugOutput << "RenderContextCairo::ClipRect - setting clip to "
//                  << x << "," << y << " " << w << "x" << h << std::endl;
        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(cairo);
    }

    void RenderContextCairo::ClipPath() {
        cairo_clip(cairo);
    }

    void RenderContextCairo::ClipRoundedRectangle(
            const Rect2Dd& rect,
            double borderTopLeftRadius, double borderTopRightRadius,
            double borderBottomRightRadius, double borderBottomLeftRadius) {
        // Clamp radii to prevent overlapping
        double x = rect.x, y = rect.y, width = rect.width, height = rect.height;
        double maxRadiusX = width / 2.0;
        double maxRadiusY = height / 2.0;

        double topLeftRadius = std::min({borderTopLeftRadius, maxRadiusX, maxRadiusY});
        double topRightRadius = std::min({borderTopRightRadius, maxRadiusX, maxRadiusY});
        double bottomRightRadius = std::min({borderBottomRightRadius, maxRadiusX, maxRadiusY});
        double bottomLeftRadius = std::min({borderBottomLeftRadius, maxRadiusX, maxRadiusY});

        // Adjust if corners overlap
        double topScale = 1.0;
        double bottomScale = 1.0;
        double leftScale = 1.0;
        double rightScale = 1.0;

        if (topLeftRadius + topRightRadius > width) {
            topScale = width / (topLeftRadius + topRightRadius);
        }
        if (bottomLeftRadius + bottomRightRadius > width) {
            bottomScale = width / (bottomLeftRadius + bottomRightRadius);
        }
        if (topLeftRadius + bottomLeftRadius > height) {
            leftScale = height / (topLeftRadius + bottomLeftRadius);
        }
        if (topRightRadius + bottomRightRadius > height) {
            rightScale = height / (topRightRadius + bottomRightRadius);
        }

        float scale = std::min({topScale, bottomScale, leftScale, rightScale});
        topLeftRadius *= scale;
        topRightRadius *= scale;
        bottomRightRadius *= scale;
        bottomLeftRadius *= scale;

        // Create the rounded rectangle path. Trace it clockwise as
        // edge-then-corner so any mix of zero / non-zero radii stays a closed
        // rectangle: a zero radius simply collapses its arc to the corner point
        // (the surrounding line_to calls still draw the full edges). The previous
        // form let a zero-radius corner draw the *next* edge instead of its own,
        // which cut a diagonal across the clip whenever only some corners were
        // rounded (e.g. an image rounded on top but square under a caption strip).
        cairo_new_path(cairo);

        // Start just after the top-left corner and run along the top edge.
        cairo_move_to(cairo, x + topLeftRadius, y);
        cairo_line_to(cairo, x + width - topRightRadius, y);
        if (topRightRadius > 0) {
            cairo_arc(cairo, x + width - topRightRadius, y + topRightRadius,
                      topRightRadius, -M_PI / 2, 0);
        }

        // Right edge -> bottom-right corner.
        cairo_line_to(cairo, x + width, y + height - bottomRightRadius);
        if (bottomRightRadius > 0) {
            cairo_arc(cairo, x + width - bottomRightRadius, y + height - bottomRightRadius,
                      bottomRightRadius, 0, M_PI / 2);
        }

        // Bottom edge -> bottom-left corner.
        cairo_line_to(cairo, x + bottomLeftRadius, y + height);
        if (bottomLeftRadius > 0) {
            cairo_arc(cairo, x + bottomLeftRadius, y + height - bottomLeftRadius,
                      bottomLeftRadius, M_PI / 2, M_PI);
        }

        // Left edge -> back up into the top-left corner.
        cairo_line_to(cairo, x, y + topLeftRadius);
        if (topLeftRadius > 0) {
            cairo_arc(cairo, x + topLeftRadius, y + topLeftRadius,
                      topLeftRadius, M_PI, 3 * M_PI / 2);
        }

        cairo_close_path(cairo);

        // Clip to the rounded rectangle for borders
        cairo_clip(cairo);
    }

// ===== BASIC DRAWING =====
    void RenderContextCairo::FillRectangle(const Rect2Dd& rect) {
//        debugOutput << "RenderContextCairo::FillRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply fill style explicitly ***
        //ApplyFillStyle(currentState.style);

        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        Fill();

//        debugOutput << "RenderContextCairo::FillRectangle: Complete" << std::endl;
    }

    void RenderContextCairo::DrawRectangle(const Rect2Dd& rect) {
//        debugOutput << "RenderContextCairo::DrawRectangle this=" << this << " cairo=" << cairo << std::endl;

        // *** CRITICAL FIX: Apply stroke style explicitly ***
        //ApplyStrokeStyle(currentState.style);

        cairo_rectangle(cairo, rect.x, rect.y, rect.width, rect.height);
        Stroke();

//        debugOutput << "RenderContextCairo::DrawRectangle: Complete" << std::endl;
    }

    void RenderContextCairo::FillRoundedRectangle(const Rect2Dd & rect, double radius) {
//        debugOutput << "RenderContextCairo::FillRoundedRectangle" << std::endl;

        // *** Apply fill style ***
        //ApplyFillStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + rect.height - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, rect.x + radius, rect.y + rect.height - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, rect.x + radius, rect.y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        Fill();
    }

    void RenderContextCairo::DrawRoundedRectangle(const Rect2Dd & rect, double radius) {
//        debugOutput << "RenderContextCairo::DrawRoundedRectangle" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        // Create rounded rectangle path
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + radius, radius, -M_PI_2, 0);
        cairo_arc(cairo, rect.x + rect.width - radius, rect.y + rect.height - radius, radius, 0, M_PI_2);
        cairo_arc(cairo, rect.x + radius, rect.y + rect.height - radius, radius, M_PI_2, M_PI);
        cairo_arc(cairo, rect.x + radius, rect.y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cairo);

        Stroke();
    }

    void RenderContextCairo::FillCircle(const Point2Dd& center, double radius) {
        cairo_new_path(cairo);   // cairo_arc connects a leftover current point to the arc
        cairo_arc(cairo, center.x, center.y, radius, 0, 2 * M_PI);
        Fill();
    }

    void RenderContextCairo::DrawCircle(const Point2Dd& center, double radius) {
        cairo_new_path(cairo);   // cairo_arc connects a leftover current point to the arc
        cairo_arc(cairo, center.x, center.y, radius, 0, 2 * M_PI);
        Stroke();
    }

    void RenderContextCairo::DrawLine(const Point2Dd& from, const Point2Dd& to) {
//        debugOutput << "RenderContextCairo::DrawLine" << std::endl;

        // *** Apply stroke style ***
        //ApplyStrokeStyle(currentState.style);

        cairo_move_to(cairo, from.x, from.y);
        cairo_line_to(cairo, to.x, to.y);
        Stroke();
    }

    PangoLayout* RenderContextCairo::CreatePangoLayout(PangoFontDescription *desc, int w, int h) {
        PangoLayout *layout = pango_layout_new(pangoContext);
        if (!layout) {
            debugOutput << "ERROR: Failed to create Pango layout" << std::endl;
            return nullptr;
        }

        pango_layout_set_font_description(layout, desc);
        if (currentState.textStyle.indent) {
            pango_layout_set_indent(layout, currentState.textStyle.indent);
        }
        if (w > 0 || h > 0) {
            if (w > 0) {
                pango_layout_set_width(layout, w * PANGO_SCALE);
            }
            if (h > 0) {
                pango_layout_set_height(layout, h * PANGO_SCALE);
            }
            pango_layout_set_line_spacing(layout, currentState.textStyle.lineHeight);

            PangoAlignment alignment = PANGO_ALIGN_LEFT;

            switch (currentState.textStyle.alignment) {
                case TextAlignment::Center:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
                    break;
                case TextAlignment::Right:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
                    break;
                case TextAlignment::Justify:
                    pango_layout_set_justify(layout, true);
                    break;
                default:
                    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
                    break;
            }

            switch (currentState.textStyle.wrap) {
                case TextWrap::WrapNone:
//                    pango_layout_set_wrap(layout, PANGO_WRAP_NONE);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
                    break;
                case TextWrap::WrapWord:
                    pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
                case TextWrap::WrapWordChar:
                    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
                case TextWrap::WrapChar:
                    pango_layout_set_wrap(layout, PANGO_WRAP_CHAR);
                    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_NONE);
                    break;
            }
        }
        return layout;
    }

    // ===== TEXT RENDERING =====
    std::unique_ptr<ITextLayout> RenderContextCairo::CreateTextLayout(const std::string& text, bool isMarkup) {
        auto ctx = std::make_unique<UCTextLayout>(pangoContext);
        if (!text.empty()) {
            if (isMarkup) {
                ctx->SetMarkup(text);
            } else {
                ctx->SetText(text);
            }
        }
        return std::move(ctx);
    }

    std::shared_ptr<ITextLayout> RenderContextCairo::GetOrCreateTextLayout(const std::string& text, const Size2Di& sz, bool isMarkup) {
        if (text.empty()) {
            return nullptr;
        }

        // Generate cache key
        std::string cacheKey = GenerateTextCacheKey(text, sz, isMarkup);

        // Try to get from cache first
        auto cached = g_TextLayoutsCache.GetFromCache(cacheKey);
        if (cached) {
            return cached;
        }

        // Not in cache - create new surface
        auto newLayout = std::make_shared<UCTextLayout>(pangoContext);
        if (newLayout) {
            newLayout->SetFontStyle(currentState.fontStyle);
            newLayout->SetAlignment(currentState.textStyle.alignment);
            newLayout->SetWrap(currentState.textStyle.wrap);
            newLayout->SetVerticalAlignment(currentState.textStyle.verticalAlignment);
            // Fixme! Need to set line height
            if (sz.width) {
                newLayout->SetExplicitWidth(sz.width);
            }
            if (sz.height) {
                newLayout->SetExplicitHeight(sz.height);
            }
            if (sz.width && sz.height) {
                newLayout->SetEllipsize(EllipsizeMode::EllipsizeEnd);
            }
            if (isMarkup) {
                newLayout->SetMarkup(text);
            } else {
                newLayout->SetText(text);
            }
            // Add to cache
            g_TextLayoutsCache.AddToCache(cacheKey, newLayout);
        }
        return newLayout;
    }


    void RenderContextCairo::DrawTextLayout(ITextLayout &layout, const Point2Dd &pos) {
        auto extents = layout.GetLayoutExtents();
//        debugOutput << "RenderContextCairo::DrawTextLayout txt=" << layout.GetText() << " pos=" << pos.x << "," << pos.y << " offset=" << layout.GetLayoutVerticalOffset() <<  " extents=" << extents.logical.x << "," << extents.logical.y << " " << extents.logical.width << "x" << extents.logical.height << " ink=" << extents.ink.x << "," << extents.ink.y << " " << extents.ink.width << "x" << extents.ink.height << std::endl;
        cairo_move_to(cairo, pos.x, pos.y + layout.GetLayoutVerticalOffset());
        ApplySourceToCairo(cairo, currentState.textSourceColor, currentState.textSourcePattern);
        pango_cairo_show_layout(cairo, static_cast<PangoLayout *>(layout.GetHandle()));
        // Drop the current point set by cairo_move_to; otherwise a following
        // cairo_arc-based primitive connects it to the arc with a stray line.
        cairo_new_path(cairo);
    }

    void RenderContextCairo::DrawText(const std::string &text, const Point2Dd &pos) {
        // Comprehensive null checks
        if (text.empty()) {
            return; // Nothing to draw
        }
        auto layout = GetOrCreateTextLayout(text, {0, 0}, false);
        if (layout) {
            ApplySourceToCairo(cairo, currentState.textSourceColor, currentState.textSourcePattern);
            DrawTextLayout(*layout, pos);
        } else {
            debugOutput << "RenderContextCairo::DrawText: No text layout" << std::endl;
        }

    }

    void RenderContextCairo::DrawTextInRect(const std::string &text, const Rect2Dd &rect) {
        DrawTextInRect(text, rect, false);
    }

    void RenderContextCairo::DrawTextInRect(const std::string &text, const Rect2Dd &rect, bool isMarkup) {
        if (text.empty()) {
            return; // Nothing to draw
        }
        auto layout = GetOrCreateTextLayout(text, rect.Size(), isMarkup);
        if (layout) {
            DrawTextLayout(*layout, rect.TopLeft());
        } else {
            debugOutput << "RenderContextCairo::DrawTextInRect: No text layout" << std::endl;
        }
    }

    Size2Di RenderContextCairo::GetTextDimensions(const std::string &text, const Size2Di& sz) {
        if (text.empty()) {
            return {0,0};
        }
        auto layout = GetOrCreateTextLayout(text, sz, false);
        if (layout) {
            return layout->GetLayoutSize();
        }
        return {0,0};
    }

    int RenderContextCairo::GetTextIndexForXY(const std::string &text, int x, int y, int w, int h) {
        int index = 0, trailing;
        if (!pangoContext || text.empty()) {
            return -1;
        }

        try {
            PangoFontDescription *desc = CreatePangoFont(currentState.fontStyle);
            if (!desc) {
                debugOutput << "ERROR: Failed to create Pango font description" << std::endl;
                return -1;
            }
            PangoLayout *layout = CreatePangoLayout(desc, w, h);
            if (!layout) {
                pango_font_description_free(desc);
                debugOutput << "ERROR: Failed to create Pango layout" << std::endl;
                return -1;
            }

            // Same repair as UCTextLayout::SetText: Pango takes UTF-8 only,
            // and complains into the debug log about anything else.
            const std::string safe = utf8_make_valid(text);
            pango_layout_set_text(layout, safe.c_str(), -1);

            if (!pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing)) {
                index = -1;
            }

            pango_font_description_free(desc);
            g_object_unref(layout);
            return index;

        } catch (...) {
            debugOutput << "ERROR: Exception in TextXYToIndex" << std::endl;
            return -1;
        }
    }

// ===== UTILITY FUNCTIONS =====
    void RenderContextCairo::Clear(const Color &color) {
        cairo_save(cairo);
        cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
        SetCairoColor(color);
        cairo_paint(cairo);
        cairo_restore(cairo);
    }

//    void RenderContextCairo::SwapBuffers() {
//        std::lock_guard<std::mutex> lock(cairoMutex);
//        if (stagingSurface) {
//            //debugOutput << "RenderContextCairo::SwapBuffers stagingSurface=" << stagingSurface << " target_surf=" << cairo_get_target(targetContext) << std::endl;
//            cairo_surface_flush(stagingSurface);
//            // Copy staging surface to window surface
//            cairo_set_source_surface(targetContext, stagingSurface, 0, 0);
//            cairo_set_operator(targetContext, CAIRO_OPERATOR_SOURCE);
//            cairo_paint(targetContext);
//        }
//    }

    void *RenderContextCairo::GetNativeContext() {
        return cairo;
    }

    void RenderContextCairo::SetTextWrap(UltraCanvas::TextWrap wrap) {
        currentState.textStyle.wrap = wrap;
    }

    void RenderContextCairo::SetTextStyle(const TextStyle &style) {
        currentState.textStyle = style;
        //SetCairoColor(style.textColor);
        //ApplyTextStyle(style);
    }

    const TextStyle &RenderContextCairo::GetTextStyle() const {
        return currentState.textStyle;
    }

    double RenderContextCairo::GetAlpha() const {
        return currentState.globalAlpha;
    }

    PangoFontDescription *RenderContextCairo::CreatePangoFont(const FontStyle &style) {
        //debugOutput << "RenderContextCairo::CreatePangoFont" << std::endl;
        try {
            PangoFontDescription *desc = pango_font_description_new();
            if (!desc) {
                debugOutput << "ERROR: Failed to create Pango font description" << std::endl;
                return nullptr;
            }

            // Use system default font if family is empty. The resolved name
            // lives in this scope until Pango has copied it: it used to be a
            // string local to the `if`, handed on as a pointer that dangled
            // by the time pango_font_description_set_family read it.
            std::string fontFamily = style.fontFamily;
            if (fontFamily.empty()) {
                auto* app = UltraCanvasApplication::GetInstance();
                fontFamily = app ? app->GetSystemFontStyle().fontFamily : "Sans";
            }
            pango_font_description_set_family(desc, fontFamily.c_str());

            // Ensure reasonable font size
            double fontSize = (style.fontSize > 0) ? style.fontSize : 12.0;
            pango_font_description_set_size(desc, fontSize * PANGO_SCALE);

            // Set weight
            PangoWeight weight = PANGO_WEIGHT_NORMAL;
            switch (style.fontWeight) {
                case FontWeight::Light:
                    weight = PANGO_WEIGHT_LIGHT;
                    break;
                case FontWeight::Bold:
                    weight = PANGO_WEIGHT_BOLD;
                    break;
                case FontWeight::ExtraBold:
                    weight = PANGO_WEIGHT_ULTRABOLD;
                    break;
                default:
                    weight = PANGO_WEIGHT_NORMAL;
                    break;
            }
            pango_font_description_set_weight(desc, weight);

            // Set style
            PangoStyle pangoStyle = PANGO_STYLE_NORMAL;
            switch (style.fontSlant) {
                case FontSlant::Italic:
                    pangoStyle = PANGO_STYLE_ITALIC;
                    break;
                case FontSlant::Oblique:
                    pangoStyle = PANGO_STYLE_OBLIQUE;
                    break;
                default:
                    pangoStyle = PANGO_STYLE_NORMAL;
                    break;
            }
            pango_font_description_set_style(desc, pangoStyle);

            return desc;

        } catch (...) {
            debugOutput << "ERROR: Exception in CreatePangoFont" << std::endl;
            return nullptr;
        }
    }

    void RenderContextCairo::SetCairoColor(const Color &color) {
        try {
            cairo_set_source_rgba(cairo,
                                  color.r / 255.0f,
                                  color.g / 255.0f,
                                  color.b / 255.0f,
                                  color.a / 255.0f * currentState.globalAlpha);
//            debugOutput << "RenderContextCairo::SetCairoColor r=" << (int) color.r << " g=" << (int) color.g << " b="
//                      << (int) color.b << std::endl;
        } catch (...) {
            debugOutput << "ERROR: Exception in SetCairoColor" << std::endl;
        }
    }


    void RenderContextCairo::FillEllipse(const Rect2Dd& rect) {
        cairo_new_path(cairo);   // the path survives cairo_save/restore, so clear it here
        // A flat ellipse encloses nothing; see Ellipse() for why it must
        // not reach cairo_scale.
        if (!(std::fabs(rect.width) > 1e-9 && std::fabs(rect.height) > 1e-9)) return;
        Ellipse(rect.x + rect.width / 2, rect.y + rect.height / 2, rect.width / 2, rect.height / 2, 0.0);
        Fill();
    }

    void RenderContextCairo::DrawEllipse(const Rect2Dd& rect) {
        cairo_new_path(cairo);   // the path survives cairo_save/restore, so clear it here
        Ellipse(rect.x + rect.width / 2, rect.y + rect.height / 2, rect.width / 2, rect.height / 2, 0.0);
        Stroke();
    }

    void RenderContextCairo::FillLinePath(const std::vector<Point2Dd> &points) {
        if (points.empty()) return;

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }
        cairo_close_path(cairo);
        Fill();
    }

    void RenderContextCairo::DrawLinePath(const std::vector<Point2Dd> &points, bool closePath) {
        if (points.empty()) return;

        cairo_move_to(cairo, points[0].x, points[0].y);
        for (size_t i = 1; i < points.size(); ++i) {
            cairo_line_to(cairo, points[i].x, points[i].y);
        }

        if (closePath) {
            cairo_close_path(cairo);
        }

        Stroke();
    }


    void RenderContextCairo::DrawArc(double x, double y, double radius, double startAngle, double endAngle) {
        //ApplyStrokeStyle(currentState.style);
        cairo_new_path(cairo);   // cairo_arc connects a leftover current point to the arc
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        Stroke();
    }

    void RenderContextCairo::FillArc(double x, double y, double radius, double startAngle, double endAngle) {
        //ApplyFillStyle(currentState.style);
        cairo_move_to(cairo, x, y);
        cairo_arc(cairo, x, y, radius, startAngle, endAngle);
        cairo_close_path(cairo);
        Fill();
    }

//    void RenderContextCairo::PathStroke() {
//        ApplyStrokeStyle(currentState.style);
//        cairo_stroke(cairo);
//    }
//    void RenderContextCairo::PathFill() {
//        ApplyFillStyle(currentState.style);
//        cairo_fill(cairo);
//    }

    void RenderContextCairo::DrawBezierCurve(const Point2Dd &start, const Point2Dd &cp1, const Point2Dd &cp2,
                                             const Point2Dd &end) {
//        ApplyStrokeStyle(currentState.style);
        cairo_move_to(cairo, start.x, start.y);
        cairo_curve_to(cairo, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
        Stroke();
    }


    // Path Methods
    void RenderContextCairo::ClearPath() {
        cairo_new_path(cairo);
    }

    void RenderContextCairo::ClosePath() {
        cairo_close_path(cairo);
    }

    void RenderContextCairo::MoveTo(double x, double y) {
        cairo_move_to(cairo, x, y);
    }

    void RenderContextCairo::RelMoveTo(double x, double y) {
        cairo_rel_move_to(cairo, x, y);
    }

    void RenderContextCairo::LineTo(double x, double y) {
        cairo_line_to(cairo, x, y);
    }

    void RenderContextCairo::RelLineTo(double x, double y) {
        cairo_rel_line_to(cairo, x, y);
    }

    void RenderContextCairo::QuadraticCurveTo(double cpx, double cpy, double x, double y) {
        double cx, cy;
        cairo_get_current_point(cairo, &cx, &cy);

        // Convert quadratic to cubic bezier
        double cp1x = cx + 2.0f/3.0f * (cpx - cx);
        double cp1y = cy + 2.0f/3.0f * (cpy - cy);
        double cp2x = x + 2.0f/3.0f * (cpx - x);
        double cp2y = y + 2.0f/3.0f * (cpy - y);

        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::BezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) {
        cairo_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::RelBezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) {
        cairo_rel_curve_to(cairo, cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void RenderContextCairo::Arc(double cx, double cy, double radius, double startAngle, double endAngle) {
        cairo_arc(cairo, cx, cy, radius, startAngle, endAngle);
    }

    void RenderContextCairo::ArcTo(double x1, double y1, double x2, double y2, double radius) {
        // Cairo has no arc_to, so emulate the HTML5-canvas semantics: draw a line
        // from the current point toward the corner (x1,y1), then a circular arc of
        // the given radius that is tangent to both the incoming segment
        // (current->corner) and the outgoing segment (corner->(x2,y2)). The arc
        // centre is inset from the corner along the angle bisector — it is NOT the
        // corner itself.
        double x0, y0;
        cairo_get_current_point(cairo, &x0, &y0);

        // Unit vectors from the corner back to the current point and on to (x2,y2).
        double v1x = x0 - x1, v1y = y0 - y1;
        double v2x = x2 - x1, v2y = y2 - y1;
        double len1 = std::hypot(v1x, v1y);
        double len2 = std::hypot(v2x, v2y);

        const double eps = 1e-9;
        if (len1 < eps || len2 < eps || radius <= 0.0) {
            cairo_line_to(cairo, x1, y1);
            return;
        }
        v1x /= len1; v1y /= len1;
        v2x /= len2; v2y /= len2;

        // Half-angle between the two segments at the corner.
        double cosTheta = std::max(-1.0, std::min(1.0, v1x * v2x + v1y * v2y));
        double theta = std::acos(cosTheta);
        double sinHalf = std::sin(theta * 0.5);
        if (sinHalf < eps) {            // collinear: nothing to round
            cairo_line_to(cairo, x1, y1);
            return;
        }

        // Tangent points along each segment and the arc centre on the bisector.
        double tanHalf = std::tan(theta * 0.5);
        double tangentDist = radius / tanHalf;
        double t1x = x1 + v1x * tangentDist, t1y = y1 + v1y * tangentDist;
        double t2x = x1 + v2x * tangentDist, t2y = y1 + v2y * tangentDist;

        double bx = v1x + v2x, by = v1y + v2y;
        double blen = std::hypot(bx, by);
        bx /= blen; by /= blen;
        double centreDist = radius / sinHalf;
        double cx = x1 + bx * centreDist;
        double cy = y1 + by * centreDist;

        double startAngle = std::atan2(t1y - cy, t1x - cx);
        double endAngle   = std::atan2(t2y - cy, t2x - cx);

        cairo_line_to(cairo, t1x, t1y);
        // Sweep along the minor arc; the cross product picks the turn direction.
        double cross = v1x * v2y - v1y * v2x;
        if (cross < 0.0) {
            cairo_arc(cairo, cx, cy, radius, startAngle, endAngle);
        } else {
            cairo_arc_negative(cairo, cx, cy, radius, startAngle, endAngle);
        }
    }

    void RenderContextCairo::Ellipse(double cx, double cy, double rx, double ry, double rotation) {
        // cairo_scale with a zero (or non-finite) factor is an invalid
        // matrix, and cairo answers it by putting the whole context into a
        // permanent error state that cairo_restore does not clear: nothing
        // drawn afterwards reaches the window. The first step of every
        // ellipse drag has a zero-size box, so picking the Ellipse tool in
        // ArtCreator and pressing the mouse blanked the window. A flat
        // ellipse is the line it collapses to; a point is nothing.
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(rx) || !std::isfinite(ry)) return;
        const bool flatX = std::fabs(rx) <= 1e-9, flatY = std::fabs(ry) <= 1e-9;
        if (flatX && flatY) return;
        cairo_save(cairo);
        cairo_translate(cairo, cx, cy);
        cairo_rotate(cairo, rotation);
        if (flatX || flatY) {
            cairo_restore(cairo);
            const double c = std::cos(rotation), s = std::sin(rotation);
            const double ex = flatY ? rx * c : -ry * s;   // one end, relative to the centre
            const double ey = flatY ? rx * s :  ry * c;
            cairo_move_to(cairo, cx - ex, cy - ey);
            cairo_line_to(cairo, cx + ex, cy + ey);
            return;
        }
        cairo_scale(cairo, rx, ry);
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);
    }

    void RenderContextCairo::Rect(double x, double y, double width, double height) {
        cairo_rectangle(cairo, x, y, width, height);
    }

    void RenderContextCairo::RoundedRect(double x, double y, double width, double height, double radius) {
        cairo_new_sub_path(cairo);
        cairo_arc(cairo, x + width - radius, y + radius, radius, -M_PI/2, 0);
        cairo_arc(cairo, x + width - radius, y + height - radius, radius, 0, M_PI/2);
        cairo_arc(cairo, x + radius, y + height - radius, radius, M_PI/2, M_PI);
        cairo_arc(cairo, x + radius, y + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(cairo);
    }

    void RenderContextCairo::DrawRoundedRectangleWidthBorders(
            const Rect2Dd & rect,
            bool fill,
            double borderLeftWidth, double borderRightWidth,
            double borderTopWidth, double borderBottomWidth,
            const Color& borderLeftColor, const Color& borderRightColor,
            const Color& borderTopColor, const Color& borderBottomColor,
            double borderTopLeftRadius, double borderTopRightRadius,
            double borderBottomRightRadius, double borderBottomLeftRadius,
            const UCDashPattern& borderLeftPattern,
            const UCDashPattern& borderRightPattern,
            const UCDashPattern& borderTopPattern,
            const UCDashPattern& borderBottomPattern
    ) {
        // Clamp radii to prevent overlapping
        double x = rect.x, y = rect.y, width = rect.width, height = rect.height;
        double maxRadiusX = width / 2.0;
        double maxRadiusY = height / 2.0;

        double topLeftRadius = std::min({borderTopLeftRadius, maxRadiusX, maxRadiusY});
        double topRightRadius = std::min({borderTopRightRadius, maxRadiusX, maxRadiusY});
        double bottomRightRadius = std::min({borderBottomRightRadius, maxRadiusX, maxRadiusY});
        double bottomLeftRadius = std::min({borderBottomLeftRadius, maxRadiusX, maxRadiusY});

        // Adjust if corners overlap
        double topScale = 1.0;
        double bottomScale = 1.0;
        double leftScale = 1.0;
        double rightScale = 1.0;

        if (topLeftRadius + topRightRadius > width) {
            topScale = width / (topLeftRadius + topRightRadius);
        }
        if (bottomLeftRadius + bottomRightRadius > width) {
            bottomScale = width / (bottomLeftRadius + bottomRightRadius);
        }
        if (topLeftRadius + bottomLeftRadius > height) {
            leftScale = height / (topLeftRadius + bottomLeftRadius);
        }
        if (topRightRadius + bottomRightRadius > height) {
            rightScale = height / (topRightRadius + bottomRightRadius);
        }

        float scale = std::min({topScale, bottomScale, leftScale, rightScale});
        topLeftRadius *= scale;
        topRightRadius *= scale;
        bottomRightRadius *= scale;
        bottomLeftRadius *= scale;

        PushState();

        // A rounded rectangle path, traced clockwise as edge-then-corner so any
        // mix of zero / non-zero radii stays a closed rectangle (a zero radius
        // collapses its arc to the corner point).
        auto roundedPath = [&](double rx, double ry, double rw, double rh,
                               double tl, double tr, double br, double bl) {
            MoveTo(rx + tl, ry);
            LineTo(rx + rw - tr, ry);
            if (tr > 0) Arc(rx + rw - tr, ry + tr, tr, -M_PI / 2, 0);
            LineTo(rx + rw, ry + rh - br);
            if (br > 0) Arc(rx + rw - br, ry + rh - br, br, 0, M_PI / 2);
            LineTo(rx + bl, ry + rh);
            if (bl > 0) Arc(rx + bl, ry + rh - bl, bl, M_PI / 2, M_PI);
            LineTo(rx, ry + tl);
            if (tl > 0) Arc(rx + tl, ry + tl, tl, M_PI, 3 * M_PI / 2);
            ClosePath();
        };

        // Background, and everything after it, inside the outer edge.
        ClearPath();
        roundedPath(x, y, width, height, topLeftRadius, topRightRadius,
                    bottomRightRadius, bottomLeftRadius);
        if (fill) {
            FillPathPreserve();
        }
        ClipPath();
        ClearPath();

        // The padding edge: inside the borders, its corners rounded by what
        // the borders leave of the outer radius.
        const double lw = std::max(0.0, borderLeftWidth),  rw = std::max(0.0, borderRightWidth);
        const double tw = std::max(0.0, borderTopWidth),   bw = std::max(0.0, borderBottomWidth);
        const double ix = x + lw, iy = y + tw;
        const double iw = width - lw - rw, ih = height - tw - bw;
        const bool hasInner = iw > 0 && ih > 0;
        const double itl = std::max(0.0, topLeftRadius - std::max(lw, tw));
        const double itr = std::max(0.0, topRightRadius - std::max(rw, tw));
        const double ibr = std::max(0.0, bottomRightRadius - std::max(rw, bw));
        const double ibl = std::max(0.0, bottomLeftRadius - std::max(lw, bw));

        // A solid side: the part of the ring between the two edges that lies in
        // its wedge - out to the outer corners, in to the inner ones - so two
        // sides meet on the corner's diagonal (CSS's mitred join), each in its
        // own colour, and a rounded corner is shared the same way.
        auto fillSide = [&](const Color& color, std::initializer_list<Point2Dd> wedge) {
            PushState();
            ClearPath();
            bool first = true;
            for (const Point2Dd& pt : wedge) {
                if (first) MoveTo(pt.x, pt.y); else LineTo(pt.x, pt.y);
                first = false;
            }
            ClosePath();
            ClipPath();
            ClearPath();
            roundedPath(x, y, width, height, topLeftRadius, topRightRadius,
                        bottomRightRadius, bottomLeftRadius);
            if (hasInner) roundedPath(ix, iy, iw, ih, itl, itr, ibr, ibl);
            SetFillRule(FillRule::EvenOdd);
            SetFillPaint(color);
            Fill();
            PopState();
        };
        // A dashed or dotted side: a stroke along its straight part.
        auto strokeSide = [&](double w, const Color& color, const UCDashPattern& dash,
                              const Point2Dd& from, const Point2Dd& to) {
            SetStrokeWidth(w);
            SetStrokePaint(color);
            SetLineDash(dash);
            DrawLine(from, to);
            SetLineDash(UCDashPattern());
        };

        // Each wedge runs from its outer corners to the inner ones and on to
        // the middle, so a ring that curves inside the inner corners (a round
        // avatar) is still all in one wedge or another.
        // The inner corners, even where the borders leave no inside (a thick
        // rule): where opposite borders meet, in proportion to their widths.
        double iL = x + lw, iR = x + width - rw, iT = y + tw, iB = y + height - bw;
        if (iL > iR) iL = iR = (lw + rw > 0) ? x + width * lw / (lw + rw) : x + width / 2.0;
        if (iT > iB) iT = iB = (tw + bw > 0) ? y + height * tw / (tw + bw) : y + height / 2.0;
        const double cx = (iL + iR) / 2.0, cy = (iT + iB) / 2.0;

        // One colour all round, all solid: the whole ring at once (no seams
        // where wedges meet).
        auto sameSolid = [&](double w, const Color& c, const UCDashPattern& d) {
            return w == tw && d.dashes.empty() && c.r == borderTopColor.r &&
                   c.g == borderTopColor.g && c.b == borderTopColor.b && c.a == borderTopColor.a;
        };
        if (tw > 0 && borderTopPattern.dashes.empty() &&
            sameSolid(rw, borderRightColor, borderRightPattern) &&
            sameSolid(bw, borderBottomColor, borderBottomPattern) &&
            sameSolid(lw, borderLeftColor, borderLeftPattern)) {
            ClearPath();
            roundedPath(x, y, width, height, topLeftRadius, topRightRadius,
                        bottomRightRadius, bottomLeftRadius);
            if (hasInner) roundedPath(ix, iy, iw, ih, itl, itr, ibr, ibl);
            SetFillRule(FillRule::EvenOdd);
            SetFillPaint(borderTopColor);
            Fill();
            PopState();
            return;
        }
        if (borderTopWidth > 0) {
            if (borderTopPattern.dashes.empty())
                fillSide(borderTopColor, { {x, y}, {x + width, y}, {iR, iT}, {cx, cy}, {iL, iT} });
            else
                strokeSide(borderTopWidth, borderTopColor, borderTopPattern,
                           {x + topLeftRadius, y + tw / 2.0}, {x + width - topRightRadius, y + tw / 2.0});
        }
        if (borderRightWidth > 0) {
            if (borderRightPattern.dashes.empty())
                fillSide(borderRightColor, { {x + width, y}, {x + width, y + height},
                                             {iR, iB}, {cx, cy}, {iR, iT} });
            else
                strokeSide(borderRightWidth, borderRightColor, borderRightPattern,
                           {x + width - rw / 2.0, y + topRightRadius},
                           {x + width - rw / 2.0, y + height - bottomRightRadius});
        }
        if (borderBottomWidth > 0) {
            if (borderBottomPattern.dashes.empty())
                fillSide(borderBottomColor, { {x + width, y + height}, {x, y + height},
                                              {iL, iB}, {cx, cy}, {iR, iB} });
            else
                strokeSide(borderBottomWidth, borderBottomColor, borderBottomPattern,
                           {x + bottomLeftRadius, y + height - bw / 2.0},
                           {x + width - bottomRightRadius, y + height - bw / 2.0});
        }
        if (borderLeftWidth > 0) {
            if (borderLeftPattern.dashes.empty())
                fillSide(borderLeftColor, { {x, y + height}, {x, y}, {iL, iT}, {cx, cy}, {iL, iB} });
            else
                strokeSide(borderLeftWidth, borderLeftColor, borderLeftPattern,
                           {x + lw / 2.0, y + topLeftRadius}, {x + lw / 2.0, y + height - bottomLeftRadius});
        }
        PopState();
    }

    void RenderContextCairo::Circle(double x, double y, double radius) {
        cairo_arc(cairo, x, y, radius, 0, 2 * M_PI);
    }

    Rect2Dd RenderContextCairo::GetPathExtents() {
        Rect2Dd result;
        Point2Dd p2;
        cairo_path_extents(cairo, &result.x, &result.y, &p2.x, &p2.y);
        result.width = std::abs(p2.x - result.x);
        result.height = std::abs(p2.y - result.y);
        return result;
    }

// Cached Gradient Pattern Methods
    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateLinearGradientPattern(double x1, double y1, double x2, double y2,
                                                                                   const std::vector<GradientStop>& stops) {
        cairo_pattern_t* pattern = cairo_pattern_create_linear(x1, y1, x2, y2);

        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }

        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_shared<PaintPatternCairo>(pattern);
        } else {
            return std::make_shared<PaintPatternCairo>(nullptr);
        }
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateRadialGradientPattern(double cx1, double cy1, double r1,
                                                                                   double cx2, double cy2, double r2,
                                                                                   const std::vector<GradientStop>& stops) {
        cairo_pattern_t* pattern = cairo_pattern_create_radial(cx1, cy1, r1, cx2, cy2, r2);

        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }

        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);

        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_shared<PaintPatternCairo>(pattern);
        } else {
            return std::make_shared<PaintPatternCairo>(nullptr);
        }
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateEllipticalGradientPattern(double cx, double cy,
                                                                                       double majorX, double majorY,
                                                                                       double minorX, double minorY,
                                                                                       const std::vector<GradientStop>& stops) {
        // Built as the unit circle gradient and mapped onto the ellipse by the
        // pattern matrix, which cairo applies on top of the CTM in force when
        // the pattern is painted. That keeps the caller's path construction
        // untouched, unlike transforming the context around the fill.
        cairo_matrix_t m;
        cairo_matrix_init(&m, majorX, majorY, minorX, minorY, cx, cy);
        // cairo_pattern_set_matrix takes user space -> pattern space, the
        // inverse of the mapping above. A degenerate pair of axes has no
        // inverse; fall back to a circle of the major axis's length rather
        // than hand cairo a matrix that would error the whole pattern.
        if (cairo_matrix_invert(&m) != CAIRO_STATUS_SUCCESS) {
            const double r = std::sqrt(majorX * majorX + majorY * majorY);
            return CreateRadialGradientPattern(cx, cy, 0.0, cx, cy, r, stops);
        }

        cairo_pattern_t* pattern = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
        for (const auto& stop : stops) {
            cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                stop.color.r / 255.0, stop.color.g / 255.0,
                stop.color.b / 255.0, stop.color.a / 255.0);
        }
        cairo_pattern_set_matrix(pattern, &m);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);

        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_shared<PaintPatternCairo>(pattern);
        } else {
            return std::make_shared<PaintPatternCairo>(nullptr);
        }
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateImagePattern(const std::string& imagePath,
                                                                          const Rect2Dd& anchorRect,
                                                                          ImageFitMode fitMode,
                                                                          bool repeat) {
        if (anchorRect.width <= 0.0 || anchorRect.height <= 0.0) return nullptr;
        auto img = UCImage::Get(imagePath);
        if (!img) return nullptr;
        auto pixmap = img->GetPixmap(static_cast<int>(anchorRect.width),
                                     static_cast<int>(anchorRect.height),
                                     fitMode, GetDeviceScale());
        if (!pixmap || !pixmap->GetSurface()) return nullptr;

        // Logical dimensions; the surface's device_scale handles raw pixels.
        const double pw = static_cast<double>(pixmap->GetWidth());
        const double ph = static_cast<double>(pixmap->GetHeight());
        if (pw <= 0.0 || ph <= 0.0) return nullptr;

        // Same fit arithmetic as DrawPixmapOrMask, expressed as a pattern
        // matrix instead of a context transform.
        double scaleX = 1.0, scaleY = 1.0, offsetX = 0.0, offsetY = 0.0;
        switch (fitMode) {
            case ImageFitMode::Fill:
                scaleX = anchorRect.width / pw;
                scaleY = anchorRect.height / ph;
                break;
            case ImageFitMode::Contain:
            case ImageFitMode::ScaleDown: {
                double s = std::min(anchorRect.width / pw, anchorRect.height / ph);
                if (fitMode == ImageFitMode::ScaleDown) s = std::min(1.0, s);
                scaleX = scaleY = s;
                offsetX = (anchorRect.width - pw * s) / 2.0;
                offsetY = (anchorRect.height - ph * s) / 2.0;
                break;
            }
            case ImageFitMode::Cover: {
                const double s = std::max(anchorRect.width / pw, anchorRect.height / ph);
                scaleX = scaleY = s;
                offsetX = (anchorRect.width - pw * s) / 2.0;
                offsetY = (anchorRect.height - ph * s) / 2.0;
                break;
            }
            case ImageFitMode::NoScale:
            default:
                break;
        }

        cairo_pattern_t* pattern = cairo_pattern_create_for_surface(pixmap->GetSurface());
        cairo_matrix_t matrix;
        cairo_matrix_init_translate(&matrix, anchorRect.x + offsetX, anchorRect.y + offsetY);
        cairo_matrix_scale(&matrix, scaleX, scaleY);
        if (cairo_matrix_invert(&matrix) != CAIRO_STATUS_SUCCESS) {
            cairo_pattern_destroy(pattern);
            return nullptr;
        }
        cairo_pattern_set_matrix(pattern, &matrix);
        cairo_pattern_set_extend(pattern, repeat ? CAIRO_EXTEND_REPEAT : CAIRO_EXTEND_PAD);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);

        if (cairo_pattern_status(pattern) == CAIRO_STATUS_SUCCESS) {
            return std::make_shared<PaintPatternCairo>(pattern);
        }
        cairo_pattern_destroy(pattern);
        return nullptr;
    }

    void RenderContextCairo::SetFillPaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.fillSourcePattern = pattern;
        currentState.fillSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetFillPaint(const Color& color) {
        currentState.fillSourcePattern = nullptr;
        currentState.fillSourceColor = color;
    }

    void RenderContextCairo::SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.strokeSourcePattern = pattern;
        currentState.strokeSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetStrokePaint(const Color& color) {
        currentState.strokeSourceColor = color;
        currentState.strokeSourcePattern = nullptr;
    }

    void RenderContextCairo::SetTextPaint(std::shared_ptr<IPaintPattern> pattern) {
        currentState.textSourcePattern = pattern;
        currentState.textSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::SetTextPaint(const Color& color) {
        currentState.textSourcePattern = nullptr;
        currentState.textSourceColor = color;
    }

    void RenderContextCairo::SetCurrentPaint(const Color& color) {
        cairo_set_source_rgba(cairo,
                              (float)color.r / 255.0f,
                              (float)color.g / 255.0f,
                              (float)color.b / 255.0f,
                              (float)color.a / 255.0f);
        // Set the immediate cairo source above, but also record it as the text
        // source. DrawText/DrawTextLayout re-apply currentState.textSourceColor
        // just before painting, so without this the raw source we just set is
        // clobbered by whatever text colour was last left in the state (e.g. a
        // grey from an earlier widget), and callers that colour text via
        // SetCurrentPaint (Label, TextArea, Barcode) render in the wrong colour.
        currentState.textSourceColor = color;
        currentState.textSourcePattern = nullptr;
    }

    void RenderContextCairo::SetCurrentPaint(std::shared_ptr<IPaintPattern> pattern) {
        auto handle = static_cast<cairo_pattern_t*>(pattern->GetHandle());
        if (handle) {
            cairo_set_source(cairo, handle);
        } else {
            debugOutput << "ERROR: ApplySourceToCairo no pattern handle";
        }
        // Keep the text source in sync so a following DrawTextLayout paints with
        // this pattern rather than re-applying a stale text colour (see above).
        currentState.textSourcePattern = pattern;
        currentState.textSourceColor = Colors::Transparent;
    }

    void RenderContextCairo::ApplySource(const Color& sourceColor, std::shared_ptr<IPaintPattern> sourcePattern) {
        ApplySourceToCairo(cairo, sourceColor, sourcePattern);
    }

    void RenderContextCairo::SetStrokeWidth(double width) {
//        currentState.style.strokeWidth = width;
        cairo_set_line_width(cairo, width);
    }

    void RenderContextCairo::SetLineCap(LineCap cap) {
//        currentState.style.lineCap = cap;
        cairo_line_cap_t cairoCap = CAIRO_LINE_CAP_BUTT;
        switch (cap) {
            case LineCap::Round: cairoCap = CAIRO_LINE_CAP_ROUND; break;
            case LineCap::Square: cairoCap = CAIRO_LINE_CAP_SQUARE; break;
            default: break;
        }
        cairo_set_line_cap(cairo, cairoCap);
    }

    void RenderContextCairo::SetLineJoin(LineJoin join) {
        cairo_line_join_t cairoJoin = CAIRO_LINE_JOIN_MITER;
        switch (join) {
            case LineJoin::Round: cairoJoin = CAIRO_LINE_JOIN_ROUND; break;
            case LineJoin::Bevel: cairoJoin = CAIRO_LINE_JOIN_BEVEL; break;
            default: break;
        }
        cairo_set_line_join(cairo, cairoJoin);
    }

    void RenderContextCairo::SetMiterLimit(double limit) {
        cairo_set_miter_limit(cairo, limit);
    }

    void RenderContextCairo::SetLineDash(const UCDashPattern& pattern) {
        if (pattern.dashes.empty()) {
            cairo_set_dash(cairo, nullptr, 0, 0);
        } else {
            cairo_set_dash(cairo, pattern.dashes.data(), pattern.dashes.size(), pattern.offset);
        }
    }

    void RenderContextCairo::SetTextLineHeight(double height) {
        currentState.textStyle.lineHeight = height;
    }

    void RenderContextCairo::SetAlpha(double alpha) {
        // Get current source and modify alpha
        double r, g, b, a;
        cairo_pattern_t* pattern = cairo_get_source(cairo);
        if (cairo_pattern_get_rgba(pattern, &r, &g, &b, &a) == CAIRO_STATUS_SUCCESS) {
            cairo_set_source_rgba(cairo, r, g, b, alpha);
        }
        currentState.globalAlpha = alpha;
    }

    // Text Methods
    void RenderContextCairo::SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) {
        currentState.fontStyle.fontFamily = family;
        currentState.fontStyle.fontWeight = fw;
        currentState.fontStyle.fontSlant = fs;
    }
    void RenderContextCairo::SetFontFamily(const std::string& family) {
        SetFontFace(family, currentState.fontStyle.fontWeight, currentState.fontStyle.fontSlant);
    }

    void RenderContextCairo::SetFontSize(double size) {
        cairo_set_font_size(cairo, size);
        currentState.fontStyle.fontSize = size;
    }

    void RenderContextCairo::SetFontWeight(UltraCanvas::FontWeight fw) {
        SetFontFace(currentState.fontStyle.fontFamily, fw, currentState.fontStyle.fontSlant);
    }

    void RenderContextCairo::SetFontSlant(UltraCanvas::FontSlant fs) {
        SetFontFace(currentState.fontStyle.fontFamily, currentState.fontStyle.fontWeight, fs);
    }

    void RenderContextCairo::SetTextAlignment(TextAlignment align) {
        currentState.textStyle.alignment = align;
    }

    void RenderContextCairo::SetTextVerticalAlignment(VerticalAlignment align) {
        currentState.textStyle.verticalAlignment = align;
    }

    void RenderContextCairo::SetTextIsMarkup(bool isMarkup) {
        currentState.textStyle.isMarkup = isMarkup;
    }

    void RenderContextCairo::FillText(const std::string& text, double x, double y) {
        ApplyFillSource();
        cairo_select_font_face(cairo, currentState.fontStyle.fontFamily.c_str(),
                               currentState.fontStyle.fontSlant == FontSlant::Oblique ? CAIRO_FONT_SLANT_OBLIQUE : (currentState.fontStyle.fontSlant == FontSlant::Italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL),
                               currentState.fontStyle.fontWeight == FontWeight::Bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);

        cairo_move_to(cairo, x, y);
        cairo_show_text(cairo, text.c_str());
        // cairo_show_text leaves the current point at the end of the text;
        // drop it so the next cairo_arc-based primitive doesn't connect to it.
        cairo_new_path(cairo);
    }

    void RenderContextCairo::StrokeText(const std::string& text, double x, double y) {
        ApplyStrokeSource();
        cairo_select_font_face(cairo, currentState.fontStyle.fontFamily.c_str(),
                               currentState.fontStyle.fontSlant == FontSlant::Oblique ? CAIRO_FONT_SLANT_OBLIQUE : (currentState.fontStyle.fontSlant == FontSlant::Italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL),
                               currentState.fontStyle.fontWeight == FontWeight::Bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_move_to(cairo, x, y);
        cairo_text_path(cairo, text.c_str());
        cairo_stroke(cairo);
    }

    void DrawPixmapOrMask(cairo_t* cairo, UCPixmap &pixmap, double x, double y, double w, double h, ImageFitMode fitMode, double alpha, bool drawMasked, const Color& c, bool smooth = true) {
        double pixWidth = static_cast<double>(pixmap.GetWidth());
        double pixHeight = static_cast<double>(pixmap.GetHeight());
        if (!w) {
            w = pixWidth;
        }
        if (!h) {
            h = pixHeight;
        }
        double scaleX = 1;
        double scaleY = 1;
        double offsetX = 0;
        double offsetY = 0;
        // Calculate scaling factors
        if (pixHeight != h || pixWidth != w) {
            switch (fitMode) {
                case ImageFitMode::Contain:
                    scaleX = w / pixWidth;
                    scaleY = h / pixHeight;

                    if (scaleX < scaleY) {
                        scaleY = scaleX;
                        offsetY = (h - (pixHeight * scaleY)) / 2;
                    } else {
                        scaleX = scaleY;
                        offsetX = (w - (pixWidth * scaleX)) / 2;
                    }
                    break;
                case ImageFitMode::Cover:
                    scaleX = w / pixWidth;
                    scaleY = h / pixHeight;

                    if (scaleX < scaleY) {
                        scaleX = scaleY;
                        offsetX = (w - (pixWidth * scaleX)) / 2;
                    } else {
                        scaleY = scaleX;
                        offsetY = (h - (pixHeight * scaleY)) / 2;
                    }
                    break;
                case ImageFitMode::NoScale:
                    offsetX = (w - pixWidth) / 2;
                    offsetY = (h - pixHeight) / 2;
                    break;
                case ImageFitMode::Fill:
                    scaleX = w / pixWidth;
                    scaleY = h / pixHeight;
                    break;
                case ImageFitMode::ScaleDown:
                    scaleX = w / pixWidth;
                    scaleY = h / pixHeight;
                    if (scaleX < scaleY) {
                        if (scaleX > 1) {
                            scaleX = 1;
                        }
                        scaleY = scaleX;
                    } else {
                        if (scaleY > 1) {
                            scaleY = 1;
                        }
                        scaleX = scaleY;
                    }
                    offsetY = (h - (pixHeight * scaleY)) / 2;
                    offsetX = (w - (pixWidth * scaleX)) / 2;
                    break;
                default:
                    break;
            }
        }

        // Apply transformations
        cairo_save(cairo);
        cairo_rectangle(cairo, x, y, w, h);
        cairo_clip(cairo);

        cairo_translate(cairo, x + offsetX, y + offsetY);
        // Apply clipping to ensure we don't draw outside the destination rectangle

        if (scaleX != 1.0 || scaleY != 1.0) {
            cairo_scale(cairo, scaleX, scaleY);
        }

        // Set the image as source and paint
        if (drawMasked) {
            // Recolor the icon: use the pixmap's alpha channel as a mask for a
            // solid color source so the SVG/icon is tinted with `c` (e.g. theme
            // foreground, or a grey for disabled/inactive icons). Honors the
            // global alpha so callers can dim via SetAlpha().
            cairo_set_source_rgba(cairo,
                                  c.r / 255.0, c.g / 255.0, c.b / 255.0,
                                  (c.a / 255.0) * alpha);
            cairo_mask_surface(cairo, pixmap.GetSurface(), 0, 0);
        } else {
            cairo_set_source_surface(cairo, pixmap.GetSurface(), 0, 0);
            if (!smooth) cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_NEAREST);
            if (alpha < 1.0f) {
                cairo_paint_with_alpha(cairo, alpha);
            } else {
                cairo_paint(cairo);
            }
        }

        // Restore cairo state
        cairo_restore(cairo);
    }

    void RenderContextCairo::DrawPixmap(UCPixmap& pixmap, const Rect2Dd& rect, ImageFitMode fitMode) {
        DrawPixmapOrMask(cairo, pixmap, rect.x, rect.y, rect.width, rect.height, fitMode,
                         currentState.globalAlpha, false, Colors::Transparent, imageSmoothing);
    }

    void RenderContextCairo::DrawMask(const Color& c, UCPixmap& mask, const Rect2Dd& rect, ImageFitMode fitMode) {
        DrawPixmapOrMask(cairo, mask, rect.x, rect.y, rect.width, rect.height, fitMode,
                         currentState.globalAlpha, true, c);
    }

    void RenderContextCairo::DrawPartOfPixmap(UCPixmap & pixmap, const Rect2Dd &srcRect, const Rect2Dd &destRect) {
        try {
            // Validate source rectangle bounds
            if (srcRect.x < 0 || srcRect.y < 0 ||
                srcRect.x + srcRect.width > pixmap.GetWidth() ||
                srcRect.y + srcRect.height > pixmap.GetHeight()) {
                debugOutput << "RenderContextCairo::DrawPartOfPixmap: Source rectangle out of bounds" << std::endl;
                return;
            }

            // Save current cairo state
            cairo_save(cairo);

            // Calculate scaling factors
            float scaleX = destRect.width / srcRect.width;
            float scaleY = destRect.height / srcRect.height;

            // Set up transformations for source rectangle mapping
            cairo_translate(cairo, destRect.x, destRect.y);
            cairo_scale(cairo, scaleX, scaleY);
            cairo_translate(cairo, -srcRect.x, -srcRect.y);

            // Set the image as source
            cairo_set_source_surface(cairo, pixmap.GetSurface(), 0, 0);
            if (!imageSmoothing) cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_NEAREST);

            // Create clipping rectangle for the destination area
            cairo_rectangle(cairo,
                            srcRect.x, srcRect.y,
                            srcRect.width, srcRect.height);
            cairo_clip(cairo);

            // Paint the image
            if (currentState.globalAlpha < 1.0f) {
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_paint(cairo);
            }

            // Restore cairo state
            cairo_restore(cairo);
        } catch (const std::exception &e) {
            debugOutput << "RenderContextCairo::DrawImage: Exception loading image: " << e.what() << std::endl;
        }
    }

    void RenderContextCairo::DrawImageTiled(std::shared_ptr<UCImage> image, float x, float y, float w, float h) {
        try {
            if (!image->IsValid()) return;
            auto pixmap = image->GetPixmap();
            if (!pixmap) return;

            cairo_save(cairo);

            // Create repeating pattern
            cairo_pattern_t *pattern = cairo_pattern_create_for_surface(pixmap->GetSurface());
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

            // Set source and fill the rectangle
            cairo_set_source(cairo, pattern);
            cairo_rectangle(cairo, x, y, w, h);

            if (currentState.globalAlpha < 1.0f) {
                cairo_clip(cairo);
                cairo_paint_with_alpha(cairo, currentState.globalAlpha);
            } else {
                cairo_fill(cairo);
            }

            cairo_pattern_destroy(pattern);
            cairo_restore(cairo);

        } catch (const std::exception &e) {
            debugOutput << "RenderContextCairo::DrawImageTiled: Exception: " << e.what() << std::endl;
        }
    }

    void RenderContextCairo::UpdateContext(cairo_t *newCairoContext) {
        debugOutput << "RenderContextCairo: Updating Cairo context..." << std::endl;

        if (!newCairoContext) {
            debugOutput << "ERROR: RenderContextCairo: New Cairo context is null!" << std::endl;
            return;
        }
        cairo_status_t status = cairo_status(newCairoContext);
        if (status != CAIRO_STATUS_SUCCESS) {
            debugOutput << "ERROR: RenderContextCairo: New Cairo context is invalid: "
                      << cairo_status_to_string(status) << std::endl;
            throw std::runtime_error("RenderContextCairo: New Cairo context is invalid");
            return;
        }

        // Update the cairo pointer
        cairo = newCairoContext;

        // Re-associate Pango context with new Cairo context
        if (pangoContext) {
            pango_cairo_update_context(cairo, pangoContext);
            pango_cairo_context_set_resolution(pangoContext, g_PangoResolution);
            ApplyPangoFontOptions();
        }

        // Reset state
        ResetState();

        debugOutput << "RenderContextCairo: Cairo context updated successfully" << std::endl;
    }

    void RenderContextCairo::FillPathPreserve() {
        ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        cairo_fill_preserve(cairo);
    }

    void RenderContextCairo::SetFillRule(FillRule rule) {
        cairo_set_fill_rule(cairo, rule == FillRule::EvenOdd
                ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
    }

    void RenderContextCairo::StrokePathPreserve() {
        ApplySource(currentState.strokeSourceColor, currentState.strokeSourcePattern);
        cairo_stroke_preserve(cairo);
    }

    void RenderContextCairo::Stroke() {
        ApplySource(currentState.strokeSourceColor, currentState.strokeSourcePattern);
        cairo_stroke(cairo);
    }

    void RenderContextCairo::Fill() {
        ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        cairo_fill(cairo);
    }

    void RenderContextCairo::FlushToSurface(NativeSurfacePtr flushToSurface, const Point2Dd& pos) {
        // Copy staging surface to window surface
        cairo_t *toCtx = cairo_create(static_cast<cairo_surface_t *>(flushToSurface));
        if (!toCtx) {
            debugOutput << "RenderContextCairo::FlushToSurface can't create context for flushToSurface" << std::endl;
            return;
        }
        cairo_surface_flush(surface);
        if (pos.x != 0 || pos.y != 0) {
            cairo_translate(toCtx, pos.x, pos.y);
        }
        cairo_rectangle(toCtx, 0, 0, surfaceSize.width, surfaceSize.height);
        cairo_clip(toCtx);
        cairo_set_source_surface(toCtx, surface, 0, 0);
        cairo_set_operator(toCtx, CAIRO_OPERATOR_SOURCE);
        cairo_paint(toCtx);
        cairo_surface_flush(static_cast<cairo_surface_t *>(flushToSurface));
        cairo_destroy(toCtx);
    }

    void RenderContextCairo::CompositeToSurface(NativeSurfacePtr flushToSurface, const Point2Dd& pos) {
        // Alpha-blend the surface over the destination (OVER, not SOURCE)
        cairo_t *toCtx = cairo_create(static_cast<cairo_surface_t *>(flushToSurface));
        if (!toCtx) {
            debugOutput << "RenderContextCairo::CompositeToSurface can't create context for flushToSurface" << std::endl;
            return;
        }
        cairo_surface_flush(surface);
        if (pos.x != 0 || pos.y != 0) {
            cairo_translate(toCtx, pos.x, pos.y);
        }
        cairo_rectangle(toCtx, 0, 0, surfaceSize.width, surfaceSize.height);
        cairo_clip(toCtx);
        cairo_set_source_surface(toCtx, surface, 0, 0);
        cairo_set_operator(toCtx, CAIRO_OPERATOR_OVER);
        cairo_paint(toCtx);
        cairo_surface_flush(static_cast<cairo_surface_t *>(flushToSurface));
        cairo_destroy(toCtx);
    }

    void RenderContextCairo::FlushRegionToSurface(NativeSurfacePtr flushToSurface,
                                                  const Rect2Dd& region, const Point2Dd& destPos) {
        // Copy only `region` of the surface, placing it at destPos
        cairo_t *toCtx = cairo_create(static_cast<cairo_surface_t *>(flushToSurface));
        if (!toCtx) {
            debugOutput << "RenderContextCairo::FlushRegionToSurface can't create context for flushToSurface" << std::endl;
            return;
        }
        cairo_surface_flush(surface);
        cairo_translate(toCtx, destPos.x, destPos.y);
        cairo_rectangle(toCtx, 0, 0, region.width, region.height);
        cairo_clip(toCtx);
        cairo_set_source_surface(toCtx, surface, -region.x, -region.y);
        cairo_set_operator(toCtx, CAIRO_OPERATOR_SOURCE);
        cairo_paint(toCtx);
        cairo_surface_flush(static_cast<cairo_surface_t *>(flushToSurface));
        cairo_destroy(toCtx);
    }


    // ===== PATTERN PLACEMENT =====
    void PaintPatternCairo::SetMatrix(double a, double b, double c, double d, double e, double f) {
        if (!pattern) return;
        // The caller gives pattern -> user; cairo wants user -> pattern, and
        // the pattern's own placement (baseMatrix, user -> pattern at
        // creation) still applies underneath: P' = M^-1 then base.
        cairo_matrix_t m;
        cairo_matrix_init(&m, a, b, c, d, e, f);
        if (cairo_matrix_invert(&m) != CAIRO_STATUS_SUCCESS) return;
        cairo_matrix_t composed;
        cairo_matrix_multiply(&composed, &m, &baseMatrix);
        cairo_pattern_set_matrix(pattern, &composed);
    }

    void PaintPatternCairo::SetExtend(PatternExtend extend) {
        if (!pattern) return;
        cairo_extend_t e = CAIRO_EXTEND_PAD;
        switch (extend) {
            case PatternExtend::Pad: e = CAIRO_EXTEND_PAD; break;
            case PatternExtend::Repeat: e = CAIRO_EXTEND_REPEAT; break;
            case PatternExtend::Reflect: e = CAIRO_EXTEND_REFLECT; break;
            case PatternExtend::NoExtend: e = CAIRO_EXTEND_NONE; break;
        }
        cairo_pattern_set_extend(pattern, e);
    }

    // ===== TRANSFORM READBACK =====
    void RenderContextCairo::GetTransform(double& a, double& b, double& c, double& d, double& e, double& f) const {
        cairo_matrix_t m;
        cairo_get_matrix(cairo, &m);
        a = m.xx; b = m.yx; c = m.xy; d = m.yy; e = m.x0; f = m.y0;
    }

    Point2Dd RenderContextCairo::UserToDevice(const Point2Dd& p) const {
        double x = p.x, y = p.y;
        cairo_user_to_device(cairo, &x, &y);
        return Point2Dd(x, y);
    }

    Point2Dd RenderContextCairo::DeviceToUser(const Point2Dd& p) const {
        double x = p.x, y = p.y;
        cairo_device_to_user(cairo, &x, &y);
        return Point2Dd(x, y);
    }

    double RenderContextCairo::DeviceToUserDistance(double devicePixels) const {
        double ax = devicePixels, ay = 0, bx = 0, by = devicePixels;
        cairo_device_to_user_distance(cairo, &ax, &ay);
        cairo_device_to_user_distance(cairo, &bx, &by);
        return (std::sqrt(ax * ax + ay * ay) + std::sqrt(bx * bx + by * by)) / 2.0;
    }

    // ===== COMPOSITING =====
    namespace {
        cairo_operator_t ToCairoOperator(BlendMode mode) {
            switch (mode) {
                case BlendMode::Normal: return CAIRO_OPERATOR_OVER;
                case BlendMode::Multiply: return CAIRO_OPERATOR_MULTIPLY;
                case BlendMode::Screen: return CAIRO_OPERATOR_SCREEN;
                case BlendMode::Overlay: return CAIRO_OPERATOR_OVERLAY;
                case BlendMode::Darken: return CAIRO_OPERATOR_DARKEN;
                case BlendMode::Lighten: return CAIRO_OPERATOR_LIGHTEN;
                case BlendMode::ColorDodge: return CAIRO_OPERATOR_COLOR_DODGE;
                case BlendMode::ColorBurn: return CAIRO_OPERATOR_COLOR_BURN;
                case BlendMode::HardLight: return CAIRO_OPERATOR_HARD_LIGHT;
                case BlendMode::SoftLight: return CAIRO_OPERATOR_SOFT_LIGHT;
                case BlendMode::Difference: return CAIRO_OPERATOR_DIFFERENCE;
                case BlendMode::Exclusion: return CAIRO_OPERATOR_EXCLUSION;
                case BlendMode::Hue: return CAIRO_OPERATOR_HSL_HUE;
                case BlendMode::Saturation: return CAIRO_OPERATOR_HSL_SATURATION;
                case BlendMode::Color: return CAIRO_OPERATOR_HSL_COLOR;
                case BlendMode::Luminosity: return CAIRO_OPERATOR_HSL_LUMINOSITY;
            }
            return CAIRO_OPERATOR_OVER;
        }

        cairo_antialias_t ToCairoAntialias(AntialiasMode mode) {
            switch (mode) {
                case AntialiasMode::DefaultQuality: return CAIRO_ANTIALIAS_DEFAULT;
                case AntialiasMode::NoAntialias: return CAIRO_ANTIALIAS_NONE;
                case AntialiasMode::Gray: return CAIRO_ANTIALIAS_GRAY;
                case AntialiasMode::Subpixel: return CAIRO_ANTIALIAS_SUBPIXEL;
                case AntialiasMode::Fast: return CAIRO_ANTIALIAS_FAST;
                case AntialiasMode::Good: return CAIRO_ANTIALIAS_GOOD;
                case AntialiasMode::Best: return CAIRO_ANTIALIAS_BEST;
            }
            return CAIRO_ANTIALIAS_DEFAULT;
        }

        void AddStops(cairo_pattern_t* pattern, const std::vector<GradientStop>& stops) {
            for (const auto& stop : stops) {
                cairo_pattern_add_color_stop_rgba(pattern, stop.position,
                    stop.color.r / 255.0, stop.color.g / 255.0,
                    stop.color.b / 255.0, stop.color.a / 255.0);
            }
        }

        // Colour of a stop list at position t (0..1), padded at both ends.
        Color ColorAt(const std::vector<GradientStop>& stops, double t) {
            if (stops.empty()) return Colors::Black;
            if (t <= stops.front().position) return stops.front().color;
            if (t >= stops.back().position) return stops.back().color;
            for (size_t i = 1; i < stops.size(); ++i) {
                if (t <= stops[i].position) {
                    const GradientStop& a = stops[i - 1];
                    const GradientStop& b = stops[i];
                    const double span = b.position - a.position;
                    const double k = span > 0 ? (t - a.position) / span : 0.0;
                    auto mix = [k](uint8_t x, uint8_t y) {
                        return static_cast<uint8_t>(std::lround(x + (y - x) * k));
                    };
                    return Color(mix(a.color.r, b.color.r), mix(a.color.g, b.color.g),
                                 mix(a.color.b, b.color.b), mix(a.color.a, b.color.a));
                }
            }
            return stops.back().color;
        }

        void SetCorner(cairo_pattern_t* mesh, unsigned corner, const Color& c) {
            cairo_mesh_pattern_set_corner_color_rgba(mesh, corner,
                c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
        }
    }

    void RenderContextCairo::SetBlendMode(BlendMode mode) {
        currentState.blendMode = mode;
        cairo_set_operator(cairo, ToCairoOperator(mode));
    }

    void RenderContextCairo::SetAntialias(AntialiasMode mode) {
        cairo_set_antialias(cairo, ToCairoAntialias(mode));
    }

    // cairo_push_group saves the cairo state and cairo_pop_group restores
    // it; the paint/font state on this side is pushed and popped alongside
    // so a group is a PushState/PopState pair from the caller's view. The
    // operator in force at BeginGroup() is what the group composites with,
    // since pop_group restores it before the paint.
    void RenderContextCairo::BeginGroup() {
        stateStack.push_back(currentState);
        cairo_push_group(cairo);
    }

    void RenderContextCairo::EndGroup(double opacity) {
        cairo_pop_group_to_source(cairo);
        if (opacity >= 1.0) cairo_paint(cairo);
        else if (opacity > 0.0) cairo_paint_with_alpha(cairo, opacity);
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
        }
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::EndGroupAsPattern() {
        cairo_pattern_t* group = cairo_pop_group(cairo);
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
        }
        if (!group || cairo_pattern_status(group) != CAIRO_STATUS_SUCCESS) {
            if (group) cairo_pattern_destroy(group);
            return nullptr;
        }
        return std::make_shared<PaintPatternCairo>(group);
    }

    void RenderContextCairo::EndGroupMasked(std::shared_ptr<IPaintPattern> mask) {
        cairo_pattern_t* group = cairo_pop_group(cairo);
        if (!stateStack.empty()) {
            currentState = stateStack.back();
            stateStack.pop_back();
        }
        if (!group) return;
        cairo_set_source(cairo, group);
        auto* maskPattern = mask ? static_cast<cairo_pattern_t*>(mask->GetHandle()) : nullptr;
        if (maskPattern) cairo_mask(cairo, maskPattern);
        else cairo_paint(cairo);
        cairo_pattern_destroy(group);
    }

    void RenderContextCairo::PaintPattern(std::shared_ptr<IPaintPattern> pattern, double opacity) {
        auto* p = pattern ? static_cast<cairo_pattern_t*>(pattern->GetHandle()) : nullptr;
        if (!p || opacity <= 0.0) return;
        cairo_save(cairo);
        cairo_set_source(cairo, p);
        if (opacity >= 1.0) cairo_paint(cairo);
        else cairo_paint_with_alpha(cairo, opacity);
        cairo_restore(cairo);
    }

    // ===== HIT TESTING =====
    bool RenderContextCairo::IsPointInFill(double x, double y) {
        return cairo_in_fill(cairo, x, y) != 0;
    }

    bool RenderContextCairo::IsPointInStroke(double x, double y) {
        return cairo_in_stroke(cairo, x, y) != 0;
    }

    Rect2Dd RenderContextCairo::GetStrokeExtents() {
        double x1, y1, x2, y2;
        cairo_stroke_extents(cairo, &x1, &y1, &x2, &y2);
        return Rect2Dd(x1, y1, x2 - x1, y2 - y1);
    }

    // ===== TEXT OUTLINES =====
    void RenderContextCairo::AppendTextPath(const std::string& text, const Point2Dd& pos) {
        if (text.empty()) return;
        auto layout = GetOrCreateTextLayout(text, {0, 0}, false);
        if (layout) AppendTextLayoutPath(*layout, pos);
    }

    void RenderContextCairo::AppendTextLayoutPath(ITextLayout& layout, const Point2Dd& pos) {
        auto* pangoLayout = static_cast<PangoLayout*>(layout.GetHandle());
        if (!pangoLayout) return;
        cairo_move_to(cairo, pos.x, pos.y + layout.GetLayoutVerticalOffset());
        pango_cairo_layout_path(cairo, pangoLayout);
    }

    // ===== CONIC / MESH / PIXMAP PATTERNS =====
    // Cairo has no conic gradient; a fan of mesh patches round the centre,
    // each spanning at most 15 degrees so that the bilinear colour across
    // the patch is indistinguishable from the true angular ramp, is one.
    // The fan reaches far enough (a fixed radius in pattern space) to cover
    // any shape it is painted into; the pattern is transparent beyond it.
    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateConicGradientPattern(double cx, double cy,
                                                                                  double startAngle, double endAngle,
                                                                                  const std::vector<GradientStop>& stops) {
        if (stops.empty()) return nullptr;
        const double twoPi = 2.0 * M_PI;
        double sweep = endAngle - startAngle;
        if (std::fabs(sweep) < 1e-9) sweep = twoPi;
        const double R = 1.0e5;
        const int minSlices = static_cast<int>(std::ceil(std::fabs(sweep) / (M_PI / 12.0)));
        // Break at every stop as well so a hard stop stays hard.
        std::vector<double> ts;
        ts.push_back(0.0);
        for (const auto& s : stops) if (s.position > 0.0 && s.position < 1.0) ts.push_back(s.position);
        ts.push_back(1.0);
        std::sort(ts.begin(), ts.end());
        ts.erase(std::unique(ts.begin(), ts.end()), ts.end());

        cairo_pattern_t* mesh = cairo_pattern_create_mesh();
        for (size_t i = 1; i < ts.size(); ++i) {
            const double t0 = ts[i - 1], t1 = ts[i];
            const int n = std::max(1, static_cast<int>(std::ceil((t1 - t0) * minSlices)));
            for (int k = 0; k < n; ++k) {
                const double ta = t0 + (t1 - t0) * k / n;
                const double tb = t0 + (t1 - t0) * (k + 1) / n;
                const double a0 = startAngle + sweep * ta;
                const double a1 = startAngle + sweep * tb;
                const Color c0 = ColorAt(stops, ta);
                const Color c1 = ColorAt(stops, tb);
                const Point2Dd p0(cx + R * std::cos(a0), cy + R * std::sin(a0));
                const Point2Dd p1(cx + R * std::cos(a1), cy + R * std::sin(a1));
                // Arc between p0 and p1 as one cubic (the slice is < 90 deg).
                const double d = a1 - a0;
                const double kk = 4.0 / 3.0 * std::tan(d / 4.0);
                const Point2Dd h0(p0.x - kk * R * std::sin(a0), p0.y + kk * R * std::cos(a0));
                const Point2Dd h1(p1.x + kk * R * std::sin(a1), p1.y - kk * R * std::cos(a1));
                cairo_mesh_pattern_begin_patch(mesh);
                cairo_mesh_pattern_move_to(mesh, cx, cy);
                cairo_mesh_pattern_line_to(mesh, p0.x, p0.y);
                cairo_mesh_pattern_curve_to(mesh, h0.x, h0.y, h1.x, h1.y, p1.x, p1.y);
                // Three sides: cairo closes the triangle back to the centre.
                SetCorner(mesh, 0, c0);
                SetCorner(mesh, 1, c0);
                SetCorner(mesh, 2, c1);
                SetCorner(mesh, 3, c1);
                cairo_mesh_pattern_end_patch(mesh);
            }
        }
        if (cairo_pattern_status(mesh) != CAIRO_STATUS_SUCCESS) {
            cairo_pattern_destroy(mesh);
            return nullptr;
        }
        return std::make_shared<PaintPatternCairo>(mesh);
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreateMeshGradientPattern(const std::vector<MeshGradientPatch>& patches) {
        if (patches.empty()) return nullptr;
        cairo_pattern_t* mesh = cairo_pattern_create_mesh();
        for (const auto& patch : patches) {
            cairo_mesh_pattern_begin_patch(mesh);
            cairo_mesh_pattern_move_to(mesh, patch.corners[0].x, patch.corners[0].y);
            for (int side = 0; side < 4; ++side) {
                const Point2Dd& to = patch.corners[(side + 1) % 4];
                if (patch.hasControls) {
                    const Point2Dd& c1 = patch.controls[2 * side];
                    const Point2Dd& c2 = patch.controls[2 * side + 1];
                    cairo_mesh_pattern_curve_to(mesh, c1.x, c1.y, c2.x, c2.y, to.x, to.y);
                } else {
                    cairo_mesh_pattern_line_to(mesh, to.x, to.y);
                }
            }
            for (unsigned i = 0; i < 4; ++i) SetCorner(mesh, i, patch.colors[i]);
            cairo_mesh_pattern_end_patch(mesh);
        }
        if (cairo_pattern_status(mesh) != CAIRO_STATUS_SUCCESS) {
            cairo_pattern_destroy(mesh);
            return nullptr;
        }
        return std::make_shared<PaintPatternCairo>(mesh);
    }

    std::shared_ptr<IPaintPattern> RenderContextCairo::CreatePixmapPattern(UCPixmap& pixmap, const Rect2Dd& anchorRect,
                                                                           PatternExtend extend) {
        if (!pixmap.GetSurface() || anchorRect.width <= 0.0 || anchorRect.height <= 0.0) return nullptr;
        const double pw = static_cast<double>(pixmap.GetWidth());
        const double ph = static_cast<double>(pixmap.GetHeight());
        if (pw <= 0.0 || ph <= 0.0) return nullptr;
        cairo_surface_flush(pixmap.GetSurface());
        cairo_pattern_t* pattern = cairo_pattern_create_for_surface(pixmap.GetSurface());
        cairo_matrix_t matrix;
        cairo_matrix_init_translate(&matrix, anchorRect.x, anchorRect.y);
        cairo_matrix_scale(&matrix, anchorRect.width / pw, anchorRect.height / ph);
        if (cairo_matrix_invert(&matrix) != CAIRO_STATUS_SUCCESS) {
            cairo_pattern_destroy(pattern);
            return nullptr;
        }
        cairo_pattern_set_matrix(pattern, &matrix);
        cairo_pattern_set_filter(pattern, imageSmoothing ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
        if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
            cairo_pattern_destroy(pattern);
            return nullptr;
        }
        auto result = std::make_shared<PaintPatternCairo>(pattern);
        result->SetExtend(extend);
        return result;
    }

    // factory
    std::unique_ptr<IRenderContext> CreateRenderContext(const Size2Di& sz, NativeSurfacePtr similarToSurface) {
        auto ctx = std::make_unique<RenderContextCairo>();
        if (ctx->CreateSurface(sz, similarToSurface)) {
            return ctx;
        } else {
            return nullptr;
        }
    }
} // namespace UltraCanvas