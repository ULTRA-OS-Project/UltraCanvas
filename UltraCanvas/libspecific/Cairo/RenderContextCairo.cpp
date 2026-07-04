// libspecific/Cairo/RenderContextCairo.cpp
// Cairo support implementation for UltraCanvas Framework
// Version: 1.0.9 - DrawTextLayout now applies text source color before pango_cairo_show_layout
// Last Modified: 2026-05-17
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasUtils.h"
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


    void RenderContextCairo::ApplyPangoFontOptions() {
        cairo_font_options_t *opts = cairo_font_options_create();
        cairo_font_options_set_antialias(opts, g_TextAntialias);
        cairo_font_options_set_hint_style(opts, g_TextHintStyle);
        cairo_font_options_set_hint_metrics(opts, g_TextHintMetrics);
        pango_cairo_context_set_font_options(pangoContext, opts);
        cairo_font_options_destroy(opts);
    }

    void RenderContextCairo::SetTextAntialias(cairo_antialias_t mode) {
        if (g_TextAntialias != mode) {
            g_TextAntialias = mode;
//            g_TextSurfacesCache.ClearCache();
//            g_TextDimensionsCache.ClearCache();
            g_TextLayoutsCache.ClearCache();
            for (auto* instance : g_Instances) {
                instance->ApplyPangoFontOptions();
            }
        }
    }

    cairo_antialias_t RenderContextCairo::GetTextAntialias() {
        return g_TextAntialias;
    }

    void RenderContextCairo::SetTextHintStyle(cairo_hint_style_t style) {
        if (g_TextHintStyle != style) {
            g_TextHintStyle = style;
//            g_TextSurfacesCache.ClearCache();
//            g_TextDimensionsCache.ClearCache();
            g_TextLayoutsCache.ClearCache();
            for (auto* instance : g_Instances) {
                instance->ApplyPangoFontOptions();
            }
        }
    }

    cairo_hint_style_t RenderContextCairo::GetTextHintStyle() {
        return g_TextHintStyle;
    }

    void RenderContextCairo::SetTextHintMetrics(cairo_hint_metrics_t metrics) {
        if (g_TextHintMetrics != metrics) {
            g_TextHintMetrics = metrics;
//            g_TextSurfacesCache.ClearCache();
//            g_TextDimensionsCache.ClearCache
            g_TextLayoutsCache.ClearCache();
            for (auto* instance : g_Instances) {
                instance->ApplyPangoFontOptions();
            }
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
        }
    }

    std::string RenderContextCairo::GenerateTextCacheKey(const std::string& text, const Size2Di &sz) {
        // Generate a unique cache key based on all parameters that affect text rendering
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
                  << (currentState.textStyle.isMarkup ? "1" : "0")
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

        // Belt-and-braces: also pin the cairo surface's fallback DPI and the
        // default Pango font map's resolution. Pango's draw-time
        // pango_cairo_update_layout() can resync metrics from cairo's
        // font_options or the font map; both must agree with our context pin
        // or text widths drift between platforms.
        cairo_surface_set_fallback_resolution(surface, g_PangoResolution, g_PangoResolution);
        if (PangoFontMap* fm = pango_cairo_font_map_get_default()) {
            pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(fm), g_PangoResolution);
        }

        // One-shot diagnostic: the only reliable way to discover which Pango/
        // Cairo layer is reporting a non-pinned resolution on a given platform.
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
            cairo_font_options_t* fo = cairo_font_options_create();
            cairo_get_font_options(cairo, fo);
            debugOutput << "UC text-render diag:"
                        << " pango_ctx_res=" << ctxRes
                        << " fontmap_res=" << fmRes
                        << " surface_fallback_res=" << sxFb << "x" << syFb
                        << " surface_device_scale=" << devSx << "x" << devSy
                        << " hint_style=" << cairo_font_options_get_hint_style(fo)
                        << " hint_metrics=" << cairo_font_options_get_hint_metrics(fo)
                        << " antialias=" << cairo_font_options_get_antialias(fo)
                        << std::endl;
            cairo_font_options_destroy(fo);
        }

        // Apply configurable text rendering font options
        ApplyPangoFontOptions();

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
        } else {
            debugOutput << "RenderContextCairo::PopState() stateStack empty!" << std::endl;
        }
        cairo_restore(cairo);
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
    void RenderContextCairo::Translate(double x, double y) {
        if (x != 0 || y != 0) {
            cairo_translate(cairo, x, y);
            currentState.translation.x += x;
            currentState.translation.y += y;
        }
    }

    void RenderContextCairo::Rotate(double angle) {
        cairo_rotate(cairo, angle);
        currentState.rotation += angle;
    }

    void RenderContextCairo::Scale(double sx, double sy) {
        cairo_scale(cairo, sx, sy);
        currentState.scale.x *= sx;
        currentState.scale.y *= sy;
    }

    void RenderContextCairo::SetTransform(double a, double b, double c, double d, double e, double f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_set_matrix(cairo, &matrix);
    }

    void RenderContextCairo::Transform(double a, double b, double c, double d, double e, double f) {
        cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, a, b, c, d, e, f);
        cairo_transform(cairo, &matrix);
    }

    void RenderContextCairo::ResetTransform() {
        cairo_identity_matrix(cairo);
        currentState.translation = Point2Dd(0, 0);
        currentState.rotation = 0;
        currentState.scale = Point2Dd(1, 1);
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
        cairo_arc(cairo, center.x, center.y, radius, 0, 2 * M_PI);
        Fill();
    }

    void RenderContextCairo::DrawCircle(const Point2Dd& center, double radius) {
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
        std::string cacheKey = GenerateTextCacheKey(text, sz);

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
        if (text.empty()) {
            return; // Nothing to draw
        }
        auto layout = GetOrCreateTextLayout(text, rect.Size(), false);
        if (layout) {
            ApplySourceToCairo(cairo, currentState.textSourceColor, currentState.textSourcePattern);
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

            pango_layout_set_text(layout, text.c_str(), -1);

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

            // Use system default font if family is empty
            const char *fontFamily;
            if (style.fontFamily.empty()) {
                std::string resolvedFamily;
                auto* app = UltraCanvasApplication::GetInstance();
                resolvedFamily = app ? app->GetSystemFontStyle().fontFamily : "Sans";
                fontFamily = resolvedFamily.c_str();
            } else {
                fontFamily = style.fontFamily.c_str();
            }
            pango_font_description_set_family(desc, fontFamily);

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

        cairo_save(cairo);
        cairo_translate(cairo, rect.x + rect.width / 2, rect.y + rect.height / 2);
        cairo_scale(cairo, rect.width / 2, rect.height / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);

        Fill();
    }

    void RenderContextCairo::DrawEllipse(const Rect2Dd& rect) {

        cairo_save(cairo);
        cairo_translate(cairo, rect.x + rect.width / 2, rect.y + rect.height / 2);
        cairo_scale(cairo, rect.width / 2, rect.height / 2);
        cairo_arc(cairo, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cairo);

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
        cairo_save(cairo);
        cairo_translate(cairo, cx, cy);
        cairo_rotate(cairo, rotation);
        cairo_scale(cairo, rx, ry);
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

        // Create the rounded rectangle path. Trace it clockwise as
        // edge-then-corner so any mix of zero / non-zero radii stays a closed
        // rectangle (a zero radius collapses its arc to the corner point while
        // the line_to calls still draw the full edges). The earlier form let a
        // zero-radius corner draw the *next* edge, cutting a diagonal across the
        // fill whenever only some corners were rounded.
        ClearPath();

        // Start just after the top-left corner and run along the top edge.
        MoveTo(x + topLeftRadius, y);
        LineTo(x + width - topRightRadius, y);
        if (topRightRadius > 0) {
            Arc(x + width - topRightRadius, y + topRightRadius,
                      topRightRadius, -M_PI / 2, 0);
        }

        // Right edge -> bottom-right corner.
        LineTo(x + width, y + height - bottomRightRadius);
        if (bottomRightRadius > 0) {
            Arc(x + width - bottomRightRadius, y + height - bottomRightRadius,
                      bottomRightRadius, 0, M_PI / 2);
        }

        // Bottom edge -> bottom-left corner.
        LineTo(x + bottomLeftRadius, y + height);
        if (bottomLeftRadius > 0) {
            Arc(x + bottomLeftRadius, y + height - bottomLeftRadius,
                      bottomLeftRadius, M_PI / 2, M_PI);
        }

        // Left edge -> back up into the top-left corner.
        LineTo(x, y + topLeftRadius);
        if (topLeftRadius > 0) {
            Arc(x + topLeftRadius, y + topLeftRadius,
                      topLeftRadius, M_PI, 3 * M_PI / 2);
        }

        ClosePath();

        // Fill background
        if (fill) {
            FillPathPreserve();
        }
        ClipPath();

        // Clip to the rounded rectangle for borders
//        cairo_clip_preserve(cr);
//        cairo_new_path(cr);

        // Draw borders (inset by half the border width for proper positioning)
        // Top border
        if (borderTopWidth > 0) {
            SetStrokeWidth(borderTopWidth);
            if (!borderTopPattern.dashes.empty()) {
                SetLineDash(borderTopPattern);
            } else {
                SetStrokePaint(borderTopColor);
            }
            float yPos = y + borderTopWidth / 2.0;
            DrawLine({x + topLeftRadius, yPos}, {x + width - topRightRadius, yPos});
//            drawBorderSide(x + topLeftRadius, yPos,
//                           x + width - topRightRadius, yPos,
//                           borderTopWidth, borderTopColor, borderTopPattern);
        }

        // Right border
        if (borderRightWidth > 0) {
            SetStrokeWidth(borderRightWidth);
            if (!borderRightPattern.dashes.empty()) {
                SetLineDash(borderRightPattern);
            } else {
                SetStrokePaint(borderRightColor);
            }
            float xPos = x + width - borderRightWidth / 2.0;
            DrawLine({xPos, y + topRightRadius},
                     {xPos, y + height - bottomRightRadius});
        }

        // Bottom border
        if (borderBottomWidth > 0) {
            SetStrokeWidth(borderBottomWidth);
            if (!borderBottomPattern.dashes.empty()) {
                SetLineDash(borderBottomPattern);
            } else {
                SetStrokePaint(borderBottomColor);
            }
            float yPos = y + height - borderBottomWidth / 2.0;
            DrawLine({x + bottomLeftRadius, yPos},
                     {x + width - bottomRightRadius, yPos});
        }

        // Left border
        if (borderLeftWidth > 0) {
            float xPos = x + borderLeftWidth / 2.0;
            SetStrokeWidth(borderLeftWidth);
            if (!borderLeftPattern.dashes.empty()) {
                SetLineDash(borderLeftPattern);
            } else {
                SetStrokePaint(borderLeftColor);
            }
            DrawLine({xPos, y + topLeftRadius},
                     {xPos, y + height - bottomLeftRadius});
        }

        // Draw rounded corners with borders.
        // The path used for ClipPath() above follows the outer edge of the
        // rounded rectangle, so a corner arc drawn at the full corner radius is
        // centred on the clip boundary and has its outer half clipped away,
        // making the corners look thinner than the straight edges. Inset each
        // arc radius by half its stroke width so the stroke's outer edge lines
        // up with the clip boundary, matching how the straight borders above are
        // inset by half their width.
        if (topLeftRadius > 0) {
            const Color avgColor = borderLeftColor.Blend(borderTopColor, 0.5);
            double avgWidth = (borderLeftWidth + borderTopWidth) / 2.0;
            double arcRadius = std::max(0.0, topLeftRadius - avgWidth / 2.0);
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            DrawArc(x + topLeftRadius, y + topLeftRadius, arcRadius,
                M_PI, 3 * M_PI / 2);
        }
        if (topRightRadius > 0) {
            const Color avgColor = borderTopColor.Blend(borderRightColor, 0.5);
            double avgWidth = (borderTopWidth + borderRightWidth) / 2.0;
            double arcRadius = std::max(0.0, topRightRadius - avgWidth / 2.0);
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            DrawArc(x + width - topRightRadius, y + topRightRadius, arcRadius,
                3 * M_PI / 2, 2 * M_PI);
        }

        if (bottomRightRadius > 0) {
            const Color avgColor = borderBottomColor.Blend(borderRightColor, 0.5);
            double avgWidth = (borderRightWidth +  borderBottomWidth) / 2.0;
            double arcRadius = std::max(0.0, bottomRightRadius - avgWidth / 2.0);
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            DrawArc(x + width - bottomRightRadius, y + height - bottomRightRadius,
                arcRadius, 0, M_PI / 2);
        }

        if (bottomLeftRadius > 0) {
            const Color avgColor = borderBottomColor.Blend(borderLeftColor, 0.5);
            double avgWidth = (borderBottomWidth + borderLeftWidth) / 2.0;
            double arcRadius = std::max(0.0, bottomLeftRadius - avgWidth / 2.0);
            SetStrokeWidth(avgWidth);
            SetStrokePaint(avgColor);
            DrawArc(x + bottomLeftRadius, y + height - bottomLeftRadius, arcRadius,
                M_PI / 2, M_PI);
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
    }

    void RenderContextCairo::SetCurrentPaint(std::shared_ptr<IPaintPattern> pattern) {
        auto handle = static_cast<cairo_pattern_t*>(pattern->GetHandle());
        if (handle) {
            cairo_set_source(cairo, handle);
        } else {
            debugOutput << "ERROR: ApplySourceToCairo no pattern handle";
        }
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

    void DrawPixmapOrMask(cairo_t* cairo, UCPixmap &pixmap, double x, double y, double w, double h, ImageFitMode fitMode, double alpha, bool drawMasked, const Color& c) {
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
                         currentState.globalAlpha, false, Colors::Transparent);
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