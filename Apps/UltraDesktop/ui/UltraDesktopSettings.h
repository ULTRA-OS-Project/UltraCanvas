// Apps/UltraDesktop/ui/UltraDesktopSettings.h
// What the desktop remembers between sessions: which edge the taskbar sits
// on, the wallpaper, the RAM disc the pinned drive button opens, how many
// virtual desktops the organiser offers, the sticky notes on the
// Stickerboard and the Sticker board's categories. One JSON file, read at
// start-up and written on every change. No UI in here, which is what lets
// the test read and write it.
// Version: 0.11.0 - ramDiscPath "ultrafiler" (kRamDiscFromFiler, the default
//                  everywhere): the button asks UltraFiler for its RAM disc
// Version: 0.10.0 - UltraSnip starts with the desktop by default (startupDefaults 2)
// Version: 0.9.0 - UltraNotifications starts with the desktop by default
//                  (startupDefaults: added once to a settings file older
//                  than that, so unticking it afterwards holds)
// Version: 0.8.0 - where files dropped on the wallpaper go (dropFilesOn) and
//                  the desktop icons' places (desktopIcons)
// Version: 0.7.0 - the bars' theme (theme: "dark" or "light")
// Version: 0.6.0 - the Sticker board's card display and hover preview size
// Version: 0.5.0 - StartupProgram::arguments and ::builtIn: UltraClipboard
//                  and UltraNotifications start in the background
//                  (--background), the Sticker board is made ready unseen
// Version: 0.4.0 - the Apps window's view, split threshold, Recent count and
//                  history (appsView, appsSplitAbove, recentAppsShown, recentApps);
//                  the applications dragged to another category (appMoves)
// Version: 0.3.0 - the programs that start with the desktop (startWithDesktop,
//                  StartupPrograms)
// Version: 0.2.0 - the pinned applications (pinnedApps)
// Version: 0.1.1 - no RAM disc by default on Windows
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <map>
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
    // How a card shows its file: "text" (the name only), "thumbnail" (the
    // picture with the name centred under it) or "row" (a small picture
    // and the name beside it).
    std::string display = "thumbnail";
    // The larger preview after the pointer rests on a card for two seconds:
    // "small", "medium" or "large".
    std::string hoverPreview = "medium";
    float x = 320;
    float y = 60;
    float width = 780;
    float height = 460;
    std::string board;      // KanbanDataSource::ToJSON; "" = the default categories
};

// A program the desktop can start with itself, in the background: one that
// keeps working while nobody looks at it (messages arrive, mail is counted).
struct StartupProgram {
    const char* program;        // LaunchProgram's name: "ChatCube"; the key in startWithDesktop
    const char* appId;          // its single-instance id: running already, it is left alone
    const char* label;          // what the settings show
    const char* name;           // the menu's entry: "ChatCube", "Sticker board"
    const char* arguments;      // how it starts unseen: "--minimized", "--background"
    bool builtIn;               // part of the desktop itself (the Sticker board), not a program
};

// DesktopSettings::ramDiscPath's value for "UltraFiler's own RAM disc".
inline constexpr const char* kRamDiscFromFiler = "ultrafiler";

struct DesktopSettings {
    TaskbarEdge taskbarEdge = TaskbarEdge::Left;
    std::string wallpaper;              // "" = the framework's default picture
    // kRamDiscFromFiler (the default): the button has UltraFiler show its RAM
    // disc, or offer to make one when there is none (`UltraFiler --ram-disc`).
    // A folder: the button opens that folder. "" = no RAM disc button.
    std::string ramDiscPath = kRamDiscFromFiler;
    std::string filerProgram = "UltraFiler";
    // The clock at the foot of the right bar: "analog-date" (the face over
    // the date, the default), "analog", "digital-date", "digital" or
    // "digital-seconds"; chosen on the clock's right-click menu.
    std::string clockStyle = "analog-date";
    static bool IsClockStyle(const std::string& style);
    // The bars' colours: "dark" (light icons on dark grey, the default) or
    // "light" (dark icons on light grey); chosen on the desktop's
    // right-click menu or in UOS-Settings.
    std::string theme = "dark";
    static bool IsTheme(const std::string& theme);
    // Files dropped on the wallpaper: "board" (onto the Sticker board, in
    // its "Unsorted" category - the default) or "desktop" (moved into the
    // Desktop folder, whose contents the wallpaper then shows as icons).
    std::string dropFilesOn = "board";
    static bool IsDropTarget(const std::string& target);
    // Where the desktop icons lie, in work-area coordinates, by the entry's
    // name in the Desktop folder. An entry not listed is placed in the
    // first free cell of the grid.
    struct IconPlace {
        float x = 0;
        float y = 0;
    };
    std::map<std::string, IconPlace> desktopIcons;
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
    // unseen, unless a copy runs already. UltraNotifications by default, so
    // the bell counts from the first sign-in, and UltraSnip, so the Print
    // Screen key snips from the first sign-in; the others are off.
    std::vector<std::string> startWithDesktop = {"UltraNotifications", "UltraSnip"};
    // Which defaults of startWithDesktop a settings file has had applied
    // (kStartupDefaults): a file from before one is given it once, at the
    // next start, and what the person changes after that is kept.
    static constexpr int kStartupDefaults = 2;   // 1: UltraNotifications, 2: UltraSnip
    int startupDefaults = kStartupDefaults;

    // The Apps window. Its view ("tiles", a grid per category as on Windows'
    // Start, or "tree", the categories beside a list of their applications);
    // how many applications a category may hold before it is split into its
    // sub-categories (0: never, else 2..50); how many of the recently
    // started ones Recent shows (1..24); and the history itself: the
    // started applications' PinKeys, the latest first, at most kRecentAppsKept.
    std::string appsView = "tiles";
    int appsSplitAbove = 8;
    int recentAppsShown = 8;
    std::vector<std::string> recentApps;
    static constexpr size_t kRecentAppsKept = 24;
    // Applications the person dragged to another category of the Apps
    // window: PinKey -> the category's key ("office", "games", or
    // "folder:<Start-menu folder>").
    std::map<std::string, std::string> appMoves;

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

    // The programs that may be in startWithDesktop: ChatCube and UltraMail
    // (minimized), UltraClipboard and UltraNotifications (no window), and
    // the desktop's own Sticker board (made ready, not shown).
    static const std::vector<StartupProgram>& StartupPrograms();
    bool StartsWithDesktop(const std::string& program) const;
    void SetStartsWithDesktop(const std::string& program, bool start);

    // `key` was started: first in recentApps, once, the oldest dropped.
    void NoteAppStarted(const std::string& key);

    bool IsPinned(const std::string& key) const;
    // Pins at the end, or unpins; a key is pinned once.
    void SetPinned(const std::string& key, bool pinned);

    Sticker* FindSticker(const std::string& id);
    void RemoveSticker(const std::string& id);
    // A fresh id no existing sticker has.
    std::string NewStickerId() const;
};

} // namespace UltraDesktop
