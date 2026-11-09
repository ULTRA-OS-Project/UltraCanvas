// Apps/UltraDesktop/ui/UltraDesktopSettings.cpp
// The desktop's settings file, through the framework's JSON (UltraCanvasJSON).
// Numbers in the file are dot-decimal by construction: the JSON writer and
// reader never consult the locale.
// Version: 0.6.0 - theme ("dark" | "light")
// Version: 0.5.0 - categoryBoard.display and hoverPreview (the Sticker board's cards)
// Version: 0.4.0 - appsView, appsSplitAbove, recentAppsShown, recentApps (NoteAppStarted), appMoves
// Version: 0.4.0 - UltraClipboard, UltraNotifications and the Sticker board
//                  among the StartupPrograms
// Version: 0.3.0 - startWithDesktop, StartupPrograms
// Version: 0.2.0 - the pinned applications
// Version: 0.1.1 - the file lives in the framework's desktop settings folder
//                  (UltraCanvasDesktopSettingsFolder): Application Support on macOS,
//                  moved there from ~/.config once; every variable read as UTF-8
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopSettings.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"   // UltraCanvasDesktopSettingsFolder

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

// The desktop's settings folder (UltraCanvasSettingsFolder.h): %APPDATA%\ultraos
// on Windows, Application Support/ultraos on macOS, $XDG_CONFIG_HOME or
// ~/.config/ultraos elsewhere, every variable read as UTF-8. On macOS a folder
// an earlier version kept under ~/.config is moved there once.
std::string DesktopSettings::DefaultPath() {
    const fs::path folder = UltraCanvas::UltraCanvasDesktopSettingsFolder();
    if (folder.empty()) return std::string();
    return PathToUtf8(folder / "desktop.json");
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
    root.Set("clockStyle", clockStyle);
    root.Set("theme", theme);
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
    JSONValue board = JSONValue::MakeObject();
    board.Set("visible", categoryBoard.visible);
    board.Set("design", categoryBoard.design);
    board.Set("display", categoryBoard.display);
    board.Set("hoverPreview", categoryBoard.hoverPreview);
    board.Set("x", static_cast<double>(categoryBoard.x));
    board.Set("y", static_cast<double>(categoryBoard.y));
    board.Set("width", static_cast<double>(categoryBoard.width));
    board.Set("height", static_cast<double>(categoryBoard.height));
    if (!categoryBoard.board.empty()) {
        // Kept as a nested object, not a string, so the file stays readable.
        JSONParseResult parsed;
        JSONValue kanban = JSON::Parse(categoryBoard.board, &parsed);
        if (parsed.success && kanban.IsObject()) board.Set("board", std::move(kanban));
    }
    root.Set("categoryBoard", std::move(board));
    JSONValue pinned = JSONValue::MakeArray();
    for (const std::string& key : pinnedApps) pinned.Append(JSONValue(key));
    root.Set("pinnedApps", std::move(pinned));
    JSONValue startup = JSONValue::MakeArray();
    for (const std::string& program : startWithDesktop) startup.Append(JSONValue(program));
    root.Set("startWithDesktop", std::move(startup));
    root.Set("appsView", appsView);
    root.Set("appsSplitAbove", appsSplitAbove);
    root.Set("recentAppsShown", recentAppsShown);
    JSONValue recent = JSONValue::MakeArray();
    for (const std::string& key : recentApps) recent.Append(JSONValue(key));
    root.Set("recentApps", std::move(recent));
    JSONValue moves = JSONValue::MakeObject();
    for (const auto& [key, group] : appMoves) moves.Set(key, group);
    root.Set("appMoves", std::move(moves));
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
    const std::string clock = root.Get("clockStyle").GetString(loaded.clockStyle);
    if (IsClockStyle(clock)) loaded.clockStyle = clock;
    const std::string theme = root.Get("theme").GetString(loaded.theme);
    if (IsTheme(theme)) loaded.theme = theme;
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
    const JSONValue& board = root.Get("categoryBoard");
    if (board.IsObject()) {
        CategoryBoardSettings& cb = loaded.categoryBoard;
        cb.visible = board.Get("visible").GetBoolean(false);
        cb.design = board.Get("design").GetString(cb.design);
        const std::string display = board.Get("display").GetString(cb.display);
        if (display == "text" || display == "thumbnail" || display == "row") cb.display = display;
        const std::string preview = board.Get("hoverPreview").GetString(cb.hoverPreview);
        if (preview == "small" || preview == "medium" || preview == "large") cb.hoverPreview = preview;
        cb.x = static_cast<float>(board.Get("x").GetNumber(cb.x));
        cb.y = static_cast<float>(board.Get("y").GetNumber(cb.y));
        cb.width = static_cast<float>(board.Get("width").GetNumber(cb.width));
        cb.height = static_cast<float>(board.Get("height").GetNumber(cb.height));
        const JSONValue& kanban = board.Get("board");
        if (kanban.IsObject()) cb.board = JSON::Serialize(kanban);
    }
    const JSONValue& startup = root.Get("startWithDesktop");
    if (startup.IsArray()) {
        for (size_t i = 0; i < startup.GetSize(); ++i) loaded.SetStartsWithDesktop(startup.At(i).GetString(), true);
    }
    const JSONValue& pinned = root.Get("pinnedApps");
    if (pinned.IsArray()) {
        for (size_t i = 0; i < pinned.GetSize(); ++i) {
            const std::string key = pinned.At(i).GetString();
            if (!key.empty()) loaded.SetPinned(key, true);
        }
    }
    const std::string view = root.Get("appsView").GetString(loaded.appsView);
    if (view == "tiles" || view == "tree") loaded.appsView = view;
    const int split = static_cast<int>(root.Get("appsSplitAbove").GetInteger(loaded.appsSplitAbove));
    loaded.appsSplitAbove = split <= 0 ? 0 : std::clamp(split, 2, 50);
    const int shown = static_cast<int>(root.Get("recentAppsShown").GetInteger(loaded.recentAppsShown));
    loaded.recentAppsShown = std::clamp(shown, 1, 24);
    const JSONValue& recent = root.Get("recentApps");
    if (recent.IsArray()) {
        for (size_t i = 0; i < recent.GetSize() && loaded.recentApps.size() < kRecentAppsKept; ++i) {
            const std::string key = recent.At(i).GetString();
            if (!key.empty() && std::find(loaded.recentApps.begin(), loaded.recentApps.end(), key) ==
                                    loaded.recentApps.end()) {
                loaded.recentApps.push_back(key);
            }
        }
    }
    const JSONValue& moves = root.Get("appMoves");
    if (moves.IsObject()) {
        for (const auto& [key, value] : moves.GetMembers()) {
            const std::string group = value.GetString();
            if (!key.empty() && !group.empty()) loaded.appMoves[key] = group;
        }
    }
    *this = std::move(loaded);
    return true;
}

bool DesktopSettings::IsClockStyle(const std::string& style) {
    return style == "analog-date" || style == "analog" || style == "digital-date" || style == "digital" ||
           style == "digital-seconds";
}

bool DesktopSettings::IsTheme(const std::string& theme) {
    return theme == "dark" || theme == "light";
}

void DesktopSettings::NoteAppStarted(const std::string& key) {
    if (key.empty()) return;
    recentApps.erase(std::remove(recentApps.begin(), recentApps.end(), key), recentApps.end());
    recentApps.insert(recentApps.begin(), key);
    if (recentApps.size() > kRecentAppsKept) recentApps.resize(kRecentAppsKept);
}

bool DesktopSettings::IsPinned(const std::string& key) const {
    return std::find(pinnedApps.begin(), pinnedApps.end(), key) != pinnedApps.end();
}

const std::vector<StartupProgram>& DesktopSettings::StartupPrograms() {
    static const std::vector<StartupProgram> programs = {
        {"ChatCube", "org.ultraos.chatcube", "ChatCube - messages arrive while its window is closed",
         "ChatCube", "--minimized", false},
        {"UltraMail", "org.ultraos.ultramail", "UltraMail - new mail is fetched and counted on the mail icon",
         "UltraMail", "--minimized", false},
        {"UltraClipboard", "org.ultraos.ultraclipboard",
         "UltraClipboard - no window: opens at once from the clipboard panel",
         "UltraClipboard", "--background", false},
        {"UltraNotifications", "org.ultraos.ultranotifications",
         "UltraNotifications - no window: counts the unread on the bell, opens at once",
         "UltraNotifications", "--background", false},
        {"StickerBoard", "", "Sticker board - made ready unseen: shows at once, the thumbnails made",
         "Sticker board", "", true},
    };
    return programs;
}

bool DesktopSettings::StartsWithDesktop(const std::string& program) const {
    return std::find(startWithDesktop.begin(), startWithDesktop.end(), program) != startWithDesktop.end();
}

void DesktopSettings::SetStartsWithDesktop(const std::string& program, bool start) {
    // Only the programs that can be started this way.
    bool known = false;
    for (const StartupProgram& candidate : StartupPrograms()) known = known || program == candidate.program;
    if (!known) return;
    const bool already = StartsWithDesktop(program);
    if (start && !already) startWithDesktop.push_back(program);
    if (!start && already) {
        startWithDesktop.erase(std::remove(startWithDesktop.begin(), startWithDesktop.end(), program),
                               startWithDesktop.end());
    }
}

void DesktopSettings::SetPinned(const std::string& key, bool pinned) {
    if (key.empty()) return;
    const bool already = IsPinned(key);
    if (pinned && !already) pinnedApps.push_back(key);
    if (!pinned && already) {
        pinnedApps.erase(std::remove(pinnedApps.begin(), pinnedApps.end(), key), pinnedApps.end());
    }
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
