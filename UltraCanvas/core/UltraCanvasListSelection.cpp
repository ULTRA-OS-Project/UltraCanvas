// core/UltraCanvasListSelection.cpp
// Selection models for ListView
#include "UltraCanvasListSelection.h"
#include <algorithm>

namespace UltraCanvas {

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

} // namespace UltraCanvas
