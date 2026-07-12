// include/UltraCanvasSupportedFormats.h
// Central runtime inventory of the file formats the framework can load and
// save. Each entry names the media category, the load/save capability and the
// library or plugin that provides it. The inventory is computed at call time:
// compile-time gates (libvips, MuPDF, audio/video backends) are combined with
// runtime probes (libvips saver/loader lookup, the graphics plugin registry,
// the eBook engine registry), so the answer always reflects what THIS build
// on THIS system can actually do — not what the framework aspires to support.
// Version: 1.0.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

    enum class MediaFormatCategory {
        Bitmap,        // raster images (png, jpg, webp, ...)
        Vector,        // vector graphics (svg, cdr, xar, ...)
        Model3D,       // 3D models (stl, ...)
        Document,      // word processing, PDF, e-books, markdown/plain text
        Spreadsheet,   // ods, xlsx, csv, tsv
        Audio,
        Video,
    };

    struct MediaFormatInfo {
        std::string extension;              // canonical extension, lowercase, no dot
        std::vector<std::string> aliases;   // other extensions of the same format
        std::string description;            // human-readable format name
        MediaFormatCategory category = MediaFormatCategory::Bitmap;
        bool canLoad = false;
        bool canSave = false;
        std::string provider;               // library or plugin implementing the support
        std::string notes;                  // caveats (codec/platform/plugin dependent)

        // True when ext (with or without leading dot, any case) equals the
        // canonical extension or one of the aliases.
        bool MatchesExtension(const std::string& ext) const;
    };

    // All queries build a fresh snapshot: graphics plugins and eBook engines
    // register at runtime, and libvips probes are cheap lookups, so results
    // are never stale. Cache the vector yourself if you query in a tight loop.
    class UltraCanvasSupportedFormats {
    public:
        static std::vector<MediaFormatInfo> GetAll();
        static std::vector<MediaFormatInfo> GetByCategory(MediaFormatCategory category);

        // Flat extension lists (canonical + aliases) of the formats that can
        // be loaded / saved in the category — ready for file dialog filters.
        static std::vector<std::string> GetLoadExtensions(MediaFormatCategory category);
        static std::vector<std::string> GetSaveExtensions(MediaFormatCategory category);

        // Alias-aware, case-insensitive lookup ("JPEG", ".jpg" and "jpg" all
        // find the same entry). Empty optional when no format matches.
        static std::optional<MediaFormatInfo> FindByExtension(const std::string& extension);

        static std::string GetCategoryName(MediaFormatCategory category);
    };

} // namespace UltraCanvas
