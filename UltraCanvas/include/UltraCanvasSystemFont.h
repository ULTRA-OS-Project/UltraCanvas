// include/UltraCanvasSystemFont.h
// The pure half of finding the desktop's interface font: the parsers for
// the forms the desktops write it in, so a test holds every platform's
// rule without a desktop, and the platform code under OS/<Platform>/ only
// fetches the text. The font itself is UltraCanvasApplication's
// GetSystemFontStyle() / GetDefaultMonospacedFontStyle(), which the
// platform's DetectSystemFontStyleNative fills from these:
//   Linux / BSD   GSettings org.gnome.desktop.interface font-name and
//                 monospace-font-name ("Cantarell 11"), ~/.config/kdeglobals
//                 [General] font= and fixed= ("Noto Sans,10,-1,5,50,..."),
//                 ~/.config/gtk-4.0 and gtk-3.0/settings.ini gtk-font-name;
//                 KDE's file first on a KDE session, GNOME's otherwise
//   Windows       SPI_GETNONCLIENTMETRICS lfMessageFont (Segoe UI, 9 pt at
//                 96 dpi), Consolas for code
//   macOS         [NSFont systemFontOfSize:0] (AppKit, in the .mm)
// and the bundled Ubuntu / Ubuntu Mono at 12 pt where none of those answer.
// Sizes are in points, as the text engine takes them.
//
// Header-only: nothing here needs the core.
//
// doc-check: example
//   std::string family; double points = 0;
//   UltraCanvas::ParseGtkFontName("Noto Sans Bold 10.5", family, points);              // "Noto Sans", 10.5
//   UltraCanvas::ParseKdeFontEntry("Noto Sans,10,-1,5,50,0,0,0,0,0", family, points);  // "Noto Sans", 10
//   const std::string value = UltraCanvas::ReadIniValue("[Settings]\ngtk-font-name=Cantarell 11\n", "Settings", "gtk-font-name");
//
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace UltraCanvas {

    namespace SystemFontDetail {
        inline std::string Trim(const std::string& s) {
            size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return s.substr(b, e - b);
        }
        inline std::string Lower(std::string s) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        inline std::string Unquote(const std::string& s) {
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
                return s.substr(1, s.size() - 2);
            return s;
        }
        // "10", "10.5", "12px" - dot-decimal, as Pango writes it, whatever the
        // locale; never strtod, which honours LC_NUMERIC.
        inline bool ParseSize(const std::string& token, double& points) {
            std::string digits = Lower(token);
            bool pixels = false;
            if (digits.size() > 2 && digits.compare(digits.size() - 2, 2, "px") == 0) {
                pixels = true;
                digits.erase(digits.size() - 2);
            }
            if (digits.empty()) return false;
            double value = 0, scale = 1;
            bool fraction = false, any = false;
            for (char c : digits) {
                if (c == '.') { if (fraction) return false; fraction = true; continue; }
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                any = true;
                if (fraction) { scale /= 10; value += (c - '0') * scale; }
                else value = value * 10 + (c - '0');
            }
            if (!any || value <= 0) return false;
            points = pixels ? value * 72.0 / 96.0 : value;
            return true;
        }
        // The words Pango reads as a style, not a family, so "DejaVu Sans Mono
        // Book 9" is the family "DejaVu Sans Mono".
        inline bool IsStyleWord(const std::string& word) {
            static const char* const kWords[] = {
                "regular", "book", "normal", "roman", "medium", "light", "thin", "heavy", "black",
                "bold", "semibold", "semi-bold", "demibold", "demi-bold", "extrabold", "extra-bold",
                "ultrabold", "ultra-bold", "extralight", "extra-light", "ultralight", "ultra-light",
                "ultraheavy", "ultra-heavy", "italic", "oblique", "condensed", "semi-condensed",
                "semicondensed", "extra-condensed", "ultra-condensed", "expanded", "semi-expanded",
                "extra-expanded", "ultra-expanded", "small-caps"};
            const std::string lower = Lower(word);
            for (const char* w : kWords) if (lower == w) return true;
            return false;
        }
    }

    // "Cantarell 11", "Noto Sans Bold 10.5", "DejaVu Sans Mono Book 9",
    // "Sans 12px": the GTK and Pango form - the family, then style words,
    // then the size. True with `family` and `points` set when the text has a
    // family and a size; false, both unchanged, otherwise. Pure.
    inline bool ParseGtkFontName(const std::string& text, std::string& family, double& points) {
        std::vector<std::string> words;
        std::string current;
        for (char c : SystemFontDetail::Unquote(SystemFontDetail::Trim(text))) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) { words.push_back(current); current.clear(); }
            } else {
                current += c;
            }
        }
        if (!current.empty()) words.push_back(current);
        if (words.size() < 2) return false;
        double size = 0;
        if (!SystemFontDetail::ParseSize(words.back(), size)) return false;
        words.pop_back();
        while (words.size() > 1 && SystemFontDetail::IsStyleWord(words.back())) words.pop_back();
        std::string name;
        for (const std::string& w : words) { if (!name.empty()) name += ' '; name += w; }
        if (name.empty()) return false;
        family = name;
        points = size;
        return true;
    }

    // "Noto Sans,10,-1,5,50,0,0,0,0,0" and the longer form that ends in the
    // style name: the kdeglobals form, the family then the point size. True
    // with `family` and `points` set when both are there. Pure.
    inline bool ParseKdeFontEntry(const std::string& text, std::string& family, double& points) {
        const std::string value = SystemFontDetail::Trim(text);
        const size_t comma = value.find(',');
        if (comma == std::string::npos) return false;
        const std::string name = SystemFontDetail::Trim(value.substr(0, comma));
        const size_t next = value.find(',', comma + 1);
        const std::string sizeText = SystemFontDetail::Trim(value.substr(comma + 1, next == std::string::npos ? std::string::npos : next - comma - 1));
        double size = 0;
        if (name.empty() || !SystemFontDetail::ParseSize(sizeText, size)) return false;
        family = name;
        points = size;
        return true;
    }

    // The value of `key` in an .ini text - "key=value" lines under
    // "[section]" headings, comments with # or ; - within `section`, or in
    // any section when `section` is empty. Quotes around the value are
    // removed (the GTK 2 file quotes). Empty when the key is not there. Pure.
    inline std::string ReadIniValue(const std::string& text, const std::string& section, const std::string& key) {
        bool inSection = section.empty();
        std::string line;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != '\n') { line += text[i]; continue; }
            const std::string s = SystemFontDetail::Trim(line);
            line.clear();
            if (s.empty() || s[0] == '#' || s[0] == ';') continue;
            if (s[0] == '[') {
                const size_t close = s.find(']');
                const std::string name = close == std::string::npos ? s.substr(1) : s.substr(1, close - 1);
                inSection = section.empty() || SystemFontDetail::Trim(name) == section;
                continue;
            }
            if (!inSection) continue;
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            if (SystemFontDetail::Trim(s.substr(0, eq)) == key) return SystemFontDetail::Unquote(SystemFontDetail::Trim(s.substr(eq + 1)));
        }
        return {};
    }

}  // namespace UltraCanvas
