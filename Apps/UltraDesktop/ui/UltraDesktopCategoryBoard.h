// Apps/UltraDesktop/ui/UltraDesktopCategoryBoard.h
// The Sticker board: a Kanban board over the wallpaper whose columns are
// categories (Docs, Work, Home to begin with). A file dropped onto a category
// joins it at once, with no question asked; the same file dropped again moves
// to the new category rather than appearing twice. Double-clicking a file
// opens it with the system's default application; the board's own
// "+ Add card" still makes plain notes, which open its editor.
//
// The board is the framework's UltraCanvasKanbanBoardElement; the bar above
// it carries the title, an UltraCanvasDropdown for the display style (Sticky
// notes, Poster, Schematic, Dark), an "Add category" button and a close
// button. It fills a window of its own over the whole work area, which the
// desktop shows below the application that was in front (see
// UltraDesktopWindow::ShowCategoryBoard). A file's card shows a thumbnail -
// the picture itself, else the system's icon for its type - made on a worker
// thread. The board and its style live in the desktop settings.
// Version: 0.2.0 - fills its own window; a close button; thumbnails; smaller
//                  card text; no more panel to drag or resize
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopSettings.h"

#include "UltraDesktopPanelWork.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
    struct KanbanCard;
    class UltraCanvasLabel;
    class UltraCanvasDropdown;
    class UltraCanvasKanbanBoardElement;
    class KanbanDataSource;
}

namespace UltraDesktop {

class UltraDesktopWindow;

class UltraDesktopCategoryBoard : public UltraCanvas::UltraCanvasContainer {
public:
    UltraDesktopCategoryBoard(const std::string& identifier, UltraDesktopWindow* desktop);

    // Rebuild the board, its style and its place from the settings.
    void LoadFrom(const DesktopSettings& settings);

    // A new category at the end, its name open for typing.
    void AddCategory();
    // Files into the category `columnId`: new ones as cards at its end,
    // ones already on the board moved there. -1 = the first category.
    void AddFiles(const std::vector<std::string>& files, int columnId);
    // "stickynotes", "poster", "schematic" or "dark"; anything else is
    // Sticky notes.
    void SetDesign(const std::string& design);

    // The categories every new board starts with.
    static const std::vector<std::string>& DefaultCategories();

private:
    void SaveBoard();
    void OpenCardFile(int cardId);
    // A file card's thumbnail, when it is ready; asks for one otherwise.
    std::shared_ptr<UltraCanvas::UCPixmap> Thumbnail(const UltraCanvas::KanbanCard& card);

    UltraDesktopWindow* desktop_;   // owns this board through its window
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> title_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> designPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasKanbanBoardElement> board_;
    std::shared_ptr<UltraCanvas::KanbanDataSource> data_;

    // Thumbnails by file path: made on worker threads, kept for the
    // session; `pending_` are those under way, `missing_` those there are
    // none for (a note, a file without picture or icon).
    UltraDesktopPanelWork work_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UCPixmap>> thumbnails_;
    std::set<std::string> pending_;
    std::set<std::string> missing_;
};

} // namespace UltraDesktop
