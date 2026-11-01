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
    void Rebuild();
    void BuildTiles(const std::vector<AppGroup>& groups);
    void BuildTreeAndList(const std::vector<AppGroup>& groups);
    void ShowListOf(const std::string& nodeId);
    void SetView(const std::string& view);
    void Launch(const UltraCanvas::UCDesktopEntry& entry);
    void ShowAppMenu(const UltraCanvas::UCDesktopEntry& entry, int windowX, int windowY);

    Hooks hooks_;
    UltraDesktopPanelWork work_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    bool open_ = false;
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
