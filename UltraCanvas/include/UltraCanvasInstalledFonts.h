// include/UltraCanvasInstalledFonts.h
// The fonts installed on this computer, as the text engine sees them: every
// family, each with its styles - weight, slant and width, named as the font
// names them - and the format of its files (OpenType, TrueType, Type 1,
// bitmap). It is what a font list (UltraCanvasFontPicker) shows, and what
// GetInstalledFontFamilies() (UltraCanvasApplication.h) reads its names from.
//
// One enumeration, the text engine's own (Pango's font map), so every name
// in it is one the renderer draws: fontconfig behind it on Linux, the BSDs,
// Android and Windows - where the framework hands fontconfig the system's
// font folder and the user's own, %LOCALAPPDATA%\Microsoft\Windows\Fonts,
// as DirectWrite reads both - and CoreText on macOS. The format, the file
// and what the font can draw come from the same engine underneath:
// fontconfig's records on the fontconfig platforms, CoreText's font
// descriptors on macOS (OS/MacOS/UltraCanvasMacOSInstalledFonts.mm).
//
// Left out, as other programs leave them out: Windows' vertical families
// (a name starting with '@'), macOS' private system families (a name
// starting with '.') and LastResort, the generic names the engine adds
// ("Sans", "Serif", "Monospace": every face of them synthesized), a face
// the engine only synthesizes (an italic slanted from the upright), and a
// family with no characters at all.
//
// The catalogue is made once and kept until the fonts change - a font
// registered (RegisterFontFile), the system fonts that arrive after a cold
// start on Windows - that is, until GetFontSetGeneration() moves. Making it
// asks the engine's caches, not the font files: a few tens of milliseconds
// for a thousand fonts. Call it on the UI thread, like everything that sets
// text.
//
// doc-check: example
//   auto fonts = UltraCanvas::GetInstalledFontCatalogue();
//   for (const UltraCanvas::InstalledFontFamily& family : *fonts) {
//       if (family.styles.size() > 1) { /* a styles submenu */ }
//   }
//   const UltraCanvas::InstalledFontFamily* ubuntu = UltraCanvas::FindInstalledFontFamily(*fonts, "ubuntu");
//   (void)ubuntu;
//
// Version: 1.0.0 - the catalogue: families, styles (weight, slant, width, the face's own family name),
//                  formats, symbol fonts and a sample of what they draw; hidden families left out
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFontFile.h"        // FontFileFormat, FontFormatName
#include "UltraCanvasRenderContext.h"   // FontSlant

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // A font's format is the font file module's FontFileFormat
    // (UltraCanvasFontFile.h): OpenType for CFF outlines or any .otf / .otc,
    // TrueType for TrueType outlines in a .ttf / .ttc, Type1 for Adobe's
    // PostScript fonts (.pfb, .t1, CID-keyed too), WOFF / WOFF2 for a web
    // font, BitmapFont for fixed pixel sizes (.pcf, .bdf, .fon); Unknown when
    // the engine did not say. FontFormatName names it.

    struct InstalledFontStyle {
        // The style as the font names it: "Regular", "SemiBold Italic",
        // "Condensed Light", "Book", "Demi Oblique".
        std::string name;
        int weight = 400;                    // CSS: 100 thin ... 400 regular ... 700 bold ... 1000
        FontSlant slant = FontSlant::Normal;
        int stretch = 100;                   // per cent of the normal width, CSS font-stretch: 50 ... 200
        FontFileFormat format = FontFileFormat::Unknown;
        std::string file;                    // the font's file, where the engine tells it; may be empty
        // The face's own family name, from the fonts' four-style naming
        // that Windows and Word use - "Ubuntu Light", "DejaVu Sans
        // Condensed", "Arial Black" - where it has one; empty where it has
        // none, and on macOS, whose text engine takes no such names. In
        // that naming a family has a regular and a bold face: "Ubuntu
        // Light"'s bold is Ubuntu Medium. A document that has bold and
        // italic but no weights keeps a light or a condensed face by this
        // name, with bold as faceFamilyReach says.
        std::string faceFamily;
        // How the text engine reaches this face by faceFamily: with bold
        // off, with bold on, or not at all - when another face of that name
        // is as near to the weight asked for (fontconfig gives "Ubuntu
        // Light" without bold as Medium, which is nearer the regular weight
        // than Light is).
        enum class FaceFamilyReach { Unreachable, WithoutBold, WithBold };
        FaceFamilyReach faceFamilyReach = FaceFamilyReach::Unreachable;

        // The regular weight - 400, or a Book within 30 of it (380) - upright, at the normal width.
        bool IsRegular() const { return IsNormalWeight() && slant == FontSlant::Normal && stretch == 100; }
        // What bold and italic alone reach: the regular weight or bold
        // (700, within 30) at the normal width.
        bool IsBoldItalicVariant() const {
            return (IsNormalWeight() || (weight >= 670 && weight <= 730)) && stretch == 100;
        }
        bool IsNormalWeight() const { return weight >= 370 && weight <= 430; }
    };

    struct InstalledFontFamily {
        std::string name;
        // Narrow to wide, light to heavy, upright before italic.
        std::vector<InstalledFontStyle> styles;
        FontFileFormat format = FontFileFormat::Unknown;   // its regular style's (or its first's)
        bool scalable = true;    // false: bitmaps at fixed sizes alone
        bool monospace = false;
        bool variable = false;   // a variable font (one file, every weight)
        // Its own letters cannot spell its name: a symbol or dingbat font
        // (Wingdings, Symbol, Webdings, an icon font) or a font of a script
        // other than its name's. A font list names it in the interface's
        // font and shows `sample` in the font itself.
        bool symbol = false;
        std::string sample;      // UTF-8: a few characters the font draws, for a symbol font

        // The style closest to the regular one; null for a family without styles.
        const InstalledFontStyle* RegularStyle() const;
        // A style bold and italic cannot reach: a weight other than 400 and
        // 700, or a width other than the normal one. A family of Regular,
        // Bold, Italic and Bold Italic alone has none.
        bool HasStylesBeyondBoldItalic() const;
    };

    // Every installed family, the hidden ones left out, A to Z regardless of
    // case, each name once. Kept until GetFontSetGeneration() moves; the
    // pointer stays valid however often it is made again.
    std::shared_ptr<const std::vector<InstalledFontFamily>> GetInstalledFontCatalogue();

    // The family of that name in `families` (case ignored); null when none.
    const InstalledFontFamily* FindInstalledFontFamily(const std::vector<InstalledFontFamily>& families,
                                                       const std::string& name);

    // The family a name belongs to: a family's own name, or the name of one
    // of its faces (InstalledFontStyle::faceFamily, "Ubuntu Thin"), with
    // the face the engine draws for that name in `face` ("Ubuntu Light" is
    // Ubuntu's Medium: see faceFamilyReach). Null when neither is
    // installed. A document that names "Segoe UI Semibold" finds Segoe UI
    // and its Semibold style.
    const InstalledFontFamily* FindInstalledFontFamilyOfName(const std::vector<InstalledFontFamily>& families,
                                                             const std::string& name,
                                                             const InstalledFontStyle** face = nullptr);
    // Whether the text engine has a font of this name, as a family or as a
    // face's own family name (GetInstalledFontCatalogue).
    bool IsInstalledFontName(const std::string& name);

    // Whether a family of this name is one font lists leave out: Windows'
    // vertical families ("@MS Gothic"), macOS' private ones (".SF NS"),
    // LastResort, an empty name.
    bool IsHiddenFontFamilyName(const std::string& name);

    // Whether a family of this name is a known symbol or dingbat font
    // (Wingdings, Webdings, Symbol, Marlett, Zapf Dingbats, the Segoe icon
    // fonts ...), for when the engine cannot tell from its characters.
    bool IsKnownSymbolFontFamily(const std::string& name);

    // Sorts styles as InstalledFontFamily::styles keeps them.
    void SortFontStyles(std::vector<InstalledFontStyle>& styles);

    // Pango's markup for `text` in exactly this style of `family` - weight,
    // slant and width - escaped, for a layout made with markup on. A style
    // the render context's four weights cannot say (SemiBold, Thin,
    // Condensed) is drawn so.
    std::string FontStyleMarkup(const std::string& family, const InstalledFontStyle& style, const std::string& text);

    // ===== FOR THE PLATFORM LAYER =====
    // What the text engine's list of families leaves out, filled in from
    // the engine underneath it, once per text engine: fontconfig's records
    // (core/UltraCanvasInstalledFonts.cpp, every platform but macOS) or
    // CoreText's (OS/MacOS/UltraCanvasMacOSInstalledFonts.mm). For each
    // style its format, file and faceFamily; for each family its format,
    // `symbol` and `sample` (with the two helpers below); and the families
    // the engine knows to have no characters at all are removed.
    void FillNativeFontDetails(std::vector<InstalledFontFamily>& families);

    // Whether a family's characters spell its name: `covers` says whether
    // the font has a character (a code point). Spaces, digits and
    // punctuation in the name are not asked.
    template <typename Covers>
    bool FontCoversItsName(const std::string& name, Covers covers);
    // A few characters to show a symbol font by, chosen from `covered` (the
    // code points it has, ascending - the first few thousand are enough):
    // its symbols in the private-use page a symbol font is mapped to
    // (U+F020 to U+F0FF), else its pictographs (an emoji font), else its
    // first letters and signs above Latin-1 (no punctuation or marks),
    // else its first printable ones. UTF-8; empty when it has none.
    std::string ChooseFontSample(const std::vector<char32_t>& covered);
    // The code points of a UTF-8 text.
    std::vector<char32_t> DecodeUtf8CodePoints(const std::string& text);

    template <typename Covers>
    bool FontCoversItsName(const std::string& name, Covers covers) {
        bool any = false;
        for (char32_t c : DecodeUtf8CodePoints(name)) {
            const bool asciiLetter = (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z');
            if (c < 0x80 && !asciiLetter) continue;   // digits, spaces, punctuation
            any = true;
            if (!covers(c)) return false;
        }
        return any || !name.empty();
    }

} // namespace UltraCanvas
