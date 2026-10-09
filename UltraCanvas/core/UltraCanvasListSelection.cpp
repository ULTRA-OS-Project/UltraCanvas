// core/UltraCanvasListSelection.cpp
// Selection models for ListView
#include "UltraCanvasListSelection.h"
#include <algorithm>

namespace UltraCanvas {

    namespace {
        // Where `selected` is after `count` rows were inserted (count > 0) or
        // removed (count < 0) at `row`; -1 when it was one of those removed.
        int ShiftedRow(int selected, int row, int count) {
            if (selected < row) return selected;
            if (count >= 0) return selected + count;
            if (selected < row - count) return -1;   // removed
            return selected + count;
        }
    }

    void IListSelection::ShiftRows(int row, int count) {
        if (count == 0 || !HasSelection()) return;
        std::vector<int> rows;
        for (int r : GetSelectedRows()) {
            const int moved = ShiftedRow(r, row, count);
            if (moved >= 0) rows.push_back(moved);
        }
        if (rows == GetSelectedRows()) return;
        Clear();
        for (std::size_t i = 0; i < rows.size(); ++i) Select(rows[i], /*addToSelection=*/i > 0);
    }

    // ===== SINGLE SELECTION =====

    void UltraCanvasSingleSelection::Select(int row, bool /*addToSelection*/) {
        if (selectedRow == row) return;
        selectedRow = row;
        NotifySelectionChanged();
    }

    void UltraCanvasSingleSelection::Deselect(int row) {
        if (selectedRow == row) {
            selectedRow = -1;
            NotifySelectionChanged();
        }
    }

    void UltraCanvasSingleSelection::Clear() {
        if (selectedRow != -1) {
            selectedRow = -1;
            NotifySelectionChanged();
        }
    }

    void UltraCanvasSingleSelection::SelectRange(int /*fromRow*/, int toRow) {
        // Single selection: just select the end of the range
        Select(toRow);
    }

    bool UltraCanvasSingleSelection::IsSelected(int row) const {
        return selectedRow == row;
    }

    std::vector<int> UltraCanvasSingleSelection::GetSelectedRows() const {
        if (selectedRow >= 0) return {selectedRow};
        return {};
    }

    int UltraCanvasSingleSelection::GetCurrentRow() const {
        return selectedRow;
    }

    bool UltraCanvasSingleSelection::HasSelection() const {
        return selectedRow >= 0;
    }

    void UltraCanvasSingleSelection::ShiftRows(int row, int count) {
        if (selectedRow < 0 || count == 0) return;
        const int moved = ShiftedRow(selectedRow, row, count);
        if (moved == selectedRow) return;
        selectedRow = moved;
        if (moved < 0) NotifySelectionChanged();   // the selected item went away
    }

    // ===== MULTI SELECTION =====

    void UltraCanvasMultiSelection::Select(int row, bool addToSelection) {
        if (!addToSelection) {
            selectedRows.clear();
            selectedRows.insert(row);
            anchorRow = row;
            currentRow = row;
        } else {
            // Toggle: if already selected, deselect; otherwise add
            if (selectedRows.count(row)) {
                selectedRows.erase(row);
            } else {
                selectedRows.insert(row);
            }
            anchorRow = row;
            currentRow = row;
        }
        NotifySelectionChanged();
    }

    void UltraCanvasMultiSelection::Deselect(int row) {
        if (selectedRows.erase(row)) {
            if (currentRow == row) {
                currentRow = selectedRows.empty() ? -1 : *selectedRows.rbegin();
            }
            NotifySelectionChanged();
        }
    }

    void UltraCanvasMultiSelection::Clear() {
        if (!selectedRows.empty()) {
            selectedRows.clear();
            anchorRow = -1;
            currentRow = -1;
            NotifySelectionChanged();
        }
    }

    void UltraCanvasMultiSelection::SelectRange(int fromRow, int toRow) {
        selectedRows.clear();
        int lo = std::min(fromRow, toRow);
        int hi = std::max(fromRow, toRow);
        for (int i = lo; i <= hi; i++) {
            selectedRows.insert(i);
        }
        anchorRow = fromRow;
        currentRow = toRow;
        NotifySelectionChanged();
    }

    bool UltraCanvasMultiSelection::IsSelected(int row) const {
        return selectedRows.count(row) > 0;
    }

    std::vector<int> UltraCanvasMultiSelection::GetSelectedRows() const {
        return {selectedRows.begin(), selectedRows.end()};
    }

    int UltraCanvasMultiSelection::GetCurrentRow() const {
        return currentRow;
    }

    bool UltraCanvasMultiSelection::HasSelection() const {
        return !selectedRows.empty();
    }

    void UltraCanvasMultiSelection::ShiftRows(int row, int count) {
        if (count == 0) return;
        std::set<int> moved;
        bool lost = false;
        for (int r : selectedRows) {
            const int m = ShiftedRow(r, row, count);
            if (m >= 0) moved.insert(m); else lost = true;
        }
        selectedRows = std::move(moved);
        auto shift = [&](int& r) { if (r >= 0) r = ShiftedRow(r, row, count); };
        shift(anchorRow);
        shift(currentRow);
        if (currentRow < 0 && !selectedRows.empty()) currentRow = *selectedRows.rbegin();
        if (lost) NotifySelectionChanged();
    }

} // namespace UltraCanvas
