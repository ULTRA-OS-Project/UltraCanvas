// Apps/UltraDesktop/ui/UltraDesktopCategoryBoard.h
// The Sticker board: a Kanban board over the wallpaper whose columns are
// categories (Docs, Work, Home to begin with). A file dropped onto a category
// joins it at once, with no question asked; the same file dropped again moves
// to the new category rather than appearing twice. Double-clicking a file
// opens it with the system's default application; the board's own
// "+ Add card" still makes plain notes, which open its editor.
//
// The board is the framework's UltraCanvasKanbanBoardElement; the bar above
// it is a label to drag the panel by, an UltraCanvasDropdown for the display
// style (Sticky notes, Poster, Schematic, Dark) and an "Add category" button.
// A grip label in the bottom-right corner resizes the panel. The board, its
// style and the panel's place and size live in the desktop settings.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopSettings.h"

#include "UltraCanvasContainer.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
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

    bool OnEvent(const UltraCanvas::UCEvent& event) override;
    // The board only answers on its panel: the rest of the wallpaper
    // belongs to the Stickerboard and the windows above.
    bool Contains(const UltraCanvas::Point2Df& point) override;

    // The categories every new board starts with.
    static const std::vector<std::string>& DefaultCategories();

private:
    void PlacePanel();
    void SaveBoard();
    void OpenCardFile(int cardId);

    UltraDesktopWindow* desktop_;   // owns this board through the window tree
    std::shared_ptr<UltraCanvasContainer> panel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> grip_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> resizeGrip_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> designPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasKanbanBoardElement> board_;
    std::shared_ptr<UltraCanvas::KanbanDataSource> data_;

    // Dragging the panel by its title, or resizing it by the corner grip.
    bool dragging_ = false;
    bool resizing_ = false;
    UltraCanvas::Point2Df dragOffset_;   // pointer to the panel's corner it holds
};

} // namespace UltraDesktop
