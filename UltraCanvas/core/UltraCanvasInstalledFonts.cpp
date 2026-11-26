// core/UltraCanvasInstalledFonts.cpp
// The installed fonts' catalogue (see the header): Pango's families and
// faces, and - on every platform whose text engine is fontconfig - their
// formats, files and characters from fontconfig's records. macOS fills the
// same in from CoreText (OS/MacOS/UltraCanvasMacOSInstalledFonts.mm).
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework

#include "UltraCanvasInstalledFonts.h"
#include "UltraCanvasApplication.h"   // GetFontSetGeneration

#include <pango/pangocairo.h>
#if !defined(__APPLE__) || defined(ULTRACANVAS_IOS)
#include <fontconfig/fontconfig.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>

namespace UltraCanvas {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int StretchPercent(PangoStretch stretch) {
    switch (stretch) {
        case PANGO_STRETCH_ULTRA_CONDENSED: return 50;
        case PANGO_STRETCH_EXTRA_CONDENSED: return 62;
        case PANGO_STRETCH_CONDENSED:       return 75;
        case PANGO_STRETCH_SEMI_CONDENSED:  return 87;
        case PANGO_STRETCH_SEMI_EXPANDED:   return 112;
        case PANGO_STRETCH_EXPANDED:        return 125;
        case PANGO_STRETCH_EXTRA_EXPANDED:  return 150;
        case PANGO_STRETCH_ULTRA_EXPANDED:  return 200;
        default:                            return 100;
    }
}

const char* StretchMarkup(int stretch) {
    if (stretch <= 56) return "ultracondensed";
    if (stretch <= 68) return "extracondensed";
    if (stretch <= 81) return "condensed";
    if (stretch <= 93) return "semicondensed";
    if (stretch <= 106) return "normal";
    if (stretch <= 118) return "semiexpanded";
    if (stretch <= 137) return "expanded";
    if (stretch <= 175) return "extraexpanded";
    return "ultraexpanded";
}

std::string EscapeMarkup(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

void AppendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// The families and faces of the text engine's default font map. A face the
// engine only synthesizes (slanted or emboldened from another) is no style
// of the font's; a family made of nothing else is a generic name ("Sans")
// the engine adds, left out unless `withGenericNames`.
std::vector<InstalledFontFamily> ListEngineFamilies(bool withGenericNames) {
    std::vector<InstalledFontFamily> out;
    PangoFontMap* fontMap = pango_cairo_font_map_get_default();
    if (!fontMap) return out;
    PangoFontFamily** families = nullptr;
    int familyCount = 0;
    pango_font_map_list_families(fontMap, &families, &familyCount);
    out.reserve(static_cast<size_t>(familyCount));
    for (int i = 0; i < familyCount; ++i) {
        const char* name = pango_font_family_get_name(families[i]);
        if (!name || !*name) continue;
        InstalledFontFamily family;
        family.name = name;
        family.monospace = pango_font_family_is_monospace(families[i]);
#if PANGO_VERSION_CHECK(1, 44, 0)
        family.variable = pango_font_family_is_variable(families[i]);
#endif
        PangoFontFace** faces = nullptr;
        int faceCount = 0;
        pango_font_family_list_faces(families[i], &faces, &faceCount);
        // A face made of bitmaps lists the sizes it has; a scalable one none.
        bool scalable = faceCount == 0;
        bool anySynthesized = false;
        for (int f = 0; f < faceCount; ++f) {
            int* sizes = nullptr;
            int sizeCount = 0;
            pango_font_face_list_sizes(faces[f], &sizes, &sizeCount);
            if (!sizes) scalable = true;
            g_free(sizes);
#if PANGO_VERSION_CHECK(1, 46, 0)
            if (pango_font_face_is_synthesized(faces[f])) { anySynthesized = true; continue; }
#endif
            const char* faceName = pango_font_face_get_face_name(faces[f]);
            InstalledFontStyle style;
            style.name = faceName && *faceName ? faceName : "Regular";
            if (PangoFontDescription* desc = pango_font_face_describe(faces[f])) {
                style.weight = std::clamp(static_cast<int>(pango_font_description_get_weight(desc)), 1, 1000);
                const PangoStyle slant = pango_font_description_get_style(desc);
                style.slant = slant == PANGO_STYLE_ITALIC ? FontSlant::Italic
                            : slant == PANGO_STYLE_OBLIQUE ? FontSlant::Oblique : FontSlant::Normal;
                style.stretch = StretchPercent(pango_font_description_get_stretch(desc));
                pango_font_description_free(desc);
            }
            // Two files of one style (the same face as Type 1 and as
            // OpenType) are one style here.
            const bool known = std::any_of(family.styles.begin(), family.styles.end(), [&](const InstalledFontStyle& s) {
                return Lower(s.name) == Lower(style.name) && s.weight == style.weight && s.slant == style.slant &&
                       s.stretch == style.stretch;
            });
            if (!known) family.styles.push_back(std::move(style));
        }
        g_free(faces);
        family.scalable = scalable;
        if (family.styles.empty() && anySynthesized && !withGenericNames) continue;   // "Sans", "Serif", ...
        SortFontStyles(family.styles);
        out.push_back(std::move(family));
    }
    g_free(families);
    std::sort(out.begin(), out.end(), [](const InstalledFontFamily& a, const InstalledFontFamily& b) {
        const std::string la = Lower(a.name), lb = Lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const InstalledFontFamily& a, const InstalledFontFamily& b) {
        return Lower(a.name) == Lower(b.name);
    }), out.end());
    return out;
}

std::mutex catalogueMutex;
std::shared_ptr<const std::vector<InstalledFontFamily>> catalogue;
unsigned long catalogueGeneration = 0;

} // namespace

// ===== THE HELPERS =====

const InstalledFontStyle* InstalledFontFamily::RegularStyle() const {
    const InstalledFontStyle* best = nullptr;
    int bestDistance = 0;
    for (const InstalledFontStyle& style : styles) {
        const int distance = std::abs(style.weight - 400) + (style.slant != FontSlant::Normal ? 1000 : 0) +
                             std::abs(style.stretch - 100) * 4;
        if (!best || distance < bestDistance) { best = &style; bestDistance = distance; }
    }
    return best;
}

bool InstalledFontFamily::HasStylesBeyondBoldItalic() const {
    // At the family's own width: a narrow family's four styles are all narrow.
    const InstalledFontStyle* regular = RegularStyle();
    const int width = regular ? regular->stretch : 100;
    return std::any_of(styles.begin(), styles.end(), [width](const InstalledFontStyle& s) {
        const bool boldOrRegular = s.IsNormalWeight() || (s.weight >= 670 && s.weight <= 730);
        return !boldOrRegular || s.stretch != width;
    });
}

void SortFontStyles(std::vector<InstalledFontStyle>& styles) {
    std::stable_sort(styles.begin(), styles.end(), [](const InstalledFontStyle& a, const InstalledFontStyle& b) {
        if (a.stretch != b.stretch) return a.stretch < b.stretch;
        if (a.weight != b.weight) return a.weight < b.weight;
        return static_cast<int>(a.slant) < static_cast<int>(b.slant);
    });
}

const InstalledFontFamily* FindInstalledFontFamily(const std::vector<InstalledFontFamily>& families,
                                                   const std::string& name) {
    if (name.empty()) return nullptr;
    const std::string key = Lower(name);
    // A to Z regardless of case: a binary search, then the neighbours.
    auto it = std::lower_bound(families.begin(), families.end(), key, [](const InstalledFontFamily& f, const std::string& k) {
        return Lower(f.name) < k;
    });
    if (it != families.end() && Lower(it->name) == key) return &*it;
    for (const InstalledFontFamily& family : families)   // a list not in that order
        if (Lower(family.name) == key) return &family;
    return nullptr;
}

const InstalledFontFamily* FindInstalledFontFamilyOfName(const std::vector<InstalledFontFamily>& families,
                                                         const std::string& name, const InstalledFontStyle** face) {
    if (face) *face = nullptr;
    if (const InstalledFontFamily* family = FindInstalledFontFamily(families, name)) return family;
    if (name.empty()) return nullptr;
    const std::string key = Lower(name);
    for (const InstalledFontFamily& family : families) {
        // A face's own name starts with its family's.
        if (key.size() <= family.name.size() || key.compare(0, family.name.size(), Lower(family.name)) != 0) continue;
        for (const InstalledFontStyle& style : family.styles) {
            if (!style.faceFamily.empty() && Lower(style.faceFamily) == key) {
                if (!face) return &family;
                // The face that name gives on its own - upright, without
                // bold - where the engine reaches one so.
                auto rank = [](const InstalledFontStyle& s) {
                    return (s.slant == FontSlant::Normal ? 0 : 2) +
                           (s.faceFamilyReach == InstalledFontStyle::FaceFamilyReach::WithoutBold ? 0 : 1);
                };
                if (!*face || rank(style) < rank(**face)) *face = &style;
            }
        }
        if (face && *face) return &family;
    }
    return nullptr;
}

bool IsInstalledFontName(const std::string& name) {
    const auto fonts = GetInstalledFontCatalogue();
    return fonts && FindInstalledFontFamilyOfName(*fonts, name) != nullptr;
}

bool IsHiddenFontFamilyName(const std::string& name) {
    if (name.empty() || name[0] == '@' || name[0] == '.') return true;
    const std::string lower = Lower(name);
    return lower == "lastresort" || lower == "last resort";
}

bool IsKnownSymbolFontFamily(const std::string& name) {
    static const char* const kSymbolFamilies[] = {
        "wingdings", "wingdings 2", "wingdings 3", "webdings", "symbol", "marlett", "mt extra",
        "zapf dingbats", "zapfdingbats", "itc zapf dingbats", "dingbats", "bookshelf symbol 7",
        "ms reference specialty", "ms outlook", "segoe mdl2 assets", "segoe fluent icons",
        "hololens mdl2 assets", "opensymbol", "webdings 2"};
    const std::string lower = Lower(name);
    return std::any_of(std::begin(kSymbolFamilies), std::end(kSymbolFamilies),
                       [&](const char* known) { return lower == known; });
}

std::vector<char32_t> DecodeUtf8CodePoints(const std::string& text) {
    std::vector<char32_t> out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + static_cast<size_t>(length) > text.size()) length = 1;
        char32_t cp = length == 1 ? c : length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < length; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + static_cast<size_t>(k)]) & 0x3F);
        out.push_back(cp);
        i += static_cast<size_t>(length);
    }
    return out;
}

std::string ChooseFontSample(const std::vector<char32_t>& covered) {
    auto printable = [](char32_t c) {
        return c > 0x20 && c != 0x7F && !(c >= 0x80 && c <= 0xA0) && c != 0xAD &&
               !(c >= 0x2000 && c <= 0x200F) && !(c >= 0x2028 && c <= 0x202F) && c != 0x3000 && c != 0xFEFF;
    };
    constexpr size_t kLength = 5;
    std::vector<char32_t> picked;
    // A symbol font's own page first, from where 'A' would be (its digits
    // and signs before that are often just that) ...
    for (char32_t c : covered)
        if (c >= 0xF041 && c <= 0xF0FF && picked.size() < kLength) picked.push_back(c);
    if (picked.size() >= 3) {
        for (char32_t c : covered)
            if (c >= 0xF021 && c < 0xF041 && picked.size() < kLength) picked.push_back(c);
    }
    // ... else its pictographs (an emoji font) ...
    if (picked.size() < 3) {
        picked.clear();
        for (char32_t c : covered)
            if (c >= 0x1F300 && c <= 0x1FAFF && picked.size() < kLength) picked.push_back(c);
    }
    // ... else what it has beyond Latin-1 (a script's letters, dingbats),
    // no punctuation and no combining marks ...
    auto combining = [](char32_t c) {
        return (c >= 0x300 && c <= 0x36F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
               (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFE20 && c <= 0xFE2F);
    };
    if (picked.size() < 3) {
        picked.clear();
        for (char32_t c : covered)
            if (c > 0xFF && printable(c) && !combining(c) && !(c >= 0x2000 && c <= 0x206F) && picked.size() < kLength)
                picked.push_back(c);
    }
    // ... else its first printable characters.
    if (picked.size() < 3) {
        picked.clear();
        for (char32_t c : covered)
            if (printable(c) && picked.size() < kLength) picked.push_back(c);
    }
    std::string out;
    for (char32_t c : picked) AppendUtf8(out, c);
    return out;
}

std::string FontStyleMarkup(const std::string& family, const InstalledFontStyle& style, const std::string& text) {
    std::string out = "<span font_family=\"" + EscapeMarkup(family) + "\" weight=\"" + std::to_string(style.weight) + "\"";
    if (style.slant == FontSlant::Italic) out += " style=\"italic\"";
    else if (style.slant == FontSlant::Oblique) out += " style=\"oblique\"";
    if (style.stretch != 100) out += std::string(" stretch=\"") + StretchMarkup(style.stretch) + "\"";
    out += ">" + EscapeMarkup(text) + "</span>";
    return out;
}

// ===== THE CATALOGUE =====

std::shared_ptr<const std::vector<InstalledFontFamily>> GetInstalledFontCatalogue() {
    const unsigned long generation = GetFontSetGeneration();
    {
        std::lock_guard<std::mutex> lock(catalogueMutex);
        if (catalogue && catalogueGeneration == generation) return catalogue;
    }
    std::vector<InstalledFontFamily> families = ListEngineFamilies(false);
    families.erase(std::remove_if(families.begin(), families.end(),
                                  [](const InstalledFontFamily& f) { return IsHiddenFontFamilyName(f.name); }),
                   families.end());
    FillNativeFontDetails(families);
    for (InstalledFontFamily& family : families) {
        if (IsKnownSymbolFontFamily(family.name)) family.symbol = true;
        if (family.format == FontFileFormat::Unknown) {
            if (const InstalledFontStyle* regular = family.RegularStyle()) family.format = regular->format;
        }
        if (!family.scalable && family.format == FontFileFormat::Unknown) family.format = FontFileFormat::BitmapFont;
    }
    auto made = std::make_shared<const std::vector<InstalledFontFamily>>(std::move(families));
    std::lock_guard<std::mutex> lock(catalogueMutex);
    catalogue = made;
    catalogueGeneration = generation;
    return made;
}

// The names alone, for a plain list (UltraCanvasApplication.h): the same
// enumeration, the scalable families, with the engine's generic names - a
// text that names "Sans" finds it - and without the hidden ones.
std::vector<std::string> GetInstalledFontFamilies() {
    std::vector<std::string> names;
    for (const InstalledFontFamily& family : ListEngineFamilies(true)) {
        if (family.scalable && !IsHiddenFontFamilyName(family.name)) names.push_back(family.name);
    }
    return names;
}

// ===== FONTCONFIG =====
#if !defined(__APPLE__) || defined(ULTRACANVAS_IOS)

namespace {

struct FcRecord {
    std::vector<std::string> styles;    // lower case, every name fontconfig gives the face
    std::vector<std::string> families;  // as written; [0] the one the engine lists
    FontFileFormat format = FontFileFormat::Unknown;
    std::string file;
    FcCharSet* charset = nullptr;       // owned by the font set
    int weight = 400;
    double fcWeight = FC_WEIGHT_REGULAR;   // fontconfig's own scale: what it matches by
    FontSlant slant = FontSlant::Normal;
    int width = 100;
};

FontFileFormat FormatOf(const std::string& fcFormat, const std::string& file, bool scalable) {
    std::string extension;
    const size_t dot = file.find_last_of('.');
    if (dot != std::string::npos) extension = Lower(file.substr(dot + 1));
    if (!scalable || fcFormat == "PCF" || fcFormat == "BDF" || fcFormat == "Windows FNT") return FontFileFormat::BitmapFont;
    if (fcFormat == "CFF" || fcFormat == "CFF2") return FontFileFormat::OpenType;
    if (fcFormat == "TrueType")
        return extension == "otf" || extension == "otc" ? FontFileFormat::OpenType : FontFileFormat::TrueType;
    if (fcFormat == "Type 1" || fcFormat == "CID Type 1" || fcFormat == "Type 42") return FontFileFormat::Type1;
    if (extension == "woff") return FontFileFormat::WOFF;
    if (extension == "woff2") return FontFileFormat::WOFF2;
    if (extension == "otf" || extension == "otc") return FontFileFormat::OpenType;
    if (extension == "ttf" || extension == "ttc") return FontFileFormat::TrueType;
    if (extension == "pfb" || extension == "pfa" || extension == "t1") return FontFileFormat::Type1;
    return FontFileFormat::Unknown;
}

int FormatRank(FontFileFormat format) {
    switch (format) {
        case FontFileFormat::OpenType: return 4;
        case FontFileFormat::TrueType: return 3;
        case FontFileFormat::Type1:    return 2;
        case FontFileFormat::WOFF:
        case FontFileFormat::WOFF2:    return 2;
        case FontFileFormat::BitmapFont:   return 1;
        default:                       return 0;
    }
}

int CssWeight(int fcWeight) {
#if defined(FC_WEIGHT_EXTRABLACK)
    return std::clamp(FcWeightToOpenType(fcWeight), 1, 1000);
#else
    return fcWeight >= FC_WEIGHT_BOLD ? 700 : 400;
#endif
}

// The code points a charset has, ascending, up to `limit` of them.
std::vector<char32_t> CoveredCodePoints(FcCharSet* charset, size_t limit) {
    std::vector<char32_t> out;
    if (!charset) return out;
    FcChar32 map[FC_CHARSET_MAP_SIZE];
    FcChar32 next = 0;
    for (FcChar32 base = FcCharSetFirstPage(charset, map, &next); base != FC_CHARSET_DONE;
         base = FcCharSetNextPage(charset, map, &next)) {
        for (int i = 0; i < FC_CHARSET_MAP_SIZE; ++i) {
            if (!map[i]) continue;
            for (int bit = 0; bit < 32; ++bit) {
                if (map[i] & (1u << bit)) {
                    out.push_back(static_cast<char32_t>(base + static_cast<FcChar32>(i) * 32 + static_cast<FcChar32>(bit)));
                    if (out.size() >= limit) return out;
                }
            }
        }
    }
    return out;
}

// Whether a charset has anything beyond Latin-1 but the symbol page
// (U+F000 to U+F0FF). A symbol or dingbat font with a symbol character map
// (Wingdings, Symbol) or a custom encoding (Zapf Dingbats as Type 1) is
// handed to fontconfig as Latin-1 - its pictures under 'A', 'b' and '!' -
// and often again on that page; a text font always has more (the dashes
// and quotation marks of U+2013 to U+201D at the least).
bool HasMoreThanLatin1(FcCharSet* charset) {
    if (!charset) return true;
    FcChar32 map[FC_CHARSET_MAP_SIZE];
    FcChar32 next = 0;
    for (FcChar32 base = FcCharSetFirstPage(charset, map, &next); base != FC_CHARSET_DONE;
         base = FcCharSetNextPage(charset, map, &next)) {
        if (base == 0 || base == 0xF000) continue;
        for (int i = 0; i < FC_CHARSET_MAP_SIZE; ++i)
            if (map[i]) return true;
    }
    return false;
}

} // namespace

void FillNativeFontDetails(std::vector<InstalledFontFamily>& families) {
    FcConfig* config = FcConfigGetCurrent();
    if (!config || families.empty()) return;
    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FONTFORMAT, FC_FILE, FC_CHARSET, FC_SCALABLE,
                                            FC_WEIGHT, FC_SLANT, FC_WIDTH, static_cast<char*>(nullptr));
    FcFontSet* set = FcFontList(config, pattern, objects);
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    if (!set) return;

    // The records by the family the engine lists them under.
    std::map<std::string, std::vector<FcRecord>> byFamily;
    for (int i = 0; i < set->nfont; ++i) {
        FcPattern* font = set->fonts[i];
        FcRecord record;
        FcChar8* value = nullptr;
        for (int n = 0; FcPatternGetString(font, FC_FAMILY, n, &value) == FcResultMatch; ++n)
            record.families.emplace_back(reinterpret_cast<const char*>(value));
        if (record.families.empty()) continue;
        for (int n = 0; FcPatternGetString(font, FC_STYLE, n, &value) == FcResultMatch; ++n)
            record.styles.push_back(Lower(reinterpret_cast<const char*>(value)));
        std::string fcFormat;
        if (FcPatternGetString(font, FC_FONTFORMAT, 0, &value) == FcResultMatch) fcFormat = reinterpret_cast<const char*>(value);
        if (FcPatternGetString(font, FC_FILE, 0, &value) == FcResultMatch) record.file = reinterpret_cast<const char*>(value);
        FcBool scalable = FcTrue;
        FcPatternGetBool(font, FC_SCALABLE, 0, &scalable);
        record.format = FormatOf(fcFormat, record.file, scalable != FcFalse);
        FcPatternGetCharSet(font, FC_CHARSET, 0, &record.charset);
        int number = 0;
        double real = 0;
        if (FcPatternGetInteger(font, FC_WEIGHT, 0, &number) == FcResultMatch) {
            record.weight = CssWeight(number);
            record.fcWeight = number;
        } else if (FcPatternGetDouble(font, FC_WEIGHT, 0, &real) == FcResultMatch) {
            record.weight = CssWeight(static_cast<int>(std::lround(real)));
            record.fcWeight = real;
        }
        if (FcPatternGetInteger(font, FC_SLANT, 0, &number) == FcResultMatch)
            record.slant = number >= FC_SLANT_OBLIQUE ? FontSlant::Oblique : number >= FC_SLANT_ITALIC ? FontSlant::Italic : FontSlant::Normal;
        if (FcPatternGetInteger(font, FC_WIDTH, 0, &number) == FcResultMatch) record.width = number;
        else if (FcPatternGetDouble(font, FC_WIDTH, 0, &real) == FcResultMatch) record.width = static_cast<int>(std::lround(real));
        byFamily[Lower(record.families.front())].push_back(std::move(record));
    }

    std::vector<InstalledFontFamily> kept;
    kept.reserve(families.size());
    for (InstalledFontFamily& family : families) {
        auto found = byFamily.find(Lower(family.name));
        if (found == byFamily.end()) { kept.push_back(std::move(family)); continue; }
        const std::vector<FcRecord>& records = found->second;
        // Each style: the record of its name (or nearest by weight, slant
        // and width), the best format where one style has two files.
        auto recordFor = [&](const InstalledFontStyle& style) -> const FcRecord* {
            const std::string name = Lower(style.name);
            const FcRecord* best = nullptr;
            int bestScore = 0;
            for (const FcRecord& record : records) {
                const bool named = std::find(record.styles.begin(), record.styles.end(), name) != record.styles.end();
                int score = (named ? 0 : 100000) + std::abs(record.weight - style.weight) +
                            (record.slant != style.slant ? 5000 : 0) + std::abs(record.width - style.stretch) * 20;
                score = score * 8 - FormatRank(record.format);
                if (!best || score < bestScore) { best = &record; bestScore = score; }
            }
            return best;
        };
        std::vector<double> fcWeights(family.styles.size(), FC_WEIGHT_REGULAR);
        for (size_t s = 0; s < family.styles.size(); ++s) {
            InstalledFontStyle& style = family.styles[s];
            const FcRecord* record = recordFor(style);
            if (!record) continue;
            style.format = record->format;
            style.file = record->file;
            fcWeights[s] = record->fcWeight;
            // A second family name that is this one's and a style's -
            // "Ubuntu Light" beside "Ubuntu" - names the face alone; a
            // translated name does not start with it.
            for (size_t n = 1; n < record->families.size(); ++n) {
                const std::string& other = record->families[n];
                if (other.size() > family.name.size() + 1 && Lower(other).rfind(Lower(family.name) + " ", 0) == 0) {
                    style.faceFamily = other;
                    break;
                }
            }
        }
        // Which faces their own family name reaches, and how: fontconfig
        // takes the face of that name and slant nearest the weight asked
        // for - the regular weight without bold, bold with it - in its own
        // weight scale. A face nearest neither, or tied, is not reached.
        for (size_t s = 0; s < family.styles.size(); ++s) {
            InstalledFontStyle& style = family.styles[s];
            if (style.faceFamily.empty()) continue;
            auto nearestTo = [&](double target) {
                const double mine = std::abs(fcWeights[s] - target);
                for (size_t o = 0; o < family.styles.size(); ++o) {
                    const InstalledFontStyle& other = family.styles[o];
                    if (o == s || other.slant != style.slant || Lower(other.faceFamily) != Lower(style.faceFamily)) continue;
                    if (std::abs(fcWeights[o] - target) <= mine) return false;
                }
                return true;
            };
            style.faceFamilyReach = nearestTo(FC_WEIGHT_REGULAR) ? InstalledFontStyle::FaceFamilyReach::WithoutBold
                                  : nearestTo(FC_WEIGHT_BOLD)    ? InstalledFontStyle::FaceFamilyReach::WithBold
                                                                 : InstalledFontStyle::FaceFamilyReach::Unreachable;
        }
        // The family's own: its regular style's record.
        const InstalledFontStyle* regular = family.RegularStyle();
        const FcRecord* regularRecord = regular ? recordFor(*regular) : &records.front();
        if (regularRecord) {
            family.format = regularRecord->format;
            FcCharSet* charset = regularRecord->charset;
            if (charset && FcCharSetCount(charset) == 0) continue;   // no characters: nothing to show
            const bool spells = FontCoversItsName(family.name, [charset](char32_t c) {
                return charset && FcCharSetHasChar(charset, static_cast<FcChar32>(c));
            });
            family.symbol = charset && (!spells || !HasMoreThanLatin1(charset));
            if (family.symbol) family.sample = ChooseFontSample(CoveredCodePoints(charset, 4000));
        }
        kept.push_back(std::move(family));
    }
    families = std::move(kept);
    FcFontSetDestroy(set);
}

#endif

} // namespace UltraCanvas
