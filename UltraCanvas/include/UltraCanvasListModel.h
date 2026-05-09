// include/UltraCanvasListModel.h
// Model interfaces and concrete models for ListView (Model-View-Delegate)
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <variant>
#include <functional>

namespace UltraCanvas {

    // ===== DATA ROLES =====

    enum class ListDataRole {
        DisplayRole = 0,        // std::string - primary text
        DecorationRole = 1,     // std::string - icon path
        ToolTipRole = 2,        // std::string - tooltip text
        UserRole = 256          // Starting point for user-defined roles
    };

    // ===== INDEX =====

    struct ListIndex {
        int row = -1;
        int column = 0;

        bool IsValid() const { return row >= 0; }

        bool operator==(const ListIndex& other) const {
            return row == other.row && column == other.column;
        }
        bool operator!=(const ListIndex& other) const {
            return !(*this == other);
        }
    };

    // ===== DATA VALUE =====

    using ListDataValue = std::variant<std::monostate, std::string, int, float, Color>;

    // Helper to extract string from ListDataValue (returns empty string if not a string)
    inline std::string GetStringValue(const ListDataValue& val) {
        if (auto* s = std::get_if<std::string>(&val)) return *s;
        return {};
    }

    // ===== COLUMN DEFINITION =====

    struct ListColumnDef {
        std::string title;
        int width = 100;
        TextAlignment alignment = TextAlignment::Left;

        ListColumnDef() = default;
        ListColumnDef(const std::string& t, int w = 100, TextAlignment a = TextAlignment::Left)
            : title(t), width(w), alignment(a) {}
    };

    // ===== ABSTRACT MODEL INTERFACE =====

    class IListModel {
    public:
        virtual ~IListModel() = default;

        virtual int GetRowCount() const = 0;
        virtual int GetColumnCount() const = 0;

        virtual ListDataValue GetData(const ListIndex& index, ListDataRole role) const = 0;
        virtual bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) = 0;

        virtual ListColumnDef GetColumnDef(int column) const {
            return ListColumnDef("", 100);
        }

        // Notification callbacks (the view connects to these)
        std::function<void()> onDataChanged;
        std::function<void(int row)> onRowChanged;
        std::function<void(int row)> onRowInserted;
        std::function<void(int row)> onRowRemoved;

    protected:
        void NotifyDataChanged() { if (onDataChanged) onDataChanged(); }
        void NotifyRowChanged(int row) { if (onRowChanged) onRowChanged(row); }
        void NotifyRowInserted(int row) { if (onRowInserted) onRowInserted(row); }
        void NotifyRowRemoved(int row) { if (onRowRemoved) onRowRemoved(row); }
    };

    // ===== SINGLE-COLUMN LIST ITEM =====

    struct ListItem {
        std::string label;
        std::string iconPath;
        std::string tooltip;
        void* userData = nullptr;

        ListItem() = default;
        ListItem(const std::string& text) : label(text) {}
        ListItem(const std::string& text, const std::string& icon)
            : label(text), iconPath(icon) {}
        ListItem(const std::string& text, const std::string& icon, const std::string& tip)
            : label(text), iconPath(icon), tooltip(tip) {}
    };

    // ===== SIMPLE SINGLE-COLUMN MODEL =====

    class UltraCanvasSimpleListModel : public IListModel {
    public:
        int GetRowCount() const override;
        int GetColumnCount() const override;
        ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
        bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) override;

        // Item management
        void AddItem(const ListItem& item);
        void AddItem(const std::string& label, const std::string& iconPath = "");
        void InsertItem(int row, const ListItem& item);
        void RemoveItem(int row);
        void Clear();
        void SetItems(const std::vector<ListItem>& newItems);

        int GetItemCount() const;
        const ListItem& GetItem(int row) const;
        ListItem& GetItem(int row);

    private:
        std::vector<ListItem> items;
    };

    // ===== MULTI-COLUMN LIST ITEM =====

    struct MultiColumnListItem {
        std::vector<std::string> labels;
        std::vector<std::string> iconPaths;
        std::string tooltip;
        void* userData = nullptr;

        MultiColumnListItem() = default;
        MultiColumnListItem(const std::vector<std::string>& texts) : labels(texts) {}
        MultiColumnListItem(const std::vector<std::string>& texts, const std::vector<std::string>& icons)
            : labels(texts), iconPaths(icons) {}
    };

    // ===== MULTI-COLUMN MODEL =====

    class UltraCanvasMultiColumnListModel : public IListModel {
    public:
        int GetRowCount() const override;
        int GetColumnCount() const override;
        ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
        bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) override;
        ListColumnDef GetColumnDef(int column) const override;

        // Column setup
        void AddColumn(const ListColumnDef& colDef);
        void SetColumns(const std::vector<ListColumnDef>& colDefs);

        // Item management
        void AddItem(const MultiColumnListItem& item);
        void InsertItem(int row, const MultiColumnListItem& item);
        void RemoveItem(int row);
        void Clear();

        int GetItemCount() const;
        const MultiColumnListItem& GetItem(int row) const;

    private:
        std::vector<ListColumnDef> columns;
        std::vector<MultiColumnListItem> items;
    };

} // namespace UltraCanvas
