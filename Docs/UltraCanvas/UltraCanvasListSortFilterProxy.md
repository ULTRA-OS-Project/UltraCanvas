# UltraCanvasListSortFilterProxy

**Sorting and filtering for any `IListModel`, without touching the model that
holds the data.** The proxy *is* an `IListModel`: it wraps a source model and
presents the same columns with the rows re-ordered and thinned out, so a view is
handed the proxy instead of the model and needs no idea that either is
happening.

- Header: `UltraCanvas/include/UltraCanvasListSortFilterProxy.h`
- Implementation: `UltraCanvas/core/UltraCanvasListSortFilterProxy.cpp`
- Tests: `Tests/ListSortFilterProxyTests.cpp` (target `ListSortFilterProxyTests`)
- Works with: [`UltraCanvasListView`](UltraCanvasListViewExamples.md), which
  already renders, virtualises, selects and navigates whatever model it is given
- Live example: DemoApp → ListView page, table 2 (`Apps/DemoApp/UltraCanvasListViewExamples.cpp`),
  with header-click sorting behind a "Sortable columns" checkbox

```cpp
auto rows  = std::make_shared<UltraCanvasMultiColumnListModel>();
rows->SetColumns({ ListColumnDef("Rechnung", 120),
                   ListColumnDef("Kunde", 200),
                   ListColumnDef("Betrag", 100, TextAlignment::Right) });
rows->AddItem(MultiColumnListItem{{ "R-202607010", "Bear Fruit Ltd.", "189,34" }});

auto proxy = std::make_shared<UltraCanvasListSortFilterProxy>(rows);

auto view = CreateListView("invoices", 0, 0, 800, 400);
view->SetModel(proxy);
view->SetShowHeader(true);
proxy->SetFilterText("olpe");       // this filters

// Header clicks sort, with an indicator. The view shows the order but never
// decides it, so the two lines that do it live here rather than inside the view.
view->onHeaderClicked = [view, proxy](int column) {
    const bool ascending = !(column == view->GetSortColumn() && view->GetSortAscending());
    proxy->SortByColumn(column, ascending ? ListSortOrder::Ascending
                                          : ListSortOrder::Descending);
    view->SetSortIndicator(column, ascending);
};
```

## The two things a caller has to know

**1. A proxy row is not a source row.** Anything that identifies a record — a
database id, an index into your own vector — goes through `MapToSource()` /
`MapFromSource()`. The selection a view reports is in *proxy* rows, and
forgetting to map it is how a sorted table deletes the wrong record.

```cpp
for (int proxyRow : view->GetSelection()->GetSelectedRows()) {
    const int sourceRow = proxy->MapToSource(proxyRow);   // ← never skip this
    store.Delete(records[sourceRow].id);
}
```

**2. Sorting is stable.** Rows that compare equal keep the order the source gave
them, so sorting by one column and then another leaves a predictable result
instead of an arbitrary one.

## Sorting

```cpp
proxy->SortByColumn(2, ListSortOrder::Descending);
proxy->ClearSort();                       // back to source order
proxy->SetColumnSortKind(0, ListSortKind::Natural);
proxy->SetColumnComparator(3, myComparator);
```

| `ListSortKind` | What it does |
|---|---|
| `Auto` (default) | numeric when both values parse as numbers, otherwise case-insensitive text |
| `Text` | case-insensitive |
| `TextCaseSensitive` | byte order |
| `Number` | `1234.56` and `1.234,56 €` alike; anything that is not a number sorts last |
| `Natural` | text with embedded numbers: `Beleg 2` before `Beleg 10` |

`TryReadNumber` reads both decimal conventions — `1234.56`, `1.234,56`,
`-37,28 €`, `(1.234,56)` — **without touching the C locale**, so a
comma-decimal desktop cannot change how a column sorts. It is deliberately
strict: a letter anywhere means "not a number". An earlier version skipped `E`,
`U` and `R` so that `EUR 89,00` would read as a number, and that made
`R-202607010` parse as **−202607010**, silently reversing a column of document
numbers. Text that is not unambiguously a number is compared as text instead.

An empty cell sorts after the filled ones in ascending order (and therefore
before them in descending order — the proxy inverts the whole comparison rather
than asking comparators to know which direction they are used in).

### `SortRole`: sort by the value, display the formatting

A column showing `1.234,56 €` or `17.09.2026` should sort by the amount or the
date, not by the text. The model returns the sort key from
`ListDataRole::SortRole`; the proxy prefers it and falls back to `DisplayRole`
when there is none:

```cpp
ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
    if (role == ListDataRole::DisplayRole) return FormatGerman(rows[index.row].datum);
    if (role == ListDataRole::SortRole)    return rows[index.row].datum.ToEpochDay();
    return {};
}
```

A custom comparator (`SetColumnComparator`) wins over both, and receives
**source** rows.

## Filtering

```cpp
proxy->SetFilterText("bratislava");              // case-insensitive substring
proxy->SetFilterColumns({ 0, 1 });               // over these columns ({} = all)
proxy->SetFilterPredicate([](const IListModel& m, int sourceRow) {
    return AmountOf(m, sourceRow) >= 200.0;      // and an arbitrary rule
});
proxy->ClearFilter();
```

The text filter and the predicate are **both** applied: a row has to pass each.

## Change notification

Attaching a proxy listens to the source's `onDataChanged` / `onRowChanged` /
`onRowInserted` / `onRowRemoved` and rebuilds when they fire. Any handler the
source already carried is **kept and called afterwards**, so putting a proxy in
front of a model never silently disconnects something else — and destroying the
proxy restores them, so a model that outlives its proxy does not call into freed
memory.

`SetDynamic(false)` defers rebuilding during a bulk load; `Invalidate()` forces
one.

## The view side

`UltraCanvasListView` shows *which* column is sorted and reports header clicks;
it never sorts anything itself, so a model that is already ordered — by a
database query, say — keeps working unchanged.

```cpp
view->onHeaderClicked = [&](int column) { /* sort your way */ };
view->SetSortIndicator(2, /*ascending=*/false);
```

The four lines in the example above wire the common case: clicking a header
sorts that column ascending, clicking the sorted one turns it round, and the
indicator follows. Sorting moves every row, so the keyboard focus row is dropped on a sort rather
than left pointing at whatever record landed on that index; the *selection* is
the caller's to map.

## What this is not

It does not add inline cell editing or a footer/aggregate row to the view — a
custom `IItemDelegate` already paints anything a cell needs (badges, checkboxes,
bars), and editing goes through `IListModel::SetData`, which the proxy forwards
to the right source row.
