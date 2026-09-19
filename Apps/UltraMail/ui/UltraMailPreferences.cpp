// Apps/UltraMail/ui/UltraMailPreferences.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPreferences.h"

#include <cctype>
#include <fstream>
#include <string>

namespace UltraMail {

namespace {

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// "true"/"1"/"yes"/"on" (case-insensitive) → true; anything else → false.
bool ParseBool(const std::string& v) {
    std::string t = Trim(v);
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "true" || t == "1" || t == "yes" || t == "on";
}

} // namespace

bool Preferences::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;   // absent file: caller keeps defaults

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments and blank lines; split on the first '='.
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' ||
            trimmed[0] == '[') {
            continue;   // comments and any [section] headers are ignored
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = Trim(trimmed.substr(0, eq));
        const std::string value = trimmed.substr(eq + 1);
        if (key == "reading_pane")       showReadingPane  = ParseBool(value);
        if (key == "fetch_sender_icons") fetchSenderIcons = ParseBool(value);
    }
    return true;
}

bool Preferences::Save(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;
    file << "# UltraMail preferences — view options remembered between runs.\n";
    file << "reading_pane = " << (showReadingPane ? "true" : "false") << "\n";
    file << "fetch_sender_icons = " << (fetchSenderIcons ? "true" : "false") << "\n";
    return static_cast<bool>(file);
}

} // namespace UltraMail
