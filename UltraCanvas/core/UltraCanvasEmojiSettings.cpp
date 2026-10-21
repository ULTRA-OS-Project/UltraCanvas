// core/UltraCanvasEmojiSettings.cpp
// Emoji.conf, the bundled emoji fonts, and the fontconfig rules that pick one.
// See UltraCanvasEmojiSettings.h.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasEmojiSettings.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasConfig.h"         // GetResourcesDir
#include "UltraCanvasUtils.h"          // NormalizePath
#include "UltraCanvasDebug.h"
#include "UltraCanvasPathUtf8.h"

#if !defined(__APPLE__)
#include <fontconfig/fontconfig.h>
#endif

#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

    namespace {
        constexpr const char* kColourFile     = "NotoColorEmoji.ttf";
        constexpr const char* kMonochromeFile = "NotoEmoji-Regular.ttf";
        constexpr const char* kColourFamily     = "Noto Color Emoji";
        constexpr const char* kMonochromeFamily = "Noto Emoji";
    } // namespace

    const char* EmojiStyleKey(EmojiStyle style) {
        switch (style) {
            case EmojiStyle::Monochrome: return "monochrome";
            case EmojiStyle::System:     return "system";
            case EmojiStyle::Colour:     break;
        }
        return "colour";
    }

    bool ParseEmojiStyle(const std::string& key, EmojiStyle& out) {
        if (key == "colour" || key == "color") { out = EmojiStyle::Colour; return true; }
        if (key == "monochrome") { out = EmojiStyle::Monochrome; return true; }
        if (key == "system") { out = EmojiStyle::System; return true; }
        return false;
    }

    EmojiSettings EmojiSettings::Parse(const std::string& text) {
        EmojiSettings s;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (line.substr(0, eq) == "style") ParseEmojiStyle(line.substr(eq + 1), s.style);
        }
        return s;
    }

    std::string EmojiSettings::Serialize() const {
        return std::string("style=") + EmojiStyleKey(style) + "\n";
    }

    std::filesystem::path EmojiSettings::FilePath() {
        const std::filesystem::path folder = UltraCanvasSettingsFolder();
        if (folder.empty()) return {};
        return folder / "Emoji.conf";
    }

    EmojiSettings EmojiSettings::Load() {
        const std::filesystem::path path = FilePath();
        if (path.empty()) return {};
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};   // no file yet: the default
        std::ostringstream text;
        text << in.rdbuf();
        return Parse(text.str());
    }

    bool EmojiSettings::Save() const {
        const std::filesystem::path path = FilePath();
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::path temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::trunc | std::ios::binary);
            if (!out) return false;
            out << Serialize();
            if (!out) return false;
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            return false;
        }
        return true;
    }

    std::string GetBundledEmojiDir() {
        return NormalizePath(GetResourcesDir() + "media/emoji/");
    }

    std::string BundledEmojiFontFile(EmojiStyle style) {
        return GetBundledEmojiDir() + (style == EmojiStyle::Monochrome ? kMonochromeFile : kColourFile);
    }

    std::string EmojiFontFamily(EmojiStyle style) {
        switch (style) {
            case EmojiStyle::Colour:     return kColourFamily;
            case EmojiStyle::Monochrome: return kMonochromeFamily;
            case EmojiStyle::System:     break;
        }
        return {};
    }

    std::string EmojiFontconfigRules(EmojiStyle style) {
        const std::string family = EmojiFontFamily(style);
        if (family.empty()) return {};
        // prepend_first: ahead of every family the system's own aliases put
        // before "emoji" (60-generic.conf lists Noto Color Emoji first).
        std::string rules =
            "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
            "<fontconfig>\n"
            "  <match target=\"pattern\">\n"
            "    <test name=\"family\"><string>emoji</string></test>\n"
            "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\"><string>" + family +
            "</string></edit>\n";
        // Pango asks for a colour font on an emoji run, and fontconfig ranks
        // that above the family: a colour font installed on the system would
        // otherwise still win over the black-and-white one.
        if (style == EmojiStyle::Monochrome)
            rules += "    <edit name=\"color\" mode=\"assign\" binding=\"strong\"><bool>false</bool></edit>\n";
        rules += "  </match>\n"
                 "</fontconfig>\n";
        return rules;
    }

    void ApplyEmojiSettings() {
#if !defined(__APPLE__)
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        FcConfig* config = FcConfigGetCurrent();
        if (!config) return;

        const EmojiStyle style = EmojiSettings::Load().style;
        const std::string file = BundledEmojiFontFile(style);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(PathFromUtf8(file), ec)) {
            // A build that ships without media/emoji (the WebAssembly pages):
            // the system's emoji, as before there were bundled ones.
            debugOutput << "UltraCanvas: bundled emoji font missing: " << file << std::endl;
            return;
        }

        // Once per configuration: the font is then one of its application fonts.
        if (FcFontSet* appFonts = FcConfigGetFonts(config, FcSetApplication)) {
            for (int i = 0; i < appFonts->nfont; ++i) {
                FcChar8* added = nullptr;
                if (FcPatternGetString(appFonts->fonts[i], FC_FILE, 0, &added) == FcResultMatch && added &&
                    std::strcmp(reinterpret_cast<const char*>(added), file.c_str()) == 0)
                    return;
            }
        }
        if (!FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(file.c_str()))) {
            debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for " << file << std::endl;
            return;
        }
        const std::string rules = EmojiFontconfigRules(style);
        if (!rules.empty() &&
            !FcConfigParseAndLoadFromMemory(config, reinterpret_cast<const FcChar8*>(rules.c_str()), FcFalse)) {
            debugOutput << "UltraCanvas: the emoji font rules did not load" << std::endl;
        }
        debugOutput << "UltraCanvas: emoji: " << EmojiStyleKey(style) << " (" << file << ")" << std::endl;
#endif
    }

} // namespace UltraCanvas
