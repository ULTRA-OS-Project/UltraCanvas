// Tests/ListViewScrollTest.cpp
// Regression tests for two ListView / list-model behaviours a mail list
// depends on:
//  - EnsureRowVisible before the first layout. The view had no height yet,
//    measured against a zero (less the header, negative) viewport, and
//    scrolled the row below the top: UltraMail's message list opened two rows
//    down at every start, the selected newest message hidden above it. The
//    row is now revealed once the view has its size.
//  - UltraCanvasMultiColumnListModel::SetItems: every row with ONE change
//    notification (AddItem notifies per row).
//  - The selection follows rows inserted or removed above it. It stayed at
//    the same index, so while new mail streamed in above the message being
//    read, the highlight sat on whatever message slid into that row.
//  - The arrow keys go on from a row selected in code. The keyboard focus
//    stayed where it was, so after the application rebuilt or re-sorted the
//    list and selected the message being read, Down jumped to the top.
//  - What fitting a column to its content takes: MeasureHeaderWidth (the
//    whole title and the sort triangle) and MeasureColumnTextWidth (the
//    widest text of the column's rows, now - not the widest ever seen).
//    UltraMail's Date column is fitted with them; it was a fixed 88 px.
//  - A model outliving its view. The callbacks the view put on the model
//    pointed back at it, so a change after the view was destroyed called into
//    freed memory. They now do nothing once the view is gone - without being
//    taken off the model, which would cut off a sort/filter proxy chained on
//    after the view. OnModelChanged tells a subclass of every change instead.
//  - Columns wider than the view. There was no way to reach the rightmost
//    columns of a wide table (UltraDBAdmin's browse grid): a horizontal
//    scrollbar now shows below the rows, the header and the cells scroll
//    sideways together, ScrollToColumn / EnsureColumnVisible move them from
//    code, and the Left / Right keys step them - and are consumed by the
//    view, where before they bubbled up and switched the tab around it.
// No window and no display: the view is sized with SetBounds, and text is
// measured on an offscreen render context.
#include "UltraCanvasListView.h"
#include "UltraCanvasListModel.h"
#include "UltraCanvasListSortFilterProxy.h"
#include "UltraCanvasRenderContext.h"

#include <cmath>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;

#define CHECK_EQ(actual, expected) do { \
    auto _a = (actual); auto _e = (expected); \
    if (!(_a == _e)) { \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ \
                  << "  " #actual " == " #expected \
                  << "  [got " << _a << ", want " << _e << "]\n"; \
        ++failures; \
    } \
} while (0)

// A view with `rows` rows, filled the way a mail list is: the model is
// attached first, then its rows arrive (and the view hears of them).
static std::shared_ptr<UltraCanvasListView> MakeView(int rows) {
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ ListColumnDef("From", 120), ListColumnDef("Subject", 200) });
    auto view = std::make_shared<UltraCanvasListView>("list");
    ListViewStyle style;
    style.showHeader = true;
    style.headerHeight = 22;
    style.rowHeight = 22;
    view->SetStyle(style);
    view->SetModel(model);
    std::vector<MultiColumnListItem> items;
    for (int i = 0; i < rows; ++i)
        items.push_back(MultiColumnListItem({ "Sender " + std::to_string(i), "Subject" }));
    model->SetItems(std::move(items));
    return view;
}

// The first row, asked for before the view has a size, is at the top once it
// has one - not two rows down.
static void TestFirstRowBeforeLayout() {
    auto view = MakeView(500);
    view->EnsureRowVisible(0);
    CHECK_EQ(view->GetScrollMetrics().scrollOffset, 0);
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    CHECK_EQ(view->GetScrollMetrics().scrollOffset, 0);
}

// A row further down, asked for before the view has a size, is in view (at the
// bottom edge) once it has one.
static void TestLaterRowBeforeLayout() {
    auto view = MakeView(500);
    view->EnsureRowVisible(100);
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    const auto m = view->GetScrollMetrics();
    const int rowTop = 100 * 22, rowBottom = rowTop + 22;
    CHECK_EQ(m.scrollOffset <= rowTop, true);
    CHECK_EQ(m.scrollOffset + m.viewportHeight >= rowBottom, true);
    CHECK_EQ(m.scrollOffset > 0, true);
}

// Once laid out, EnsureRowVisible works as before.
static void TestAfterLayout() {
    auto view = MakeView(500);
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    view->EnsureRowVisible(200);
    const auto m = view->GetScrollMetrics();
    CHECK_EQ(m.scrollOffset + m.viewportHeight >= 201 * 22, true);
    view->EnsureRowVisible(0);
    CHECK_EQ(view->GetScrollMetrics().scrollOffset, 0);
}

// SetItems replaces the rows with one notification.
static void TestSetItemsNotifiesOnce() {
    UltraCanvasMultiColumnListModel model;
    model.SetColumns({ ListColumnDef("A", 100) });
    int changed = 0, inserted = 0;
    model.onDataChanged = [&changed]() { ++changed; };
    model.onRowInserted = [&inserted](int) { ++inserted; };
    std::vector<MultiColumnListItem> items;
    for (int i = 0; i < 1000; ++i) items.push_back(MultiColumnListItem({ std::to_string(i) }));
    model.SetItems(std::move(items));
    CHECK_EQ(changed, 1);
    CHECK_EQ(inserted, 0);
    CHECK_EQ(model.GetItemCount(), 1000);
    CHECK_EQ(GetStringValue(model.GetData(ListIndex{999, 0}, ListDataRole::DisplayRole)),
             std::string("999"));
    model.SetItems({});
    CHECK_EQ(model.GetItemCount(), 0);
    CHECK_EQ(changed, 2);
}

// The view moves the selection with the rows; the selected item stays
// selected without a selection-changed notification - only where it is
// changed - and a removed selected row is dropped (and notified).
static void TestSelectionFollowsInsertedAndRemovedRows() {
    auto view = MakeView(10);
    auto* model = static_cast<UltraCanvasMultiColumnListModel*>(view->GetModel());
    view->SetSelection(std::make_shared<UltraCanvasSingleSelection>());
    int notified = 0;
    view->onSelectionChanged = [&notified](const std::vector<int>&) { ++notified; };
    view->GetSelection()->Select(5);                       // "Sender 5"
    notified = 0;

    model->InsertItem(0, MultiColumnListItem({ "New", "mail" }));   // above it
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 6);
    model->InsertItem(9, MultiColumnListItem({ "Old", "mail" }));   // below it
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 6);
    CHECK_EQ(GetStringValue(model->GetData(ListIndex{6, 0}, ListDataRole::DisplayRole)),
             std::string("Sender 5"));
    model->RemoveItem(0);                                  // above it again
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 5);
    CHECK_EQ(notified, 0);                                 // the same item all along

    model->RemoveItem(5);                                  // the selected row itself
    CHECK_EQ(view->GetSelection()->HasSelection(), false);
    CHECK_EQ(notified, 1);
}

static void TestMultiSelectionShift() {
    UltraCanvasMultiSelection sel;
    sel.Select(2);
    sel.Select(4, /*addToSelection=*/true);
    sel.Select(7, /*addToSelection=*/true);
    sel.ShiftRows(3, 2);                  // two rows in at 3: 4 and 7 move down
    CHECK_EQ(sel.IsSelected(2), true);
    CHECK_EQ(sel.IsSelected(6), true);
    CHECK_EQ(sel.IsSelected(9), true);
    CHECK_EQ(sel.GetCurrentRow(), 9);
    sel.ShiftRows(5, -2);                 // rows 5 and 6 out: 6 goes, 9 moves up
    CHECK_EQ(sel.GetSelectedRows().size(), (size_t)2);
    CHECK_EQ(sel.IsSelected(2), true);
    CHECK_EQ(sel.IsSelected(7), true);
}

static UCEvent Key(UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    return e;
}

static void TestKeyboardFollowsSelectionSetInCode() {
    auto view = MakeView(20);
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    view->GetSelection()->Select(7);                 // the application selects row 7
    view->OnEvent(Key(UCKeys::Down));
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 8);

    // What a rebuild does: reset, then select the message being read.
    view->ResetSelection();
    view->GetSelection()->Select(12);
    view->OnEvent(Key(UCKeys::Up));
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 11);

    // Rows inserted above it move the keyboard focus with the selection.
    auto* model = static_cast<UltraCanvasMultiColumnListModel*>(view->GetModel());
    model->InsertItem(0, MultiColumnListItem({ "New", "mail" }));
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 12);
    view->OnEvent(Key(UCKeys::Down));
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 13);

    // Home still starts over.
    view->OnEvent(Key(UCKeys::Home));
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 0);
}

// `text` on one line in `font`, rounded up - what the view's measurements
// are made of.
static int TextWidth(IRenderContext* ctx, const std::string& text, const FontStyle& font) {
    auto layout = ctx->CreateTextLayout(text, false);
    layout->SetFontStyle(font);
    return static_cast<int>(std::ceil(layout->GetLayoutWidth()));
}

static void TestColumnFitMeasurements() {
    auto ctx = CreateRenderContext(Size2Di(200, 100), nullptr);
    CHECK_EQ(ctx != nullptr, true);
    if (!ctx) return;
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ ListColumnDef("Subject", 200), ListColumnDef("Date", 88) });
    auto view = std::make_shared<UltraCanvasListView>("list");
    ListViewStyle style;
    style.showHeader = true;
    style.headerFontSize = 9;
    style.sortIndicatorSize = 8;
    view->SetStyle(style);
    view->SetModel(model);
    model->SetItems({ MultiColumnListItem({ "a", "Oct 07" }),
                      MultiColumnListItem({ "b", "Jan 14, 2025" }),
                      MultiColumnListItem({ "c", "10:42" }) });

    // The header: its insets (4 px each end), the title, and the triangle's
    // strip (the triangle and 6 px), sorted or not.
    FontStyle headerFont;
    headerFont.fontSize = 9;
    const int title = TextWidth(ctx.get(), "Date", headerFont);
    CHECK_EQ(title > 0, true);
    CHECK_EQ(view->MeasureHeaderWidth(ctx.get(), 1), 8 + title + 14);
    view->SetSortIndicator(1, false);
    CHECK_EQ(view->MeasureHeaderWidth(ctx.get(), 1), 8 + title + 14);
    // A longer title - another language's - is measured as it reads.
    model->SetColumns({ ListColumnDef("Subject", 200), ListColumnDef("Empfangsdatum", 88) });
    CHECK_EQ(view->MeasureHeaderWidth(ctx.get(), 1),
             8 + TextWidth(ctx.get(), "Empfangsdatum", headerFont) + 14);
    CHECK_EQ(view->MeasureHeaderWidth(ctx.get(), 1) > 8 + title + 14, true);
    CHECK_EQ(view->MeasureHeaderWidth(ctx.get(), 7), 0);   // no such column

    // The rows: the widest text, in the font asked for.
    FontStyle body;
    body.fontSize = 9;
    FontStyle bold = body;
    bold.fontWeight = FontWeight::Bold;
    CHECK_EQ(view->MeasureColumnTextWidth(ctx.get(), 1, body), TextWidth(ctx.get(), "Jan 14, 2025", body));
    CHECK_EQ(view->MeasureColumnTextWidth(ctx.get(), 1, bold), TextWidth(ctx.get(), "Jan 14, 2025", bold));
    CHECK_EQ(view->MeasureColumnTextWidth(ctx.get(), 1, bold) >
             view->MeasureColumnTextWidth(ctx.get(), 1, body), true);
    // Without the old year, the column's text is only as wide as what is left.
    model->SetItems({ MultiColumnListItem({ "a", "Oct 07" }), MultiColumnListItem({ "c", "10:42" }) });
    CHECK_EQ(view->MeasureColumnTextWidth(ctx.get(), 1, body), TextWidth(ctx.get(), "Oct 07", body));
    model->SetItems({});
    CHECK_EQ(view->MeasureColumnTextWidth(ctx.get(), 1, body), 0);
}

// A view that counts the changes it is told of, in a counter it does not own.
class CountingView : public UltraCanvasListView {
public:
    CountingView(std::shared_ptr<int> counter) : UltraCanvasListView("list"), changes(std::move(counter)) {}
protected:
    void OnModelChanged() override { ++*changes; }
private:
    std::shared_ptr<int> changes;
};

static void TestModelOutlivesView() {
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ ListColumnDef("Subject", 200) });
    auto changes = std::make_shared<int>(0);
    auto view = std::make_shared<CountingView>(changes);
    view->SetModel(model);
    model->SetItems({ MultiColumnListItem({ "a" }), MultiColumnListItem({ "b" }) });
    model->InsertItem(0, MultiColumnListItem({ "c" }));
    model->RemoveItem(0);
    model->SetData(ListIndex{ 0, 0 }, ListDataRole::DisplayRole, std::string("a2"));
    CHECK_EQ(*changes, 4);   // replaced, inserted, removed, changed

    // A proxy chained on after the view keeps a copy of the view's callbacks.
    auto proxy = std::make_shared<UltraCanvasListSortFilterProxy>(model);
    CHECK_EQ(proxy->GetRowCount(), 2);
    view.reset();            // the view goes; the model and the proxy stay
    model->AddItem(MultiColumnListItem({ "d" }));
    model->SetItems({ MultiColumnListItem({ "e" }) });
    CHECK_EQ(*changes, 4);   // nothing reached the destroyed view
    CHECK_EQ(proxy->GetRowCount(), 1);   // and the proxy still follows its source
}

// A view 400 x 300 with four columns 900 px wide in all: the columns scroll
// sideways under the header, the rows area keeps its width beside the
// vertical scrollbar, and the keys and the code paths agree on the offset.
static std::shared_ptr<UltraCanvasListView> MakeWideView(int rows) {
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ ListColumnDef("A", 150), ListColumnDef("B", 200),
                        ListColumnDef("C", 250), ListColumnDef("D", 300) });
    auto view = std::make_shared<UltraCanvasListView>("wide");
    ListViewStyle style;
    style.showHeader = true;
    style.headerHeight = 22;
    style.rowHeight = 22;
    view->SetStyle(style);
    view->SetModel(model);
    std::vector<MultiColumnListItem> items;
    for (int i = 0; i < rows; ++i)
        items.push_back(MultiColumnListItem({ "a", "b", "c", "d" }));
    model->SetItems(std::move(items));
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    return view;
}

static void TestHorizontalScroll() {
    auto view = MakeWideView(5);          // 110 px of rows: no vertical scrollbar
    const int track = view->GetStyle().scrollbarStyle.trackSize;
    auto m = view->GetScrollMetrics();
    CHECK_EQ(m.contentWidth, 900);
    CHECK_EQ(m.viewportWidth, 398);       // 400 less the 1 px border each side
    CHECK_EQ(m.maxScrollX, 502);
    CHECK_EQ(m.horizontalScrollbarVisible, true);
    CHECK_EQ(m.scrollbarVisible, false);
    CHECK_EQ(m.viewportHeight, 298 - 22 - track);   // the header and the horizontal track
    CHECK_EQ(m.horizontalScrollbarBounds.x, 1);
    CHECK_EQ(m.horizontalScrollbarBounds.y, 299 - track);
    CHECK_EQ(m.horizontalScrollbarBounds.width, 398);
    CHECK_EQ(m.horizontalScrollbarBounds.height, track);
    CHECK_EQ(m.scrollOffsetX, 0);

    // The keys step the columns, and the view keeps them.
    CHECK_EQ(view->OnEvent(Key(UCKeys::Right)), true);
    const int step = view->GetHorizontalScrollOffset();
    CHECK_EQ(step > 0, true);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Right)), true);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 2 * step);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Left)), true);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Left)), true);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 0);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Left)), true);   // at the start: still the view's
    CHECK_EQ(view->GetHorizontalScrollOffset(), 0);

    // The cells, the columns under a point and the header follow the scroll.
    CHECK_EQ(view->GetCellRect(0, 1).x, 1 + 150);
    view->ScrollToColumn(2);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 350);
    CHECK_EQ(view->GetCellRect(0, 2).x, 1);
    CHECK_EQ(view->GetCellRect(0, 2).width, 250);
    CHECK_EQ(view->GetCellRect(0, 0).x, 1 - 350);
    int columnStart = -1;
    CHECK_EQ(view->GetColumnAt(1, &columnStart), 2);
    CHECK_EQ(columnStart, 1);
    CHECK_EQ(view->GetColumnAt(1 + 250, &columnStart), 3);
    CHECK_EQ(columnStart, 251);
    CHECK_EQ(view->GetHeaderColumnAt(1, 5, &columnStart), 2);
    CHECK_EQ(columnStart, 1);
    CHECK_EQ(view->ColumnBoundaryAt(251, 5), 2);          // the border after column C

    // EnsureColumnVisible scrolls just far enough, either way.
    view->EnsureColumnVisible(0);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 0);
    view->EnsureColumnVisible(3);                          // [600, 900) into 398 px
    CHECK_EQ(view->GetHorizontalScrollOffset(), 502);
    view->EnsureColumnVisible(3);                          // already in view
    CHECK_EQ(view->GetHorizontalScrollOffset(), 502);
    view->EnsureColumnVisible(2);                          // [350, 600): cut off on the left
    CHECK_EQ(view->GetHorizontalScrollOffset(), 350);
    view->EnsureColumnVisible(3);                          // [600, 900): cut off on the right again
    CHECK_EQ(view->GetHorizontalScrollOffset(), 502);
    view->EnsureColumnVisible(2);                          // [350, 600): its left edge hidden at 502
    CHECK_EQ(view->GetHorizontalScrollOffset(), 350);
    view->EnsureColumnVisible(1);                          // [150, 350): its left edge
    CHECK_EQ(view->GetHorizontalScrollOffset(), 150);
    // A column wider than the view is shown from its left edge.
    view->SetColumnWidth(1, 500);
    CHECK_EQ(view->GetScrollMetrics().contentWidth, 1200);
    view->ScrollToColumn(0);
    view->EnsureColumnVisible(1);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 150);
    // Scrolling to a column near the end stops at the end.
    view->ScrollToColumn(3);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 1200 - 398);
    // Narrower columns bring the offset back into range.
    view->SetColumnWidth(1, 10);
    view->SetColumnWidth(2, 10);
    view->SetColumnWidth(3, 10);
    CHECK_EQ(view->GetScrollMetrics().horizontalScrollbarVisible, false);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 0);
    CHECK_EQ(view->GetScrollMetrics().viewportHeight, 298 - 22);
}

// Both scrollbars at once: each stops short of the other's track, and the
// rows area loses a track in each direction.
static void TestBothScrollbars() {
    auto view = MakeWideView(500);
    const int track = view->GetStyle().scrollbarStyle.trackSize;
    const auto m = view->GetScrollMetrics();
    CHECK_EQ(m.scrollbarVisible, true);
    CHECK_EQ(m.horizontalScrollbarVisible, true);
    CHECK_EQ(m.viewportWidth, 398 - track);
    CHECK_EQ(m.viewportHeight, 298 - 22 - track);
    CHECK_EQ(m.maxScrollX, 900 - (398 - track));
    CHECK_EQ(m.maxScroll, 500 * 22 - (298 - 22 - track));
    CHECK_EQ(m.scrollbarBounds.x, 399 - track);
    CHECK_EQ(m.scrollbarBounds.y, 1 + 22);
    CHECK_EQ(m.scrollbarBounds.height, 298 - 22 - track);
    CHECK_EQ(m.horizontalScrollbarBounds.y, 299 - track);
    CHECK_EQ(m.horizontalScrollbarBounds.width, 398 - track);

    // Columns that fit exactly need the horizontal scrollbar once the
    // vertical one takes its track off their width.
    auto fits = MakeWideView(500);
    fits->SetColumnWidth(0, 98);
    fits->SetColumnWidth(1, 100);
    fits->SetColumnWidth(2, 100);
    fits->SetColumnWidth(3, 100);
    const auto f = fits->GetScrollMetrics();
    CHECK_EQ(f.contentWidth, 398);
    CHECK_EQ(f.scrollbarVisible, true);
    CHECK_EQ(f.horizontalScrollbarVisible, true);
    CHECK_EQ(f.maxScrollX, track);

    // The end of the rows is still reachable with the horizontal track
    // taken off the rows area.
    view->EnsureRowVisible(499);
    CHECK_EQ(view->GetScrollMetrics().scrollOffset, m.maxScroll);
}

// A table whose columns fit keeps the Left / Right keys too, so a focused
// table never switches the tab around it; and the rows' Up / Down are as
// they were.
static void TestKeysStayWithTheTable() {
    auto view = MakeView(5);                               // 320 px of columns in 398
    view->SetBounds(Rect2Df(0, 0, 400, 300));
    CHECK_EQ(view->GetScrollMetrics().horizontalScrollbarVisible, false);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Right)), true);
    CHECK_EQ(view->GetHorizontalScrollOffset(), 0);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Left)), true);
    CHECK_EQ(view->OnEvent(Key(UCKeys::Down)), true);
    CHECK_EQ(view->GetSelection()->GetCurrentRow(), 0);
    // An empty table has nothing for the row keys, but keeps the column keys.
    auto empty = MakeWideView(0);
    CHECK_EQ(empty->GetScrollMetrics().horizontalScrollbarVisible, true);   // the header scrolls
    CHECK_EQ(empty->OnEvent(Key(UCKeys::Down)), false);
    CHECK_EQ(empty->OnEvent(Key(UCKeys::Right)), true);
    CHECK_EQ(empty->GetHorizontalScrollOffset() > 0, true);
}

int main() {
    TestHorizontalScroll();
    TestBothScrollbars();
    TestKeysStayWithTheTable();
    TestFirstRowBeforeLayout();
    TestLaterRowBeforeLayout();
    TestAfterLayout();
    TestSetItemsNotifiesOnce();
    TestSelectionFollowsInsertedAndRemovedRows();
    TestMultiSelectionShift();
    TestKeyboardFollowsSelectionSetInCode();
    TestColumnFitMeasurements();
    TestModelOutlivesView();
    if (failures) {
        std::cerr << "ListViewScrollTest: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ListViewScrollTest: all passed\n";
    return 0;
}
