// core/UltraCanvasSupportedFormats.cpp
// Builds the runtime supported-format inventory. Sources of truth, per
// category:
//   Bitmap       — candidate table probed per-extension against the installed
//                  libvips build (VipsCanLoad / VipsCanSave).
//   Vector       — svg/svgz through libvips+librsvg (probed); everything else
//                  comes from the graphics plugin registry (CDR/XAR/... appear
//                  once the host application registers the plugin).
//   Model3D      — graphics plugin registry only (STL plugin et al.).
//   Document     — MuPDF (compile-gated), the built-in ODT/DOCX/DOC engines,
//                  the Markdown/HTML text path, plus every extension the
//                  eBook engine registry reports at runtime.
//   Spreadsheet  — the built-in ODS/XLSX/CSV engines (always compiled in).
//   Audio        — the miniaudio backend: WAV/MP3/FLAC decode, WAV encode.
//                  (OGG/AAC/Opus are deliberately absent: no decoder is wired.)
//   Video        — the platform backend's demuxer/muxer matrix (GStreamer /
//                  Media Foundation / AVFoundation).
// Version: 1.0.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasImage.h"                 // VipsCanLoad / VipsCanSave
#include "Documents/eBook/IEBookEngine.h"     // GetRegisteredEBookExtensions
#include "Documents/eBook/TXTEngine.h"        // RegisterBuiltinEBookEngines

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

    std::string ToLowerNoDot(const std::string& ext) {
        std::string out = ext;
        if (!out.empty() && out[0] == '.') out.erase(0, 1);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool ListContains(const std::vector<MediaFormatInfo>& list, const std::string& ext) {
        return std::any_of(list.begin(), list.end(),
                           [&ext](const MediaFormatInfo& f) { return f.MatchesExtension(ext); });
    }

#ifdef HAS_LIBVIPS
    // The libvips probes require vips_init; normally the application has done
    // this long before anyone queries formats, but the API must also be safe
    // to call first thing in main(). InitializeImageSubsysterm is a VIPS_INIT
    // wrapper and vips_init is a no-op after the first successful call.
    bool EnsureImageSubsystem() {
        static const bool ok = UCImageRaster::InitializeImageSubsysterm(nullptr);
        return ok;
    }
#endif

    // ---- Bitmap: probe the candidate formats against the installed libvips ----
    void AddBitmapFormats(std::vector<MediaFormatInfo>& out) {
        struct Candidate {
            const char* ext;
            std::vector<std::string> aliases;
            const char* description;
        };
        // Everything the UCImage load/save path implements; whether a
        // particular loader/saver is really present depends on how libvips
        // was built (e.g. .heic needs libheif with an HEVC codec).
        static const std::vector<Candidate> candidates = {
            { "png",  {},                 "Portable Network Graphics" },
            { "jpg",  { "jpeg", "jfif" }, "JPEG image" },
            { "webp", {},                 "WebP image" },
            { "avif", {},                 "AV1 Image File Format" },
            { "heic", { "heif" },         "High Efficiency Image Format" },
            { "gif",  {},                 "GIF image (incl. animation)" },
            { "bmp",  {},                 "Windows Bitmap" },
            { "tiff", { "tif" },          "Tagged Image File Format" },
            { "tga",  {},                 "Truevision Targa" },
            { "hdr",  {},                 "Radiance HDR" },
            { "exr",  {},                 "OpenEXR" },
            { "jxl",  {},                 "JPEG XL" },
            { "jp2",  { "j2k", "jpf" },   "JPEG 2000" },
            { "ppm",  { "pgm", "pbm", "pnm" }, "Portable anymap" },
            { "qoi",  {},                 "Quite OK Image" },
            { "psd",  {},                 "Adobe Photoshop document" },
            { "ico",  {},                 "Windows icon" },
            { "fits", {},                 "Flexible Image Transport System" },
        };

#ifdef HAS_LIBVIPS
        if (!EnsureImageSubsystem()) return;
        // magickload advertises no suffixes (content-sniffing), so for these
        // known-raster candidates its presence means the format will load
        // even when no dedicated loader matched the extension.
        const bool magick = VipsHasMagickLoadFallback();
        for (const auto& c : candidates) {
            const std::string dotExt = std::string(".") + c.ext;
            MediaFormatInfo f;
            f.extension   = c.ext;
            f.aliases     = c.aliases;
            f.description = c.description;
            f.category    = MediaFormatCategory::Bitmap;
            f.canLoad     = VipsCanLoad(dotExt);
            f.canSave     = VipsCanSave(dotExt);
            f.provider    = "libvips";
            if (!f.canLoad && magick) {
                f.canLoad = true;
                f.provider = "libvips (ImageMagick delegate)";
            }
            if (f.canLoad || f.canSave) out.push_back(std::move(f));
        }
#else
        // Without libvips only cairo's native PNG reader is available.
        MediaFormatInfo png;
        png.extension   = "png";
        png.description = "Portable Network Graphics";
        png.category    = MediaFormatCategory::Bitmap;
        png.canLoad     = true;
        png.provider    = "cairo";
        png.notes       = "framework built without libvips";
        out.push_back(std::move(png));
        (void)candidates;
#endif
    }

    // ---- Vector: svg via libvips+librsvg; eps/ps depend on the vips build ----
    void AddVectorFormats(std::vector<MediaFormatInfo>& out) {
#ifdef HAS_LIBVIPS
        if (!EnsureImageSubsystem()) return;
        struct Candidate {
            const char* ext;
            const char* description;
            const char* provider;
            const char* notes;
        };
        static const std::vector<Candidate> candidates = {
            { "svg",  "Scalable Vector Graphics",  "librsvg (via libvips)", "" },
            { "svgz", "Compressed SVG",            "librsvg (via libvips)", "" },
            { "eps",  "Encapsulated PostScript",   "libvips delegate",
              "requires a libvips build with a PostScript loader" },
            { "ps",   "PostScript",                "libvips delegate",
              "requires a libvips build with a PostScript loader" },
        };
        for (const auto& c : candidates) {
            MediaFormatInfo f;
            f.extension   = c.ext;
            f.description = c.description;
            f.category    = MediaFormatCategory::Vector;
            f.canLoad     = VipsCanLoad(std::string(".") + c.ext);
            f.canSave     = false;   // no vector saver in the image pipeline
            f.provider    = c.provider;
            f.notes       = c.notes;
            if (f.canLoad) out.push_back(std::move(f));
        }
#else
        (void)out;
#endif
        // CDR, XAR, ... arrive through the graphics plugin registry (merged in
        // AddRegisteredGraphicsPlugins) once the application registers them.
    }

    // ---- Documents: MuPDF + built-in word/text engines + eBook registry ----
    void AddDocumentFormats(std::vector<MediaFormatInfo>& out) {
#ifdef ULTRACANVAS_PDF_MUPDF
        out.push_back({ "pdf", {}, "Portable Document Format",
                        MediaFormatCategory::Document, true, true,
                        "MuPDF", "save supports incremental update" });
#endif
        out.push_back({ "odt", {}, "OpenDocument Text",
                        MediaFormatCategory::Document, true, true,
                        "built-in (miniz + tinyxml2)", "" });
        out.push_back({ "docx", {}, "Word document (OOXML)",
                        MediaFormatCategory::Document, true, true,
                        "built-in (miniz + tinyxml2)", "" });
        out.push_back({ "doc", {}, "Word 97-2003 document",
                        MediaFormatCategory::Document, true, false,
                        "built-in OLE2/CFB parser", "plain-text import only" });
        out.push_back({ "md", { "markdown" }, "Markdown text",
                        MediaFormatCategory::Document, true, true,
                        "built-in Markdown engine", "" });
        out.push_back({ "txt", {}, "Plain text",
                        MediaFormatCategory::Document, true, true,
                        "built-in", "" });
        out.push_back({ "html", { "htm", "xhtml" }, "HTML document",
                        MediaFormatCategory::Document, true, true,
                        "built-in HTMLReader / ToHTML serializer",
                        "reader covers the e-book/document subset of HTML" });

        // Make sure the built-in engines (EPUB/FB2/MOBI/TXT) are in the
        // registry — registration is idempotent — then ask the registry so
        // any additional engines the application registered are reported
        // too. All current engines are read-only.
        RegisterBuiltinEBookEngines();
        struct EBookName { const char* ext; const char* description; };
        static const std::vector<EBookName> ebookNames = {
            { "epub", "EPUB e-book" },
            { "fb2",  "FictionBook 2" },
            { "mobi", "Mobipocket e-book" },
            { "prc",  "Palm resource e-book" },
            { "azw",  "Kindle e-book" },
            { "azw3", "Kindle KF8 e-book" },
        };
        for (const std::string& registered : GetRegisteredEBookExtensions()) {
            const std::string ext = ToLowerNoDot(registered);
            if (ext.empty() || ListContains(out, ext)) continue;   // txt etc.
            // "fb2.zip" folds into the fb2 entry as an alias.
            if (ext == "fb2.zip") {
                for (auto& f : out) {
                    if (f.extension == "fb2") { f.aliases.push_back(ext); break; }
                }
                continue;
            }
            MediaFormatInfo f;
            f.extension   = ext;
            f.description = ext + " e-book";
            for (const auto& n : ebookNames) {
                if (ext == n.ext) { f.description = n.description; break; }
            }
            f.category    = MediaFormatCategory::Document;
            f.canLoad     = true;
            f.canSave     = false;
            f.provider    = "built-in eBook engine";
            out.push_back(std::move(f));
        }
    }

    // ---- Spreadsheets: always compiled into the core library ----
    void AddSpreadsheetFormats(std::vector<MediaFormatInfo>& out) {
        out.push_back({ "ods", {}, "OpenDocument Spreadsheet",
                        MediaFormatCategory::Spreadsheet, true, true,
                        "built-in (miniz + tinyxml2)", "" });
        out.push_back({ "xlsx", {}, "Excel workbook (OOXML)",
                        MediaFormatCategory::Spreadsheet, true, true,
                        "built-in (miniz + tinyxml2)", "" });
        out.push_back({ "csv", {}, "Comma-separated values",
                        MediaFormatCategory::Spreadsheet, true, true,
                        "built-in CSV engine",
                        "encoding, separator and decimal auto-detected" });
        out.push_back({ "tsv", {}, "Tab-separated values",
                        MediaFormatCategory::Spreadsheet, true, true,
                        "built-in CSV engine", "" });
    }

    // ---- Audio: the miniaudio backend's real decode/encode matrix ----
    void AddAudioFormats(std::vector<MediaFormatInfo>& out) {
#ifdef ULTRACANVAS_ENABLE_AUDIO
        out.push_back({ "wav", {}, "Waveform audio",
                        MediaFormatCategory::Audio, true, true,
                        "miniaudio (dr_wav)", "" });
        out.push_back({ "mp3", {}, "MPEG layer III audio",
                        MediaFormatCategory::Audio, true, false,
                        "miniaudio (dr_mp3)", "" });
        out.push_back({ "flac", {}, "Free Lossless Audio Codec",
                        MediaFormatCategory::Audio, true, false,
                        "miniaudio (dr_flac)", "" });
        // NOTE deliberately absent: ogg/oga (no Vorbis decoder is wired into
        // the miniaudio backend), aac, opus. Add entries here only after a
        // decoder actually lands in libspecific/Audio.
#else
        (void)out;
#endif
    }

    // ---- Video: per-platform backend demuxer/muxer matrix ----
    void AddVideoFormats(std::vector<MediaFormatInfo>& out) {
#ifdef ULTRACANVAS_ENABLE_VIDEO
        struct Candidate {
            const char* ext;
            std::vector<std::string> aliases;
            const char* description;
            bool load;
            bool save;
        };
#if defined(__linux__)
        const char* provider = "GStreamer";
        const char* notes = "codec availability depends on installed GStreamer plugins";
        static const std::vector<Candidate> candidates = {
            { "mp4",  { "m4v" }, "MPEG-4 container", true, true  },
            { "mov",  {},        "QuickTime movie",  true, true  },
            { "mkv",  {},        "Matroska video",   true, true  },
            { "webm", {},        "WebM video",       true, true  },
            { "avi",  {},        "AVI video",        true, true  },
        };
#elif defined(_WIN32)
        const char* provider = "Media Foundation";
        const char* notes = "codec availability depends on installed Media Foundation codecs";
        static const std::vector<Candidate> candidates = {
            { "mp4",  { "m4v" }, "MPEG-4 container", true, true  },
            { "mov",  {},        "QuickTime movie",  true, false },
            { "mkv",  {},        "Matroska video",   true, false },
            { "webm", {},        "WebM video",       true, false },
            { "avi",  {},        "AVI video",        true, false },
        };
#else   // macOS / AVFoundation
        const char* provider = "AVFoundation";
        const char* notes = "";
        static const std::vector<Candidate> candidates = {
            { "mp4",  { "m4v" }, "MPEG-4 container", true, true  },
            { "mov",  {},        "QuickTime movie",  true, true  },
        };
#endif
        for (const auto& c : candidates) {
            MediaFormatInfo f;
            f.extension   = c.ext;
            f.aliases     = c.aliases;
            f.description = c.description;
            f.category    = MediaFormatCategory::Video;
            f.canLoad     = c.load;
            f.canSave     = c.save;
            f.provider    = provider;
            f.notes       = notes;
            out.push_back(std::move(f));
        }
#else
        (void)out;
#endif
    }

    MediaFormatCategory CategoryFromGraphicsType(GraphicsFormatType type) {
        switch (type) {
            case GraphicsFormatType::Vector:    return MediaFormatCategory::Vector;
            case GraphicsFormatType::ThreeD:    return MediaFormatCategory::Model3D;
            case GraphicsFormatType::Video:     return MediaFormatCategory::Video;
            case GraphicsFormatType::Text:      return MediaFormatCategory::Document;
            case GraphicsFormatType::Data:      return MediaFormatCategory::Spreadsheet;
            case GraphicsFormatType::Bitmap:
            case GraphicsFormatType::Animation:
            default:                            return MediaFormatCategory::Bitmap;
        }
    }

    // ---- Runtime-registered graphics plugins (CDR, XAR, STL, ...) ----
    // Whatever the host application registered with
    // UltraCanvasGraphicsPluginRegistry is reported with the plugin's own
    // name as the provider. The IGraphicsPlugin interface is load-only; the
    // STL plugin is the one plugin known to also implement saving (via its
    // SaveModel API outside the interface).
    void AddRegisteredGraphicsPlugins(std::vector<MediaFormatInfo>& out) {
        for (const auto& plugin : UltraCanvasGraphicsPluginRegistry::GetAllPlugins()) {
            if (!plugin) continue;
            const std::string name = plugin->GetPluginName();
            const bool pluginSaves = (name.find("STL") != std::string::npos);

            // A plugin's extensions are one format family (e.g. XAR's
            // xar/web/wix), so classify the whole plugin by its first
            // extension the detector recognises rather than per extension —
            // alias extensions are usually unknown to the detector.
            GraphicsFormatType type = GraphicsFormatType::Unknown;
            for (const std::string& rawExt : plugin->GetSupportedExtensions()) {
                type = GraphicsFormatDetector::DetectFromExtension(ToLowerNoDot(rawExt));
                if (type != GraphicsFormatType::Unknown) break;
            }

            for (const std::string& rawExt : plugin->GetSupportedExtensions()) {
                const std::string ext = ToLowerNoDot(rawExt);
                if (ext.empty() || ListContains(out, ext)) continue;
                MediaFormatInfo f;
                f.extension   = ext;
                f.description = ext + " (" + name + ")";
                f.category    = CategoryFromGraphicsType(type);
                f.canLoad     = true;
                f.canSave     = pluginSaves;
                f.provider    = name;
                out.push_back(std::move(f));
            }
        }
    }

} // anonymous namespace

    bool MediaFormatInfo::MatchesExtension(const std::string& ext) const {
        const std::string wanted = ToLowerNoDot(ext);
        if (wanted.empty()) return false;
        if (wanted == extension) return true;
        return std::find(aliases.begin(), aliases.end(), wanted) != aliases.end();
    }

    std::vector<MediaFormatInfo> UltraCanvasSupportedFormats::GetAll() {
        std::vector<MediaFormatInfo> out;
        AddBitmapFormats(out);
        AddVectorFormats(out);
        AddDocumentFormats(out);
        AddSpreadsheetFormats(out);
        AddAudioFormats(out);
        AddVideoFormats(out);
        AddRegisteredGraphicsPlugins(out);
        return out;
    }

    std::vector<MediaFormatInfo> UltraCanvasSupportedFormats::GetByCategory(MediaFormatCategory category) {
        std::vector<MediaFormatInfo> out;
        for (auto& f : GetAll()) {
            if (f.category == category) out.push_back(std::move(f));
        }
        return out;
    }

    std::vector<std::string> UltraCanvasSupportedFormats::GetLoadExtensions(MediaFormatCategory category) {
        std::vector<std::string> out;
        for (const auto& f : GetByCategory(category)) {
            if (!f.canLoad) continue;
            out.push_back(f.extension);
            out.insert(out.end(), f.aliases.begin(), f.aliases.end());
        }
        return out;
    }

    std::vector<std::string> UltraCanvasSupportedFormats::GetSaveExtensions(MediaFormatCategory category) {
        std::vector<std::string> out;
        for (const auto& f : GetByCategory(category)) {
            if (!f.canSave) continue;
            out.push_back(f.extension);
            out.insert(out.end(), f.aliases.begin(), f.aliases.end());
        }
        return out;
    }

    std::optional<MediaFormatInfo> UltraCanvasSupportedFormats::FindByExtension(const std::string& extension) {
        for (auto& f : GetAll()) {
            if (f.MatchesExtension(extension)) return f;
        }
        return std::nullopt;
    }

    std::string UltraCanvasSupportedFormats::GetCategoryName(MediaFormatCategory category) {
        switch (category) {
            case MediaFormatCategory::Bitmap:      return "Bitmap graphics";
            case MediaFormatCategory::Vector:      return "Vector graphics";
            case MediaFormatCategory::Model3D:     return "3D models";
            case MediaFormatCategory::Document:    return "Documents";
            case MediaFormatCategory::Spreadsheet: return "Spreadsheets";
            case MediaFormatCategory::Audio:       return "Audio";
            case MediaFormatCategory::Video:       return "Video";
        }
        return "Unknown";
    }

} // namespace UltraCanvas
