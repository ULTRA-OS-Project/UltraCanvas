// Apps/UltraDesktop/ui/UltraDesktopSettings.h
// What the desktop remembers between sessions: which edge the taskbar sits
// on, the wallpaper, the RAM disc the pinned drive button opens, how many
// virtual desktops the organiser offers, and the sticky notes on the
// Stickerboard. One JSON file, read at start-up and written on every change.
// No UI in here, which is what lets the test read and write it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraDesktop {

enum class TaskbarEdge { Left, Top, Bottom };

const char* TaskbarEdgeName(TaskbarEdge edge);          // "left", "top", "bottom"
bool ParseTaskbarEdge(const std::string& text, TaskbarEdge& out);

// One note on the Stickerboard, in work-area coordinates.
struct Sticker {
    std::string id;         // stable across sessions; the element id derives from it
    std::string text;
    float x = 40;
    float y = 40;
    float width = 220;
    float height = 160;
    std::string color = "#FFF59D";   // the note's paper, as CSS hex
};

struct DesktopSettings {
    TaskbarEdge taskbarEdge = TaskbarEdge::Left;
    std::string wallpaper;              // "" = the framework's default picture
    std::string ramDiscPath = "/dev/shm";
    std::string filerProgram = "UltraFiler";
    int virtualDesktops = 3;            // 1..9, what the organiser offers
    bool stickerboardVisible = false;
    std::vector<Sticker> stickers;

    // "<config dir>/ultraos/desktop.json": $XDG_CONFIG_HOME, else ~/.config
    // (%APPDATA% on Windows). "" when there is no home to keep it in.
    static std::string DefaultPath();

    // Missing file = defaults, and true; a file that cannot be parsed keeps
    // the defaults and returns false with the reason in `error`.
    bool Load(const std::string& path, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;

    std::string ToJson() const;
    bool FromJson(const std::string& json, std::string* error = nullptr);

    Sticker* FindSticker(const std::string& id);
    void RemoveSticker(const std::string& id);
    // A fresh id no existing sticker has.
    std::string NewStickerId() const;
};

} // namespace UltraDesktop
