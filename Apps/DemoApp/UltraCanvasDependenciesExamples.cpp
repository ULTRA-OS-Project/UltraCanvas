// Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
// Dependencies & Third-Party libraries table for the UltraCanvas core and the
// additional ULTRA OS modules listed in this demo app. Rendered entirely with
// UltraCanvas GUI elements (UltraCanvasContainer / UltraCanvasLabel) and the
// UltraCanvasListView multi-column element.
//
// The list is grouped PER MODULE: a section-header row names each module
// (UltraCanvas core, the optional plugins, then FileLoader / UltraNet /
// SmartHome / UltraAI / AudioFX / PixelFX / VideoFX / VirtualFS /
// IODeviceManager). Under each header the rows give the purpose and the
// third-party libraries used on Linux, macOS and Windows, one library per line.
//
// Library names are rendered as links: hovering shows a tooltip with the
// library's website / source-repository / license, clicking opens a small
// popup with "Website" and "Source code" entries (or opens the website
// directly when the project has no separate repository, e.g. OS frameworks).
// The license tag after a name — "(MIT)", "(LGPL 2.1)", … — is its own link
// and opens the license description page on spdx.org.
//
// QOI and XAR are intentionally NOT listed: those formats are implemented by
// UltraCanvas itself, not pulled in as third-party libraries.
//
// "(bundled)" = vendored in-tree, "(optional)" = built only when present.
// Version: 5.3.0
// Last Modified: 2026-07-17
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasUtils.h"   // OpenURL
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

    // ===== LICENSE / LIBRARY LINK DATABASE =====
    // The license label is the short tag shown in the table after the library
    // name; it resolves to the full license name and its spdx.org info page.
    struct DepLicense {
        const char* label;      // as written in the table, e.g. "LGPL 2.1"
        const char* fullName;
        const char* url;
    };

    static const DepLicense kDepLicenses[] = {
        {"MIT",         "MIT License",                                "https://spdx.org/licenses/MIT.html"},
        {"MIT-0",       "MIT No Attribution License",                 "https://spdx.org/licenses/MIT-0.html"},
        {"BSD 2",       "BSD 2-Clause License",                       "https://spdx.org/licenses/BSD-2-Clause.html"},
        {"BSD 3",       "BSD 3-Clause License",                       "https://spdx.org/licenses/BSD-3-Clause.html"},
        {"ISC",         "ISC License",                                "https://spdx.org/licenses/ISC.html"},
        {"zlib",        "zlib License",                               "https://spdx.org/licenses/Zlib.html"},
        {"Apache 2",    "Apache License 2.0",                         "https://spdx.org/licenses/Apache-2.0.html"},
        {"LGPL 2.1",    "GNU Lesser General Public License v2.1",    "https://spdx.org/licenses/LGPL-2.1-or-later.html"},
        {"LGPL 3",      "GNU Lesser General Public License v3.0",    "https://spdx.org/licenses/LGPL-3.0-or-later.html"},
        {"GPL 2",       "GNU General Public License v2.0",           "https://spdx.org/licenses/GPL-2.0-or-later.html"},
        {"AGPL 3",      "GNU Affero General Public License v3.0",    "https://spdx.org/licenses/AGPL-3.0-or-later.html"},
        {"MPL 2.0",     "Mozilla Public License 2.0",                 "https://spdx.org/licenses/MPL-2.0.html"},
        {"FTL",         "FreeType Project License",                   "https://spdx.org/licenses/FTL.html"},
        {"libpng 2",    "PNG Reference Library License v2",           "https://spdx.org/licenses/libpng-2.0.html"},
        {"libtiff",     "libtiff License",                            "https://spdx.org/licenses/libtiff.html"},
        {"ImageMagick", "ImageMagick License",                        "https://spdx.org/licenses/ImageMagick.html"},
        {"Unicode",     "Unicode License v3",                         "https://spdx.org/licenses/Unicode-3.0.html"},
        {"curl",        "curl License",                               "https://spdx.org/licenses/curl.html"},
        {"LPPL",        "LaTeX Project Public License 1.3c (GUST Font License)", "https://spdx.org/licenses/LPPL-1.3c.html"},
    };

    static const DepLicense* FindDepLicense(const std::string& label) {
        for (const auto& lic : kDepLicenses) {
            if (label == lic.label) return &lic;
        }
        return nullptr;
    }

    // Library entry: `name` exactly as it appears in the table cells. An empty
    // repo marks OS frameworks / spec registries without a public repository;
    // an empty license omits the license tag (OS components, build toolchains).
    struct DepLibrary {
        const char* name;
        const char* website;
        const char* repo;
        const char* license;    // label into kDepLicenses ("" = none)
    };

    static const DepLibrary kDepLibraries[] = {
        // --- UltraCanvas core ---
        {"Cairo",             "https://www.cairographics.org/",                                    "https://gitlab.freedesktop.org/cairo/cairo",                    "LGPL 2.1"},
        {"Pango",             "https://pango.gnome.org/",                                          "https://gitlab.gnome.org/GNOME/pango",                          "LGPL 2.1"},
        {"HarfBuzz",          "https://harfbuzz.github.io/",                                       "https://github.com/harfbuzz/harfbuzz",                          "MIT"},
        {"FreeType",          "https://freetype.org/",                                             "https://gitlab.freedesktop.org/freetype/freetype",              "FTL"},
        {"FontConfig",        "https://www.freedesktop.org/wiki/Software/fontconfig/",             "https://gitlab.freedesktop.org/fontconfig/fontconfig",          "MIT"},
        {"CoreText",          "https://developer.apple.com/documentation/coretext/",               "",                                                              ""},
        {"X11",               "https://www.x.org/",                                                "https://gitlab.freedesktop.org/xorg/lib/libX11",                "MIT"},
        {"Xcursor",           "https://www.x.org/",                                                "https://gitlab.freedesktop.org/xorg/lib/libXcursor",            "MIT"},
        {"Cocoa",             "https://developer.apple.com/documentation/appkit",                  "",                                                              ""},
        {"QuartzCore",        "https://developer.apple.com/documentation/quartzcore",              "",                                                              ""},
        {"Win32",             "https://learn.microsoft.com/en-us/windows/win32/api/",              "",                                                              ""},
        {"GTK 3",             "https://www.gtk.org/",                                              "https://gitlab.gnome.org/GNOME/gtk",                            "LGPL 2.1"},
        {"GLib",              "https://docs.gtk.org/glib/",                                        "https://gitlab.gnome.org/GNOME/glib",                           "LGPL 2.1"},
        {"TinyXML2",          "https://leethomason.github.io/tinyxml2/",                           "https://github.com/leethomason/tinyxml2",                       "zlib"},
        {"fmt",               "https://fmt.dev/",                                                  "https://github.com/fmtlib/fmt",                                 "MIT"},
        {"libvips",           "https://www.libvips.org/",                                          "https://github.com/libvips/libvips",                            "LGPL 2.1"},
        {"libpng",            "http://www.libpng.org/pub/png/libpng.html",                         "https://github.com/pnggroup/libpng",                            "libpng 2"},
        {"libjpeg-turbo",     "https://libjpeg-turbo.org/",                                        "https://github.com/libjpeg-turbo/libjpeg-turbo",                "BSD 3"},
        {"libtiff",           "http://www.simplesystems.org/libtiff/",                             "https://gitlab.com/libtiff/libtiff",                            "libtiff"},
        {"libwebp",           "https://developers.google.com/speed/webp",                          "https://chromium.googlesource.com/webm/libwebp/",               "BSD 3"},
        {"giflib",            "https://giflib.sourceforge.net/",                                   "https://sourceforge.net/projects/giflib/",                      "MIT"},
        {"libheif",           "https://github.com/strukturag/libheif",                             "https://github.com/strukturag/libheif",                         "LGPL 3"},
        {"ImageMagick",       "https://imagemagick.org/",                                          "https://github.com/ImageMagick/ImageMagick",                    "ImageMagick"},
        {"miniaudio",         "https://miniaud.io/",                                               "https://github.com/mackron/miniaudio",                          "MIT-0"},
        {"ALSA",              "https://www.alsa-project.org/",                                     "https://github.com/alsa-project/alsa-lib",                      "LGPL 2.1"},
        {"PulseAudio",        "https://www.freedesktop.org/wiki/Software/PulseAudio/",             "https://gitlab.freedesktop.org/pulseaudio/pulseaudio",          "LGPL 2.1"},
        {"CoreAudio",         "https://developer.apple.com/documentation/coreaudio",               "",                                                              ""},
        {"WASAPI",            "https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi",  "",                                                              ""},
        {"GStreamer",         "https://gstreamer.freedesktop.org/",                                "https://gitlab.freedesktop.org/gstreamer/gstreamer",            "LGPL 2.1"},
        {"AVFoundation",      "https://developer.apple.com/documentation/avfoundation",            "",                                                              ""},
        {"Media Foundation",  "https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk", "",                                       ""},
        {"OpenGL",            "https://www.opengl.org/",                                           "https://github.com/KhronosGroup/OpenGL-Registry",               ""},
        {"EGL",               "https://www.khronos.org/egl/",                                      "https://github.com/KhronosGroup/EGL-Registry",                  ""},
        {"GLX",               "https://registry.khronos.org/OpenGL/",                              "https://github.com/KhronosGroup/OpenGL-Registry",               ""},
        {"CGL",               "https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_intro/opengl_intro.html", "", ""},
        {"GLEW",              "https://glew.sourceforge.net/",                                     "https://github.com/nigels-com/glew",                            "BSD 3"},
        {"WGL",               "https://learn.microsoft.com/en-us/windows/win32/opengl/wgl-and-windows-reference", "",                                              ""},
        {"zbar",              "https://zbar.sourceforge.net/",                                     "https://github.com/mchehab/zbar",                               "LGPL 2.1"},
        {"qrcodegen",         "https://www.nayuki.io/page/qr-code-generator-library",              "https://github.com/nayuki/QR-Code-generator",                   "MIT"},
        {"miniz",             "https://github.com/richgel999/miniz",                               "https://github.com/richgel999/miniz",                           "MIT"},
        {"KissFFT",           "https://github.com/mborgerding/kissfft",                            "https://github.com/mborgerding/kissfft",                        "BSD 3"},
        // --- PDF plugin ---
        {"MuPDF",             "https://mupdf.com/",                                                "https://github.com/ArtifexSoftware/mupdf",                      "AGPL 3"},
        {"mujs",              "https://mujs.com/",                                                 "https://codeberg.org/ccxvii/mujs",                              "ISC"},
        {"gumbo",             "https://github.com/google/gumbo-parser",                            "https://github.com/google/gumbo-parser",                        "Apache 2"},
        {"jbig2dec",          "https://jbig2dec.com/",                                             "https://github.com/ArtifexSoftware/jbig2dec",                   "AGPL 3"},
        {"openjp2",           "https://www.openjpeg.org/",                                         "https://github.com/uclouvain/openjpeg",                         "BSD 2"},
        // --- CDR plugin ---
        {"libcdr",            "https://wiki.documentfoundation.org/DLP/Libraries/libcdr",          "https://github.com/LibreOffice/libcdr",                         "MPL 2.0"},
        {"librevenge-stream", "https://sourceforge.net/p/libwpd/wiki/librevenge/",                 "https://sourceforge.net/p/libwpd/librevenge/",                  "MPL 2.0"},
        {"librevenge",        "https://sourceforge.net/p/libwpd/wiki/librevenge/",                 "https://sourceforge.net/p/libwpd/librevenge/",                  "MPL 2.0"},
        {"LCMS2",             "https://www.littlecms.com/",                                        "https://github.com/mm2/Little-CMS",                             "MIT"},
        {"ICU",               "https://icu.unicode.org/",                                          "https://github.com/unicode-org/icu",                            "Unicode"},
        // --- OCR plugin ---
        {"Tesseract",         "https://tesseract-ocr.github.io/",                                  "https://github.com/tesseract-ocr/tesseract",                    "Apache 2"},
        {"Leptonica",         "http://www.leptonica.org/",                                         "https://github.com/DanBloomberg/leptonica",                     "BSD 2"},
        // --- Vectorizer plugin ---
        {"VTracer",           "https://www.visioncortex.org/vtracer-docs",                         "https://github.com/visioncortex/vtracer",                       "MIT"},
        {"visioncortex",      "https://www.visioncortex.org/",                                     "https://github.com/visioncortex/visioncortex",                  "MIT"},
        {"cargo",             "https://doc.rust-lang.org/cargo/",                                  "https://github.com/rust-lang/cargo",                            ""},
        {"rustc",             "https://www.rust-lang.org/",                                        "https://github.com/rust-lang/rust",                             ""},
        {"corrosion-rs",      "https://corrosion-rs.github.io/corrosion/",                         "https://github.com/corrosion-rs/corrosion",                     "MIT"},
        {"corrosion",         "https://corrosion-rs.github.io/corrosion/",                         "https://github.com/corrosion-rs/corrosion",                     ""},
        // --- LaTeX plugin ---
        {"MicroTeX",          "https://github.com/NanoMichael/MicroTeX",                           "https://github.com/NanoMichael/MicroTeX",                       "MIT"},
        {"Latin Modern Math", "https://www.gust.org.pl/projects/e-foundry/lm-math",                "",                                                              "LPPL"},
        // --- DataFormats (UltraCanvasJSON, vendored engine) ---
        {"yyjson",            "https://ibireme.github.io/yyjson/doc/doxygen/html/",                "https://github.com/ibireme/yyjson",                             "MIT"},
        // --- AudioFX / VideoFX ---
        {"FFmpeg",            "https://ffmpeg.org/",                                               "https://git.ffmpeg.org/ffmpeg.git",                             "LGPL 2.1"},
        {"libsndfile",        "https://libsndfile.github.io/libsndfile/",                          "https://github.com/libsndfile/libsndfile",                      "LGPL 2.1"},
        {"FFTW3",             "https://www.fftw.org/",                                             "https://github.com/FFTW/fftw3",                                 "GPL 2"},
        // --- IODeviceManager ---
        {"SANE",              "http://www.sane-project.org/",                                      "https://gitlab.com/sane-project/backends",                      "GPL 2"},
        {"V4L2",              "https://docs.kernel.org/userspace-api/media/v4l/v4l2.html",         "https://git.linuxtv.org/v4l-utils.git",                         ""},
        {"CUPS",              "https://openprinting.github.io/cups/",                              "https://github.com/OpenPrinting/cups",                          "Apache 2"},
        {"ICA",               "https://developer.apple.com/documentation/imagecapturecore",        "",                                                              ""},
        {"WIA",               "https://learn.microsoft.com/en-us/windows/win32/wia/-wia-startpage","",                                                              ""},
        {"TWAIN",             "https://www.twain.org/",                                            "https://github.com/twain",                                      ""},
        // --- UltraNet ---
        {"libcurl",           "https://curl.se/libcurl/",                                          "https://github.com/curl/curl",                                  "curl"},
        {"OpenSSL",           "https://www.openssl.org/",                                          "https://github.com/openssl/openssl",                            "Apache 2"},
        {"nlohmann/json",     "https://json.nlohmann.me/",                                         "https://github.com/nlohmann/json",                              "MIT"},
        {"Winsock2",          "https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2", "",                                          ""},
        {"Network.framework", "https://developer.apple.com/documentation/network",                 "",                                                              ""},
        // --- VirtualFS ---
        {"libarchive",        "https://www.libarchive.org/",                                       "https://github.com/libarchive/libarchive",                      "BSD 2"},
        {"zlib",              "https://zlib.net/",                                                 "https://github.com/madler/zlib",                                "zlib"},
        {"libzstd",           "https://facebook.github.io/zstd/",                                  "https://github.com/facebook/zstd",                              "BSD 3"},
        {"liblz4",            "https://lz4.org/",                                                  "https://github.com/lz4/lz4",                                    "BSD 2"},
        {"libmspack",         "https://www.cabextract.org.uk/libmspack/",                          "https://github.com/kyz/libmspack",                              "LGPL 2.1"},
        {"wimlib",            "https://wimlib.net/",                                               "https://wimlib.net/git/wimlib",                                 "LGPL 3"},
        {"libbrotli",         "https://www.brotli.org/",                                           "https://github.com/google/brotli",                              "MIT"},
    };

    // Library names sorted longest-first so "librevenge-stream" wins over
    // "librevenge" and "corrosion-rs" over "corrosion" during line parsing.
    static const std::vector<const DepLibrary*>& DepLibrariesByLength() {
        static std::vector<const DepLibrary*> sorted = [] {
            std::vector<const DepLibrary*> v;
            for (const auto& lib : kDepLibraries) v.push_back(&lib);
            std::sort(v.begin(), v.end(), [](const DepLibrary* a, const DepLibrary* b) {
                return std::strlen(a->name) > std::strlen(b->name);
            });
            return v;
        }();
        return sorted;
    }

    // ===== CELL LINE PARSING =====
    // A cell line like "GLEW (BSD 3) / WGL (optional)" is split into segments:
    //   name "GLEW" • license "(BSD 3)" • plain " / " • name "WGL" • plain " (optional)"
    // A "(...)" group directly after a known name becomes a license segment only
    // when it matches that library's license label from the database.
    struct DepCellSegment {
        enum Kind { Plain, Name, License };
        Kind kind = Plain;
        std::string text;
        const DepLibrary* lib = nullptr;     // Name and License segments
        const DepLicense* license = nullptr; // License segments
        Rect2Di rect;                        // cell-local, filled during render
    };

    static bool IsWordChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0;
    }

    static std::vector<DepCellSegment> ParseDependencyLine(const std::string& line) {
        struct NameMatch { size_t pos, len; const DepLibrary* lib; };
        std::vector<NameMatch> matches;

        auto overlaps = [&matches](size_t pos, size_t len) {
            for (const auto& m : matches) {
                if (pos < m.pos + m.len && m.pos < pos + len) return true;
            }
            return false;
        };

        for (const DepLibrary* lib : DepLibrariesByLength()) {
            size_t nameLen = std::strlen(lib->name);
            size_t from = 0;
            while (true) {
                size_t pos = line.find(lib->name, from);
                if (pos == std::string::npos) break;
                from = pos + 1;
                bool leftOk = (pos == 0) || !IsWordChar(line[pos - 1]);
                bool rightOk = (pos + nameLen >= line.size()) || !IsWordChar(line[pos + nameLen]);
                if (leftOk && rightOk && !overlaps(pos, nameLen)) {
                    matches.push_back({pos, nameLen, lib});
                }
            }
        }
        std::sort(matches.begin(), matches.end(),
                  [](const NameMatch& a, const NameMatch& b) { return a.pos < b.pos; });

        std::vector<DepCellSegment> segments;
        auto addPlain = [&segments](const std::string& text) {
            if (text.empty()) return;
            DepCellSegment seg;
            seg.kind = DepCellSegment::Plain;
            seg.text = text;
            segments.push_back(seg);
        };

        size_t cursor = 0;
        for (const auto& m : matches) {
            // A consumed license tag may contain a library name itself, e.g.
            // "zlib (zlib)" — drop matches that fall inside consumed text.
            if (m.pos < cursor) continue;
            addPlain(line.substr(cursor, m.pos - cursor));

            DepCellSegment name;
            name.kind = DepCellSegment::Name;
            name.text = line.substr(m.pos, m.len);
            name.lib = m.lib;
            segments.push_back(name);
            cursor = m.pos + m.len;

            // "(license)" group directly after the name
            if (m.lib->license && *m.lib->license) {
                std::string tag = std::string(" (") + m.lib->license + ")";
                if (line.compare(cursor, tag.size(), tag) == 0) {
                    addPlain(" ");
                    DepCellSegment lic;
                    lic.kind = DepCellSegment::License;
                    lic.text = std::string("(") + m.lib->license + ")";
                    lic.lib = m.lib;
                    lic.license = FindDepLicense(m.lib->license);
                    segments.push_back(lic);
                    cursor += tag.size();
                }
            }
        }
        addPlain(line.substr(cursor));
        return segments;
    }

    // ===== MULTI-LINE / GROUPED CELL DELEGATE =====
    // Two row kinds:
    //  * Module section header  - all OS columns empty: drawn as a coloured band
    //                             with the module name in the first column.
    //  * Dependency row         - each newline-separated library on its own line;
    //                             library names and license tags are links.
    class DependencyCellDelegate : public IItemDelegate {
    public:
        DependencyCellDelegate(float font, int lineH, int pad)
            : fontSize(font), lineHeight(lineH), padding(pad) {}

        void RenderItem(IRenderContext* ctx, const IListModel* model,
                        int row, int column,
                        const ListItemStyleOption& option) override {
            if (!ctx || !model) return;

            // A row is a module section header when its OS columns are all empty.
            bool isHeader = true;
            for (int c = 1; c < option.columnCount; ++c) {
                if (!GetStringValue(model->GetData(ListIndex{row, c}, ListDataRole::DisplayRole)).empty()) {
                    isHeader = false;
                    break;
                }
            }

            int textX = option.columnX + padding;
            int availW = option.columnWidth - padding * 2;

            if (isHeader) {
                // Coloured band across every cell of the row.
                ctx->SetFillPaint(headerBackground);
                ctx->FillRectangle(Rect2Dd(static_cast<float>(option.columnX),
                                           static_cast<float>(option.rect.y),
                                           static_cast<float>(option.columnWidth),
                                           static_cast<float>(option.rect.height)));
                if (column == 0 && availW > 0) {
                    std::string name = GetStringValue(model->GetData(ListIndex{row, 0}, ListDataRole::DisplayRole));
                    ctx->SetFontSize(fontSize + 1.0f);
                    ctx->SetTextWrap(TextWrap::WrapNone);
                    ctx->SetTextAlignment(TextAlignment::Left);
                    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
                    ctx->SetTextPaint(headerTextColor);
                    ctx->DrawTextInRect(name, Rect2Dd(static_cast<float>(textX),
                                                      static_cast<float>(option.rect.y),
                                                      static_cast<float>(availW),
                                                      static_cast<float>(option.rect.height)));
                }
                return;
            }

            std::string text = GetStringValue(model->GetData(ListIndex{row, column}, ListDataRole::DisplayRole));
            if (text.empty() || availW <= 0) return;

            ctx->SetFontSize(fontSize);
            ctx->SetTextWrap(TextWrap::WrapNone);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);

            if (column == 0) {
                // Purpose column: plain multi-line text, no links.
                ctx->SetTextAlignment(option.columnAlignment);
                ctx->SetTextPaint(option.isSelected ? selectedTextColor : textColor);

                std::vector<std::string> lines = SplitLines(text);
                int blockH = static_cast<int>(lines.size()) * lineHeight;
                int lineY = option.rect.y + (option.rect.height - blockH) / 2;
                if (lineY < option.rect.y) lineY = option.rect.y;
                for (const auto& line : lines) {
                    if (!line.empty()) {
                        ctx->DrawTextInRect(line, Rect2Dd(static_cast<float>(textX),
                                                          static_cast<float>(lineY),
                                                          static_cast<float>(availW),
                                                          static_cast<float>(lineHeight)));
                    }
                    lineY += lineHeight;
                }
                return;
            }

            // OS columns: segment-based rendering with clickable links.
            const CellCache& cache = GetCellCache(ctx, row, column, text, option.rect.height);
            ctx->SetTextAlignment(TextAlignment::Left);

            for (size_t i = 0; i < cache.segments.size(); ++i) {
                const DepCellSegment& seg = cache.segments[i];
                Color color = textColor;
                if (seg.kind == DepCellSegment::Name) color = linkColor;
                else if (seg.kind == DepCellSegment::License) color = licenseColor;

                Rect2Di r(option.columnX + seg.rect.x, option.rect.y + seg.rect.y,
                          seg.rect.width, seg.rect.height);
                ctx->SetTextPaint(color);
                ctx->DrawTextInRect(seg.text, Rect2Dd(static_cast<float>(r.x),
                                                      static_cast<float>(r.y),
                                                      static_cast<float>(r.width + 2),
                                                      static_cast<float>(r.height)));

                if (seg.kind != DepCellSegment::Plain &&
                    row == hoverRow && column == hoverColumn &&
                    static_cast<int>(i) == hoverSegment) {
                    ctx->SetStrokePaint(color);
                    ctx->SetStrokeWidth(1.0);
                    ctx->DrawLine(Point2Di(r.x, r.y + r.height - 2),
                                  Point2Di(r.x + r.width, r.y + r.height - 2));
                }
            }
        }

        int GetRowHeight(const IListModel* /*model*/, int /*row*/) const override {
            return rowHeight;
        }

        void SetRowHeight(int h) { rowHeight = h; }

        // Link segment under a cell-local position; index (for hover identity)
        // is returned through segmentIndex. Only usable after first render.
        const DepCellSegment* HitTestLink(int row, int column, const Point2Di& posInCell,
                                          int* segmentIndex = nullptr) const {
            auto it = cellCache.find(CellKey(row, column));
            if (it == cellCache.end()) return nullptr;
            for (size_t i = 0; i < it->second.segments.size(); ++i) {
                const DepCellSegment& seg = it->second.segments[i];
                if (seg.kind == DepCellSegment::Plain) continue;
                if (seg.rect.Contains(posInCell)) {
                    if (segmentIndex) *segmentIndex = static_cast<int>(i);
                    return &seg;
                }
            }
            return nullptr;
        }

        // Returns true when the hovered link changed (caller should redraw).
        bool SetHoveredLink(int row, int column, int segmentIndex) {
            if (row == hoverRow && column == hoverColumn && segmentIndex == hoverSegment) {
                return false;
            }
            hoverRow = row;
            hoverColumn = column;
            hoverSegment = segmentIndex;
            return true;
        }

    private:
        struct CellCache {
            std::vector<DepCellSegment> segments;  // rects are cell-local
        };

        static long long CellKey(int row, int column) {
            return (static_cast<long long>(row) << 8) | column;
        }

        static std::vector<std::string> SplitLines(const std::string& text) {
            std::vector<std::string> lines;
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                lines.push_back(nl == std::string::npos ? text.substr(start)
                                                        : text.substr(start, nl - start));
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            return lines;
        }

        // Cell content is static, so segments + measured rects are built once
        // per cell on first render. Rects are cell-local and thus scroll-proof.
        const CellCache& GetCellCache(IRenderContext* ctx, int row, int column,
                                      const std::string& text, int cellHeight) {
            long long key = CellKey(row, column);
            auto it = cellCache.find(key);
            if (it != cellCache.end()) return it->second;

            CellCache cache;
            std::vector<std::string> lines = SplitLines(text);
            int blockH = static_cast<int>(lines.size()) * lineHeight;
            int lineY = (cellHeight - blockH) / 2;
            if (lineY < 0) lineY = 0;

            for (const auto& line : lines) {
                int x = padding;
                for (auto& seg : ParseDependencyLine(line)) {
                    int w = ctx->GetTextLineWidth(seg.text);
                    seg.rect = Rect2Di(x, lineY, w, lineHeight);
                    x += w;
                    cache.segments.push_back(std::move(seg));
                }
                lineY += lineHeight;
            }
            return cellCache.emplace(key, std::move(cache)).first->second;
        }

        float fontSize;
        int lineHeight;
        int padding;
        int rowHeight = 24;
        int hoverRow = -1;
        int hoverColumn = -1;
        int hoverSegment = -1;
        std::unordered_map<long long, CellCache> cellCache;
        Color textColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
        Color headerBackground = Color(222, 228, 245);
        Color headerTextColor = Color(40, 50, 120);
        Color linkColor = Color(40, 96, 192);        // matches the demo's link blue
        Color licenseColor = Color(110, 120, 165);   // quieter than the name link
    };

    // ===== LINK HELPERS =====

    // "https://www.cairographics.org/" -> "cairographics.org" for menu labels.
    static std::string ShortenUrlForDisplay(std::string url) {
        auto strip = [&url](const char* prefix) {
            size_t len = std::strlen(prefix);
            if (url.compare(0, len, prefix) == 0) url.erase(0, len);
        };
        strip("https://");
        strip("http://");
        strip("www.");
        if (!url.empty() && url.back() == '/') url.pop_back();
        return url;
    }

    static std::string BuildLinkTooltip(const DepCellSegment& seg) {
        if (seg.kind == DepCellSegment::License && seg.license) {
            return std::string(seg.license->fullName) + "\nClick to open " + seg.license->url;
        }
        const DepLibrary* lib = seg.lib;
        if (!lib) return "";
        std::string tip = lib->name;
        tip += "\nWebsite:  ";
        tip += lib->website;
        if (lib->repo && *lib->repo && std::strcmp(lib->repo, lib->website) != 0) {
            tip += "\nSource:  ";
            tip += lib->repo;
        }
        if (lib->license && *lib->license) {
            if (const DepLicense* lic = FindDepLicense(lib->license)) {
                tip += "\nLicense:  ";
                tip += lic->fullName;
            }
        }
        return tip;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDependenciesExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DependenciesExamples");

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DepTitle", 20, 10, 700, 35);
        title->SetText("Dependencies & Third-Party Libraries");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Subtitle
        auto subtitle = std::make_shared<UltraCanvasLabel>("DepSubtitle", 20, 45, 940, 25);
        subtitle->SetText("Grouped per module; (bundled) = vendored, (optional) = feature-gated.  "
                          "Click a library name for its website & source-code links, "
                          "click a license tag — e.g. (MIT) — for the license description.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // ============================================================
        // Build the model. A row with empty OS columns is a module section
        // header; otherwise it is a dependency row (libraries split on '\n').
        // The "(License)" tag directly after a known library name must match
        // the library's license label in kDepLibraries to become a link.
        // ============================================================
        auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
        model->AddColumn(ListColumnDef("Module / Purpose", 215, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Linux", 230, TextAlignment::Left));
        model->AddColumn(ListColumnDef("macOS", 225, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Windows", 225, TextAlignment::Left));

        auto header = [&](const std::string& moduleName) {
            model->AddItem(MultiColumnListItem({moduleName, "", "", ""}));
        };
        auto dep = [&](const std::string& purpose, const std::string& lnux, const std::string& mac, const std::string& win) {
            model->AddItem(MultiColumnListItem({purpose, lnux, mac, win}));
        };

        // ===== UltraCanvas (core) =====
        header("UltraCanvas (core)");
        dep("2D graphics rendering", "Cairo (LGPL 2.1)", "Cairo (LGPL 2.1)", "Cairo (LGPL 2.1)");
        dep("Text layout & shaping", "Pango (LGPL 2.1)\nHarfBuzz (MIT)\nFreeType (FTL)", "Pango (LGPL 2.1)\nHarfBuzz (MIT)\nFreeType (FTL)", "Pango (LGPL 2.1)\nHarfBuzz (MIT)\nFreeType (FTL)");
        dep("Font discovery", "FontConfig (MIT)", "CoreText", "FontConfig (MIT)");
        dep("Native windowing & cursors", "X11 (MIT)\nXcursor (MIT)", "Cocoa\nQuartzCore", "Win32 (gdi32)\nWin32 (user32)");
        dep("Native dialogs & printing", "GTK 3 (LGPL 2.1)", "Cocoa (AppKit)", "Win32 (comdlg32)");
        dep("Core utilities", "GLib (LGPL 2.1)", "GLib (LGPL 2.1)", "GLib (LGPL 2.1)");
        dep("XML parsing (SVG/config/ODS)", "TinyXML2 (zlib)", "TinyXML2 (zlib)", "TinyXML2 (zlib)");
        dep("String formatting", "fmt (MIT)", "fmt (MIT)", "fmt (MIT)");
        dep("Image processing & resize", "libvips (LGPL 2.1)", "libvips (LGPL 2.1)", "libvips (LGPL 2.1)");
        dep("Bitmap codecs (via libvips)", "libpng (libpng 2)\nlibjpeg-turbo (BSD 3)\nlibtiff (libtiff)\nlibwebp (BSD 3)", "libpng (libpng 2)\nlibjpeg-turbo (BSD 3)\nlibtiff (libtiff)\nlibwebp (BSD 3)", "libpng (libpng 2)\nlibjpeg-turbo (BSD 3)\nlibtiff (libtiff)\nlibwebp (BSD 3)");
        dep("GIF / HEIF / AVIF (via libvips)", "giflib (MIT)\nlibheif (LGPL 3)", "giflib (MIT)\nlibheif (LGPL 3)", "giflib (MIT)\nlibheif (LGPL 3)");
        dep("BMP / PCX coders (via libvips)", "ImageMagick (ImageMagick)", "ImageMagick (ImageMagick)", "ImageMagick (ImageMagick)");
        dep("Audio playback & capture", "miniaudio (MIT-0) (bundled)\n→ ALSA (LGPL 2.1)\n→ PulseAudio (LGPL 2.1)", "miniaudio (MIT-0) (bundled)\n→ CoreAudio", "miniaudio (MIT-0) (bundled)\n→ WASAPI");
        dep("Video playback & capture", "GStreamer (LGPL 2.1) (optional)", "AVFoundation (optional)", "Media Foundation (optional)");
        dep("OpenGL 3D surface", "OpenGL\nEGL / GLX (optional)", "OpenGL\nCGL (optional)", "OpenGL\nGLEW (BSD 3) / WGL (optional)");
        dep("QR / barcode decoding", "zbar (LGPL 2.1) (optional)", "zbar (LGPL 2.1) (optional)", "zbar (LGPL 2.1) (optional)");
        dep("QR code generation", "qrcodegen (MIT) (bundled)", "qrcodegen (MIT) (bundled)", "qrcodegen (MIT) (bundled)");
        dep("ZIP for ODS / XLSX I/O", "miniz (MIT) (bundled)", "miniz (MIT) (bundled)", "miniz (MIT) (bundled)");
        dep("FFT / spectrogram", "KissFFT (BSD 3) (bundled)", "KissFFT (BSD 3) (bundled)", "KissFFT (BSD 3) (bundled)");
        dep("JSON parsing (UltraCanvasJSON)", "yyjson (MIT) (bundled)", "yyjson (MIT) (bundled)", "yyjson (MIT) (bundled)");

        // ===== Optional plugins =====
        header("PDF plugin (optional)");
        dep("PDF rendering", "MuPDF (AGPL 3)\nmujs (ISC)\ngumbo (Apache 2)\njbig2dec (AGPL 3) / openjp2 (BSD 2)", "MuPDF (AGPL 3)\nmujs (ISC)\ngumbo (Apache 2)", "MuPDF (AGPL 3)\nmujs (ISC)\ngumbo (Apache 2)");

        header("CDR plugin (optional)");
        dep("CorelDRAW import", "libcdr (MPL 2.0)\nlibrevenge (MPL 2.0)\nlibrevenge-stream (MPL 2.0)", "libcdr (MPL 2.0)\nlibrevenge (MPL 2.0)\nlibrevenge-stream (MPL 2.0)", "– (not available)");
        dep("Color mgmt / Unicode (optional)", "LCMS2 (MIT)\nICU (Unicode)", "LCMS2 (MIT)\nICU (Unicode)", "– (not available)");

        header("OCR plugin (optional)");
        dep("Optical character recognition", "Tesseract (Apache 2)\nLeptonica (BSD 2)", "Tesseract (Apache 2)\nLeptonica (BSD 2)", "Tesseract (Apache 2)\nLeptonica (BSD 2)");
        dep("Language data (tessdata)", "eng.traineddata (bundled)\n+ user downloads", "eng.traineddata (bundled)\n+ user downloads", "eng.traineddata (bundled)\n+ user downloads");

        header("Vectorizer plugin (optional)");
        dep("Raster → SVG tracing", "VTracer (MIT)\nvisioncortex (MIT)\n(Rust, via corrosion)", "VTracer (MIT)\nvisioncortex (MIT)\n(Rust, via corrosion)", "VTracer (MIT)\nvisioncortex (MIT)\n(Rust, via corrosion)");
        dep("Build toolchain", "cargo + rustc\ncorrosion-rs (MIT) (FetchContent)", "cargo + rustc\ncorrosion-rs (MIT) (FetchContent)", "cargo + rustc\ncorrosion-rs (MIT) (FetchContent)");

        header("LaTeX plugin (optional, on-demand module)");
        dep("LaTeX math rendering", "MicroTeX (MIT) (bundled)\nLatin Modern Math (LPPL) (bundled)\n→ Cairo / Pango (core)", "MicroTeX (MIT) (bundled)\nLatin Modern Math (LPPL) (bundled)\n→ Cairo / Pango (core)", "MicroTeX (MIT) (bundled)\nLatin Modern Math (LPPL) (bundled)\n→ Cairo / Pango (core)");

        // ===== Demo application =====
        header("Demo application");
        dep("App icon → .exe (build tool)", "– (Windows only)", "– (Windows only)", "ImageMagick (ImageMagick)\n(magick CLI)");

        // ===== Additional ULTRA OS modules =====
        header("AudioFX module");
        dep("Audio effects / decode", "FFmpeg (LGPL 2.1)\nlibsndfile (LGPL 2.1)\nFFTW3 (GPL 2)", "FFmpeg (LGPL 2.1)\nlibsndfile (LGPL 2.1)\nFFTW3 (GPL 2)", "FFmpeg (LGPL 2.1)\nlibsndfile (LGPL 2.1)\nFFTW3 (GPL 2)");

        header("FileLoader module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("IODeviceManager module");
        dep("Scanners / cameras / print", "SANE (GPL 2)\nV4L2\nCUPS (Apache 2)", "ICA\nAVFoundation", "WIA\nTWAIN\nMedia Foundation");

        header("PixelFX module");
        dep("Image effects", "libvips (LGPL 2.1)", "libvips (LGPL 2.1)", "libvips (LGPL 2.1)");

        header("Smart Home module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("Ultra AI module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("Ultra Net module");
        dep("Core protocols (HTTP/WS/FTP/TLS/DNS)", "libcurl (curl)\nOpenSSL (Apache 2)", "libcurl (curl)\nOpenSSL (Apache 2)", "libcurl (curl)\nOpenSSL (Apache 2)");
        dep("Native sockets / platform glue", "POSIX sockets (glibc)", "BSD sockets\nNetwork.framework", "Winsock2 (ws2_32)");
        dep("Extra protocols (SMTP/MQTT/SSH/gRPC/…)", "plugin-supplied\n(libs tracked separately)", "plugin-supplied\n(libs tracked separately)", "plugin-supplied\n(libs tracked separately)");
        dep("JMAP mail plug-in (RFC 8620/8621)", "nlohmann/json (MIT) (bundled)", "nlohmann/json (MIT) (bundled)", "nlohmann/json (MIT) (bundled)");

        header("VideoFX module");
        dep("Video effects / transcode", "FFmpeg (LGPL 2.1)", "FFmpeg (LGPL 2.1)", "FFmpeg (LGPL 2.1)");

        header("VirtualFS module");
        dep("Archive formats", "libarchive (BSD 2)", "libarchive (BSD 2)", "libarchive (BSD 2)");
        dep("Compression", "zlib (zlib)\nlibzstd (BSD 3) (optional)\nliblz4 (BSD 2) (optional)\n(VIRTUALFS_USE_*, default OFF)", "zlib (zlib)\nlibzstd (BSD 3) (optional)\nliblz4 (BSD 2) (optional)\n(VIRTUALFS_USE_*, default OFF)", "zlib (zlib)\nlibzstd (BSD 3) (optional)\nliblz4 (BSD 2) (optional)\n(VIRTUALFS_USE_*, default OFF)");
        dep("Planned providers (CHM/LIT, WIM, Brotli)", "libmspack (LGPL 2.1)\nwimlib (LGPL 3)\nlibbrotli (MIT)\n(not yet wired)", "libmspack (LGPL 2.1)\nwimlib (LGPL 3)\nlibbrotli (MIT)\n(not yet wired)", "libmspack (LGPL 2.1)\nwimlib (LGPL 3)\nlibbrotli (MIT)\n(not yet wired)");

        // ============================================================
        // The UltraCanvasListView table itself
        // ============================================================
        // The tallest cells carry 4 libraries, so size every row for 4 lines.
        const int lineHeight = 16;
        const int maxLines = 4;
        const int rowHeight = lineHeight * maxLines + 8;   // 72 px

        auto listView = std::make_shared<UltraCanvasListView>("DependenciesListView", 20, 80, 920, 730);
        listView->SetModel(model);
        // Rows are not selectable — the links are the interaction here.
        listView->SetSelection(nullptr);

        ListViewStyle style;
        style.showHeader = true;
        style.showGridLines = true;
        style.headerHeight = 28;
        style.headerFontSize = 11;
        style.rowHeight = rowHeight;
        style.alternateRowColors = false;   // grouping bands provide the structure
        listView->SetStyle(style);

        auto delegate = std::make_shared<DependencyCellDelegate>(11.0f, lineHeight, 8);
        delegate->SetRowHeight(rowHeight);
        listView->SetDelegate(delegate);

        // --- Link interaction wiring ---
        // Raw pointer captures are safe: the callbacks live on the listview and
        // are only invoked by it. A shared_ptr capture would create a cycle.
        UltraCanvasListView* lv = listView.get();

        // Popup shown when a library has both a website and a repository.
        auto linkMenu = std::make_shared<UltraCanvasMenu>("DepLinkMenu", 0, 0, 300, 0);
        linkMenu->SetMenuType(MenuType::PopupMenu);

        listView->onCellHovered = [lv, delegate](int row, int column, const Point2Di& posInCell) {
            int segIndex = -1;
            const DepCellSegment* seg = (row >= 0)
                ? delegate->HitTestLink(row, column, posInCell, &segIndex)
                : nullptr;
            if (!seg) { row = -1; column = -1; segIndex = -1; }
            if (!delegate->SetHoveredLink(row, column, segIndex)) return;

            lv->SetMouseCursor(seg ? UCMouseCursor::Hand : UCMouseCursor::Default);
            auto* win = lv->GetWindow();
            if (seg && win) {
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                    win, BuildLinkTooltip(*seg),
                    UltraCanvasApplication::GetInstance()->GetCurrentEvent().pointerWindow);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
            lv->RequestRedraw();
        };

        listView->onCellClicked = [lv, delegate, linkMenu](int row, int column, const Point2Di& posInCell) {
            const DepCellSegment* seg = delegate->HitTestLink(row, column, posInCell);
            if (!seg) return;
            UltraCanvasTooltipManager::HideTooltip();

            if (seg->kind == DepCellSegment::License && seg->license) {
                OpenURL(seg->license->url);
                return;
            }

            const DepLibrary* lib = seg->lib;
            if (!lib) return;
            bool hasRepo = lib->repo && *lib->repo && std::strcmp(lib->repo, lib->website) != 0;
            if (!hasRepo) {
                // OS frameworks and single-home projects: open directly.
                OpenURL(lib->website);
                return;
            }

            auto* win = lv->GetWindow();
            if (!win) return;
            std::string website = lib->website;
            std::string repo = lib->repo;

            linkMenu->Clear();
            linkMenu->AddItem(MenuItemData::Header(lib->name));
            linkMenu->AddItem(MenuItemData::Action(
                "🌐 Website — " + ShortenUrlForDisplay(website),
                [website]() { OpenURL(website); }));
            linkMenu->AddItem(MenuItemData::Action(
                "📦 Source code — " + ShortenUrlForDisplay(repo),
                [repo]() { OpenURL(repo); }));
            if (lib->license && *lib->license) {
                if (const DepLicense* lic = FindDepLicense(lib->license)) {
                    std::string licUrl = lic->url;
                    linkMenu->AddItem(MenuItemData::Separator());
                    linkMenu->AddItem(MenuItemData::Action(
                        std::string("📄 License — ") + lic->fullName,
                        [licUrl]() { OpenURL(licUrl); }));
                }
            }
            linkMenu->OpenMenu(
                UltraCanvasApplication::GetInstance()->GetCurrentEvent().pointerWindow,
                *win, PopupElementSettings());
        };

        container->AddChild(listView);

        return container;
    }

} // namespace UltraCanvas
