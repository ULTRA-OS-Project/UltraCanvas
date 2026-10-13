// Tests/KanbanFileDropTest.cpp
// Files dropped onto a Kanban board, and what UltraDesktop's Sticker board
// keeps of it.
//
// The board is drawn into an offscreen context, which lays out its columns
// as a window would, and then handed the events the platform backends make
// of a file drag: DragOver while the files hover, Drop with the paths. The
// files must reach onFilesDropped with the column under the pointer; with no
// callback the drop must pass on. The desktop's settings must keep the
// Sticker board - its style, its place and the board itself - across a save.
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasKanbanBoard.h"
#include "UltraCanvasRenderContext.h"

#include "UltraDesktopSettings.h"

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

} // namespace

int main() {
    std::cout << "KanbanFileDropTest\n";
    TestColumnsAreFound();
    TestDropReachesTheColumn();
    TestDropWithoutCallbackPassesOn();
    TestRenameFromCode();
    TestSettingsKeepTheBoard();
    std::cout << "\n" << (g_failures == 0 ? "All passed" : std::to_string(g_failures) + " failed") << "\n";
    return g_failures == 0 ? 0 : 1;
}
