// Apps/UltraDesktop/ui/UltraDesktopDesktopIcons.h
// The desktop icons: the Desktop folder's contents as icons on the
// wallpaper, shown while files dropped on the wallpaper go to the desktop
// (DesktopSettings::dropFilesOn == "desktop"). Each icon is the file's
// picture (an image element: the thumbnail of a photo or a document, else
// the system's icon for its type) over its name (a label); the layer is an
// UltraCanvasContainer laid over the work area, like the notes.
//
//   - A click selects an icon, a double click opens it with its default
//     application, a drag moves it (the place is kept in the settings, by
//     the entry's name); a right click offers Open, Show in the file
//     manager, Delete (into the trash, after a question) and Arrange icons.
//   - Files dropped on the wallpaper or on an icon are moved into the
//     Desktop folder (copied when they are on another drive) and appear
//     where they were dropped (TakeFiles).
//   - The folder is read again when it changes (Refresh, from the desktop's
//     timer), so a file saved to the Desktop elsewhere appears too.
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

    bool OnEvent(const UltraCanvas::UCEvent& event) override;
    // The layer answers only where an icon is: the wallpaper between them
    // belongs to the wallpaper (its menu, its drops).
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
    void Select(const std::string& name);
    void Open(const std::string& name);
    void ShowMenu(const std::string& name, int windowX, int windowY);
    void Delete(const std::string& name);
    void LoadPicture(const std::string& name);
    void SavePlace(const Icon& icon);

    UltraDesktopWindow* desktop_;   // owns this layer through the window tree
    std::map<std::string, Icon> icons_;
    std::string selected_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    std::filesystem::file_time_type folderStamp_{};
    bool haveStamp_ = false;

    // A press on an icon: dragged once the pointer moves a few pixels.
    std::string pressedName_;
    UltraCanvas::Point2Df pressPoint_;
    UltraCanvas::Point2Df dragOffset_;
    bool dragging_ = false;
    bool rightPressPending_ = false;

    UltraDesktopPanelWork work_;
};

} // namespace UltraDesktop
