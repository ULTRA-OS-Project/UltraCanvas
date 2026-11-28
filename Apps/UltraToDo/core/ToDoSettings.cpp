// Apps/UltraToDo/core/ToDoSettings.cpp
// See ToDoSettings.h.
// Version: 0.2.0 - display: "full" or "compact"
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ToDoSettings.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraToDo {

namespace {
const char* const kSettingsFileName = "settings.json";

const char* LevelName(ManagementLevel level) {
    return level == ManagementLevel::Supervised ? "supervised" : "simple";
}
} // namespace

ToDoSettings ToDoSettings::Load(const std::string& folder) {
    ToDoSettings settings;
    const fs::path file = PathFromUtf8(folder) / kSettingsFileName;
    std::error_code ec;
    if (!fs::exists(file, ec)) return settings;
    const JSONValue root = UltraCanvas::JSON::ParseFile(PathToUtf8(file));
    if (!root.IsObject()) return settings;
    if (root.Get("management").GetString() == "supervised") settings.management = ManagementLevel::Supervised;
    if (root.Get("display").GetString() == "compact") settings.display = DisplayMode::Compact;
    return settings;
}

bool ToDoSettings::Save(const std::string& folder, std::string& error) const {
    error.clear();
    std::error_code ec;
    fs::create_directories(PathFromUtf8(folder), ec);
    JSONValue root = JSONValue::MakeObject();
    root.Set("management", LevelName(management));
    root.Set("display", display == DisplayMode::Compact ? "compact" : "full");
    UltraCanvas::JSONSerializeOptions options;
    options.pretty = true;
    options.indentWidth = 2;
    const fs::path file = PathFromUtf8(folder) / kSettingsFileName;
    if (!UltraCanvas::JSON::SerializeToFile(PathToUtf8(file), root, options)) {
        error = "cannot write " + PathToUtf8(file);
        return false;
    }
    return true;
}

} // namespace UltraToDo
