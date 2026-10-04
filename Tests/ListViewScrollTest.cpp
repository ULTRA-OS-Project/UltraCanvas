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
// No window and no display: the view is sized with SetBounds.
#include "UltraCanvasListView.h"
#include "UltraCanvasListModel.h"

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

int main() {
    TestFirstRowBeforeLayout();
    TestLaterRowBeforeLayout();
    TestAfterLayout();
    TestSetItemsNotifiesOnce();
    TestSelectionFollowsInsertedAndRemovedRows();
    TestMultiSelectionShift();
    if (failures) {
        std::cerr << "ListViewScrollTest: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "ListViewScrollTest: all passed\n";
    return 0;
}
