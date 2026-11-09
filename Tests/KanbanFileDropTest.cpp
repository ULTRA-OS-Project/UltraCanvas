// Tests/KanbanFileDropTest.cpp
// Files dropped onto a Kanban board, and what UltraDesktop's Sticker board
// keeps of it.
//
// The board is drawn into an offscreen context, which lays out its columns
// as a window would, and then handed the events the platform backends make
// of a file drag: DragOver while the files hover, Drop with the paths. The
// files must reach onFilesDropped with the column under the pointer; with no
// callback the drop must pass on. ScrollToColumn must bring a column that is
// off to the side into view. The desktop's settings must keep the
// Sticker board - its style, its place and the board itself - across a save.
// A card's picture above or beside its text, the right click
// (onContextMenu), the card under the pointer (onCardHoverChanged) and the
// larger preview over the board (ShowCardPreview) are checked the same way.
// With KANBAN_TEST_PNG_DIR set, the three Sticker board displays and the
// preview are written there as PNGs to look at.
// Version: 1.1.0 - card displays, context menu, hover, preview, their settings
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "UltraCanvasRenderContext.h"

#include "UltraDesktopSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo/cairo.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

struct Board {
    std::shared_ptr<KanbanDataSource> data = std::make_shared<KanbanDataSource>();
    std::shared_ptr<UltraCanvasKanbanBoardElement> element;
    int docs = -1, work = -1, home = -1;

    Board() {
        docs = data->AddColumn("Docs");
        work = data->AddColumn("Work");
        home = data->AddColumn("Home");
        element = CreateKanbanBoardWithData("board", 0, 0, 720, 360, data, KanbanDesign::StickyNotes);
        // The layout is made while drawing.
        auto ctx = CreateRenderContext(Size2Di(720, 360), nullptr);
        if (ctx) element->Render(ctx.get(), Rect2Df(0, 0, 720, 360));
    }

    // A point inside the column `columnId`, found the way a user would: by
    // moving across the board until it is under the pointer.
    Point2Di PointIn(int columnId) const {
        for (int x = 0; x < 720; x += 4) {
            if (element->ColumnIdAt(Point2Di(x, 200)) == columnId) return Point2Di(x + 20, 200);
        }
        return Point2Di(-1, -1);
    }

    // Whether the whole column is on the board, not a sliver at an edge:
    // columns are at least 150 px wide (the style's minColumnWidth).
    bool Shows(int columnId) const {
        int span = 0;
        for (int x = 0; x < 720; ++x) {
            if (element->ColumnIdAt(Point2Di(x, 200)) == columnId) ++span;
        }
        return span >= 150;
    }
};

UCEvent FileEvent(UCEventType type, const Point2Di& at, const std::vector<std::string>& files = {}) {
    UCEvent event;
    event.type = type;
    event.pointer = at;
    event.pointerWindow = at;
    event.droppedFiles = files;
    return event;
}

void TestColumnsAreFound() {
    std::cout << "\nThe columns under the pointer\n";
    Board board;
    const Point2Di docs = board.PointIn(board.docs);
    const Point2Di work = board.PointIn(board.work);
    const Point2Di home = board.PointIn(board.home);
    Check(docs.x >= 0 && work.x >= 0 && home.x >= 0, "each of Docs, Work and Home is somewhere on the board");
    Check(docs.x < work.x && work.x < home.x, "in their order, left to right");
    Check(board.element->ColumnIdAt(Point2Di(-50, 200)) == -1, "and outside the board there is none");
}

void TestDropReachesTheColumn() {
    std::cout << "\nFiles dropped on a column\n";
    Board board;
    std::vector<std::string> received;
    int receivedColumn = -2;
    int calls = 0;
    board.element->onFilesDropped = [&](const std::vector<std::string>& files, int columnId, int) {
        received = files;
        receivedColumn = columnId;
        ++calls;
    };
    const Point2Di work = board.PointIn(board.work);
    Check(board.element->OnEvent(FileEvent(UCEventType::DragOver, work)),
          "the hover is the board's while it takes files");
    const std::vector<std::string> files = {"/home/me/plan.pdf", "/home/me/Fotos/Strand.jpg"};
    Check(board.element->OnEvent(FileEvent(UCEventType::Drop, work, files)), "the drop is taken");
    Check(calls == 1, "the callback runs once for the drop");
    Check(receivedColumn == board.work, "with the column under the pointer");
    Check(received == files, "and every file, in order");

    const Point2Di home = board.PointIn(board.home);
    board.element->OnEvent(FileEvent(UCEventType::Drop, home, {"/tmp/x.txt"}));
    Check(receivedColumn == board.home, "a drop on another column names that one");
}

void TestDropWithoutCallbackPassesOn() {
    std::cout << "\nA board that takes no files\n";
    Board board;
    const Point2Di work = board.PointIn(board.work);
    Check(!board.element->OnEvent(FileEvent(UCEventType::DragOver, work)), "leaves the hover to its parents");
    Check(!board.element->OnEvent(FileEvent(UCEventType::Drop, work, {"/tmp/x.txt"})),
          "and the drop too");
}

void TestRenameFromCode() {
    std::cout << "\nA column renamed from code\n";
    Board board;
    const int added = board.data->AddColumn("New category");
    board.element->BeginColumnRename(added, /*startEmpty=*/true);
    UCEvent text;
    text.type = UCEventType::TextInput;
    text.text = "Ideas";
    board.element->OnEvent(text);
    UCEvent enter;
    enter.type = UCEventType::KeyDown;
    enter.virtualKey = UCKeys::Return;
    board.element->OnEvent(enter);
    const KanbanColumn* column = board.data->FindColumn(added);
    Check(column && column->title == "Ideas", "takes what is typed, not added to the old title");

    board.element->BeginColumnRename(added, /*startEmpty=*/true);
    board.element->OnEvent(enter);
    column = board.data->FindColumn(added);
    Check(column && column->title == "Ideas", "and confirming it empty keeps the title");

    // What the platforms really send: the typed text on the KeyDown itself.
    board.element->BeginColumnRename(added, /*startEmpty=*/true);
    for (const char* typed : {"T", "r", "\t", "a", "v", "e", "l"}) {
        UCEvent key;
        key.type = UCEventType::KeyDown;
        key.text = typed;
        key.character = typed[0];
        if (typed[0] == '\t') key.virtualKey = UCKeys::Tab;
        board.element->OnEvent(key);
    }
    UCEvent shortcut;
    shortcut.type = UCEventType::KeyDown;
    shortcut.ctrl = true;
    shortcut.text = "x";
    shortcut.character = 'x';
    board.element->OnEvent(shortcut);
    board.element->OnEvent(enter);
    column = board.data->FindColumn(added);
    Check(column && column->title == "Travel",
          "keys that type become the name; Tab and Ctrl+X do not");
}

void TestScrollToColumn() {
    std::cout << "\nA board too narrow for its columns\n";
    Board board;   // 720 px wide: four columns of 150 px or more do not fit with "+"
    std::vector<int> extra;
    for (const char* name : {"Ideas", "Travel", "Bills"}) extra.push_back(board.data->AddColumn(name));
    auto ctx = CreateRenderContext(Size2Di(720, 360), nullptr);
    auto draw = [&] { if (ctx) board.element->Render(ctx.get(), Rect2Df(0, 0, 720, 360)); };
    draw();
    const int last = extra.back();
    Check(!board.Shows(last), "does not show the last column to begin with");
    board.element->ScrollToColumn(last);
    draw();
    Check(board.Shows(last), "shows it after ScrollToColumn");
    Check(!board.Shows(board.docs), "having scrolled the first one out of view");
    board.element->ScrollToColumn(board.docs);
    draw();
    Check(board.Shows(board.docs), "and scrolls back for the first one");

    const int added = board.data->AddColumn("Added");
    board.element->BeginColumnRename(added, true);
    draw();
    Check(board.Shows(added), "a column opened for renaming is scrolled into view");
}

void TestSettingsKeepTheBoard() {
    std::cout << "\nThe Sticker board in the desktop settings\n";
    UltraDesktop::DesktopSettings settings;
    Check(settings.categoryBoard.board.empty(), "a new desktop has no board yet (the default categories apply)");

    KanbanDataSource data;
    const int docs = data.AddColumn("Docs");
    data.AddColumn("Work");
    const int card = data.AddCard(docs, "plan.pdf");
    data.SetCardDescription(card, "/home/me/plan.pdf");

    settings.categoryBoard.visible = true;
    settings.categoryBoard.design = "poster";
    settings.categoryBoard.x = 140;
    settings.categoryBoard.y = 90;
    settings.categoryBoard.width = 900;
    settings.categoryBoard.height = 500;
    settings.categoryBoard.board = data.ToJSON(false);

    UltraDesktop::DesktopSettings read;
    std::string error;
    Check(read.FromJson(settings.ToJson(), &error), "a saved desktop reads back" + (error.empty() ? "" : ": " + error));
    const auto& cb = read.categoryBoard;
    Check(cb.visible && cb.design == "poster", "with the board shown, in the Poster style");
    Check(cb.x == 140 && cb.y == 90 && cb.width == 900 && cb.height == 500, "where it was, at its size");

    KanbanDataSource back;
    Check(back.LoadFromJSONText(cb.board, &error), "and the board itself loads");
    Check(back.GetColumns().size() == 2 && back.GetColumns()[0].title == "Docs", "with its categories");
    const std::vector<int> cards = back.GetCardsInColumn(back.GetColumns()[0].id);
    const KanbanCard* file = cards.empty() ? nullptr : back.FindCard(cards.front());
    Check(file && file->title == "plan.pdf" && file->description == "/home/me/plan.pdf",
          "and its file, path and all");
}

void TestSettingsKeepThePins() {
    std::cout << "\nThe pinned applications in the desktop settings\n";
    UltraDesktop::DesktopSettings settings;
    Check(settings.pinnedApps.empty(), "a new desktop has no pins");
    settings.SetPinned("/usr/bin/gimp", true);
    settings.SetPinned("/usr/bin/kate", true);
    settings.SetPinned("/usr/bin/gimp", true);
    Check(settings.pinnedApps.size() == 2 && settings.pinnedApps[0] == "/usr/bin/gimp",
          "a pin is kept once, in the order it was pinned");
    Check(settings.IsPinned("/usr/bin/kate") && !settings.IsPinned("/usr/bin/inkscape"), "IsPinned answers for each");
    settings.SetPinned("", true);
    Check(settings.pinnedApps.size() == 2, "an empty key is no pin");

    UltraDesktop::DesktopSettings read;
    std::string error;
    Check(read.FromJson(settings.ToJson(), &error), "a saved desktop reads back" + (error.empty() ? "" : ": " + error));
    Check(read.pinnedApps == settings.pinnedApps, "with its pins, in order");
    read.SetPinned("/usr/bin/gimp", false);
    Check(read.pinnedApps.size() == 1 && read.pinnedApps[0] == "/usr/bin/kate", "and one unpinned leaves the other");
}

// A picture for a card: a plain coloured rectangle, w x h.
std::shared_ptr<UCPixmap> Picture(int w, int h) {
    auto pixmap = std::make_shared<UCPixmap>(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) pixmap->SetPixel(x, y, (x / 8 + y / 8) % 2 ? 0xFF3D7BD9u : 0xFFF2A93Bu);
    }
    pixmap->MarkDirty();
    return pixmap;
}

void Draw(Board& board, const std::string& png = "") {
    auto ctx = CreateRenderContext(Size2Di(720, 360), nullptr);
    if (!ctx) return;
    board.element->Render(ctx.get(), Rect2Df(0, 0, 720, 360));
    const std::string dir = GetEnvUtf8("KANBAN_TEST_PNG_DIR");
    if (!png.empty() && !dir.empty()) {
        cairo_surface_t* surface = cairo_get_target(static_cast<cairo_t*>(ctx->GetNativeContext()));
        cairo_surface_flush(surface);
        cairo_surface_write_to_png(surface, (dir + "/" + png).c_str());
    }
}

UCEvent MouseEvent(UCEventType type, const Point2Di& at, UCMouseButton button = UCMouseButton::NoneButton) {
    UCEvent event;
    event.type = type;
    event.pointer = at;
    event.pointerWindow = at;
    event.button = button;
    return event;
}

Point2Di CentreOf(const Rect2Df& r) {
    return Point2Di(static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + r.height / 2));
}

void TestCardDisplays() {
    std::cout << "\nA card's picture above or beside its name\n";
    Board board;
    const int card = board.data->AddCard(board.docs, "Holiday photo.jpg");
    board.data->AddCardTag(card, "jpg");
    board.element->cardImageProvider = [](const KanbanCard&) { return Picture(160, 110); };
    KanbanBoardStyle& style = board.element->EditStyle();

    style.cardImageHeight = 0;
    board.element->StyleChanged();
    Draw(board, "display-text.png");
    const Rect2Df text = board.element->GetCardBounds(card);

    style.cardImageHeight = 58;
    style.cardImagePlacement = KanbanCardImagePlacement::Top;
    style.centerCardTitle = true;
    board.element->StyleChanged();
    Draw(board, "display-thumbnail.png");
    const Rect2Df top = board.element->GetCardBounds(card);

    style.cardImageHeight = 34;
    style.cardImagePlacement = KanbanCardImagePlacement::Left;
    style.centerCardTitle = false;
    board.element->StyleChanged();
    Draw(board, "display-row.png");
    const Rect2Df left = board.element->GetCardBounds(card);

    Check(text.height > 0 && top.height > 0 && left.height > 0, "the card is laid out in each display");
    Check(top.height >= text.height + 58, "a picture on top makes the card taller by its band");
    Check(left.height >= 34 + 2 * style.cardPadding, "a picture beside the name is never cut off");
    Check(left.height < top.height, "and the row is lower than the picture on top");
    Check(board.element->GetCardBounds(9999).width == 0, "a card that is not there has no bounds");
}

void TestContextMenu() {
    std::cout << "\nA right click\n";
    Board board;
    const int card = board.data->AddCard(board.work, "plan.pdf");
    Draw(board);
    int calls = 0, gotCard = -2, gotColumn = -2;
    board.element->onContextMenu = [&](int cardId, int columnId, const Point2Di&) {
        ++calls;
        gotCard = cardId;
        gotColumn = columnId;
    };
    const Point2Di onCard = CentreOf(board.element->GetCardBounds(card));
    Check(board.element->OnEvent(MouseEvent(UCEventType::MouseDown, onCard, UCMouseButton::Right)),
          "on a card is the board's");
    Check(calls == 1 && gotCard == card && gotColumn == board.work, "and names the card and its category");
    Check(board.element->GetSelectedCardId() == card, "with the card selected");

    const Point2Di empty(board.PointIn(board.home).x, 300);
    board.element->OnEvent(MouseEvent(UCEventType::MouseDown, empty, UCMouseButton::Right));
    Check(calls == 2 && gotCard == -1 && gotColumn == board.home, "on an empty category it names only the category");

    Board quiet;
    Draw(quiet);
    Check(!quiet.element->OnEvent(MouseEvent(UCEventType::MouseDown, quiet.PointIn(quiet.docs), UCMouseButton::Right)),
          "with no callback the right click passes on");
}

void TestHoverAndPreview() {
    std::cout << "\nThe pointer resting on a card\n";
    Board board;
    const int card = board.data->AddCard(board.docs, "Holiday photo.jpg");
    const int other = board.data->AddCard(board.home, "notes.txt");
    board.element->SetEnableTooltips(false);   // a tooltip needs a window
    Draw(board);
    std::vector<int> hovered;
    board.element->onCardHoverChanged = [&](int cardId) { hovered.push_back(cardId); };
    const Point2Di onCard = CentreOf(board.element->GetCardBounds(card));
    board.element->OnEvent(MouseEvent(UCEventType::MouseMove, onCard));
    board.element->OnEvent(MouseEvent(UCEventType::MouseMove, Point2Di(onCard.x + 1, onCard.y)));
    Check(hovered.size() == 1 && hovered.back() == card, "names the card once, however the pointer moves on it");

    board.element->ShowCardPreview(card, Picture(400, 300), 300);
    Check(board.element->GetPreviewCardId() == card, "a preview can be shown for it");
    Draw(board, "hover-preview.png");
    board.element->OnEvent(MouseEvent(UCEventType::MouseMove, CentreOf(board.element->GetCardBounds(other))));
    Check(hovered.back() == other, "moving to another card names that one");
    Check(board.element->GetPreviewCardId() == -1, "and the first card's preview goes");

    board.element->ShowCardPreview(other, Picture(400, 300), 300);
    board.element->OnEvent(MouseEvent(UCEventType::MouseDown, CentreOf(board.element->GetCardBounds(other)),
                                      UCMouseButton::Left));
    Check(board.element->GetPreviewCardId() == -1, "a press takes the preview away");
    board.element->OnEvent(MouseEvent(UCEventType::MouseUp, CentreOf(board.element->GetCardBounds(other)),
                                      UCMouseButton::Left));

    board.element->ShowCardPreview(other, nullptr, 300);
    Check(board.element->GetPreviewCardId() == -1, "a preview without a picture is none");

    UCEvent leave;
    leave.type = UCEventType::MouseLeave;
    board.element->OnEvent(leave);
    Check(hovered.back() == -1, "leaving the board names no card");
}

void TestCardDragStillMoves() {
    std::cout << "\nA card dragged to another category\n";
    Board board;
    board.element->SetEditable(true);
    const int card = board.data->AddCard(board.docs, "plan.pdf");
    board.element->cardImageProvider = [](const KanbanCard&) { return Picture(160, 110); };
    board.element->EditStyle().cardImagePlacement = KanbanCardImagePlacement::Left;
    board.element->StyleChanged();
    Draw(board);
    const Point2Di from = CentreOf(board.element->GetCardBounds(card));
    const Point2Di to = board.PointIn(board.home);
    board.element->OnEvent(MouseEvent(UCEventType::MouseDown, from, UCMouseButton::Left));
    board.element->OnEvent(MouseEvent(UCEventType::MouseMove, Point2Di(from.x + 30, from.y)));
    board.element->OnEvent(MouseEvent(UCEventType::MouseMove, to));
    board.element->OnEvent(MouseEvent(UCEventType::MouseUp, to, UCMouseButton::Left));
    const KanbanCard* moved = board.data->FindCard(card);
    Check(moved && moved->columnId == board.home, "lands in that category");
}

void TestSettingsKeepTheDisplay() {
    std::cout << "\nThe Sticker board's display in the desktop settings\n";
    UltraDesktop::DesktopSettings settings;
    Check(settings.categoryBoard.display == "thumbnail", "a new desktop shows thumbnails with the name under them");
    Check(settings.categoryBoard.hoverPreview == "medium", "and a medium hover preview");
    settings.categoryBoard.display = "row";
    settings.categoryBoard.hoverPreview = "large";
    UltraDesktop::DesktopSettings read;
    std::string error;
    Check(read.FromJson(settings.ToJson(), &error), "a saved desktop reads back" + (error.empty() ? "" : ": " + error));
    Check(read.categoryBoard.display == "row" && read.categoryBoard.hoverPreview == "large", "with both choices");

    settings.categoryBoard.display = "sideways";
    settings.categoryBoard.hoverPreview = "huge";
    UltraDesktop::DesktopSettings odd;
    odd.FromJson(settings.ToJson(), &error);
    Check(odd.categoryBoard.display == "thumbnail" && odd.categoryBoard.hoverPreview == "medium",
          "a value it does not know is the default");
}

void TestDeleteKeyGoesToTheApplication() {
    std::cout << "\nThe Delete key on a selected card\n";
    auto press = [](Board& b) {
        UCEvent key;
        key.type = UCEventType::KeyDown;
        key.virtualKey = UCKeys::Delete;
        return b.element->OnEvent(key);
    };
    Board asked;
    asked.element->SetEditable(true);
    const int card = asked.data->AddCard(asked.work, "plan.pdf");
    Draw(asked);
    asked.element->SelectCard(card);
    int askedFor = -2;
    asked.element->onDeleteKey = [&](int cardId) {
        askedFor = cardId;
        return true;
    };
    Check(press(asked), "is taken");
    Check(askedFor == card && asked.data->FindCard(card) != nullptr,
          "the application is asked about the card, and the board removes nothing itself");

    Board plain;
    plain.element->SetEditable(true);
    const int other = plain.data->AddCard(plain.work, "notes");
    Draw(plain);
    plain.element->SelectCard(other);
    plain.element->onDeleteKey = [](int) { return false; };
    press(plain);
    Check(plain.data->FindCard(other) == nullptr, "answered false, the board removes the card as before");
}

void TestSettingsKeepTheTheme() {
    std::cout << "\nThe bars' theme in the desktop settings\n";
    UltraDesktop::DesktopSettings settings;
    Check(settings.theme == "dark", "a new desktop has dark bars");
    settings.theme = "light";
    UltraDesktop::DesktopSettings read;
    std::string error;
    Check(read.FromJson(settings.ToJson(), &error), "a saved desktop reads back" + (error.empty() ? "" : ": " + error));
    Check(read.theme == "light", "with the light theme");
    settings.theme = "purple";
    UltraDesktop::DesktopSettings odd;
    odd.FromJson(settings.ToJson(), &error);
    Check(odd.theme == "dark", "a theme it does not know is the dark one");
}

void TestSettingsKeepTheDropTarget() {
    std::cout << "\nWhere files dropped on the wallpaper go\n";
    UltraDesktop::DesktopSettings settings;
    Check(settings.dropFilesOn == "board", "a new desktop drops them on the Sticker board");
    settings.dropFilesOn = "desktop";
    UltraDesktop::DesktopSettings::IconPlace place;
    place.x = 120;
    place.y = 216;
    settings.desktopIcons["Report – März.pdf"] = place;
    UltraDesktop::DesktopSettings read;
    std::string error;
    Check(read.FromJson(settings.ToJson(), &error), "a saved desktop reads back" + (error.empty() ? "" : ": " + error));
    Check(read.dropFilesOn == "desktop", "dropping on the desktop");
    auto found = read.desktopIcons.find("Report – März.pdf");
    Check(found != read.desktopIcons.end() && found->second.x == 120 && found->second.y == 216,
          "with an icon's place, under a name that is not ASCII");
    settings.dropFilesOn = "floor";
    UltraDesktop::DesktopSettings odd;
    odd.FromJson(settings.ToJson(), &error);
    Check(odd.dropFilesOn == "board", "a target it does not know is the Sticker board");
}

} // namespace

int main() {
    std::cout << "KanbanFileDropTest\n";
    TestColumnsAreFound();
    TestDropReachesTheColumn();
    TestDropWithoutCallbackPassesOn();
    TestRenameFromCode();
    TestScrollToColumn();
    TestSettingsKeepTheBoard();
    TestSettingsKeepThePins();
    TestCardDisplays();
    TestContextMenu();
    TestHoverAndPreview();
    TestCardDragStillMoves();
    TestSettingsKeepTheDisplay();
    TestDeleteKeyGoesToTheApplication();
    TestSettingsKeepTheTheme();
    TestSettingsKeepTheDropTarget();
    std::cout << "\n" << (g_failures == 0 ? "All passed" : std::to_string(g_failures) + " failed") << "\n";
    return g_failures == 0 ? 0 : 1;
}
