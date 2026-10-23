// core/UltraCanvasFontCoverage.cpp
// The characters of a text no font covers, by script. See UltraCanvasFontCoverage.h.
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasFontCoverage.h"
#include "UltraCanvasApplication.h"   // GetSystemFontStyle, GetInstalledFontFamilies

#include <pango/pangocairo.h>

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

    namespace {
        // A Pango layout of `text` in `fontFamily` (the system UI font when
        // empty), on a context of its own; null without a text engine.
        PangoLayout* CoverageLayout(const std::string& text, const std::string& fontFamily) {
            PangoFontMap* fontMap = pango_cairo_font_map_get_default();
            if (!fontMap) return nullptr;
            PangoContext* context = pango_font_map_create_context(fontMap);
            if (!context) return nullptr;
            std::string family = fontFamily;
            if (family.empty()) {
                auto* app = UltraCanvasApplicationBase::GetCurrent();
                family = app ? app->GetSystemFontStyle().fontFamily : "Sans";
            }
            PangoFontDescription* description = pango_font_description_new();
            pango_font_description_set_family(description, family.c_str());
            pango_font_description_set_size(description, 12 * PANGO_SCALE);
            PangoLayout* layout = pango_layout_new(context);
            pango_layout_set_font_description(layout, description);
            pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
            pango_font_description_free(description);
            g_object_unref(context);   // the layout keeps its own reference
            return layout;
        }

        std::string UrlQuery(const std::string& text) {
            static const char* const hex = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : text) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
                else if (c == ' ') out += '+';
                else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
            }
            return out;
        }

        struct ScriptFont {
            GUnicodeScript script;
            const char* name;
            const char* font;
        };
        // The scripts a document is most often written in, and the Noto font
        // for each. Another script is named by its ISO 15924 code and pointed
        // at the whole Noto family.
        const ScriptFont kScriptFonts[] = {
            { G_UNICODE_SCRIPT_HAN,        "Chinese",    "Noto Sans SC" },
            { G_UNICODE_SCRIPT_BOPOMOFO,   "Chinese",    "Noto Sans TC" },
            { G_UNICODE_SCRIPT_HIRAGANA,   "Japanese",   "Noto Sans JP" },
            { G_UNICODE_SCRIPT_KATAKANA,   "Japanese",   "Noto Sans JP" },
            { G_UNICODE_SCRIPT_HANGUL,     "Korean",     "Noto Sans KR" },
            { G_UNICODE_SCRIPT_ARABIC,     "Arabic",     "Noto Sans Arabic" },
            { G_UNICODE_SCRIPT_HEBREW,     "Hebrew",     "Noto Sans Hebrew" },
            { G_UNICODE_SCRIPT_SYRIAC,     "Syriac",     "Noto Sans Syriac" },
            { G_UNICODE_SCRIPT_THAANA,     "Thaana",     "Noto Sans Thaana" },
            { G_UNICODE_SCRIPT_DEVANAGARI, "Devanagari", "Noto Sans Devanagari" },
            { G_UNICODE_SCRIPT_BENGALI,    "Bengali",    "Noto Sans Bengali" },
            { G_UNICODE_SCRIPT_GURMUKHI,   "Gurmukhi",   "Noto Sans Gurmukhi" },
            { G_UNICODE_SCRIPT_GUJARATI,   "Gujarati",   "Noto Sans Gujarati" },
            { G_UNICODE_SCRIPT_ORIYA,      "Odia",       "Noto Sans Oriya" },
            { G_UNICODE_SCRIPT_TAMIL,      "Tamil",      "Noto Sans Tamil" },
            { G_UNICODE_SCRIPT_TELUGU,     "Telugu",     "Noto Sans Telugu" },
            { G_UNICODE_SCRIPT_KANNADA,    "Kannada",    "Noto Sans Kannada" },
            { G_UNICODE_SCRIPT_MALAYALAM,  "Malayalam",  "Noto Sans Malayalam" },
            { G_UNICODE_SCRIPT_SINHALA,    "Sinhala",    "Noto Sans Sinhala" },
            { G_UNICODE_SCRIPT_THAI,       "Thai",       "Noto Sans Thai" },
            { G_UNICODE_SCRIPT_LAO,        "Lao",        "Noto Sans Lao" },
            { G_UNICODE_SCRIPT_KHMER,      "Khmer",      "Noto Sans Khmer" },
            { G_UNICODE_SCRIPT_MYANMAR,    "Myanmar",    "Noto Sans Myanmar" },
            { G_UNICODE_SCRIPT_TIBETAN,    "Tibetan",    "Noto Serif Tibetan" },
            { G_UNICODE_SCRIPT_MONGOLIAN,  "Mongolian",  "Noto Sans Mongolian" },
            { G_UNICODE_SCRIPT_GEORGIAN,   "Georgian",   "Noto Sans Georgian" },
            { G_UNICODE_SCRIPT_ARMENIAN,   "Armenian",   "Noto Sans Armenian" },
            { G_UNICODE_SCRIPT_ETHIOPIC,   "Ethiopic",   "Noto Sans Ethiopic" },
            { G_UNICODE_SCRIPT_CHEROKEE,   "Cherokee",   "Noto Sans Cherokee" },
            { G_UNICODE_SCRIPT_CYRILLIC,   "Cyrillic",   "Noto Sans" },
            { G_UNICODE_SCRIPT_GREEK,      "Greek",      "Noto Sans" },
            { G_UNICODE_SCRIPT_LATIN,      "Latin",      "Noto Sans" },
            { G_UNICODE_SCRIPT_COMMON,     "Symbols",    "Noto Sans Symbols 2" },
            { G_UNICODE_SCRIPT_INHERITED,  "Symbols",    "Noto Sans Symbols 2" },
        };
    } // namespace

    int CountCharactersWithoutFont(const std::string& text, const std::string& fontFamily) {
        if (text.empty()) return 0;
        PangoLayout* layout = CoverageLayout(text, fontFamily);
        if (!layout) return -1;
        // A glyph Pango found no font for carries PANGO_GLYPH_UNKNOWN_FLAG;
        // this counts them.
        const int missing = pango_layout_get_unknown_glyphs_count(layout);
        g_object_unref(layout);
        return missing;
    }

    std::vector<MissingScriptFont> FindScriptsWithoutFont(const std::string& text, const std::string& fontFamily) {
        std::vector<MissingScriptFont> scripts;
        if (text.empty()) return scripts;
        PangoLayout* layout = CoverageLayout(text, fontFamily);
        if (!layout) return scripts;
        if (pango_layout_get_unknown_glyphs_count(layout) > 0) {
            // The characters without a font, in the text's order, each with
            // its own script.
            std::vector<GUnicodeScript> missing;
            PangoLayoutIter* iter = pango_layout_get_iter(layout);
            do {
                PangoLayoutRun* run = pango_layout_iter_get_run_readonly(iter);
                if (!run || !run->glyphs) continue;
                for (int i = 0; i < run->glyphs->num_glyphs; ++i) {
                    const PangoGlyph glyph = run->glyphs->glyphs[i].glyph;
                    if (!(glyph & PANGO_GLYPH_UNKNOWN_FLAG)) continue;
                    // An unknown glyph is the flag and the character itself.
                    const gunichar character = glyph & ~PANGO_GLYPH_UNKNOWN_FLAG;
                    if (character == 0 || g_unichar_iszerowidth(character)) continue;
                    missing.push_back(g_unichar_get_script(character));
                }
            } while (pango_layout_iter_next_run(iter));
            pango_layout_iter_free(iter);

            // Punctuation and digits (the Common script) belong to the script
            // of the nearest character without a font - a Chinese comma among
            // Chinese is Chinese, whatever stands next to it - and are a
            // script of their own, symbols, only when nothing else is missing.
            auto shared = [](GUnicodeScript script) {
                return script == G_UNICODE_SCRIPT_COMMON || script == G_UNICODE_SCRIPT_INHERITED ||
                       script == G_UNICODE_SCRIPT_UNKNOWN;
            };
            for (size_t i = 0; i < missing.size(); ++i) {
                if (!shared(missing[i]) || missing[i] == G_UNICODE_SCRIPT_UNKNOWN) continue;
                GUnicodeScript nearest = missing[i];
                for (size_t j = i; j-- > 0;)
                    if (!shared(missing[j])) { nearest = missing[j]; break; }
                if (nearest == missing[i]) {
                    for (size_t j = i + 1; j < missing.size(); ++j)
                        if (!shared(missing[j])) { nearest = missing[j]; break; }
                }
                missing[i] = nearest;
            }

            for (const GUnicodeScript script : missing) {
                std::string name, font = "Noto Sans", url;
                for (const ScriptFont& known : kScriptFonts) {
                    if (known.script == script) { name = known.name; font = known.font; break; }
                }
                char iso[5] = {};
                const guint32 tag = g_unicode_script_to_iso15924(script);
                for (int b = 0; b < 4; ++b) iso[b] = static_cast<char>((tag >> (24 - 8 * b)) & 0xFF);
                if (name.empty()) {
                    name = iso;
                    url = "https://fonts.google.com/noto";
                } else {
                    url = "https://fonts.google.com/noto/specimen/" + UrlQuery(font);
                }
                auto found = std::find_if(scripts.begin(), scripts.end(),
                    [&](const MissingScriptFont& s) { return s.script == name && s.suggestedFont == font; });
                if (found == scripts.end()) {
                    scripts.push_back({ name, iso, font, url, 0 });
                    found = scripts.end() - 1;
                }
                found->characters++;
            }
        }
        g_object_unref(layout);
        return scripts;
    }

    std::string FontDownloadUrl(const std::string& family) {
        return "https://fonts.google.com/?query=" + UrlQuery(family);
    }

    bool IsFontFamilyAvailable(const std::string& family) {
        if (family.empty()) return true;
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        const std::string wanted = lower(family);
        for (const std::string& installed : GetInstalledFontFamilies())
            if (lower(installed) == wanted) return true;
        return false;
    }

} // namespace UltraCanvas
