// libspecific/Cairo/SvgDocumentCairo.cpp
// Implementation of the parse-once SVG document (retained RsvgHandle + LRU
// document cache). See SvgDocumentCairo.h for the design rationale.
// Version: 1.1.0
// Last Modified: 2026-07-21
// Author: UltraCanvas Framework

#ifdef HAS_LIBRSVG

#include "SvgDocumentCairo.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#include <librsvg/rsvg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <vector>

namespace UltraCanvas {

    struct UCSvgDocumentCacheEntry {
        std::shared_ptr<UCSvgDocument> payload;
        std::chrono::steady_clock::time_point lastAccess;
        size_t GetEntrySize() {
            return payload->GetMemoryBytes() + sizeof(UCSvgDocumentCacheEntry);
        }
    };

    // Parsed-document cache. Entries are estimated from the source size (the
    // parse tree itself is not queryable), so the budget is generous relative
    // to typical SVG sources — a diagram is a few KB to a few hundred KB.
    static UCCache<UCSvgDocument, UCSvgDocumentCacheEntry> g_SvgDocsCache(16 * 1024 * 1024);

    bool UCSvgDocument::IsSvgPath(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || path.size() - dot != 4) return false;
        return (path[dot + 1] == 's' || path[dot + 1] == 'S') &&
               (path[dot + 2] == 'v' || path[dot + 2] == 'V') &&
               (path[dot + 3] == 'g' || path[dot + 3] == 'G');
    }

    UCSvgDocument::~UCSvgDocument() {
        if (handle) {
            g_object_unref(handle);
        }
    }

    std::shared_ptr<UCSvgDocument> UCSvgDocument::Get(const std::string& path) {
        std::shared_ptr<UCSvgDocument> doc = g_SvgDocsCache.GetFromCache(path);
        if (!doc) {
            doc = Parse(path);
            if (doc && doc->IsValid()) {
                g_SvgDocsCache.AddToCache(path, doc);
            }
        }
        return doc;
    }

    void UCSvgDocument::RemoveFromCache(const std::string& path) {
        g_SvgDocsCache.RemoveFromCache(path);
    }

    std::shared_ptr<UCSvgDocument> UCSvgDocument::Parse(const std::string& path) {
        auto doc = std::make_shared<UCSvgDocument>();

        // The source bytes only live for the duration of the parse — the
        // retained state is the parsed document, not the XML.
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.good()) {
            debugOutput << "UCSvgDocument: Cannot read " << path << std::endl;
            return doc;
        }
        const std::streamsize size = file.tellg();
        file.seekg(0);
        std::vector<char> bytes(static_cast<size_t>(size));
        if (size > 0) file.read(bytes.data(), size);
        if (!file.good()) {
            debugOutput << "UCSvgDocument: Failed reading " << path << std::endl;
            return doc;
        }

        GError* error = nullptr;
        RsvgHandle* hnd = rsvg_handle_new_from_data(
                reinterpret_cast<const guint8*>(bytes.data()),
                static_cast<gsize>(size), &error);
        if (!hnd) {
            debugOutput << "UCSvgDocument: Parse failed for " << path << ": "
                        << (error ? error->message : "unknown error") << std::endl;
            if (error) g_error_free(error);
            return doc;
        }
        rsvg_handle_set_dpi(hnd, 96.0);

        double w = 0.0, h = 0.0;
#if LIBRSVG_CHECK_VERSION(2, 52, 0)
        gdouble ow = 0.0, oh = 0.0;
        if (rsvg_handle_get_intrinsic_size_in_pixels(hnd, &ow, &oh) && ow > 0.0 && oh > 0.0) {
            w = ow;
            h = oh;
        }
#endif
        if (w <= 0.0 || h <= 0.0) {
            // Documents without absolute width/height (viewBox only): fall
            // back to the legacy dimension query, which derives them.
            RsvgDimensionData dim = {};
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            rsvg_handle_get_dimensions(hnd, &dim);
            G_GNUC_END_IGNORE_DEPRECATIONS
            w = dim.width;
            h = dim.height;
        }
        if (w <= 0.0 || h <= 0.0) {
            debugOutput << "UCSvgDocument: No usable intrinsic size in " << path << std::endl;
            g_object_unref(hnd);
            return doc;
        }

        doc->handle = hnd;
        doc->intrinsicWidth = w;
        doc->intrinsicHeight = h;
        // The parse tree's real footprint isn't exposed; estimate it as a
        // multiple of the source size for the cache accounting.
        doc->memoryBytes = static_cast<size_t>(size) * 4 + sizeof(UCSvgDocument) + 1024;
        return doc;
    }

    std::shared_ptr<UCPixmapCairo> UCSvgDocument::RenderPixmap(int w, int h,
                                                               ImageFitMode fitMode,
                                                               float scale) {
        if (!handle) return nullptr;
        if (scale <= 0.0f) scale = 1.0f;
        const double iw = intrinsicWidth;
        const double ih = intrinsicHeight;
        if (w <= 0) w = static_cast<int>(std::lround(iw));
        if (h <= 0) h = static_cast<int>(std::lround(ih));

        // Reproduce the generic loader's fit-mode geometry: Contain/ScaleDown
        // produce the fitted size, Fill/Cover produce exactly w × h.
        double sx, sy, tx = 0.0, ty = 0.0;
        int outW, outH;
        bool tagDeviceScale = (scale != 1.0f);
        switch (fitMode) {
            case ImageFitMode::Fill:
                sx = w / iw;
                sy = h / ih;
                outW = w;
                outH = h;
                break;
            case ImageFitMode::Cover: {
                const double s = std::max(w / iw, h / ih);
                sx = sy = s;
                outW = w;
                outH = h;
                tx = (w - iw * s) / 2.0;
                ty = (h - ih * s) / 2.0;
                break;
            }
            case ImageFitMode::ScaleDown: {
                const double s = std::min(1.0, std::min(w / iw, h / ih));
                sx = sy = s;
                outW = static_cast<int>(std::lround(iw * s));
                outH = static_cast<int>(std::lround(ih * s));
                break;
            }
            case ImageFitMode::NoScale:
                // Matches the generic path: intrinsic size, no HiDPI oversampling.
                sx = sy = 1.0;
                outW = static_cast<int>(std::lround(iw));
                outH = static_cast<int>(std::lround(ih));
                scale = 1.0f;
                tagDeviceScale = false;
                break;
            case ImageFitMode::Contain:
            default: {
                const double s = std::min(w / iw, h / ih);
                sx = sy = s;
                outW = static_cast<int>(std::lround(iw * s));
                outH = static_cast<int>(std::lround(ih * s));
                break;
            }
        }
        const int rawW = std::max(1, static_cast<int>(std::lround(outW * scale)));
        const int rawH = std::max(1, static_cast<int>(std::lround(outH * scale)));

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rawW, rawH);
        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UCSvgDocument: Cannot create " << rawW << "x" << rawH
                        << " surface" << std::endl;
            cairo_surface_destroy(surf);
            return nullptr;
        }
        cairo_t* cr = cairo_create(surf);
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, tx, ty);
        cairo_scale(cr, sx, sy);

        gboolean ok;
        {
            std::lock_guard<std::mutex> lock(renderMutex);
            GError* error = nullptr;
#if LIBRSVG_CHECK_VERSION(2, 46, 0)
            // Viewport = intrinsic size, so the document renders at its own
            // scale and the cairo transform above does all the sizing.
            RsvgRectangle viewport = {0.0, 0.0, iw, ih};
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error);
#else
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            ok = rsvg_handle_render_cairo(handle, cr);
            G_GNUC_END_IGNORE_DEPRECATIONS
#endif
            if (error) {
                debugOutput << "UCSvgDocument: Render failed: " << error->message << std::endl;
                g_error_free(error);
            }
        }
        cairo_destroy(cr);
        if (!ok) {
            cairo_surface_destroy(surf);
            return nullptr;
        }
        cairo_surface_flush(surf);

        auto pm = std::make_shared<UCPixmapCairo>(surf);   // takes ownership
        if (tagDeviceScale) {
            pm->SetDeviceScale(scale);
        }
        return pm;
    }

} // namespace UltraCanvas

#endif // HAS_LIBRSVG
