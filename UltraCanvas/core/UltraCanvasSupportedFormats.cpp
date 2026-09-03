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
//   Audio        — the miniaudio backend (WAV/MP3/FLAC decode, WAV encode)
//                  plus the optional system codec libraries: libFLAC (FLAC
//                  encode), libvorbis (OGG encode+decode), libopusenc/opusfile
//                  (Opus encode/decode), LAME (MP3 encode). AAC stays absent.
//   Video        — the platform backend's demuxer/muxer matrix (GStreamer /
//                  Media Foundation / AVFoundation).
//   Font         — FreeType, a hard dependency, so the list is fixed. "Load"
//                  means UltraCanvasFontFile can read the name records and
//                  rasterize a specimen, not that the image pipeline decodes
//                  it (CanImagePipelineLoad stays false for every font).
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
#include <mutex>
#include <set>

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

    struct BitmapCandidate {
        const char* ext;
        std::vector<std::string> aliases;
        const char* description;
    };
    // Everything the UCImage load/save path implements; whether a
    // particular loader/saver is really present depends on how libvips
    // was built (e.g. .heic needs libheif with an HEVC codec). Shared by
    // AddBitmapFormats and CanImagePipelineLoad: the magick fallback only
    // applies to these known-raster extensions.
    const std::vector<BitmapCandidate>& BitmapCandidates() {
        static const std::vector<BitmapCandidate> candidates = {
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
        return candidates;
    }

    // True for an extension (or alias) of the table above - "the UCImage load
    // path implements this format", independently of any runtime probe.
    bool IsKnownRasterCandidate(const std::string& ext) {
        for (const BitmapCandidate& c : BitmapCandidates()) {
            if (ext == c.ext) return true;
            for (const std::string& alias : c.aliases)
                if (ext == alias) return true;
        }
        return false;
    }

    // ---- Bitmap: probe the candidate formats against the installed libvips ----
    void AddBitmapFormats(std::vector<MediaFormatInfo>& out) {
        const std::vector<BitmapCandidate>& candidates = BitmapCandidates();
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

    // ---- Audio: miniaudio's decode/encode matrix plus the optional system
    // codec libraries wired through AudioCodecsExtra (compile-gated on the
    // ULTRACANVAS_HAS_* defines their CMake detection sets) ----
    void AddAudioFormats(std::vector<MediaFormatInfo>& out) {
#ifdef ULTRACANVAS_ENABLE_AUDIO
        out.push_back({ "wav", {}, "Waveform audio",
                        MediaFormatCategory::Audio, true, true,
                        "miniaudio (dr_wav)", "" });
#ifdef ULTRACANVAS_HAS_LAME
        out.push_back({ "mp3", {}, "MPEG layer III audio",
                        MediaFormatCategory::Audio, true, true,
                        "miniaudio (dr_mp3) + LAME", "" });
#else
        out.push_back({ "mp3", {}, "MPEG layer III audio",
                        MediaFormatCategory::Audio, true, false,
                        "miniaudio (dr_mp3)",
                        "saving requires LAME (libmp3lame)" });
#endif
#ifdef ULTRACANVAS_HAS_LIBFLAC
        out.push_back({ "flac", {}, "Free Lossless Audio Codec",
                        MediaFormatCategory::Audio, true, true,
                        "miniaudio (dr_flac) + libFLAC", "" });
#else
        out.push_back({ "flac", {}, "Free Lossless Audio Codec",
                        MediaFormatCategory::Audio, true, false,
                        "miniaudio (dr_flac)",
                        "saving requires libFLAC" });
#endif
#ifdef ULTRACANVAS_HAS_VORBIS
        out.push_back({ "ogg", { "oga" }, "Ogg Vorbis audio",
                        MediaFormatCategory::Audio, true, true,
                        "libvorbis (vorbisfile + vorbisenc)", "" });
#endif
#if defined(ULTRACANVAS_HAS_OPUSFILE) || defined(ULTRACANVAS_HAS_OPUSENC)
        {
            bool opusLoad = false, opusSave = false;
#ifdef ULTRACANVAS_HAS_OPUSFILE
            opusLoad = true;
#endif
#ifdef ULTRACANVAS_HAS_OPUSENC
            opusSave = true;
#endif
            out.push_back({ "opus", {}, "Opus audio",
                            MediaFormatCategory::Audio, opusLoad, opusSave,
                            "opusfile + libopusenc", "" });
        }
#endif
        // NOTE deliberately absent: aac/m4a (no codec is wired). Add an entry
        // here only after a codec actually lands in libspecific/Audio.
#else
        (void)out;
#endif
    }

    // ---- Fonts: FreeType is a hard dependency of the framework, so every
    // build reads these. "Load" here means what UltraCanvasFontFile does -
    // read the name records and rasterize a specimen - not that the format
    // goes through the image pipeline; CanImagePipelineLoad still says no
    // for all of them. WOFF and WOFF2 depend on the zlib/Brotli support the
    // installed FreeType was built with, which is why they carry a note. ----
    void AddFontFormats(std::vector<MediaFormatInfo>& out) {
        out.push_back({ "ttf", { "ttc" }, "TrueType font",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "" });
        out.push_back({ "otf", { "otc" }, "OpenType font",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "" });
        out.push_back({ "pfb", { "pfa" }, "PostScript Type 1 font",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "" });
        out.push_back({ "woff", {}, "Web Open Font Format",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "needs a FreeType built with zlib" });
        out.push_back({ "woff2", {}, "Web Open Font Format 2",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "needs a FreeType built with Brotli" });
        out.push_back({ "bdf", { "pcf", "fnt", "fon" }, "Bitmap font",
                        MediaFormatCategory::Font, true, false,
                        "FreeType", "fixed strikes only - no outlines" });
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
    // name as the provider. Loading comes from GetSupportedExtensions,
    // saving from GetSaveExtensions — the two lists are independent, so a
    // save-only extension (the vector formats plugin writes EPS/CDR/PDF/...
    // it cannot read) appears with canLoad=false, and a plugin that saves a
    // format an earlier provider already listed upgrades that entry's
    // canSave instead of duplicating it.
    void AddRegisteredGraphicsPlugins(std::vector<MediaFormatInfo>& out) {
        auto findEntry = [&out](const std::string& ext) -> MediaFormatInfo* {
            for (auto& f : out) {
                if (f.MatchesExtension(ext)) return &f;
            }
            return nullptr;
        };

        for (const auto& plugin : UltraCanvasGraphicsPluginRegistry::GetAllPlugins()) {
            if (!plugin) continue;
            const std::string name = plugin->GetPluginName();

            std::set<std::string> saveExts;
            for (const std::string& rawExt : plugin->GetSaveExtensions()) {
                const std::string ext = ToLowerNoDot(rawExt);
                if (!ext.empty()) saveExts.insert(ext);
            }

            // A plugin's extensions are one format family (e.g. XAR's
            // xar/web/wix), so classify the whole plugin by its first
            // extension the detector recognises rather than per extension —
            // alias extensions are usually unknown to the detector.
            GraphicsFormatType type = GraphicsFormatType::Unknown;
            for (const std::string& rawExt : plugin->GetSupportedExtensions()) {
                type = GraphicsFormatDetector::DetectFromExtension(ToLowerNoDot(rawExt));
                if (type != GraphicsFormatType::Unknown) break;
            }

            auto addOrUpgrade = [&](const std::string& ext, bool canLoad) {
                if (MediaFormatInfo* existing = findEntry(ext)) {
                    if (canLoad) existing->canLoad = true;
                    if (saveExts.count(ext)) existing->canSave = true;
                    return;
                }
                MediaFormatInfo f;
                f.extension   = ext;
                f.description = ext + " (" + name + ")";
                f.category    = CategoryFromGraphicsType(
                        canLoad ? type
                                : GraphicsFormatDetector::DetectFromExtension(ext));
                f.canLoad     = canLoad;
                f.canSave     = saveExts.count(ext) > 0;
                f.provider    = name;
                out.push_back(std::move(f));
            };

            for (const std::string& rawExt : plugin->GetSupportedExtensions()) {
                const std::string ext = ToLowerNoDot(rawExt);
                if (!ext.empty()) addOrUpgrade(ext, true);
            }
            for (const std::string& ext : saveExts) {
                addOrUpgrade(ext, false);
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
        // Assembling the inventory probes libvips, the media backends and the
        // engine registries. Callers are no longer all on the UI thread - the
        // filer classifies the entries of a folder it scans in the background
        // from here - so the assembly is serialized: everything it reads is
        // populated at start-up, but two threads walking those registries at
        // once is not something they are built for.
        static std::mutex inventoryMutex;
        std::lock_guard<std::mutex> lk(inventoryMutex);
        std::vector<MediaFormatInfo> out;
        AddBitmapFormats(out);
        AddVectorFormats(out);
        AddDocumentFormats(out);
        AddSpreadsheetFormats(out);
        AddAudioFormats(out);
        AddVideoFormats(out);
        AddFontFormats(out);
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

    bool UltraCanvasSupportedFormats::CanImagePipelineLoad(const std::string& extension) {
        const std::string ext = ToLowerNoDot(extension);
        if (ext.empty()) return false;
        // SVG bypasses libvips: UCImage routes it to the built-in SVG renderer.
        if (ext == "svg" || ext == "svgz") return true;
#ifdef HAS_LIBVIPS
        // The candidate table is what the UCImage load path implements, and it
        // is the answer whenever the libvips probes cannot contribute one.
        // This must never collapse to "nothing loads": callers use it to decide
        // whether to attempt a decode at all, so a probe that comes back empty
        // would silently strip every image preview in the application while
        // UCImage::Get goes on decoding the very same files. Two ways that
        // happened: VIPS_INIT reports non-zero on an ABI mismatch between the
        // headers and the installed libvips even though the library works, and
        // the loader suffix list reads empty when the loader classes are not
        // registered yet. Attempting a decode that then fails costs one read;
        // not attempting it loses the picture with nothing to show why.
        if (IsKnownRasterCandidate(ext)) return true;
        if (!EnsureImageSubsystem()) return false;
        // Past the table, only a loader vips actually advertises counts:
        // handing it an arbitrary extension is exactly the mis-dispatch this
        // API guards against. (magickload advertises no suffixes at all - it
        // content-sniffs - so it could only ever confirm the table above.)
        return VipsCanLoad("." + ext);
#else
        return ext == "png";   // cairo's native PNG reader
#endif
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
            case MediaFormatCategory::Font:        return "Fonts";
        }
        return "Unknown";
    }

} // namespace UltraCanvas
