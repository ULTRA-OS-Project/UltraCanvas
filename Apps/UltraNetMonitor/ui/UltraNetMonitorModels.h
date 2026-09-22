// Apps/UltraNetMonitor/ui/UltraNetMonitorModels.h
// The list models the window shows: one row per connection, per process,
// per recorded flow, and per name the name table knows. All are plain
// IListModel implementations over the module's own structs, so
// UltraCanvasListView renders them and UltraCanvasListSortFilterProxy sorts
// and filters them; the numeric columns answer SortRole with the number, so
// "10" sorts after "9". A peer's name shows in a *Host* column, with a
// trailing "?" when it is a weak one (reverse DNS), never as a fact.
// Version: 0.4.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"
#include "NetworkMonitor/NetworkMonitorNames.h"
#include "NetworkMonitor/NetworkMonitorStore.h"
#include "UltraCanvasListModel.h"

#include <vector>

namespace UltraNetMonitor {

class ConnectionListModel : public UltraCanvas::IListModel {
public:
    enum Column { Application = 0, Pid, Protocol, Local, Remote, Host, State, Sent, Received, User, ColumnCount };

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
    enum Column { Application = 0, Pid, Connections, Established, Listening, Remotes, Sent, Received, ColumnCount };

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

// One row per recorded flow, for the History tab.
class FlowListModel : public UltraCanvas::IListModel {
public:
    enum Column { Application = 0, Pid, Protocol, Local, Remote, Host, State, FirstSeen, LastSeen,
                  Seen, Sent, Received, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override;
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(std::vector<UltraCanvas::RecordedFlow> rows);
    const UltraCanvas::RecordedFlow* At(int row) const;

private:
    std::vector<UltraCanvas::RecordedFlow> rows_;
};

// One row per address the name table has a name for, for the Names tab.
class NameListModel : public UltraCanvas::IListModel {
public:
    enum Column { Name = 0, Address, Source, Observed, Expires, Application, ColumnCount };

    int GetRowCount() const override;
    int GetColumnCount() const override;
    UltraCanvas::ListDataValue GetData(const UltraCanvas::ListIndex& index,
                                       UltraCanvas::ListDataRole role) const override;
    bool SetData(const UltraCanvas::ListIndex&, UltraCanvas::ListDataRole,
                 const UltraCanvas::ListDataValue&) override { return false; }
    UltraCanvas::ListColumnDef GetColumnDef(int column) const override;

    void Replace(std::vector<UltraCanvas::NameRecord> rows);
    const UltraCanvas::NameRecord* At(int row) const;

private:
    std::vector<UltraCanvas::NameRecord> rows_;
};

// "www.example.com" for an observed name, "www.example.com ?" for a weak
// one, empty for none. Shared by every list that shows a host.
std::string HostText(const std::string& name, UltraCanvas::NameSource source);
// The tooltip behind a host cell: which source, and how much to trust it.
std::string HostTooltip(const std::string& name, UltraCanvas::NameSource source);

} // namespace UltraNetMonitor
