// include/UltraCanvasListSortFilterProxy.h
// Sorting and filtering for any IListModel, without touching the model that
// holds the data.
//
// The proxy *is* an IListModel: it wraps a source model and presents the same
// columns with the rows re-ordered and thinned out. So a view is given the
// proxy instead of the model and needs no idea that either is happening -
// UltraCanvasListView already renders, virtualises, selects and navigates
// whatever model it is handed.
//
//     auto rows  = std::make_shared<UltraCanvasMultiColumnListModel>();
//     auto proxy = std::make_shared<UltraCanvasListSortFilterProxy>(rows);
//     listView->SetModel(proxy);
//     listView->SetSortingEnabled(true);          // header clicks sort
//     proxy->SetFilterText("olpe");               // and this filters
//
// Two things callers have to know, and the API makes both explicit:
//
//  1. **A proxy row is not a source row.** Anything that identifies a record -
//     a database id, an index into the caller's own vector - must go through
//     MapToSource() / MapFromSource(). The selection a view reports is in proxy
//     rows, and forgetting to map it is how a sorted table deletes the wrong
//     record.
//  2. **Sorting is stable**, so rows that compare equal keep the order the
//     source gave them, and sorting by one column then another leaves a
//     predictable result instead of an arbitrary one.
//
// Version: 1.0.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasListModel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ListSortOrder lives in UltraCanvasListModel.h, which both this proxy and the
// view that draws the sort indicator already include.

// How a column is compared when no custom comparator is installed for it.
enum class ListSortKind {
    Auto,               // numeric when both values parse as numbers, else Text
    Text,               // case-insensitive, the usual answer for names
    TextCaseSensitive,
    Number,             // "1234.56" and "1.234,56 €" alike; non-numbers sort last
    Natural             // text with embedded numbers: "Beleg 2" before "Beleg 10"
};

// Three-way comparison of two *source* rows on one column: negative when left
// sorts first, 0 when they are equal, positive otherwise. The order (ascending
// or descending) is applied by the proxy afterwards, so a comparator never has
// to know which way round it is being used.
using ListRowComparator =
    std::function<int(const IListModel& source, int leftRow, int rightRow, int column)>;

// True keeps the row. Called with *source* rows.
using ListRowFilter = std::function<bool(const IListModel& source, int sourceRow)>;

class UltraCanvasListSortFilterProxy : public IListModel {
public:
    UltraCanvasListSortFilterProxy() = default;
    explicit UltraCanvasListSortFilterProxy(std::shared_ptr<IListModel> source);
    ~UltraCanvasListSortFilterProxy() override;

    UltraCanvasListSortFilterProxy(const UltraCanvasListSortFilterProxy&) = delete;
    UltraCanvasListSortFilterProxy& operator=(const UltraCanvasListSortFilterProxy&) = delete;

    // ===== SOURCE =====
    // Attaching listens to the source's change notifications so the proxy
    // rebuilds itself when rows appear, vanish or change. Any handler the
    // source already carried is kept and called afterwards, so attaching a
    // proxy never silently disconnects something else.
    void SetSourceModel(std::shared_ptr<IListModel> source);
    IListModel* GetSourceModel() const { return source_.get(); }

    // ===== IListModel =====
    int GetRowCount() const override;
    int GetColumnCount() const override;
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
    bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) override;
    ListColumnDef GetColumnDef(int column) const override;

    // ===== SORTING =====
    // Sort by one column. Passing column < 0 clears the sort and restores the
    // source order.
    void SortByColumn(int column, ListSortOrder order = ListSortOrder::Ascending);
    void ClearSort() { SortByColumn(-1); }
    int           GetSortColumn() const { return sortColumn_; }
    ListSortOrder GetSortOrder() const { return sortOrder_; }
    bool          IsSorted() const { return sortColumn_ >= 0; }

    // What Auto/Text/Number/Natural mean per column. Unset columns use
    // ListSortKind::Auto.
    void SetColumnSortKind(int column, ListSortKind kind);
    ListSortKind GetColumnSortKind(int column) const;

    // A comparator for one column, which wins over the sort kind. Pass an empty
    // function to drop it.
    void SetColumnComparator(int column, ListRowComparator comparator);

    // ===== FILTERING =====
    // Case-insensitive substring over the filter columns (all of them by
    // default). An empty string clears it.
    void SetFilterText(const std::string& text);
    const std::string& GetFilterText() const { return filterText_; }

    // Which columns the text filter looks at. Empty means every column.
    void SetFilterColumns(std::vector<int> columns);
    const std::vector<int>& GetFilterColumns() const { return filterColumns_; }

    // An arbitrary predicate, applied in addition to the text filter: a row has
    // to pass both. Pass an empty function to drop it.
    void SetFilterPredicate(ListRowFilter predicate);

    void ClearFilter();
    bool IsFiltered() const { return !filterText_.empty() || static_cast<bool>(filterPredicate_); }

    // ===== ROW MAPPING =====
    // The proxy row for a source row, or -1 when that row is filtered out.
    int MapFromSource(int sourceRow) const;
    // The source row behind a proxy row, or -1 when the index is out of range.
    int MapToSource(int proxyRow) const;
    // Rows in the source, whether or not they survive the filter.
    int GetSourceRowCount() const;

    // ===== REBUILD =====
    // Re-run the filter and the sort. Needed only when the data changed behind
    // the proxy's back - a source model that reports its changes triggers this
    // by itself.
    void Invalidate();

    // Whether a source change rebuilds immediately (the default). Turning it
    // off is for bulk loading: the proxy then rebuilds on the next Invalidate()
    // or read.
    void SetDynamic(bool dynamic);
    bool IsDynamic() const { return dynamic_; }

    // ===== COMPARATOR BUILDING BLOCKS =====
    // The default comparisons, exposed because a custom comparator usually
    // wants one of them for its tie-breaks. Both return <0 / 0 / >0.
    static int CompareValues(const ListDataValue& left, const ListDataValue& right,
                             ListSortKind kind);
    static int CompareText(const std::string& left, const std::string& right,
                           bool caseSensitive);
    static int CompareNatural(const std::string& left, const std::string& right);
    // Reads a number out of text that may be written either way round -
    // "1234.56", "1.234,56", "-37,28 €" - without touching the C locale.
    // Returns false when the text is not a number.
    static bool TryReadNumber(const std::string& text, double& out);

private:
    void EnsureBuilt() const;
    void Rebuild() const;
    bool PassesFilter(int sourceRow) const;
    int  CompareRows(int leftSourceRow, int rightSourceRow) const;
    void AttachToSource();
    void DetachFromSource();
    void OnSourceChanged();

    std::shared_ptr<IListModel> source_;

    // The source's own notification handlers, chained rather than replaced.
    std::function<void()>    previousDataChanged_;
    std::function<void(int)> previousRowChanged_;
    std::function<void(int)> previousRowInserted_;
    std::function<void(int)> previousRowRemoved_;
    bool attached_ = false;

    int           sortColumn_ = -1;
    ListSortOrder sortOrder_  = ListSortOrder::Ascending;
    std::vector<ListSortKind>     sortKinds_;      // per column, grown on demand
    std::vector<ListRowComparator> comparators_;   // per column, grown on demand

    std::string      filterText_;       // already lower-cased
    std::vector<int> filterColumns_;
    ListRowFilter    filterPredicate_;

    bool dynamic_ = true;

    // Built lazily: proxy row -> source row, and its inverse.
    mutable std::vector<int> proxyToSource_;
    mutable std::vector<int> sourceToProxy_;
    mutable bool             built_ = false;
};

} // namespace UltraCanvas
