// Apps/UltraDesktop/ui/UltraDesktopSettings.h
// What the desktop remembers between sessions: which edge the taskbar sits
// on, the wallpaper, the RAM disc the pinned drive button opens, how many
// virtual desktops the organiser offers, the sticky notes on the
// Stickerboard and the Sticker board's categories. One JSON file, read at
// start-up and written on every change. No UI in here, which is what lets
// the test read and write it.
// Version: 0.3.0 - the programs that start with the desktop (startWithDesktop,
//                  StartupPrograms)
// Version: 0.2.0 - the pinned applications (pinnedApps)
// Version: 0.1.1 - no RAM disc by default on Windows
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

// The Sticker board: files and notes sorted into categories on a Kanban
// board over the wallpaper. The board itself is kept as the Kanban
// element's own JSON document, so this file needs no UI to read it.
struct CategoryBoardSettings {
    bool visible = false;
    std::string design = "stickynotes";   // stickynotes | poster | schematic | dark
    float x = 320;
    float y = 60;
    float width = 780;
    float height = 460;
    std::string board;      // KanbanDataSource::ToJSON; "" = the default categories
};

// A program the desktop can start with itself, in the background: one that
// keeps working while nobody looks at it (messages arrive, mail is counted).
struct StartupProgram {
    const char* program;        // LaunchProgram's name: "ChatCube"
    const char* appId;          // its single-instance id: running already, it is left alone
    const char* label;          // what the settings show
};

struct DesktopSettings {
    TaskbarEdge taskbarEdge = TaskbarEdge::Left;
    std::string wallpaper;              // "" = the framework's default picture
#if defined(_WIN32)
    std::string ramDiscPath;            // Windows has no RAM disc of its own: no button
#else
    std::string ramDiscPath = "/dev/shm";   // "" = no RAM disc button
#endif
    std::string filerProgram = "UltraFiler";
    int virtualDesktops = 3;            // 1..9, what the organiser offers
    bool stickerboardVisible = false;
    std::vector<Sticker> stickers;
    CategoryBoardSettings categoryBoard;
    // The applications pinned to the taskbar, after the RAM disc and the
    // file manager, in the order they were pinned: each the key
    // UltraDesktopWindow::PinKey gives its desktop entry (the program's
    // path; on Windows the shortcut or the Store application's id), which
    // is stable across sessions and survives the entry's name changing.
    std::vector<std::string> pinnedApps;
    // The programs started with the desktop (their StartupProgram::program),
    // minimized, unless a copy runs already. Off by default.
    std::vector<std::string> startWithDesktop;

    // "<config dir>/ultraos/desktop.json": $XDG_CONFIG_HOME, else ~/.config
    // (%APPDATA% on Windows, ~/Library/Application Support on macOS - the
    // framework's UltraCanvasDesktopSettingsFolder). "" when there is no home
    // to keep it in.
    static std::string DefaultPath();

    // Missing file = defaults, and true; a file that cannot be parsed keeps
    // the defaults and returns false with the reason in `error`.
    bool Load(const std::string& path, std::string* error = nullptr);
    bool Save(const std::string& path, std::string* error = nullptr) const;

    std::string ToJson() const;
    bool FromJson(const std::string& json, std::string* error = nullptr);

    // The programs that may be in startWithDesktop: ChatCube and UltraMail.
    static const std::vector<StartupProgram>& StartupPrograms();
    bool StartsWithDesktop(const std::string& program) const;
    void SetStartsWithDesktop(const std::string& program, bool start);

    bool IsPinned(const std::string& key) const;
    // Pins at the end, or unpins; a key is pinned once.
    void SetPinned(const std::string& key, bool pinned);

    Sticker* FindSticker(const std::string& id);
    void RemoveSticker(const std::string& id);
    // A fresh id no existing sticker has.
    std::string NewStickerId() const;
};

} // namespace UltraDesktop
