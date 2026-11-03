// include/UltraCanvasFontCoverage.h
// Whether this computer has a font for a text: the characters no font in the
// process covers - the system's fonts, the bundled ones and any added with
// RegisterFontFile - which the text engine draws as boxes of hex digits, by
// script, with a free font to get for each.
//
// A word processor tells a reader whose computer lacks a document's scripts
// which fonts to install rather than leaving boxes that look like a fault;
// UltraCanvasRichTextEdit does with these (GetFontReport and its notice).
//
// doc-check: example
//   for (const UltraCanvas::MissingScriptFont& missing :
//            UltraCanvas::FindScriptsWithoutFont("你好 مرحبا")) {
//       // missing.script "Chinese", missing.suggestedFont "Noto Sans SC",
//       // missing.downloadUrl the page the font is downloaded from
//   }
//
// Asked afresh on every call, so a font installed or registered meanwhile
// counts. UI thread: the text engine's default font map is that thread's.
//
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // How many characters of `text` no font covers, set in `fontFamily` (the
    // system UI font when empty) with every other font as the fallback, as
    // any text is: 0 when the whole text can be drawn (and for no text), -1
    // when there is no text engine to ask.
    int CountCharactersWithoutFont(const std::string& text, const std::string& fontFamily = {});

    // A writing system a text uses that no font covers.
    struct MissingScriptFont {
        std::string script;          // its English name: "Chinese", "Arabic", ...
        std::string isoCode;         // ISO 15924: "Hani", "Arab", ...
        std::string suggestedFont;   // a free font that covers it: "Noto Sans SC"
        std::string downloadUrl;     // the page that font is downloaded from
        int characters = 0;          // how many characters of the text it is
    };

    // CountCharactersWithoutFont's characters by script, each with a free font
    // that covers it (the Noto family, SIL Open Font License) and its page on
    // Google Fonts; a script without an entry of its own is named by its ISO
    // 15924 code and pointed at the whole Noto family. In the order the text
    // first uses them; empty when every character has a font.
    std::vector<MissingScriptFont> FindScriptsWithoutFont(const std::string& text,
                                                          const std::string& fontFamily = {});

    // Where a font family is found to download: a search for it on Google
    // Fonts. For a family a document names that is not installed.
    std::string FontDownloadUrl(const std::string& family);

    // Whether the text engine knows `family` (case ignored): installed, bundled
    // or registered. A family it does not know is drawn in a substitute.
    bool IsFontFamilyAvailable(const std::string& family);

} // namespace UltraCanvas
