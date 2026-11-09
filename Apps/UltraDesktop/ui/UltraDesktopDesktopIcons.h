// Apps/UltraDesktop/ui/UltraDesktopDesktopIcons.h
// The desktop icons: the Desktop folder's contents as icons on the
// wallpaper, shown while files dropped on the wallpaper go to the desktop
// (DesktopSettings::dropFilesOn == "desktop"). Each icon is the file's
// picture (an image element: the thumbnail of a photo or a document, else
// the system's icon for its type) over its name (a label); the layer is an
// UltraCanvasContainer laid over the work area, like the notes.
//
//   - A click selects an icon; Ctrl+click adds or removes one, Shift+click
//     adds one, and a rubber band drawn on the wallpaper selects the icons
//     it touches (with Ctrl, adds them). A double click opens the selection
//     with its default applications. A drag moves the selected icons
//     together (their places are kept in the settings, by the entries'
//     names) - until the pointer reaches another program's window: then the
//     icons go back and the files continue as a native drag, which a file
//     manager, an editor or a mail program takes. A right click offers
//     Open, Show in the file manager, Delete (into the trash, after one
//     question), Select all and Arrange icons, for the whole selection.
//   - Files dropped on the wallpaper or on an icon are moved into the
//     Desktop folder (copied when they are on another drive) and appear
//     where they were dropped (TakeFiles).
//   - The folder is read again when it changes (Refresh, from the desktop's
//     timer), so a file saved to the Desktop elsewhere appears too.
// Version: 0.2.0 - several icons selected (Ctrl+click, Shift+click, a rubber
//                  band from the wallpaper), moved together, and dragged out
//                  to another program as a native file drag
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopPanelWork.h"
#include "UltraDesktopSettings.h"

#include "UltraCanvasContainer.h"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasImageElement;
    class UltraCanvasLabel;
    class UltraCanvasMenu;
}

namespace UltraDesktop {

class UltraDesktopWindow;

class UltraDesktopDesktopIcons : public UltraCanvas::UltraCanvasContainer {
public:
    UltraDesktopDesktopIcons(const std::string& identifier, UltraDesktopWindow* desktop);
    ~UltraDesktopDesktopIcons() override;

    // The user's Desktop folder (the platform's own: the known folder on
    // Windows, xdg-user-dirs on Linux), else "<home>/Desktop". It may not
    // exist yet: TakeFiles makes it.
    static std::string DesktopFolder();

    // Read the folder again when it changed since the last read (or always,
    // with `force`): icons for new entries, none for gone ones.
    void Refresh(bool force = false);
    // Move (or, across drives, copy) `files` into the Desktop folder; their
    // icons lie from `at` (work-area coordinates) on. A file already in the
    // folder only moves to `at`. False with the reason when one could not
    // be moved; the others are moved all the same.
    bool TakeFiles(const std::vector<std::string>& files, const UltraCanvas::Point2Df& at, std::string& error);
    // Every icon back on the grid, in name order; the kept places forgotten.
    void ArrangeIcons();
    // A press on the wallpaper between the icons, at `at`: a rubber band
    // from there selects what it touches (`add`: Ctrl held, added to the
    // selection; else the selection starts empty - a plain click clears it).
    void BeginRubberBand(const UltraCanvas::Point2Df& at, bool add);

    bool OnEvent(const UltraCanvas::UCEvent& event) override;
    // The layer answers only where an icon is, or while a drag or a rubber
    // band is under way. Presses that still reach it between the icons are
    // the wallpaper's: a rubber band, the desktop's menu, a drop.
    bool Contains(const UltraCanvas::Point2Df& point) override;

private:
    struct Icon {
        std::string name;    // the entry's name in the folder: the key of its place
        std::string path;
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> tile;
        std::shared_ptr<UltraCanvas::UltraCanvasImageElement> picture;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> label;
        UltraCanvas::Point2Df place;
    };

    void BuildIcon(const std::string& name, const std::string& path);
    void PlaceIcon(Icon& icon, const UltraCanvas::Point2Df& at);
    // The first grid cell no icon covers, column by column from the top left.
    UltraCanvas::Point2Df FreeCell() const;
    Icon* IconAt(const UltraCanvas::Point2Df& point);
    void SetSelection(const std::set<std::string>& names);
    void ClearSelection();
    std::vector<std::string> SelectedPaths() const;
    void OpenSelected();
    void DeleteSelected();
    void ShowMenu(int windowX, int windowY);
    void UpdateRubberBand(const UltraCanvas::Point2Df& to);
    void EndRubberBand();
    void DragOut();
    void LoadPicture(const std::string& name);
    void SavePlace(const Icon& icon);

    UltraDesktopWindow* desktop_;   // owns this layer through the window tree
    std::map<std::string, Icon> icons_;
    std::set<std::string> selected_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    std::filesystem::file_time_type folderStamp_{};
    bool haveStamp_ = false;

    // A press on an icon: dragged once the pointer moves a few pixels.
    std::string pressedName_;
    UltraCanvas::Point2Df pressPoint_;
    std::map<std::string, UltraCanvas::Point2Df> dragStart_;   // the selected icons' places at the press
    bool dragging_ = false;
    // The rubber band: where it started, the selection it adds to, and the
    // translucent box that shows it.
    bool banding_ = false;
    UltraCanvas::Point2Df bandStart_;
    std::set<std::string> bandBase_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> band_;
    bool rightPressPending_ = false;
    bool rightOnGround_ = false;   // a right press on the wallpaper between the icons

    UltraDesktopPanelWork work_;
};

} // namespace UltraDesktop
