// Apps/UltraStore/core/StoreSettings.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreSettings.h"

#include "StoreQuery.h"
#include "UltraStoreBuiltin.h"

#include "UltraCanvasAppSettings.h"

#include <algorithm>
#include <cctype>

namespace UltraStore {

namespace {
constexpr const char* kAppName = "UltraStore";

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}
} // namespace

std::vector<std::string> BuiltinServers() {
    return std::vector<std::string>{ULTRASTORE_BUILTIN_SERVERS};
}

std::vector<std::string> BuiltinKeysHex() {
    return std::vector<std::string>{ULTRASTORE_BUILTIN_KEYS};
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t end = text.find('\n', pos);
        std::string line = Trim(text.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        if (!line.empty() && line[0] != '#') out.push_back(line);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

bool IsPublicKeyHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    return FromHex(hex, bytes) && bytes.size() == 32;
}

std::string StoreSettings::Language() const {
    return language.empty() ? SystemLanguage() : language;
}

std::vector<std::string> StoreSettings::Servers() const {
    return servers.empty() ? BuiltinServers() : servers;
}

PublicKeys StoreSettings::TrustedKeys() const {
    PublicKeys keys;
    auto add = [&keys](const std::string& hex) {
        std::vector<uint8_t> key;
        if (!FromHex(Trim(hex), key) || key.size() != 32) return;
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(std::move(key));
    };
    for (const std::string& hex : BuiltinKeysHex()) add(hex);
    for (const std::string& hex : extraKeys) add(hex);
    return keys;
}

StoreSettings StoreSettings::From(const UltraCanvas::UltraCanvasAppSettings& file) {
    StoreSettings s;
    s.channel = file.GetString("catalog.channel", s.channel);
    if (s.channel != "beta") s.channel = "stable";
    s.language = file.GetString("texts.language", s.language);
    s.servers = file.GetList("catalog.servers");
    s.extraKeys = file.GetList("catalog.trustedKeys");
    s.refreshOnStart = file.GetBool("catalog.refreshOnStart", s.refreshOnStart);
    s.windowWidth = file.GetInt("window.width", s.windowWidth);
    s.windowHeight = file.GetInt("window.height", s.windowHeight);
    s.downloadsExpanded = file.GetBool("window.downloadsExpanded", s.downloadsExpanded);
    s.downloadsHeight = file.GetInt("window.downloadsHeight", s.downloadsHeight);
    return s;
}

void StoreSettings::WriteTo(UltraCanvas::UltraCanvasAppSettings& file) const {
    file.Set("catalog.channel", channel);
    file.Set("texts.language", language);
    file.Set("catalog.servers", servers);
    file.Set("catalog.trustedKeys", extraKeys);
    file.Set("catalog.refreshOnStart", refreshOnStart);
    file.Set("window.width", windowWidth);
    file.Set("window.height", windowHeight);
    file.Set("window.downloadsExpanded", downloadsExpanded);
    file.Set("window.downloadsHeight", downloadsHeight);
}

StoreSettings StoreSettings::Load() {
    UltraCanvas::UltraCanvasAppSettings file(kAppName);
    file.Load();
    return From(file);
}

bool StoreSettings::Save() const {
    UltraCanvas::UltraCanvasAppSettings file(kAppName);
    file.Load();
    WriteTo(file);
    return file.Save();
}

} // namespace UltraStore
