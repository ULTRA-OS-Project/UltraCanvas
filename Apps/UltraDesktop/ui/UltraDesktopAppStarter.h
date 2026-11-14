// Apps/UltraDesktop/ui/UltraDesktopAppStarter.h
// The Apps window: every installed application, sorted into categories
// (UltraDesktopAppCategories: Recent, Office, Internet, Graphics, Video,
// Audio, CAD, Games, Tools, System), shown one of two ways, switched by the
// two icons at the window's top left:
//   - Tiles: each category a heading over a grid of tiles - the icon over
//     the name, the name on as many lines as it needs - as Windows' Start
//     shows them;
//   - Tree: the categories as a tree (their sub-categories under them when a
//     category is split) beside a list of the chosen one's applications.
// A category with more applications than the desktop's threshold
// (DesktopSettings::appsSplitAbove, 8 by default) is split into its
// sub-categories. Recent - the last applications started, eight by default
// - comes first, and is where the window opens.
//
// The list comes from UltraCanvasDesktopShell::ListApplications, which reads
// the freedesktop entries (on Windows the Start menu) and takes a moment: the
// window opens at once, says it is loading, and fills when the list is
// there, read on a thread of its own. The desktop reads the list once ahead
// of time (Preload, a little after it starts), and a later Open shows that
// list at once and reads it again behind it. One click starts an
// application; a right click pins it to the taskbar or unpins it.
// Version: 0.5.0 - the colours from the theme in force (UltraCanvasTheme), and
//                  again when it changes while the window is open (ApplyLook)
// Version: 0.4.0 - an application dragged onto another category (or moved by
//                  its menu) stays there; a big Start-menu folder is a category
// Version: 0.3.0 - categories and sub-categories, Recent, the tiles and the
//                  tree-and-list views, read off the UI thread after the
//                  window is open; names wrap on the tiles
// Version: 0.2.0 - pin to the taskbar
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopAppCategories.h"
#include "UltraDesktopPanelWork.h"

#include "UltraCanvasDesktopEntry.h"

#include "UltraCanvasWindow.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasContainer;
    class UltraCanvasMenu;
    class UltraCanvasTextInput;
    class UltraCanvasLabel;
    class UltraCanvasListView;
    class UltraCanvasSegmentedControl;
    class UltraCanvasSimpleListModel;
    class UltraCanvasTreeView;
    class UltraCanvasUIElement;
    struct UCEvent;
}

namespace UltraDesktop {

using AppList = std::shared_ptr<const std::vector<UltraCanvas::UCDesktopEntry>>;

class UltraDesktopAppStarter {
public:
    // What the window needs of the desktop.
    struct Hooks {
        // The taskbar's pins, which the right-click menu reads and sets.
        std::function<bool(const UltraCanvas::UCDesktopEntry&)> isPinned;
        std::function<void(const UltraCanvas::UCDesktopEntry&, bool pinned)> setPinned;
        // The application's history key (the desktop's PinKey).
        std::function<std::string(const UltraCanvas::UCDesktopEntry&)> keyOf;
        // An application was started from the window: into the history.
        std::function<void(const UltraCanvas::UCDesktopEntry&)> started;
        // The history, the latest first, and how Recent and the categories
        // are shown (DesktopSettings: recentApps, recentAppsShown,
        // appsSplitAbove, appsView).
        std::function<std::vector<std::string>()> recent;
        size_t recentShown = 8;
        size_t splitAbove = 8;
        std::string view = "tiles";      // "tiles" | "tree"
        // The person switched the view: remembered for the next time.
        std::function<void(const std::string& view)> setView;
        // Applications dragged to another category (DesktopSettings::
        // appMoves: history key -> category key), and a new move ("" puts
        // the application back where its own categories put it).
        std::function<std::map<std::string, std::string>()> moved;
        std::function<void(const std::string& key, const std::string& groupKey)> setMoved;
    };

    explicit UltraDesktopAppStarter(Hooks hooks);
    ~UltraDesktopAppStarter();

    void Open();
    void Raise();
    bool IsOpen() const { return open_; }

    // Read the application list on a thread of its own and keep it, so the
    // next Open shows it at once. The desktop calls it a little after it
    // starts; a second call while one runs does nothing.
    static void Preload();

private:
    void ShowApps(AppList apps);
    // The theme changed: the status, and the headings, tiles and drag label
    // made again in the new colours.
    void ApplyLook();
    void Rebuild();
    void BuildTiles(const std::vector<AppGroup>& groups);
    void BuildTreeAndList(const std::vector<AppGroup>& groups);
    void ShowListOf(const std::string& nodeId);
    void SetView(const std::string& view);
    void Launch(const UltraCanvas::UCDesktopEntry& entry);
    void ShowAppMenu(const UltraCanvas::UCDesktopEntry& entry, int windowX, int windowY);

    // ----- Dragging an application to another category -----
    // The window's event filter: a press on a tile or a list row, a move of
    // a few pixels, a release over a category (a heading or grid on the
    // tiles, a node of the tree). It only watches - every event still
    // reaches its element - and the tile's click after a drag is ignored.
    bool OnPointerEvent(const UltraCanvas::UCEvent& event);
    // The application under a window point (a tile, or a list row), -1 none.
    long AppAt(const UltraCanvas::Point2Df& point, int* listRow = nullptr) const;
    // The category under a window point ("" none, never Recent).
    std::string GroupAt(const UltraCanvas::Point2Df& point) const;
    // Where its own categories (and its Start-menu folder) put `entry`.
    std::string NaturalGroupOf(const UltraCanvas::UCDesktopEntry& entry) const;
    void MoveApp(const UltraCanvas::UCDesktopEntry& entry, const std::string& groupKey);
    void ShowDragLabel(const UltraCanvas::Point2Di& at, const std::string& text);
    void HideDragLabel();
    void RebuildSoon();

    struct Drag {
        bool pressed = false;
        bool dragging = false;
        long app = -1;
        int listRow = -1;              // the press was on this list row
        UltraCanvas::Point2Di start;
        std::string target;            // the category under the pointer
    };
    Drag drag_;
    bool suppressClick_ = false;       // a drag just ended on a tile: no click
    std::vector<std::pair<UltraCanvas::UltraCanvasUIElement*, size_t>> tileApps_;      // tile -> application
    std::vector<std::pair<UltraCanvas::UltraCanvasUIElement*, std::string>> dropZones_; // heading/grid -> category
    UltraCanvas::UltraCanvasUIElement* tilesArea_ = nullptr;
    std::map<std::string, std::string> nodeGroup_;   // tree node id -> category key
    std::vector<AppGroup> groups_;                    // what the view shows now
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> dragLabel_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    Hooks hooks_;
    UltraDesktopPanelWork work_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    bool open_ = false;
    int themeHandler_ = 0;   // UltraCanvasTheme::AddChangeHandler's id, removed in the destructor
    std::string filter_;
    std::string view_;
    AppList apps_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> viewSwitch_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> filterBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> body_;
    // The tree view: the category tree, the list beside it, and which
    // applications each tree node stands for.
    std::shared_ptr<UltraCanvas::UltraCanvasTreeView> tree_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> list_;
    std::shared_ptr<UltraCanvas::UltraCanvasSimpleListModel> listModel_;
    std::map<std::string, std::vector<size_t>> nodeApps_;
    std::vector<size_t> listed_;
    std::string selectedNode_;
};

} // namespace UltraDesktop
