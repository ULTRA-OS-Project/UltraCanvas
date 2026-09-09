// core/UltraCanvasFontFile.cpp
// Reading and previewing font definition files - see UltraCanvasFontFile.h.
//
// Everything in here goes straight at the file with FreeType: the font does
// not have to be installed, registered with fontconfig, or known to Pango,
// and no render context has to exist. That is deliberate. A file manager
// thumbnailing a folder of downloaded fonts must not have to install them
// first, and it does the work on background threads - so every entry point
// creates and destroys its own FT_Library rather than sharing one, which is
// what makes concurrent calls safe (an FT_Library is not thread-safe, but
// two separate ones are independent).
// Version: 1.0.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasFontFile.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_FONT_FORMATS_H

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

namespace UltraCanvas {

    namespace {

        // ===== RAII AROUND THE TWO FREETYPE HANDLES =====
        struct FTLibrary {
            FT_Library handle = nullptr;
            FTLibrary() { if (FT_Init_FreeType(&handle) != 0) handle = nullptr; }
            ~FTLibrary() { if (handle) FT_Done_FreeType(handle); }
            FTLibrary(const FTLibrary&) = delete;
            FTLibrary& operator=(const FTLibrary&) = delete;
            explicit operator bool() const { return handle != nullptr; }
        };

        struct FTFace {
            FT_Face handle = nullptr;
            ~FTFace() { if (handle) FT_Done_Face(handle); }
            FTFace() = default;
            FTFace(const FTFace&) = delete;
            FTFace& operator=(const FTFace&) = delete;
            explicit operator bool() const { return handle != nullptr; }
        };

        std::string LowerExtensionOf(const std::string& pathOrExtension) {
            std::string s = pathOrExtension;
            const size_t slash = s.find_last_of("/\\");
            if (slash != std::string::npos) s.erase(0, slash + 1);
            const size_t dot = s.find_last_of('.');
            if (dot != std::string::npos) s.erase(0, dot + 1);
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // ===== NAME RECORDS =====
        // The name table stores the same string several times over, once per
        // platform / encoding / language the font was built for. Every record
        // is scored and the best one per name ID wins, so a font that carries
        // its family name in Windows Unicode English and again in MacRoman
        // Japanese reads as the English one rather than as whichever record
        // happened to come first.
        int NameRecordScore(const FT_SfntName& name) {
            switch (name.platform_id) {
                case TT_PLATFORM_MICROSOFT:
                    if (name.encoding_id == TT_MS_ID_UNICODE_CS ||
                        name.encoding_id == TT_MS_ID_UCS_4) {
                        return name.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES
                                       ? 100 : 60;
                    }
                    return 20;
                case TT_PLATFORM_APPLE_UNICODE:
                    return 50;
                case TT_PLATFORM_MACINTOSH:
                    return name.language_id == TT_MAC_LANGID_ENGLISH ? 40 : 15;
                default:
                    return 10;
            }
        }

        bool NameRecordIsUtf16(const FT_SfntName& name) {
            if (name.platform_id == TT_PLATFORM_APPLE_UNICODE) return true;
            if (name.platform_id != TT_PLATFORM_MICROSOFT) return false;
            return name.encoding_id == TT_MS_ID_UNICODE_CS ||
                   name.encoding_id == TT_MS_ID_UCS_4 ||
                   name.encoding_id == TT_MS_ID_SYMBOL_CS;
        }

        std::string DecodeNameRecord(const FT_SfntName& name) {
            if (!name.string || name.string_len == 0) return {};
            if (NameRecordIsUtf16(name)) {
                // UTF-16BE, and not necessarily aligned in the font's buffer:
                // swap into our own array before handing it to glib.
                const FT_UInt units = name.string_len / 2;
                if (units == 0) return {};
                std::vector<gunichar2> utf16(units);
                for (FT_UInt i = 0; i < units; ++i) {
                    utf16[i] = static_cast<gunichar2>(
                            (static_cast<uint16_t>(name.string[i * 2]) << 8) |
                             static_cast<uint16_t>(name.string[i * 2 + 1]));
                }
                gchar* utf8 = g_utf16_to_utf8(utf16.data(), static_cast<glong>(units),
                                              nullptr, nullptr, nullptr);
                if (!utf8) return {};
                std::string out(utf8);
                g_free(utf8);
                return out;
            }
            // Single-byte record (MacRoman or an ISO encoding). The ASCII
            // range is identical in all of them and covers the Latin names
            // these records actually hold; anything above it is dropped
            // rather than mojibake'd, and only ever reached when the font
            // carries no Unicode record at all.
            std::string out;
            out.reserve(name.string_len);
            for (FT_UInt i = 0; i < name.string_len; ++i) {
                const unsigned char c = name.string[i];
                if (c >= 0x20 && c < 0x7F) out.push_back(static_cast<char>(c));
            }
            return out;
        }

        // Strip the control characters some fonts embed in their name records
        // (stray newlines in a license text, mostly) so a single-line label
        // never breaks the layout it is drawn into.
        std::string Flatten(std::string s) {
            for (char& c : s) {
                if (static_cast<unsigned char>(c) < 0x20) c = ' ';
            }
            const size_t first = s.find_first_not_of(' ');
            if (first == std::string::npos) return {};
            const size_t last = s.find_last_not_of(' ');
            return s.substr(first, last - first + 1);
        }

        void ReadSfntNames(FT_Face face, FontFaceInfo& out) {
            const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
            std::map<FT_UShort, std::pair<int, std::string>> best;
            for (FT_UInt i = 0; i < count; ++i) {
                FT_SfntName name;
                if (FT_Get_Sfnt_Name(face, i, &name) != 0) continue;
                const int score = NameRecordScore(name);
                auto it = best.find(name.name_id);
                if (it != best.end() && it->second.first >= score) continue;
                std::string text = Flatten(DecodeNameRecord(name));
                if (text.empty()) continue;
                best[name.name_id] = { score, std::move(text) };
            }

            auto take = [&](FT_UShort id) -> std::string {
                auto it = best.find(id);
                return it == best.end() ? std::string{} : it->second.second;
            };

            // The typographic (id 16/17) names describe the real family of a
            // font whose weights were split up to fit the four-style limit of
            // the legacy ids - "Ubuntu" / "Light", where id 1/2 say
            // "Ubuntu Light" / "Regular". Prefer them where present.
            out.family = take(TT_NAME_ID_TYPOGRAPHIC_FAMILY);
            if (out.family.empty()) out.family = take(TT_NAME_ID_FONT_FAMILY);
            out.subfamily = take(TT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
            if (out.subfamily.empty()) out.subfamily = take(TT_NAME_ID_FONT_SUBFAMILY);
            out.fullName = take(TT_NAME_ID_FULL_NAME);
            out.postScriptName = take(TT_NAME_ID_PS_NAME);
            out.version = take(TT_NAME_ID_VERSION_STRING);
            out.copyright = take(TT_NAME_ID_COPYRIGHT);
            out.trademark = take(TT_NAME_ID_TRADEMARK);
            out.manufacturer = take(TT_NAME_ID_MANUFACTURER);
            out.designer = take(TT_NAME_ID_DESIGNER);
            out.license = take(TT_NAME_ID_LICENSE);
            out.licenseURL = take(TT_NAME_ID_LICENSE_URL);
            out.sampleText = take(TT_NAME_ID_SAMPLE_TEXT);
        }

        // What FreeType calls the format, mapped onto our enum. The file is
        // already open here, so this is what the container really is - the
        // extension only decides between the two WOFF flavours and between
        // TrueType and OpenType outlines, which share a driver.
        FontFileFormat FormatOfOpenFace(FT_Face face, const std::string& ext) {
            const char* fmt = FT_Get_Font_Format(face);
            const std::string driver = fmt ? fmt : "";
            if (ext == "woff2") return FontFileFormat::WOFF2;
            if (ext == "woff")  return FontFileFormat::WOFF;
            if (driver == "CFF") {
                // The CFF driver serves both bare .otf and CFF outlines
                // wrapped in an SFNT; either way it is OpenType.
                return FontFileFormat::OpenType;
            }
            if (driver == "TrueType") {
                // An .otf/.otc that reached the TrueType driver holds glyf
                // outlines; its extension is the only thing that is OpenType
                // about it, so report what the outlines actually are.
                return FontFileFormat::TrueType;
            }
            if (driver == "Type 1" || driver == "CID Type 1" || driver == "Type 42")
                return FontFileFormat::Type1;
            if (driver == "BDF" || driver == "PCF" || driver == "Windows FNT")
                return FontFileFormat::BitmapFont;
            if (!FT_IS_SCALABLE(face)) return FontFileFormat::BitmapFont;
            return FontFileFormat::Unknown;
        }

        void ReadFaceInfo(FT_Face face, FontFaceInfo& out) {
            out.index = static_cast<int>(face->face_index & 0xFFFF);
            out.glyphCount = face->num_glyphs;
            out.scalable = FT_IS_SCALABLE(face) != 0;
            out.unitsPerEM = out.scalable ? face->units_per_EM : 0;
            out.fixedWidth = FT_IS_FIXED_WIDTH(face) != 0;
            out.hasKerning = FT_HAS_KERNING(face) != 0;
            out.bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
            out.italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

            for (int i = 0; i < face->num_charmaps; ++i) {
                const FT_Encoding enc = face->charmaps[i]->encoding;
                if (enc == FT_ENCODING_UNICODE) { out.hasUnicodeCharmap = true; break; }
            }

            for (FT_Int i = 0; i < face->num_fixed_sizes; ++i)
                out.fixedSizes.push_back(face->available_sizes[i].height);

            ReadSfntNames(face, out);

            // Type 1 and the bitmap formats have no name table; FreeType
            // still exposes the family and style it parsed out of them.
            if (out.family.empty() && face->family_name)
                out.family = Flatten(face->family_name);
            if (out.subfamily.empty() && face->style_name)
                out.subfamily = Flatten(face->style_name);
            if (out.postScriptName.empty()) {
                if (const char* ps = FT_Get_Postscript_Name(face))
                    out.postScriptName = Flatten(ps);
            }
            if (out.fullName.empty()) {
                out.fullName = out.family;
                if (!out.subfamily.empty() && out.subfamily != "Regular") {
                    if (!out.fullName.empty()) out.fullName += ' ';
                    out.fullName += out.subfamily;
                }
            }
        }

        // ===== GLYPH RUNS =====
        // The specimen is a bare sequence of glyph indices: no shaping, no
        // bidi, no fallback face. That is enough for the Latin sample text a
        // specimen uses, and it is the only thing that can work here at all -
        // HarfBuzz would need the font registered and a Pango context, which
        // is exactly what this module exists to avoid.
        std::vector<FT_UInt> GlyphsForText(FT_Face face, const std::string& text) {
            std::vector<FT_UInt> glyphs;
            if (text.empty() || !g_utf8_validate(text.c_str(),
                                                 static_cast<gssize>(text.size()),
                                                 nullptr)) {
                return glyphs;
            }
            for (const char* p = text.c_str(); *p; p = g_utf8_next_char(p)) {
                const gunichar cp = g_utf8_get_char(p);
                const FT_UInt gi = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
                if (gi != 0) glyphs.push_back(gi);
            }
            return glyphs;
        }

        // An icon or symbol font has no glyph for "AaBbCc" - drawing its own
        // first few glyphs instead is what makes it preview as something
        // rather than as an empty card.
        std::vector<FT_UInt> FirstGlyphsOf(FT_Face face, int count) {
            std::vector<FT_UInt> glyphs;
            for (FT_UInt gi = 1; gi < static_cast<FT_UInt>(face->num_glyphs) &&
                                 static_cast<int>(glyphs.size()) < count; ++gi) {
                if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT) != 0) continue;
                // Skip the blanks: a run of spaces is not a specimen.
                if (face->glyph->metrics.width <= 0 || face->glyph->metrics.height <= 0)
                    continue;
                glyphs.push_back(gi);
            }
            return glyphs;
        }

        struct RunMetrics {
            bool valid = false;
            double inkLeft = 0.0, inkTop = 0.0;     // relative to the origin,
            double inkRight = 0.0, inkBottom = 0.0; // y down from the baseline
            double Width() const { return inkRight - inkLeft; }
            double Height() const { return inkBottom - inkTop; }
        };

        // Ink extent of the run at the size the face is currently set to.
        // Loads outlines only - no rasterization - so it is cheap enough to
        // repeat while converging on the size that fills the box.
        RunMetrics MeasureRun(FT_Face face, const std::vector<FT_UInt>& glyphs) {
            RunMetrics m;
            const bool kerning = FT_HAS_KERNING(face) != 0;
            double pen = 0.0;
            FT_UInt previous = 0;
            for (const FT_UInt gi : glyphs) {
                if (kerning && previous) {
                    FT_Vector delta;
                    if (FT_Get_Kerning(face, previous, gi, FT_KERNING_DEFAULT, &delta) == 0)
                        pen += delta.x / 64.0;
                }
                previous = gi;
                if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT) != 0) continue;
                const FT_Glyph_Metrics& gm = face->glyph->metrics;
                const double left = pen + gm.horiBearingX / 64.0;
                const double top = -(gm.horiBearingY / 64.0);
                const double right = left + gm.width / 64.0;
                const double bottom = top + gm.height / 64.0;
                if (gm.width > 0 && gm.height > 0) {
                    if (!m.valid) {
                        m.inkLeft = left; m.inkTop = top;
                        m.inkRight = right; m.inkBottom = bottom;
                        m.valid = true;
                    } else {
                        m.inkLeft = std::min(m.inkLeft, left);
                        m.inkTop = std::min(m.inkTop, top);
                        m.inkRight = std::max(m.inkRight, right);
                        m.inkBottom = std::max(m.inkBottom, bottom);
                    }
                }
                pen += gm.horiAdvance / 64.0;
            }
            return m;
        }

        constexpr int kMinSpecimenPpem = 6;
        constexpr int kMaxSpecimenPpem = 512;
        // Bounds the work one tile can cost: previews are small, and a
        // request for a huge one must not turn into a huge rasterization on
        // a worker thread.
        constexpr int kMaxSpecimenEdge = 1024;

        bool SetPpem(FT_Face face, int ppem) {
            return FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(ppem)) == 0;
        }

        // The strike of a bitmap-only face that best fills the box. Bitmap
        // faces cannot be scaled, so the choice is "largest that fits", and
        // the smallest strike when even that overflows (a too-big specimen
        // that gets clipped still shows the face; nothing at all does not).
        int SelectBitmapStrike(FT_Face face, int availableHeight) {
            int best = 0;
            int bestHeight = -1;
            for (FT_Int i = 0; i < face->num_fixed_sizes; ++i) {
                const int h = face->available_sizes[i].height;
                if (h <= availableHeight && h > bestHeight) { bestHeight = h; best = i; }
            }
            if (bestHeight >= 0) return best;
            int smallest = 0;
            for (FT_Int i = 1; i < face->num_fixed_sizes; ++i) {
                if (face->available_sizes[i].height <
                    face->available_sizes[smallest].height) smallest = i;
            }
            return smallest;
        }

        // Largest size at which the run's ink still fits the box. Converges
        // from the box height by the ratio it overflows or underfills by;
        // three passes settle it for any real font, and the trailing shrink
        // guarantees the result actually fits rather than nearly does.
        int FitPpem(FT_Face face, const std::vector<FT_UInt>& glyphs,
                    double availableWidth, double availableHeight,
                    RunMetrics& outMetrics) {
            int ppem = std::clamp(static_cast<int>(std::lround(availableHeight)),
                                  kMinSpecimenPpem, kMaxSpecimenPpem);
            RunMetrics m;
            for (int pass = 0; pass < 3; ++pass) {
                if (!SetPpem(face, ppem)) return 0;
                m = MeasureRun(face, glyphs);
                if (!m.valid || m.Width() <= 0.0 || m.Height() <= 0.0) return 0;
                const double factor = std::min(availableWidth / m.Width(),
                                               availableHeight / m.Height());
                const int next = std::clamp(
                        static_cast<int>(std::floor(ppem * factor)),
                        kMinSpecimenPpem, kMaxSpecimenPpem);
                if (next == ppem) break;
                ppem = next;
            }
            while (ppem > kMinSpecimenPpem &&
                   (m.Width() > availableWidth || m.Height() > availableHeight)) {
                --ppem;
                if (!SetPpem(face, ppem)) return 0;
                m = MeasureRun(face, glyphs);
                if (!m.valid) return 0;
            }
            outMetrics = m;
            return ppem;
        }

        // ===== COMPOSITING =====
        // The pixmap holds premultiplied ARGB32 in little-endian byte order,
        // which is what every Cairo-backed surface in the framework expects.
        uint32_t PremultiplyColor(const Color& c) {
            const uint32_t a = c.a;
            return (a << 24)
                 | (((static_cast<uint32_t>(c.r) * a + 127) / 255) << 16)
                 | (((static_cast<uint32_t>(c.g) * a + 127) / 255) << 8)
                 |  ((static_cast<uint32_t>(c.b) * a + 127) / 255);
        }

        void BlendPixel(uint32_t& dst, const Color& ink, unsigned coverage) {
            if (coverage == 0) return;
            const uint32_t sa = (static_cast<uint32_t>(ink.a) * coverage + 127) / 255;
            if (sa == 0) return;
            const uint32_t sr = (static_cast<uint32_t>(ink.r) * sa + 127) / 255;
            const uint32_t sg = (static_cast<uint32_t>(ink.g) * sa + 127) / 255;
            const uint32_t sb = (static_cast<uint32_t>(ink.b) * sa + 127) / 255;
            const uint32_t inv = 255 - sa;
            const uint32_t da = (dst >> 24) & 0xFF, dr = (dst >> 16) & 0xFF;
            const uint32_t dg = (dst >> 8) & 0xFF,  db = dst & 0xFF;
            const uint32_t oa = sa + (da * inv + 127) / 255;
            const uint32_t orr = sr + (dr * inv + 127) / 255;
            const uint32_t og = sg + (dg * inv + 127) / 255;
            const uint32_t ob = sb + (db * inv + 127) / 255;
            dst = (std::min(oa, 255u) << 24) | (std::min(orr, 255u) << 16)
                | (std::min(og, 255u) << 8)  |  std::min(ob, 255u);
        }

        // Coverage of one pixel of a rendered glyph bitmap, for the two pixel
        // modes FT_RENDER_MODE_NORMAL can produce: 8-bit gray for an outline
        // face, and 1-bit for a bitmap-only face whose strikes are monochrome.
        unsigned GlyphCoverage(const FT_Bitmap& bmp, int x, int y) {
            // `pitch` is the offset that walks one row down whichever way the
            // bitmap flows, so it is added as-is for both signs.
            const unsigned char* row =
                    bmp.buffer + static_cast<std::ptrdiff_t>(y) * bmp.pitch;
            switch (bmp.pixel_mode) {
                case FT_PIXEL_MODE_GRAY:
                    if (bmp.num_grays == 256) return row[x];
                    if (bmp.num_grays <= 1) return row[x] ? 255u : 0u;
                    return static_cast<unsigned>(row[x] * 255 / (bmp.num_grays - 1));
                case FT_PIXEL_MODE_MONO:
                    return (row[x >> 3] & (0x80 >> (x & 7))) ? 255u : 0u;
                default:
                    return 0u;
            }
        }

        void DrawRun(FT_Face face, const std::vector<FT_UInt>& glyphs,
                     uint32_t* pixels, int pw, int ph,
                     double originX, double baselineY, const Color& ink) {
            const bool kerning = FT_HAS_KERNING(face) != 0;
            double pen = originX;
            FT_UInt previous = 0;
            for (const FT_UInt gi : glyphs) {
                if (kerning && previous) {
                    FT_Vector delta;
                    if (FT_Get_Kerning(face, previous, gi, FT_KERNING_DEFAULT, &delta) == 0)
                        pen += delta.x / 64.0;
                }
                previous = gi;
                if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT) != 0) continue;
                FT_GlyphSlot slot = face->glyph;
                if (slot->format != FT_GLYPH_FORMAT_BITMAP &&
                    FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
                    pen += slot->metrics.horiAdvance / 64.0;
                    continue;
                }
                const FT_Bitmap& bmp = slot->bitmap;
                if (bmp.buffer && bmp.width > 0 && bmp.rows > 0) {
                    const int left = static_cast<int>(std::lround(pen)) + slot->bitmap_left;
                    const int top = static_cast<int>(std::lround(baselineY)) - slot->bitmap_top;
                    for (unsigned gy = 0; gy < bmp.rows; ++gy) {
                        const int py = top + static_cast<int>(gy);
                        if (py < 0 || py >= ph) continue;
                        uint32_t* dstRow = pixels + static_cast<size_t>(py) * pw;
                        for (unsigned gx = 0; gx < bmp.width; ++gx) {
                            const int px = left + static_cast<int>(gx);
                            if (px < 0 || px >= pw) continue;
                            BlendPixel(dstRow[px], ink,
                                       GlyphCoverage(bmp, static_cast<int>(gx),
                                                     static_cast<int>(gy)));
                        }
                    }
                }
                pen += slot->metrics.horiAdvance / 64.0;
            }
        }

    } // namespace

    // ===== RECOGNITION =====

    FontFileFormat FontFormatForExtension(const std::string& pathOrExtension) {
        const std::string ext = LowerExtensionOf(pathOrExtension);
        if (ext == "ttf" || ext == "ttc") return FontFileFormat::TrueType;
        if (ext == "otf" || ext == "otc") return FontFileFormat::OpenType;
        if (ext == "pfa" || ext == "pfb") return FontFileFormat::Type1;
        if (ext == "woff")  return FontFileFormat::WOFF;
        if (ext == "woff2") return FontFileFormat::WOFF2;
        if (ext == "bdf" || ext == "pcf" || ext == "fon" || ext == "fnt")
            return FontFileFormat::BitmapFont;
        return FontFileFormat::Unknown;
    }

    bool IsFontFileExtension(const std::string& pathOrExtension) {
        return FontFormatForExtension(pathOrExtension) != FontFileFormat::Unknown;
    }

    const char* FontFormatName(FontFileFormat format) {
        switch (format) {
            case FontFileFormat::TrueType:    return "TrueType";
            case FontFileFormat::OpenType:    return "OpenType";
            case FontFileFormat::Type1:       return "Type 1";
            case FontFileFormat::WOFF:        return "WOFF";
            case FontFileFormat::WOFF2:       return "WOFF2";
            case FontFileFormat::BitmapFont:  return "Bitmap font";
            case FontFileFormat::Unknown:     break;
        }
        return "Unknown";
    }

    // ===== METADATA =====

    bool ReadFontFileInfo(const std::string& filePath, FontFileInfo& out) {
        out = FontFileInfo{};
        out.path = filePath;

        FTLibrary lib;
        if (!lib) return false;

        FTFace first;
        if (FT_New_Face(lib.handle, filePath.c_str(), 0, &first.handle) != 0)
            return false;

        std::error_code ec;
        const auto size = std::filesystem::file_size(filePath, ec);
        if (!ec) out.fileSize = static_cast<uint64_t>(size);

        const std::string ext = LowerExtensionOf(filePath);
        out.format = FormatOfOpenFace(first.handle, ext);
        if (const char* fmt = FT_Get_Font_Format(first.handle)) out.formatName = fmt;
        out.faceCount = static_cast<int>(first.handle->num_faces);

        FontFaceInfo info;
        ReadFaceInfo(first.handle, info);
        out.faces.push_back(std::move(info));

        // A collection's remaining faces. Each is opened separately: a face
        // handle is bound to one index for its lifetime.
        for (FT_Long i = 1; i < first.handle->num_faces; ++i) {
            FTFace next;
            if (FT_New_Face(lib.handle, filePath.c_str(), i, &next.handle) != 0) continue;
            FontFaceInfo faceInfo;
            ReadFaceInfo(next.handle, faceInfo);
            out.faces.push_back(std::move(faceInfo));
        }
        return true;
    }

    // ===== SPECIMEN =====

    std::shared_ptr<UCPixmap> RenderFontSpecimenPixmap(
            const std::string& filePath, int width, int height, float scale,
            const FontSpecimenOptions& options) {
        if (width <= 0 || height <= 0) return nullptr;
        const float deviceScale = std::max(1.0f, scale);
        const int pw = std::clamp(static_cast<int>(std::lround(width * deviceScale)),
                                  8, kMaxSpecimenEdge);
        const int ph = std::clamp(static_cast<int>(std::lround(height * deviceScale)),
                                  8, kMaxSpecimenEdge);

        FTLibrary lib;
        if (!lib) return nullptr;
        FTFace face;
        if (FT_New_Face(lib.handle, filePath.c_str(),
                        std::max(0, options.faceIndex), &face.handle) != 0) {
            return nullptr;
        }
        if (face.handle->num_glyphs <= 0) return nullptr;

        const int padding = std::max(0, static_cast<int>(std::lround(
                options.padding * deviceScale)));
        const double availableWidth = std::max(1.0, static_cast<double>(pw - 2 * padding));
        const double availableHeight = std::max(1.0, static_cast<double>(ph - 2 * padding));

        // The size has to be set before the charmap lookups below: a bitmap
        // face has no glyph metrics at all until a strike is selected.
        const bool scalable = FT_IS_SCALABLE(face.handle) != 0;
        if (!scalable) {
            if (face.handle->num_fixed_sizes <= 0) return nullptr;
            const int strike = SelectBitmapStrike(
                    face.handle, static_cast<int>(availableHeight));
            if (FT_Select_Size(face.handle, strike) != 0) return nullptr;
        } else if (!SetPpem(face.handle, static_cast<int>(availableHeight))) {
            return nullptr;
        }

        // The default sample follows the shape of the box. A six-glyph line
        // in a square tile is fitted by its width and ends up a fraction of
        // the tile tall - too small to read the letterforms off, which is the
        // whole point of a specimen. Two glyphs fill the same tile instead,
        // and the full line is kept for boxes wide enough to give it height.
        const std::string text =
                !options.text.empty()                     ? options.text
                : availableWidth >= availableHeight * 2.0 ? std::string("AaBbCc")
                                                          : std::string("Ag");
        std::vector<FT_UInt> glyphs = GlyphsForText(face.handle, text);
        if (glyphs.size() < 2) {
            // Fewer than two of the sample characters exist in this font:
            // a symbol or icon face. Show what it does have instead.
            std::vector<FT_UInt> fallback = FirstGlyphsOf(face.handle, 6);
            if (!fallback.empty()) glyphs = std::move(fallback);
        }
        if (glyphs.empty()) return nullptr;

        RunMetrics metrics;
        if (scalable) {
            if (FitPpem(face.handle, glyphs, availableWidth, availableHeight,
                        metrics) == 0) {
                return nullptr;
            }
        } else {
            metrics = MeasureRun(face.handle, glyphs);
            if (!metrics.valid) return nullptr;
        }

        auto pm = std::make_shared<UCPixmap>();
        if (!pm->Init(pw, ph)) return nullptr;
        uint32_t* pixels = pm->GetPixelData();
        if (!pixels) return nullptr;

        const uint32_t background = PremultiplyColor(options.backgroundColor);
        std::fill(pixels, pixels + static_cast<size_t>(pw) * ph, background);

        // Centre the ink box in the padded area, then work back to the pen
        // origin and baseline that put it there.
        const double originX = padding + (availableWidth - metrics.Width()) * 0.5
                             - metrics.inkLeft;
        const double baselineY = padding + (availableHeight - metrics.Height()) * 0.5
                               - metrics.inkTop;
        DrawRun(face.handle, glyphs, pixels, pw, ph, originX, baselineY,
                options.textColor);

        pm->MarkDirty();
        return pm;
    }

} // namespace UltraCanvas
