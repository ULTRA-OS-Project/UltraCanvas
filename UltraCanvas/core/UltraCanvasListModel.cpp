// core/UltraCanvasListModel.cpp
// Concrete model implementations for ListView
#include "UltraCanvasListModel.h"

namespace UltraCanvas {

    // ===== SIMPLE SINGLE-COLUMN MODEL =====

    int UltraCanvasSimpleListModel::GetRowCount() const {
        return static_cast<int>(items.size());
    }

    int UltraCanvasSimpleListModel::GetColumnCount() const {
        return 1;
    }

    ListDataValue UltraCanvasSimpleListModel::GetData(const ListIndex& index, ListDataRole role) const {
        if (index.row < 0 || index.row >= static_cast<int>(items.size()))
            return std::monostate{};

        const auto& item = items[index.row];
        switch (role) {
            case ListDataRole::DisplayRole:    return item.label;
            case ListDataRole::DecorationRole: return item.iconPath;
            case ListDataRole::ToolTipRole:    return item.tooltip;
            default: return std::monostate{};
        }
    }

    bool UltraCanvasSimpleListModel::SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) {
        if (index.row < 0 || index.row >= static_cast<int>(items.size()))
            return false;

        auto* str = std::get_if<std::string>(&value);
        if (!str) return false;

        auto& item = items[index.row];
        switch (role) {
            case ListDataRole::DisplayRole:    item.label = *str; break;
            case ListDataRole::DecorationRole: item.iconPath = *str; break;
            case ListDataRole::ToolTipRole:    item.tooltip = *str; break;
            default: return false;
        }
        NotifyRowChanged(index.row);
        return true;
    }

    void UltraCanvasSimpleListModel::AddItem(const ListItem& item) {
        items.push_back(item);
        NotifyRowInserted(static_cast<int>(items.size()) - 1);
    }

    void UltraCanvasSimpleListModel::AddItem(const std::string& label, const std::string& iconPath) {
        AddItem(ListItem(label, iconPath));
    }

    void UltraCanvasSimpleListModel::InsertItem(int row, const ListItem& item) {
        if (row < 0) row = 0;
        if (row > static_cast<int>(items.size())) row = static_cast<int>(items.size());
        items.insert(items.begin() + row, item);
        NotifyRowInserted(row);
    }

    void UltraCanvasSimpleListModel::RemoveItem(int row) {
        if (row < 0 || row >= static_cast<int>(items.size())) return;
        items.erase(items.begin() + row);
        NotifyRowRemoved(row);
    }

    void UltraCanvasSimpleListModel::Clear() {
        items.clear();
        NotifyDataChanged();
    }

    void UltraCanvasSimpleListModel::SetItems(const std::vector<ListItem>& newItems) {
        items = newItems;
        NotifyDataChanged();
    }

    int UltraCanvasSimpleListModel::GetItemCount() const {
        return static_cast<int>(items.size());
    }

    const ListItem& UltraCanvasSimpleListModel::GetItem(int row) const {
        return items[row];
    }

    ListItem& UltraCanvasSimpleListModel::GetItem(int row) {
        return items[row];
    }

    // ===== MULTI-COLUMN MODEL =====

    int UltraCanvasMultiColumnListModel::GetRowCount() const {
        return static_cast<int>(items.size());
    }

    int UltraCanvasMultiColumnListModel::GetColumnCount() const {
        return static_cast<int>(columns.size());
    }

    ListDataValue UltraCanvasMultiColumnListModel::GetData(const ListIndex& index, ListDataRole role) const {
        if (index.row < 0 || index.row >= static_cast<int>(items.size()))
            return std::monostate{};
        if (index.column < 0 || index.column >= static_cast<int>(columns.size()))
            return std::monostate{};

        const auto& item = items[index.row];
        switch (role) {
            case ListDataRole::DisplayRole:
                if (index.column < static_cast<int>(item.labels.size()))
                    return item.labels[index.column];
                return std::string{};

            case ListDataRole::DecorationRole:
                if (index.column < static_cast<int>(item.iconPaths.size()))
                    return item.iconPaths[index.column];
                return std::string{};

            case ListDataRole::ToolTipRole:
                return item.tooltip;

            default:
                return std::monostate{};
        }
    }

    bool UltraCanvasMultiColumnListModel::SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) {
        if (index.row < 0 || index.row >= static_cast<int>(items.size()))
            return false;
        if (index.column < 0 || index.column >= static_cast<int>(columns.size()))
            return false;

        auto* str = std::get_if<std::string>(&value);
        if (!str) return false;

        auto& item = items[index.row];
        switch (role) {
            case ListDataRole::DisplayRole:
                if (index.column >= static_cast<int>(item.labels.size()))
                    item.labels.resize(index.column + 1);
                item.labels[index.column] = *str;
                break;

            case ListDataRole::DecorationRole:
                if (index.column >= static_cast<int>(item.iconPaths.size()))
                    item.iconPaths.resize(index.column + 1);
                item.iconPaths[index.column] = *str;
                break;

            case ListDataRole::ToolTipRole:
                item.tooltip = *str;
                break;

            default:
                return false;
        }
        NotifyRowChanged(index.row);
        return true;
    }

    ListColumnDef UltraCanvasMultiColumnListModel::GetColumnDef(int column) const {
        if (column >= 0 && column < static_cast<int>(columns.size()))
            return columns[column];
        return ListColumnDef();
    }

    void UltraCanvasMultiColumnListModel::AddColumn(const ListColumnDef& colDef) {
        columns.push_back(colDef);
    }

    void UltraCanvasMultiColumnListModel::SetColumns(const std::vector<ListColumnDef>& colDefs) {
        columns = colDefs;
        NotifyDataChanged();
    }

    void UltraCanvasMultiColumnListModel::AddItem(const MultiColumnListItem& item) {
        items.push_back(item);
        NotifyRowInserted(static_cast<int>(items.size()) - 1);
    }

    void UltraCanvasMultiColumnListModel::InsertItem(int row, const MultiColumnListItem& item) {
        if (row < 0) row = 0;
        if (row > static_cast<int>(items.size())) row = static_cast<int>(items.size());
        items.insert(items.begin() + row, item);
        NotifyRowInserted(row);
    }

    void UltraCanvasMultiColumnListModel::RemoveItem(int row) {
        if (row < 0 || row >= static_cast<int>(items.size())) return;
        items.erase(items.begin() + row);
        NotifyRowRemoved(row);
    }

    void UltraCanvasMultiColumnListModel::Clear() {
        items.clear();
        NotifyDataChanged();
    }

    int UltraCanvasMultiColumnListModel::GetItemCount() const {
        return static_cast<int>(items.size());
    }

    const MultiColumnListItem& UltraCanvasMultiColumnListModel::GetItem(int row) const {
        return items[row];
    }

} // namespace UltraCanvas
