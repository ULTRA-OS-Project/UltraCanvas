// include/UltraCanvasEmojiSettings.h
// Which emoji every UltraCanvas application draws: the bundled colour font
// (Noto Color Emoji), the bundled black-and-white one (Noto Emoji), or the
// emoji font of the system (Segoe UI Emoji on Windows, whatever fontconfig
// prefers on Linux) with the bundled colour font behind it. ULTRA OS settings
// (UOS-Settings, Appearance > Emoji) edits it; an application reads it when it
// starts, so a change shows in the applications started after it.
//
// The fonts are media/emoji/ in the resources dir, kept out of media/fonts/ so
// only the one the setting picks is loaded (and the WebAssembly pages, which
// preload media/fonts/, do not carry 11 MB of emoji). The setting is applied
// to the text engine where it is Pango on fontconfig - Linux, Windows,
// Android. macOS draws its own Apple Color Emoji.
//
// The file is Emoji.conf in the UltraCanvas settings folder:
//   Windows  %APPDATA%\UltraCanvas
//   macOS    ~/Library/Application Support/UltraCanvas
//   others   $XDG_CONFIG_HOME/UltraCanvas, else ~/.config/UltraCanvas
// holding one line, `style=colour`, `style=monochrome` or `style=system`.
//
// doc-check: example
//   UltraCanvas::EmojiSettings settings = UltraCanvas::EmojiSettings::Load();
//   settings.style = UltraCanvas::EmojiStyle::Monochrome;
//   settings.Save();   // the next application started draws black-and-white emoji
//
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>
#include <string>

namespace UltraCanvas {

    enum class EmojiStyle {
        Colour,       // the bundled Noto Color Emoji (the default)
        Monochrome,   // the bundled Noto Emoji, black and white, drawn in the text's colour
        System        // the system's emoji font; the bundled colour one where it has none
    };

    // "colour", "monochrome", "system": the setting as the file writes it.
    const char* EmojiStyleKey(EmojiStyle style);
    // The style a key names; false (and `out` unchanged) for any other text.
    bool ParseEmojiStyle(const std::string& key, EmojiStyle& out);

    struct EmojiSettings {
        EmojiStyle style = EmojiStyle::Colour;

        bool operator==(const EmojiSettings&) const = default;

        // The file's text and back. Parse ignores lines it does not know and
        // keeps the default for a value it does not know.
        static EmojiSettings Parse(const std::string& text);
        std::string Serialize() const;

        // <settings folder>/Emoji.conf; empty when the environment names no
        // settings folder.
        static std::filesystem::path FilePath();
        // A missing or unreadable file is the defaults.
        static EmojiSettings Load();
        // Written beside the file and renamed over it, so an application
        // starting at that moment reads the old setting or the new one.
        bool Save() const;
    };

    // The folder of the bundled emoji fonts: media/emoji/ in the resources
    // dir (GetResourcesDir), absolute and ending in a separator.
    std::string GetBundledEmojiDir();
    // The bundled font file the style loads: NotoColorEmoji.ttf for Colour and
    // System (behind the system's own), NotoEmoji-Regular.ttf for Monochrome.
    std::string BundledEmojiFontFile(EmojiStyle style);
    // The family the style puts first for emoji: "Noto Color Emoji",
    // "Noto Emoji", or "" for System, which leaves the order to the system.
    std::string EmojiFontFamily(EmojiStyle style);
    // The fontconfig rules that make the style's family the one emoji are
    // drawn in, as a <fontconfig> document. Pango asks for the family "emoji"
    // and for a colour font on a run of emoji; the rule puts the style's family
    // first, and for Monochrome withdraws the request for colour, which
    // fontconfig weighs above the family. System has no rule.
    std::string EmojiFontconfigRules(EmojiStyle style);

    // Reads the setting and applies it to the text engine's current font
    // configuration: adds the style's bundled font and its rules. Called by
    // each platform's LoadBundledFontsNative, and again for the full font set
    // that replaces the start's on Windows. Once per configuration; a no-op
    // where the text engine is not fontconfig (macOS, WebAssembly).
    void ApplyEmojiSettings();

} // namespace UltraCanvas
