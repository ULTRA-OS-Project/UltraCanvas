// Tests/InstalledFontsTest.cpp
// The installed fonts' catalogue (UltraCanvasInstalledFonts.h), with no
// window: many families, the bundled Ubuntu with its styles (Thin to Bold,
// the italics, the face's own family name "Ubuntu Light"), the formats, the
// symbol fonts with a sample, the hidden families left out, the cache, and
// the helpers - the markup of a style, the coverage of a name, the sample.
// On Linux it also wants one of the fonts every Ubuntu has (fontconfig's own
// dependency: DejaVu, Liberation, ...), and names the formats it found.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"     // RegisterBundledFontsWithoutApplication, GetInstalledFontFamilies
#include "UltraCanvasInstalledFonts.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>

using namespace UltraCanvas;

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++checks;                                                                         \
        if (!(cond)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static const InstalledFontStyle* StyleNamed(const InstalledFontFamily& family, const std::string& name) {
    for (const InstalledFontStyle& style : family.styles)
        if (Lower(style.name) == Lower(name)) return &style;
    return nullptr;
}

static void TestHelpers() {
    CHECK(IsHiddenFontFamilyName("@MS Gothic"));
    CHECK(IsHiddenFontFamilyName(".SF NS Text"));
    CHECK(IsHiddenFontFamilyName("LastResort"));
    CHECK(IsHiddenFontFamilyName(""));
    CHECK(!IsHiddenFontFamilyName("Ubuntu"));
    CHECK(!IsHiddenFontFamilyName("MS Gothic"));
    CHECK(IsKnownSymbolFontFamily("Wingdings"));
    CHECK(IsKnownSymbolFontFamily("webdings"));
    CHECK(IsKnownSymbolFontFamily("Symbol"));
    CHECK(!IsKnownSymbolFontFamily("Symbola"));

    // A name its font can spell, and one it cannot.
    auto latin = [](char32_t c) { return c < 0x250; };
    auto symbols = [](char32_t c) { return c >= 0xF020 && c <= 0xF0FF; };
    CHECK(FontCoversItsName("DejaVu Sans 2", latin));
    CHECK(!FontCoversItsName("Wingdings", symbols));
    CHECK(!FontCoversItsName("Noto Sans \xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2", latin));   // Thai letters in the name

    // The sample: the symbol page first, else what is beyond Latin-1, else the start.
    CHECK(ChooseFontSample({0x20, 0x41, 0xF021, 0xF022, 0xF023, 0xF024, 0xF025, 0xF026}) ==
          "\xEF\x80\xA1\xEF\x80\xA2\xEF\x80\xA3\xEF\x80\xA4\xEF\x80\xA5");
    // ... from where 'A' would be, where it has that.
    CHECK(ChooseFontSample({0xF030, 0xF031, 0xF041, 0xF042, 0xF043}) ==
          "\xEF\x81\x81\xEF\x81\x82\xEF\x81\x83\xEF\x80\xB0\xEF\x80\xB1");
    CHECK(ChooseFontSample({0x21, 0x1F300, 0x1F301, 0x1F302, 0x20E3}) == "\xF0\x9F\x8C\x80\xF0\x9F\x8C\x81\xF0\x9F\x8C\x82");
    CHECK(ChooseFontSample({0x21, 0x22, 0x0E01, 0x0E02, 0x0E03}) == "\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x83");
    CHECK(ChooseFontSample({0x20, 0x21, 0x22, 0x23}) == "!\"#");
    CHECK(ChooseFontSample({}).empty());
    CHECK(DecodeUtf8CodePoints("A\xC3\xA9\xE2\x82\xAC") == std::vector<char32_t>({0x41, 0xE9, 0x20AC}));

    // The markup draws exactly the style, escaped.
    InstalledFontStyle semibold;
    semibold.name = "SemiBold Condensed Italic";
    semibold.weight = 600;
    semibold.slant = FontSlant::Italic;
    semibold.stretch = 75;
    const std::string markup = FontStyleMarkup("A&B \"Sans\"", semibold, "<x>");
    CHECK(markup.find("font_family=\"A&amp;B &quot;Sans&quot;\"") != std::string::npos);
    CHECK(markup.find("weight=\"600\"") != std::string::npos);
    CHECK(markup.find("style=\"italic\"") != std::string::npos);
    CHECK(markup.find("stretch=\"condensed\"") != std::string::npos);
    CHECK(markup.find("&lt;x&gt;") != std::string::npos);

    // The styles' order, and what bold and italic reach.
    std::vector<InstalledFontStyle> styles(4);
    styles[0].name = "Bold"; styles[0].weight = 700;
    styles[1].name = "Condensed"; styles[1].stretch = 75;
    styles[2].name = "Italic"; styles[2].slant = FontSlant::Italic;
    styles[3].name = "Regular";
    SortFontStyles(styles);
    CHECK(styles[0].name == "Condensed" && styles[1].name == "Regular" && styles[2].name == "Italic" &&
          styles[3].name == "Bold");
    InstalledFontFamily plain;
    plain.styles = {styles[1], styles[2], styles[3]};
    CHECK(!plain.HasStylesBeyondBoldItalic());
    CHECK(plain.RegularStyle() && plain.RegularStyle()->name == "Regular");
    plain.styles.push_back(styles[0]);
    CHECK(plain.HasStylesBeyondBoldItalic());
    // A narrow family's four styles are all narrow: none beyond.
    InstalledFontFamily narrow;
    narrow.styles = {styles[1], styles[2], styles[3]};
    for (InstalledFontStyle& style : narrow.styles) style.stretch = 87;
    CHECK(!narrow.HasStylesBeyondBoldItalic());
}

static void TestCatalogue() {
    const int registered = RegisterBundledFontsWithoutApplication();
    std::printf("bundled font files registered: %d\n", registered);
    CHECK(registered > 0);
    const auto fonts = GetInstalledFontCatalogue();
    CHECK(fonts != nullptr);
    if (!fonts) return;
    std::printf("families: %zu\n", fonts->size());
    // Many families: the bundled ones alone are six (Ubuntu, Ubuntu Condensed,
    // Ubuntu Mono, Inter, Manrope, Open Sans).
    CHECK(fonts->size() >= 6);
    // Kept: the same list again, until the fonts change.
    CHECK(GetInstalledFontCatalogue() == fonts);

    // A to Z regardless of case, each once, nothing hidden.
    for (size_t i = 1; i < fonts->size(); ++i) CHECK(Lower((*fonts)[i - 1].name) < Lower((*fonts)[i].name));
    std::map<FontFileFormat, int> formats;
    int symbolFonts = 0;
    for (const InstalledFontFamily& family : *fonts) {
        CHECK(!IsHiddenFontFamilyName(family.name));
        CHECK(family.name[0] != '@');
        CHECK(!family.styles.empty());
        ++formats[family.format];
        if (family.symbol) {
            ++symbolFonts;
            std::printf("symbol font: %s, sample \"%s\"\n", family.name.c_str(), family.sample.c_str());
        }
    }
    std::printf("formats: OpenType %d, TrueType %d, Type 1 %d, bitmap %d, unknown %d; symbol fonts %d\n",
                formats[FontFileFormat::OpenType], formats[FontFileFormat::TrueType], formats[FontFileFormat::Type1],
                formats[FontFileFormat::BitmapFont], formats[FontFileFormat::Unknown], symbolFonts);
    // The engine's generic names are no fonts.
    CHECK(!FindInstalledFontFamily(*fonts, "Sans") || !FindInstalledFontFamily(*fonts, "Sans")->styles.empty());

    // The bundled Ubuntu: every style, by its own names, in order.
    const InstalledFontFamily* ubuntu = FindInstalledFontFamily(*fonts, "ubuntu");
    CHECK(ubuntu != nullptr);
    if (ubuntu) {
        CHECK(ubuntu->name == "Ubuntu");
        for (const char* name : {"Thin", "Light", "Regular", "Medium", "Bold", "Light Italic", "Italic", "Medium Italic",
                                 "Bold Italic"})
            CHECK(StyleNamed(*ubuntu, name) != nullptr);
        const InstalledFontStyle* light = StyleNamed(*ubuntu, "Light");
        const InstalledFontStyle* bold = StyleNamed(*ubuntu, "Bold");
        const InstalledFontStyle* italic = StyleNamed(*ubuntu, "Medium Italic");
        // The weights as the engine reads them: fontconfig gives 300, 700
        // and 500 exactly; CoreText's traits come back near them.
        CHECK(light && light->weight < 400 && light->slant == FontSlant::Normal && light->stretch == 100);
        CHECK(bold && bold->weight >= 600);
        CHECK(italic && italic->weight > 430 && italic->weight < 600 && italic->slant == FontSlant::Italic);
#if !defined(__APPLE__)
        CHECK(light && light->weight == 300);
        CHECK(bold && bold->weight == 700);
        CHECK(italic && italic->weight == 500);
#endif
        CHECK(ubuntu->HasStylesBeyondBoldItalic());
        CHECK(ubuntu->RegularStyle() && ubuntu->RegularStyle()->name == "Regular");
        CHECK(ubuntu->format == FontFileFormat::TrueType);
        CHECK(!ubuntu->symbol);
        for (const InstalledFontStyle& style : ubuntu->styles) CHECK(style.format == FontFileFormat::TrueType);
        for (size_t i = 1; i < ubuntu->styles.size(); ++i) {
            const auto& a = ubuntu->styles[i - 1];
            const auto& b = ubuntu->styles[i];
            CHECK(a.stretch < b.stretch || (a.stretch == b.stretch && (a.weight < b.weight ||
                  (a.weight == b.weight && static_cast<int>(a.slant) <= static_cast<int>(b.slant)))));
        }
#if !defined(__APPLE__)
        // The face's own family name, from fontconfig: what a document
        // without weights keeps a light face by.
        CHECK(light && light->faceFamily == "Ubuntu Light");
        CHECK(light && light->file.find("Ubuntu-L.ttf") != std::string::npos);
        // In Ubuntu's four-style naming "Ubuntu Light" holds Light and
        // Medium (as its bold); fontconfig gives that name without bold as
        // Medium, nearer the regular weight in its scale - so Light is not
        // reached by it, Medium is, and Thin by "Ubuntu Thin".
        using Reach = InstalledFontStyle::FaceFamilyReach;
        const InstalledFontStyle* medium = StyleNamed(*ubuntu, "Medium");
        const InstalledFontStyle* thin = StyleNamed(*ubuntu, "Thin");
        CHECK(light && light->faceFamilyReach == Reach::Unreachable);
        CHECK(medium && medium->faceFamily == "Ubuntu Light" && medium->faceFamilyReach == Reach::WithoutBold);
        CHECK(thin && thin->faceFamily == "Ubuntu Thin" && thin->faceFamilyReach == Reach::WithoutBold);
        CHECK(bold && bold->faceFamily.empty());
        const InstalledFontStyle* face = nullptr;
        CHECK(FindInstalledFontFamilyOfName(*fonts, "Ubuntu Light", &face) == ubuntu);
        CHECK(face && face->name == "Medium");   // what the engine draws for that name
        CHECK(FindInstalledFontFamilyOfName(*fonts, "Ubuntu Thin", &face) == ubuntu && face && face->name == "Thin");
        CHECK(IsInstalledFontName("ubuntu light"));
#endif
    }
    CHECK(!IsInstalledFontName("No Such Font Family 0815"));
    CHECK(FindInstalledFontFamily(*fonts, "Ubuntu Mono") != nullptr);
    if (const InstalledFontFamily* mono = FindInstalledFontFamily(*fonts, "Ubuntu Mono")) {
        CHECK(mono->monospace);
        CHECK(mono->styles.size() == 4);
        CHECK(!mono->HasStylesBeyondBoldItalic());   // B and I reach all four: no styles list
    }
    // Inter: one variable file, every weight a named style.
    if (const InstalledFontFamily* inter = FindInstalledFontFamily(*fonts, "Inter")) {
        CHECK(inter->styles.size() > 4);
        CHECK(inter->HasStylesBeyondBoldItalic());
    } else {
        CHECK(!"the bundled Inter is missing");
    }

#if defined(__linux__)
    // One of the fonts fontconfig itself brings on every Ubuntu.
    bool common = false;
    for (const char* name : {"DejaVu Sans", "Liberation Sans", "Bitstream Vera Sans", "FreeSans", "Arimo", "Noto Sans"})
        if (FindInstalledFontFamily(*fonts, name)) { common = true; std::printf("system font present: %s\n", name); }
    CHECK(common);
    if (const InstalledFontFamily* dejavu = FindInstalledFontFamily(*fonts, "DejaVu Sans")) {
        CHECK(dejavu->format == FontFileFormat::TrueType);
        // DejaVu Sans has its condensed faces under their own family name.
        if (const InstalledFontStyle* condensed = StyleNamed(*dejavu, "Condensed")) {
            CHECK(condensed->stretch < 100);
            CHECK(condensed->faceFamily == "DejaVu Sans Condensed");
        }
    }
    // The URW fonts (fonts-urw-base35, where installed): CFF OpenType and
    // Type 1 files of the same faces, and two symbol fonts.
    if (const InstalledFontFamily* nimbus = FindInstalledFontFamily(*fonts, "Nimbus Sans")) {
        CHECK(nimbus->format == FontFileFormat::OpenType || nimbus->format == FontFileFormat::Type1);
        CHECK(nimbus->styles.size() == 4);   // the .otf and the .t1 of a style are one style
    }
    for (const char* name : {"D050000L", "Standard Symbols PS"}) {
        if (const InstalledFontFamily* symbol = FindInstalledFontFamily(*fonts, name)) {
            CHECK(symbol->symbol);
            CHECK(!symbol->sample.empty());
        }
    }
#endif

    // The plain names: the same enumeration, with the generic names.
    const std::vector<std::string> names = GetInstalledFontFamilies();
    CHECK(names.size() >= fonts->size());
    CHECK(std::find(names.begin(), names.end(), "Ubuntu") != names.end());
    for (const std::string& name : names) CHECK(!IsHiddenFontFamilyName(name));
}

int main() {
    TestHelpers();
    TestCatalogue();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
