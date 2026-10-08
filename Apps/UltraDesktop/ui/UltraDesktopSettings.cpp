// Apps/UltraDesktop/ui/UltraDesktopSettings.cpp
// The desktop's settings file, through the framework's JSON (UltraCanvasJSON).
// Numbers in the file are dot-decimal by construction: the JSON writer and
// reader never consult the locale.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopSettings.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace UltraDesktop {

const char* TaskbarEdgeName(TaskbarEdge edge) {
    switch (edge) {
        case TaskbarEdge::Top:    return "top";
        case TaskbarEdge::Bottom: return "bottom";
        case TaskbarEdge::Left:
        default:                  return "left";
    }
}

bool ParseTaskbarEdge(const std::string& text, TaskbarEdge& out) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "left")   { out = TaskbarEdge::Left;   return true; }
    if (lower == "top")    { out = TaskbarEdge::Top;    return true; }
    if (lower == "bottom") { out = TaskbarEdge::Bottom; return true; }
    return false;
}

std::string DesktopSettings::DefaultPath() {
    fs::path root;
#if defined(_WIN32)
    // UTF-8 from the wide environment, as PathFromUtf8 expects: getenv would
    // answer in the ANSI code page and miss a profile folder named outside it.
    if (const std::string appData = GetEnvUtf8("APPDATA"); !appData.empty()) root = PathFromUtf8(appData);
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) root = PathFromUtf8(xdg);
    else if (const char* home = std::getenv("HOME"); home && *home) root = PathFromUtf8(home) / ".config";
#endif
    if (root.empty()) return std::string();
    return PathToUtf8(root / "ultraos" / "desktop.json");
}

bool DesktopSettings::Load(const std::string& path, std::string* error) {
    if (path.empty()) return true;
    std::error_code ec;
    if (!fs::is_regular_file(PathFromUtf8(path), ec)) return true;   // first run: defaults
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) {
        if (error) *error = "Could not read \"" + path + "\".";
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return FromJson(buffer.str(), error);
}

bool DesktopSettings::Save(const std::string& path, std::string* error) const {
    if (path.empty()) {
        if (error) *error = "There is no home directory to keep the settings in.";
        return false;
    }
    std::error_code ec;
    const fs::path target = PathFromUtf8(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create \"" + PathToUtf8(target.parent_path()) + "\": " + ec.message();
        return false;
    }
    // Written beside the target and renamed over it, so a crash mid-write
    // never leaves half a settings file behind.
    const fs::path temp = target.parent_path() / PathFromUtf8(PathToUtf8(target.filename()) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "Could not write \"" + PathToUtf8(temp) + "\".";
            return false;
        }
        out << ToJson();
    }
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        if (error) *error = "Could not replace \"" + path + "\": " + ec.message();
        return false;
    }
    return true;
}

std::string DesktopSettings::ToJson() const {
    JSONValue root = JSONValue::MakeObject();
    root.Set("taskbarEdge", TaskbarEdgeName(taskbarEdge));
    root.Set("wallpaper", wallpaper);
    root.Set("ramDiscPath", ramDiscPath);
    root.Set("filerProgram", filerProgram);
    root.Set("virtualDesktops", virtualDesktops);
    root.Set("stickerboardVisible", stickerboardVisible);
    JSONValue notes = JSONValue::MakeArray();
    for (const Sticker& s : stickers) {
        JSONValue note = JSONValue::MakeObject();
        note.Set("id", s.id);
        note.Set("text", s.text);
        note.Set("x", static_cast<double>(s.x));
        note.Set("y", static_cast<double>(s.y));
        note.Set("width", static_cast<double>(s.width));
        note.Set("height", static_cast<double>(s.height));
        note.Set("color", s.color);
        notes.Append(std::move(note));
    }
    root.Set("stickers", std::move(notes));
    JSONSerializeOptions options;
    options.pretty = true;
    return JSON::Serialize(root, options);
}

bool DesktopSettings::FromJson(const std::string& json, std::string* error) {
    JSONParseResult result;
    JSONValue root = JSON::Parse(json, &result);
    if (!result.success || !root.IsObject()) {
        if (error) *error = result.errorMessage.empty() ? "The settings file is not a JSON object."
                                                        : result.errorMessage;
        return false;
    }
    DesktopSettings loaded;
    TaskbarEdge edge;
    if (ParseTaskbarEdge(root.Get("taskbarEdge").GetString(), edge)) loaded.taskbarEdge = edge;
    loaded.wallpaper = root.Get("wallpaper").GetString();
    loaded.ramDiscPath = root.Get("ramDiscPath").GetString(loaded.ramDiscPath);
    loaded.filerProgram = root.Get("filerProgram").GetString(loaded.filerProgram);
    const int desktops = static_cast<int>(root.Get("virtualDesktops").GetInteger(loaded.virtualDesktops));
    loaded.virtualDesktops = std::clamp(desktops, 1, 9);
    loaded.stickerboardVisible = root.Get("stickerboardVisible").GetBoolean(false);
    const JSONValue& notes = root.Get("stickers");
    if (notes.IsArray()) {
        for (size_t i = 0; i < notes.GetSize(); ++i) {
            const JSONValue& note = notes.At(i);
            if (!note.IsObject()) continue;
            Sticker s;
            s.id = note.Get("id").GetString();
            if (s.id.empty()) s.id = loaded.NewStickerId();
            s.text = note.Get("text").GetString();
            s.x = static_cast<float>(note.Get("x").GetNumber(s.x));
            s.y = static_cast<float>(note.Get("y").GetNumber(s.y));
            s.width = static_cast<float>(note.Get("width").GetNumber(s.width));
            s.height = static_cast<float>(note.Get("height").GetNumber(s.height));
            s.color = note.Get("color").GetString(s.color);
            loaded.stickers.push_back(std::move(s));
        }
    }
    *this = std::move(loaded);
    return true;
}

Sticker* DesktopSettings::FindSticker(const std::string& id) {
    for (Sticker& s : stickers) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

void DesktopSettings::RemoveSticker(const std::string& id) {
    stickers.erase(std::remove_if(stickers.begin(), stickers.end(),
                                  [&id](const Sticker& s) { return s.id == id; }),
                   stickers.end());
}

std::string DesktopSettings::NewStickerId() const {
    int next = 1;
    for (;;) {
        const std::string candidate = "note" + std::to_string(next);
        bool taken = false;
        for (const Sticker& s : stickers) {
            if (s.id == candidate) { taken = true; break; }
        }
        if (!taken) return candidate;
        ++next;
    }
}

} // namespace UltraDesktop
