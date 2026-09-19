// Apps/UltraNetMonitor/ui/UltraNetMonitorModels.h
// The two list models the window shows: one row per connection, and one row
// per process. Both are plain IListModel implementations over the module's
// own structs, so UltraCanvasListView renders them and
// UltraCanvasListSortFilterProxy sorts and filters them; the numeric columns
// answer SortRole with the number, so "10" sorts after "9".
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"
#include "UltraCanvasListModel.h"

#include <vector>

namespace UltraNetMonitor {

class ConnectionListModel : public UltraCanvas::IListModel {
public:
    enum Column { Application = 0, Pid, Protocol, Local, Remote, State, User, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override;
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    // Replaces every row; the view and any proxy rebuild from onDataChanged.
    void Replace(std::vector<UltraCanvas::NetworkConnection> rows);
    const UltraCanvas::NetworkConnection* At(int row) const;
    const std::vector<UltraCanvas::NetworkConnection>& Rows() const { return rows_; }

private:
    std::vector<UltraCanvas::NetworkConnection> rows_;
};

class ProcessListModel : public UltraCanvas::IListModel {
public:
    enum Column { Application = 0, Pid, Connections, Established, Listening, Remotes, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override;
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(std::vector<UltraCanvas::ProcessTrafficSummary> rows);
    const UltraCanvas::ProcessTrafficSummary* At(int row) const;

private:
    std::vector<UltraCanvas::ProcessTrafficSummary> rows_;
};

} // namespace UltraNetMonitor
