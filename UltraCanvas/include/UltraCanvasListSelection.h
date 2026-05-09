// include/UltraCanvasListSelection.h
// Selection models for ListView: single and multi-selection
#pragma once

#include <vector>
#include <set>
#include <functional>

namespace UltraCanvas {

    // Abstract selection interface
    class IListSelection {
    public:
        virtual ~IListSelection() = default;

        virtual void Select(int row, bool addToSelection = false) = 0;
        virtual void Deselect(int row) = 0;
        virtual void Clear() = 0;
        virtual void SelectRange(int fromRow, int toRow) = 0;

        virtual bool IsSelected(int row) const = 0;
        virtual std::vector<int> GetSelectedRows() const = 0;
        virtual int GetCurrentRow() const = 0;
        virtual bool HasSelection() const = 0;

        // Notification callback
        std::function<void(const std::vector<int>&)> onSelectionChanged;

    protected:
        void NotifySelectionChanged() {
            if (onSelectionChanged) onSelectionChanged(GetSelectedRows());
        }
    };

    // Single-row selection
    class UltraCanvasSingleSelection : public IListSelection {
    public:
        void Select(int row, bool addToSelection = false) override;
        void Deselect(int row) override;
        void Clear() override;
        void SelectRange(int fromRow, int toRow) override;

        bool IsSelected(int row) const override;
        std::vector<int> GetSelectedRows() const override;
        int GetCurrentRow() const override;
        bool HasSelection() const override;

    private:
        int selectedRow = -1;
    };

    // Multi-row selection with ctrl-click toggle and shift-click range
    class UltraCanvasMultiSelection : public IListSelection {
    public:
        void Select(int row, bool addToSelection = false) override;
        void Deselect(int row) override;
        void Clear() override;
        void SelectRange(int fromRow, int toRow) override;

        bool IsSelected(int row) const override;
        std::vector<int> GetSelectedRows() const override;
        int GetCurrentRow() const override;
        bool HasSelection() const override;

    private:
        std::set<int> selectedRows;
        int anchorRow = -1;
        int currentRow = -1;
    };

} // namespace UltraCanvas
