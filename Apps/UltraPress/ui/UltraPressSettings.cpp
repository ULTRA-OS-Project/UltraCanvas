// Apps/UltraPress/ui/UltraPressSettings.cpp
// See UltraPressSettings.h.
// Version: 0.8.4
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressSettings.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRecentList.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const char* kSettingsFile = "settings.json";
const char* kOldLookFile = "look";       // 0.8.0 to 0.8.3
const char* kOldRecentFile = "recent";   // 0.8.2 and 0.8.3
const size_t kKeep = 8;

// One spelling per folder, so "./site" and "site/" are the same entry:
// lexically_normal keeps a trailing separator, so it is dropped here too.
std::string Normal(const std::string& dir) {
    std::string normal = PathToUtf8(PathFromUtf8(dir).lexically_normal());
    while (normal.size() > 1 && (normal.back() == '/' || normal.back() == '\\')) normal.pop_back();
    return normal;
}

} // namespace

std::string UltraPressSettings::Directory() {
    const std::filesystem::path folder = UltraCanvasSettingsFolder();
    if (folder.empty()) return "";
    return PathToUtf8(folder / "UltraPress");
}

UltraPressSettings UltraPressSettings::Load(const std::string& dir) {
    UltraPressSettings s;
    if (dir.empty()) return s;
    const std::filesystem::path folder = PathFromUtf8(dir);
    std::ifstream in(folder / kSettingsFile);
    if (!in) {
        // The two files of 0.8, until the settings are first written.
        std::ifstream look(folder / kOldLookFile);
        std::string word;
        if (look && (look >> word)) s.dark = word == "dark";
        s.recentSites = LoadRecentList(PathToUtf8(folder / kOldRecentFile));
        return s;
    }
    std::stringstream text;
    text << in.rdbuf();
    JSONParseResult result;
    const JSONValue json = JSON::Parse(text.str(), &result);
    if (!result.success || !json.IsObject()) return s;
    s.dark = json["look"].GetString("light") == "dark";
    if (json["recent"].IsArray()) {
        for (const JSONValue& entry : json["recent"].GetElements()) {
            const std::string site = entry.GetString();
            if (!site.empty() && std::find(s.recentSites.begin(), s.recentSites.end(), site) == s.recentSites.end())
                s.recentSites.push_back(site);
        }
    }
    s.windowWidth = static_cast<int>(json["window"]["width"].GetNumber(0));
    s.windowHeight = static_cast<int>(json["window"]["height"].GetNumber(0));
    return s;
}

bool UltraPressSettings::Save(const std::string& dir) const {
    if (dir.empty()) return false;
    const std::filesystem::path folder = PathFromUtf8(dir);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    JSONValue recent = JSONValue::MakeArray();
    for (const std::string& site : recentSites) recent.Append(JSONValue(site));
    JSONValue json = JSONValue::MakeObject()
                         .Set("look", JSONValue(dark ? "dark" : "light"))
                         .Set("recent", std::move(recent))
                         .Set("window", JSONValue::MakeObject()
                                            .Set("width", JSONValue(windowWidth))
                                            .Set("height", JSONValue(windowHeight)));
    JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    std::ofstream out(folder / kSettingsFile, std::ios::trunc);
    if (!out) return false;
    out << JSON::Serialize(json, pretty) << '\n';
    if (!out) return false;
    // The 0.8 files are in here now.
    std::filesystem::remove(folder / kOldLookFile, ec);
    std::filesystem::remove(folder / kOldRecentFile, ec);
    return true;
}

void UltraPressSettings::RememberSite(const std::string& siteDir) {
    if (siteDir.empty()) return;
    const std::string site = Normal(siteDir);
    recentSites.erase(std::remove(recentSites.begin(), recentSites.end(), site), recentSites.end());
    recentSites.insert(recentSites.begin(), site);
    if (recentSites.size() > kKeep) recentSites.resize(kKeep);
}

void UltraPressSettings::ForgetSite(const std::string& siteDir) {
    const std::string site = Normal(siteDir);
    recentSites.erase(std::remove(recentSites.begin(), recentSites.end(), site), recentSites.end());
}

} // namespace UltraPress
