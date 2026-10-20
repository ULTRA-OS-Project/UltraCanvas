// Tests/EmojiSettingsTest.cpp
// The emoji setting (UltraCanvasEmojiSettings.h): Emoji.conf read and written,
// and an emoji drawn from the bundled font the setting picks.
//
// The drawing half runs on a font configuration of the bundled Ubuntu fonts
// alone - what a Windows start has before its system fonts arrive, and a
// machine with no emoji font of its own - and lays out "👤 Someone" (the
// subject of a LinkedIn mail that showed a box of hex digits in UltraMail) in
// Ubuntu Bold: the emoji must come from Noto Color Emoji, or Noto Emoji for
// the black-and-white setting, and no glyph may be missing.
// The generic aliases (GenerateFontAliasConfig) are checked the same way: in a
// configuration of the bundled fonts alone, "monospace" is Ubuntu Mono.
// Version: 1.1.0 - the generic aliases
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"   // GetBundledFontsDir / kEmbeddedAllFonts
#include "UltraCanvasEmojiSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

// The settings folder's root, where the setting reads it: %APPDATA% on Windows
// (GetEnvUtf8 reads the wide environment), $XDG_CONFIG_HOME elsewhere.
void SetSettingsRoot(const std::filesystem::path& root) {
#if defined(_WIN32)
    SetEnvironmentVariableW(L"APPDATA", root.c_str());   // path-string-ok: the wide path itself
#else
    setenv("XDG_CONFIG_HOME", PathToUtf8(root).c_str(), 1);
#endif
}

struct EmojiRun {
    std::string family;
    int missing = -1;   // glyphs drawn as a box of hex digits
};

// A configuration of the bundled fonts alone, made current, with the emoji
// setting applied to it; then the emoji run of "👤 Someone" in Ubuntu Bold.
EmojiRun LayOutWithBundledFonts() {
    FcConfig* config = FcConfigCreate();
    const std::string dir = GetBundledFontsDir();
    for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
        const std::string file = dir + kEmbeddedAllFonts[i];
        if (std::filesystem::exists(PathFromUtf8(file)))
            FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(file.c_str()));
    }
    FcConfigSetCurrent(config);
    FcConfigDestroy(config);   // the current-config reference keeps it
    ApplyEmojiSettings();
    FcConfigBuildFonts(FcConfigGetCurrent());

    EmojiRun result;
    PangoFontMap* map = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
    if (!map) return result;   // made after the configuration: it reads the current one
    PangoContext* context = pango_font_map_create_context(map);
    PangoLayout* layout = pango_layout_new(context);
    pango_layout_set_text(layout, "\xF0\x9F\x91\xA4 Someone", -1);
    PangoFontDescription* description = pango_font_description_from_string("Ubuntu Bold 14");
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);

    result.missing = 0;
    PangoLayoutIter* it = pango_layout_get_iter(layout);
    do {
        PangoLayoutRun* run = pango_layout_iter_get_run(it);
        if (!run) continue;
        for (int i = 0; i < run->glyphs->num_glyphs; ++i)
            if (run->glyphs->glyphs[i].glyph & PANGO_GLYPH_UNKNOWN_FLAG) ++result.missing;
        if (run->item->offset == 0) {   // the emoji, first in the text
            PangoFontDescription* used = pango_font_describe(run->item->analysis.font);
            if (const char* family = pango_font_description_get_family(used)) result.family = family;
            pango_font_description_free(used);
        }
    } while (pango_layout_iter_next_run(it));
    pango_layout_iter_free(it);
    g_object_unref(layout);
    g_object_unref(context);
    g_object_unref(map);
    return result;
}

// The family fontconfig matches `family` to in a configuration of the
// bundled fonts alone, with the generic aliases loaded into it or not - as
// Windows' inline configuration for an empty fontconfig had none.
std::string MatchInBundledFonts(const char* family, bool withAliases) {
    FcConfig* config = FcConfigCreate();
    const std::string dir = GetBundledFontsDir();
    for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
        const std::string file = dir + kEmbeddedAllFonts[i];
        if (std::filesystem::exists(PathFromUtf8(file)))
            FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(file.c_str()));
    }
    if (withAliases) {
        const std::string aliases = GenerateFontAliasConfig();
        FcConfigParseAndLoadFromMemory(config, reinterpret_cast<const FcChar8*>(aliases.c_str()), FcFalse);
    }
    FcConfigBuildFonts(config);
    std::string matched;
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family));
    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    if (FcPattern* match = FcFontMatch(config, pattern, &result)) {
        FcChar8* name = nullptr;
        if (FcPatternGetString(match, FC_FAMILY, 0, &name) == FcResultMatch && name)
            matched = reinterpret_cast<const char*>(name);
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    FcConfigDestroy(config);
    return matched;
}

} // namespace

int main() {
    // ----- the setting's text -----
    TEST("the default is colour", EmojiSettings().style == EmojiStyle::Colour);
    for (EmojiStyle style : {EmojiStyle::Colour, EmojiStyle::Monochrome, EmojiStyle::System}) {
        EmojiSettings s;
        s.style = style;
        TEST(std::string("round trip: ") + EmojiStyleKey(style), EmojiSettings::Parse(s.Serialize()) == s);
    }
    TEST("American spelling read too",
         EmojiSettings::Parse("style=color\n").style == EmojiStyle::Colour);
    TEST("an unknown value keeps the default",
         EmojiSettings::Parse("style=sepia\n").style == EmojiStyle::Colour);
    TEST("CRLF and other lines are read past",
         EmojiSettings::Parse("other=1\r\nstyle=monochrome\r\n").style == EmojiStyle::Monochrome);

    // ----- the rules -----
    TEST("colour puts Noto Color Emoji first",
         EmojiFontconfigRules(EmojiStyle::Colour).find("<string>Noto Color Emoji</string>") != std::string::npos);
    TEST("colour does not withdraw the colour request",
         EmojiFontconfigRules(EmojiStyle::Colour).find("name=\"color\"") == std::string::npos);
    TEST("monochrome withdraws the colour request",
         EmojiFontconfigRules(EmojiStyle::Monochrome).find("name=\"color\"") != std::string::npos);
    TEST("system has no rule", EmojiFontconfigRules(EmojiStyle::System).empty());
    TEST("monochrome loads the black-and-white file",
         BundledEmojiFontFile(EmojiStyle::Monochrome).find("NotoEmoji-Regular.ttf") != std::string::npos);
    TEST("system loads the colour file behind the system's",
         BundledEmojiFontFile(EmojiStyle::System).find("NotoColorEmoji.ttf") != std::string::npos);

    // ----- the file -----
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(ec) / ("uc-emoji-test-" + std::to_string(std::rand()));
    std::filesystem::create_directories(root, ec);
    SetSettingsRoot(root);
    TEST("no file: the default", EmojiSettings::Load().style == EmojiStyle::Colour);
    {
        EmojiSettings s;
        s.style = EmojiStyle::Monochrome;
        TEST("saved", s.Save());
        TEST("read back", EmojiSettings::Load() == s);
        TEST("in the UltraCanvas settings folder",
             EmojiSettings::FilePath().filename() == "Emoji.conf" &&
             std::filesystem::exists(EmojiSettings::FilePath()));
    }

    // ----- the generic aliases (GenerateFontAliasConfig) -----
    {
        const std::string aliases = GenerateFontAliasConfig();
        TEST("the aliases name the generic families",
             aliases.find("<family>monospace</family>") != std::string::npos &&
             aliases.find("<family>emoji</family>") != std::string::npos);
        TEST("the aliases are aliases only", aliases.find("<dir") == std::string::npos);
        const std::string without = MatchInBundledFonts("monospace", false);
        const std::string with = MatchInBundledFonts("monospace", true);
        std::cerr << "  monospace: \"" << without << "\" without the aliases, \"" << with << "\" with them"
                  << std::endl;
        TEST("monospace is Ubuntu Mono with the aliases", with == "Ubuntu Mono");
    }

    // ----- drawn -----
    TEST("the bundled colour font is there",
         std::filesystem::exists(PathFromUtf8(BundledEmojiFontFile(EmojiStyle::Colour))));
    TEST("the bundled black-and-white font is there",
         std::filesystem::exists(PathFromUtf8(BundledEmojiFontFile(EmojiStyle::Monochrome))));

    struct Case { EmojiStyle style; const char* family; };
    for (const Case& c : {Case{EmojiStyle::Colour, "Noto Color Emoji"},
                          Case{EmojiStyle::Monochrome, "Noto Emoji"},
                          Case{EmojiStyle::System, "Noto Color Emoji"}}) {
        EmojiSettings s;
        s.style = c.style;
        s.Save();
        const EmojiRun run = LayOutWithBundledFonts();
        const std::string name = EmojiStyleKey(c.style);
        std::cerr << "  " << name << ": the emoji drawn in \"" << run.family << "\", " << run.missing
                  << " missing glyph(s)" << std::endl;
        TEST(name + ": no box of hex digits", run.missing == 0);
        TEST(name + ": drawn in " + c.family, run.family == c.family);
    }

    std::filesystem::remove_all(root, ec);
    std::cerr << (testCount - failCount) << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}
