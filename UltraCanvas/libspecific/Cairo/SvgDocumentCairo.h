// libspecific/Cairo/SvgDocumentCairo.h
// Parse-once SVG document: retains the parsed librsvg handle so repeated
// rasterizations (zoom levels, HiDPI, layout changes) skip the file read and
// the XML parse and only pay for the vector render itself.
// Version: 1.0.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework
#pragma once
#ifdef HAS_LIBRSVG

#include "UltraCanvasImage.h"
#include <string>
#include <memory>
#include <mutex>

typedef struct _RsvgHandle RsvgHandle;

namespace UltraCanvas {

    // A parsed SVG document. Documents are shared through Get()'s internal
    // LRU cache, so a file is read and parsed at most once while it stays
    // cached; every RenderPixmap call replays the retained parse tree at the
    // requested size. Render calls on one document are serialized with a
    // mutex, so a document can be rasterized from a worker thread while the
    // UI thread holds the same handle.
    class UCSvgDocument {
    public:
        ~UCSvgDocument();

        // Cached accessor: returns the already-parsed document for `path`,
        // parsing (and caching) it on first use. The returned document may be
        // invalid (parse/read failure) — check IsValid(); failures are not
        // cached so a corrected file is picked up on the next call.
        static std::shared_ptr<UCSvgDocument> Get(const std::string& path);

        // True for paths this parse-once path handles: plain ".svg" files.
        // Compressed ".svgz" is left to the generic loader (librsvg's
        // from-data entry point does not gunzip).
        static bool IsSvgPath(const std::string& path);

        bool IsValid() const { return handle != nullptr; }
        double GetIntrinsicWidth() const { return intrinsicWidth; }
        double GetIntrinsicHeight() const { return intrinsicHeight; }

        // Rasterize into a fresh pixmap of logical size derived from `w × h`
        // and `fitMode`, at `scale` raw pixels per logical unit (HiDPI) —
        // same semantics as UCImageRaster::CreatePixmap so it can stand in
        // for the generic loader transparently.
        std::shared_ptr<UCPixmapCairo> RenderPixmap(int w, int h,
                                                    ImageFitMode fitMode,
                                                    float scale);

        // Approximate retained footprint (for the document cache budget).
        size_t GetMemoryBytes() const { return memoryBytes; }

    private:
        static std::shared_ptr<UCSvgDocument> Parse(const std::string& path);

        RsvgHandle* handle = nullptr;
        double intrinsicWidth = 0.0;
        double intrinsicHeight = 0.0;
        size_t memoryBytes = 0;
        std::mutex renderMutex;
    };

} // namespace UltraCanvas

#endif // HAS_LIBRSVG
