// core/UltraCanvasAppearanceSettings.cpp
// Appearance.conf: the text and back, the choices over the host, the file.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#include "UltraCanvasAppearanceSettings.h"
#include "UltraCanvasSettingsFolder.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

    namespace {
        std::string Lower(std::string text) {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
        std::string Number(double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return buffer;
        }
    }

    bool AppearanceSettings::IsSystem() const {
        return look == "system" && accent == "system" && font == "system" && fontSize <= 0.0
            && scale == 1.0 && contrast == "system";
    }

    AppearanceSettings AppearanceSettings::Parse(const std::string& text) {
        AppearanceSettings s;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Lower(SystemFontDetail::Trim(line.substr(0, eq)));
            const std::string value = SystemFontDetail::Trim(line.substr(eq + 1));
            const std::string lower = Lower(value);
            if (key == "look") {
                if (lower == "system" || lower == "light" || lower == "dark") s.look = lower;
            } else if (key == "accent") {
                Color color;
                if (lower == "system") s.accent = "system";
                else if (ColorFromHex(value, color)) s.accent = HexOfColor(color);
            } else if (key == "font") {
                if (lower == "system") s.font = "system";
                else if (!value.empty()) s.font = value;
            } else if (key == "fontsize") {
                double points = 0;
                if (lower == "system") s.fontSize = 0.0;
                else if (SystemFontDetail::ParseSize(value, points) && points > 0 && points <= 200) s.fontSize = points;
            } else if (key == "scale") {
                double factor = 0;
                if (SystemFontDetail::ParseSize(value, factor) && factor >= 0.5 && factor <= 4.0) s.scale = factor;
            } else if (key == "contrast") {
                if (lower == "system" || lower == "high" || lower == "normal") s.contrast = lower;
            }
        }
        return s;
    }

    std::string AppearanceSettings::Serialize() const {
        std::string out;
        out += "look=" + look + "\n";
        out += "accent=" + accent + "\n";
        out += "font=" + font + "\n";
        out += "fontsize=" + (fontSize > 0.0 ? Number(fontSize) : std::string("system")) + "\n";
        out += "scale=" + Number(scale) + "\n";
        out += "contrast=" + contrast + "\n";
        return out;
    }

    bool AppearanceSettings::ApplyTo(HostAppearance& host) const {
        bool applied = false;
        if (look == "light" || look == "dark") { host.dark = look == "dark"; applied = true; }
        if (accent != "system") {
            Color color;
            if (ColorFromHex(accent, color)) { host.accent = color; applied = true; }
        }
        if (font != "system" && !font.empty()) { host.uiFont.fontFamily = font; applied = true; }
        if (fontSize > 0.0) { host.uiFont.fontSize = fontSize; applied = true; }
        if (scale != 1.0 && scale >= 0.5 && scale <= 4.0) { host.textScale *= scale; applied = true; }
        if (contrast == "high" || contrast == "normal") { host.highContrast = contrast == "high"; applied = true; }
        if (applied) host.source += "+settings";
        return applied;
    }

    std::filesystem::path AppearanceSettings::FilePath() {
        const std::filesystem::path folder = UltraCanvasSettingsFolder();
        if (folder.empty()) return {};
        return folder / "Appearance.conf";
    }

    AppearanceSettings AppearanceSettings::Load() {
        const std::filesystem::path path = FilePath();
        if (path.empty()) return {};
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};   // no file yet: the host's look
        std::ostringstream text;
        text << in.rdbuf();
        return Parse(text.str());
    }

    bool AppearanceSettings::Save() const {
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

} // namespace UltraCanvas
