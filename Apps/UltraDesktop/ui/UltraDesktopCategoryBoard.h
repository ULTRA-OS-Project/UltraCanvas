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
//
// A right click on a card offers Open, Copy and Delete for its file (a note:
// its text), moving it to another category, and the Display choices: the
// cards as text only, as a thumbnail with the name centred under it, or as a
// small thumbnail with the name beside it (the default is the thumbnail),
// and the size of the hover preview - the larger picture of a file that
// opens beside its card as soon as the pointer comes to rest on it.
// UOS-Settings offers the same two choices.
// Version: 0.6.0 - the hover preview opens when the pointer stops on a card
//                  (a short rest, not two seconds), its picture made while
//                  the pointer is on its way
// Version: 0.5.0 - the Delete key asks first and does what the menu's Delete
//                  does (the file to the trash, a note deleted)
// Version: 0.4.0 - AddFilesToCategory (a drop on the wallpaper: "Unsorted"); MakeFilePicture shared
// Version: 0.3.0 - the context menu (Open, Copy, Delete, Move to, Display),
//                  three card displays, the hover preview
// Version: 0.2.0 - fills its own window; a close button; thumbnails; smaller
//                  card text; no more panel to drag or resize
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDesktopSettings.h"

#include "UltraDesktopPanelWork.h"

#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"

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
    class UltraCanvasMenu;
    class KanbanDataSource;
}

namespace UltraDesktop {

class UltraDesktopWindow;

// A file as a picture `pixels` high (and up to twice as wide): what it holds
// as UltraFiler's tiles show it - the picture, a PDF's first page, a book's
// cover, a page of a document's first lines ... - else the icon of its type
// (`icons`: the framework's media/icons/ folder, for a system without an
// icon theme). Blocking: call it on a worker thread. The board's cards and
// the desktop's icons use it.
std::shared_ptr<UltraCanvas::UCPixmap> MakeFilePicture(const std::string& path, int pixels,
                                                       const std::string& icons);

class UltraDesktopCategoryBoard : public UltraCanvas::UltraCanvasContainer {
public:
    UltraDesktopCategoryBoard(const std::string& identifier, UltraDesktopWindow* desktop);
    ~UltraDesktopCategoryBoard() override;

    // Rebuild the board, its style and its place from the settings.
    void LoadFrom(const DesktopSettings& settings);

    // A new category at the end, its name open for typing.
    void AddCategory();
    // Files into the category `columnId`: new ones as cards at its end,
    // ones already on the board moved there. -1 = the first category.
    void AddFiles(const std::vector<std::string>& files, int columnId);
    // Files into the category named `category`, made at the end when the
    // board has none of that name (files dropped on the wallpaper go to
    // "Unsorted").
    void AddFilesToCategory(const std::vector<std::string>& files, const std::string& category);
    // "stickynotes", "poster", "schematic" or "dark"; anything else is
    // Sticky notes.
    void SetDesign(const std::string& design);
    // How cards show their files: "text", "thumbnail" or "row"; anything
    // else is "thumbnail". Kept in the settings.
    void SetDisplay(const std::string& display);
    // The hover preview's size: "small", "medium" or "large"; anything else
    // is "medium". Kept in the settings.
    void SetHoverPreview(const std::string& size);
    // The two above as the settings now hold them (UOS-Settings changed
    // them), without writing the settings again.
    void ApplyDisplaySettings(const CategoryBoardSettings& settings);

    // The categories every new board starts with.
    static const std::vector<std::string>& DefaultCategories();

private:
    void SaveBoard();
    void OpenCardFile(int cardId);
    // The card's file when it is one that exists; "" for a note.
    std::string CardFile(int cardId) const;
    void ShowContextMenu(int cardId, int columnId, const UltraCanvas::Point2Di& windowPos);
    void CopyCard(int cardId);
    void DeleteCardFile(int cardId);
    // A note (or the card of a file that has gone) deleted, after a question.
    void DeleteNote(int cardId);
    void RemoveCard(int cardId);
    void MoveCardTo(int cardId, int columnId);
    // The style's card metrics for display_ (after a design is applied).
    void ApplyDisplayStyle();
    // The pointer reaches a card: its preview is made ahead (not shown).
    void OnCardHover(int cardId);
    // The pointer moved on a card: the rest timer starts again.
    void OnCardPointerMoved(int cardId);
    // The preview of `cardId`, made on a worker if not yet; shown when made
    // if `show` and the pointer still rests on that card.
    void PrepareHoverPreview(int cardId, bool show);
    void StopHoverTimer();
    float PreviewSize() const;
    // A file card's thumbnail, when it is ready; asks for one otherwise.
    std::shared_ptr<UltraCanvas::UCPixmap> Thumbnail(const UltraCanvas::KanbanCard& card);

    UltraDesktopWindow* desktop_;   // owns this board through its window
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> title_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> designPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasKanbanBoardElement> board_;
    std::shared_ptr<UltraCanvas::KanbanDataSource> data_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    std::string design_ = "stickynotes";
    std::string display_ = "thumbnail";
    std::string hoverPreview_ = "medium";

    // The hover preview: the card under the pointer, the card the pointer
    // has come to rest on (-1 while it moves), the timer that waits for the
    // rest, and the larger pictures made so far, by path and size.
    int hoverCardId_ = -1;
    int restingCardId_ = -1;
    UltraCanvas::TimerId hoverTimer_ = UltraCanvas::InvalidTimerId;
    std::map<std::string, std::shared_ptr<UltraCanvas::UCPixmap>> previews_;
    std::set<std::string> previewPending_;

    // Thumbnails by file path: made on worker threads, kept for the
    // session; `pending_` are those under way, `missing_` those there are
    // none for (a note, a file without picture or icon).
    UltraDesktopPanelWork work_;
    std::map<std::string, std::shared_ptr<UltraCanvas::UCPixmap>> thumbnails_;
    std::set<std::string> pending_;
    std::set<std::string> missing_;
};

} // namespace UltraDesktop
