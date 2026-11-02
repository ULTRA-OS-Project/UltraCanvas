// Tests/FilerDetailsColumnsTest.cpp
// How the Details columns of UltraCanvasFilerWidget take a new width.
// PushFollowing (the default) changes the column it is given and nothing else
// - the columns after it move along with their widths intact, the Name column
// included once it is given a width of its own. SplitWithNeighbour, the
// split-pane behaviour, takes the width the Name column gains from the column
// after it, so the table keeps spanning the view.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

int Width(const UltraCanvasFilerWidget& f, FilerDetailsColumn c) {
    return f.GetDetailsColumnWidth(c);
}

void TestPushFollowing() {
    std::cout << "\n-- push the following columns (default) --\n";
    UltraCanvasFilerWidget filer("test-filer", 0, 0, 900, 400);
    Check(filer.GetDetailsColumnResizeMode() == FilerColumnResizeMode::PushFollowing,
          "pushing is the default");

    const int size = Width(filer, FilerDetailsColumn::Size);
    const int modified = Width(filer, FilerDetailsColumn::ModifiedDate);

    filer.SetDetailsColumnWidth(FilerDetailsColumn::Type, 240);
    Check(Width(filer, FilerDetailsColumn::Type) == 240, "the column takes its width");
    Check(Width(filer, FilerDetailsColumn::ModifiedDate) == modified,
          "the column after it keeps its width");

    filer.SetDetailsColumnWidth(FilerDetailsColumn::Name, 400);
    Check(Width(filer, FilerDetailsColumn::Name) == 400, "Name takes its width");
    Check(Width(filer, FilerDetailsColumn::Size) == size,
          "widening Name leaves Size alone");

    filer.SetDetailsColumnWidth(FilerDetailsColumn::Info, 5000);
    Check(Width(filer, FilerDetailsColumn::Info) == 5000,
          "a column may be wider than the view (it scrolls sideways)");

    filer.ResetDetailsColumnWidths();
    Check(Width(filer, FilerDetailsColumn::Type) != 240, "reset restores the built-in widths");
}

void TestSplitWithNeighbour() {
    std::cout << "\n-- split with the neighbour --\n";
    UltraCanvasFilerWidget filer("test-filer", 0, 0, 900, 400);
    filer.SetDetailsColumnResizeMode(FilerColumnResizeMode::SplitWithNeighbour);
    Check(filer.GetDetailsColumnResizeMode() == FilerColumnResizeMode::SplitWithNeighbour,
          "the mode is taken");

    const int name = Width(filer, FilerDetailsColumn::Name);
    const int size = Width(filer, FilerDetailsColumn::Size);
    filer.SetDetailsColumnWidth(FilerDetailsColumn::Name, name + 30);
    Check(Width(filer, FilerDetailsColumn::Size) == size - 30,
          "the width Name gains comes out of the column after it");

    const int modified = Width(filer, FilerDetailsColumn::ModifiedDate);
    filer.SetDetailsColumnWidth(FilerDetailsColumn::Type, 200);
    Check(Width(filer, FilerDetailsColumn::ModifiedDate) == modified,
          "a column other than Name set from code leaves its neighbour alone");
}

}  // namespace

int main() {
    std::cout << "=== Filer Details columns ===\n";
    TestPushFollowing();
    TestSplitWithNeighbour();
    std::cout << "\n" << (g_failures == 0 ? "All Details column tests passed."
                                          : "SOME DETAILS COLUMN TESTS FAILED.")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
