// Tests/ListSortFilterProxyTests.cpp
// Unit tests for UltraCanvasListSortFilterProxy: the sorting, the filtering and
// - above all - the row mapping, because a proxy row that is mistaken for a
// source row is how a sorted table edits or deletes the wrong record.
//
// Runs against the real list models with no UI stack, like
// Tests/ListViewTooltipTest.cpp.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasListModel.h"
#include "UltraCanvasListSortFilterProxy.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static void CheckText(const std::string& actual, const std::string& expected,
                      const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected \"%s\"\n    got      \"%s\"\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

static void CheckInt(long long actual, long long expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected %lld\n    got      %lld\n",
                    what.c_str(), expected, actual);
    }
}

// The display text of a proxy row's column.
static std::string TextAt(const IListModel& model, int row, int column = 0) {
    return GetStringValue(model.GetData(ListIndex{row, column}, ListDataRole::DisplayRole));
}

// The first column of every row, in order - the shape most assertions want.
static std::vector<std::string> Column(const IListModel& model, int column = 0) {
    std::vector<std::string> out;
    for (int row = 0; row < model.GetRowCount(); ++row) out.push_back(TextAt(model, row, column));
    return out;
}

static void CheckOrder(const IListModel& model, const std::vector<std::string>& expected,
                       const std::string& what, int column = 0) {
    const std::vector<std::string> actual = Column(model, column);
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("  FAIL: %s\n    expected:", what.c_str());
        for (const std::string& value : expected) std::printf(" [%s]", value.c_str());
        std::printf("\n    got     :");
        for (const std::string& value : actual) std::printf(" [%s]", value.c_str());
        std::printf("\n");
    }
}

// A three-column model of the kind the accounting screens use: a document
// number, a customer and an amount written the German way.
static std::shared_ptr<UltraCanvasMultiColumnListModel> MakeInvoiceModel() {
    auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
    model->SetColumns({ ListColumnDef("Rechnung", 120), ListColumnDef("Kunde", 200),
                        ListColumnDef("Betrag", 100, TextAlignment::Right) });
    model->AddItem(MultiColumnListItem{{ "R-202607010", "Bear Fruit Ltd.",   "189,34" }});
    model->AddItem(MultiColumnListItem{{ "R-202607002", "Andrea del Riva",   "1.234,56" }});
    model->AddItem(MultiColumnListItem{{ "R-202607009", "Sebastian Brixner", "273,01" }});
    model->AddItem(MultiColumnListItem{{ "R-202607001", "olonda s.r.o.",     "99,90" }});
    return model;
}

// ---- 1. Pass-through -------------------------------------------------------

static void TestPassThrough() {
    std::printf("pass-through\n");

    auto source = MakeInvoiceModel();
    UltraCanvasListSortFilterProxy proxy(source);

    CheckInt(proxy.GetRowCount(), 4, "every row is visible without a filter");
    CheckInt(proxy.GetColumnCount(), 3, "the columns come from the source");
    CheckText(proxy.GetColumnDef(1).title, "Kunde", "and so do the column definitions");
    CheckOrder(proxy, { "R-202607010", "R-202607002", "R-202607009", "R-202607001" },
               "the source order is preserved when nothing is sorted");
    CheckText(TextAt(proxy, 1, 1), "Andrea del Riva", "cells read through to the source");
    Check(proxy.GetSourceModel() == source.get(), "the source is reachable");
    Check(!proxy.IsSorted() && !proxy.IsFiltered(), "and neither sorted nor filtered yet");

    // Every row maps to itself while nothing is reordered.
    bool identity = true;
    for (int row = 0; row < 4; ++row)
        if (proxy.MapToSource(row) != row || proxy.MapFromSource(row) != row) identity = false;
    Check(identity, "the mapping is the identity while nothing is reordered");
    CheckInt(proxy.MapToSource(4), -1, "an out-of-range proxy row maps to nothing");
    CheckInt(proxy.MapToSource(-1), -1, "and so does a negative one");

    // A proxy with no source at all answers rather than crashing.
    UltraCanvasListSortFilterProxy empty;
    CheckInt(empty.GetRowCount(), 0, "a proxy without a source has no rows");
    CheckInt(empty.GetColumnCount(), 0, "and no columns");
    Check(std::holds_alternative<std::monostate>(
              empty.GetData(ListIndex{0, 0}, ListDataRole::DisplayRole)),
          "and no data");
    Check(!empty.SetData(ListIndex{0, 0}, ListDataRole::DisplayRole, std::string("x")),
          "and refuses a write");
}

// ---- 2. Sorting ------------------------------------------------------------

static void TestSorting() {
    std::printf("sorting\n");

    auto source = MakeInvoiceModel();
    UltraCanvasListSortFilterProxy proxy(source);

    proxy.SortByColumn(0, ListSortOrder::Ascending);
    Check(proxy.IsSorted() && proxy.GetSortColumn() == 0, "the sort column is recorded");
    CheckOrder(proxy, { "R-202607001", "R-202607002", "R-202607009", "R-202607010" },
               "ascending by document number");

    proxy.SortByColumn(0, ListSortOrder::Descending);
    CheckOrder(proxy, { "R-202607010", "R-202607009", "R-202607002", "R-202607001" },
               "descending by document number");

    proxy.SortByColumn(1, ListSortOrder::Ascending);
    CheckOrder(proxy, { "Andrea del Riva", "Bear Fruit Ltd.", "olonda s.r.o.",
                        "Sebastian Brixner" },
               "ascending by customer, case-insensitively - \"olonda\" belongs between "
               "B and S, not after Z", 1);

    // Amounts written the German way sort as numbers, not as text: as text
    // "1.234,56" would come before "189,34".
    proxy.SortByColumn(2, ListSortOrder::Ascending);
    CheckOrder(proxy, { "99,90", "189,34", "273,01", "1.234,56" },
               "ascending by amount, numerically", 2);
    proxy.SortByColumn(2, ListSortOrder::Descending);
    CheckOrder(proxy, { "1.234,56", "273,01", "189,34", "99,90" },
               "descending by amount", 2);

    proxy.ClearSort();
    Check(!proxy.IsSorted(), "the sort can be cleared");
    CheckOrder(proxy, { "R-202607010", "R-202607002", "R-202607009", "R-202607001" },
               "which restores the source order");

    // Stability: rows that compare equal keep the order the source gave them.
    auto ties = std::make_shared<UltraCanvasMultiColumnListModel>();
    ties->SetColumns({ ListColumnDef("Gruppe"), ListColumnDef("Name") });
    ties->AddItem(MultiColumnListItem{{ "B", "erste" }});
    ties->AddItem(MultiColumnListItem{{ "A", "zweite" }});
    ties->AddItem(MultiColumnListItem{{ "B", "dritte" }});
    ties->AddItem(MultiColumnListItem{{ "A", "vierte" }});
    UltraCanvasListSortFilterProxy stable(ties);
    stable.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(stable, { "zweite", "vierte", "erste", "dritte" },
               "a stable sort keeps equal rows in source order", 1);
    stable.SortByColumn(0, ListSortOrder::Descending);
    CheckOrder(stable, { "erste", "dritte", "zweite", "vierte" },
               "and does so in the other direction too", 1);

    // Empty cells go last when ascending.
    auto sparse = std::make_shared<UltraCanvasMultiColumnListModel>();
    sparse->SetColumns({ ListColumnDef("Wert") });
    sparse->AddItem(MultiColumnListItem{{ "b" }});
    sparse->AddItem(MultiColumnListItem{{ "" }});
    sparse->AddItem(MultiColumnListItem{{ "a" }});
    UltraCanvasListSortFilterProxy blanks(sparse);
    blanks.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(blanks, { "a", "b", "" }, "an empty cell sorts last when ascending");
}

// ---- 3. Sort kinds ---------------------------------------------------------

static void TestSortKinds() {
    std::printf("sort kinds\n");

    auto source = std::make_shared<UltraCanvasMultiColumnListModel>();
    source->SetColumns({ ListColumnDef("Beleg"), ListColumnDef("Menge") });
    source->AddItem(MultiColumnListItem{{ "Beleg 10", "7" }});
    source->AddItem(MultiColumnListItem{{ "Beleg 2",  "abc" }});
    source->AddItem(MultiColumnListItem{{ "Beleg 1",  "12" }});
    UltraCanvasListSortFilterProxy proxy(source);

    // Plain text sorting puts "Beleg 10" before "Beleg 2".
    proxy.SetColumnSortKind(0, ListSortKind::Text);
    proxy.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(proxy, { "Beleg 1", "Beleg 10", "Beleg 2" }, "text sorting is lexicographic");

    // Natural sorting reads the digits as a number.
    proxy.SetColumnSortKind(0, ListSortKind::Natural);
    CheckOrder(proxy, { "Beleg 1", "Beleg 2", "Beleg 10" },
               "natural sorting reads embedded numbers");
    Check(proxy.GetColumnSortKind(0) == ListSortKind::Natural, "the kind is remembered");
    Check(proxy.GetColumnSortKind(1) == ListSortKind::Auto, "and defaults to Auto elsewhere");

    // In an explicitly numeric column, text that is not a number goes last
    // rather than being compared as text.
    proxy.SetColumnSortKind(1, ListSortKind::Number);
    proxy.SortByColumn(1, ListSortOrder::Ascending);
    CheckOrder(proxy, { "7", "12", "abc" }, "a numeric column puts non-numbers last", 1);

    CheckInt(UltraCanvasListSortFilterProxy::CompareNatural("Beleg 2", "Beleg 10") < 0, 1,
             "CompareNatural is exposed and orders 2 before 10");
    CheckInt(UltraCanvasListSortFilterProxy::CompareText("ABC", "abd", false) < 0, 1,
             "CompareText ignores case when asked to");
    CheckInt(UltraCanvasListSortFilterProxy::CompareText("ABC", "abd", true) < 0, 1,
             "and still orders sensibly when it does not");
}

// ---- 4. Numbers ------------------------------------------------------------

static void TestNumberReading() {
    std::printf("number reading\n");

    double value = 0.0;
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("1234.56", value) && value > 1234.55 &&
          value < 1234.57, "a dot decimal");
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("1.234,56", value) && value > 1234.55 &&
          value < 1234.57, "a German amount with grouping");
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("1.234", value) && value > 1233.9 &&
          value < 1234.1, "a lone separator three digits from the end is grouping");
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("-37,28 \xE2\x82\xAC", value) &&
          value < -37.27 && value > -37.29, "a negative amount with a euro sign");
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("(1.234,56)", value) && value < 0,
          "parentheses mean negative");
    Check(UltraCanvasListSortFilterProxy::TryReadNumber("42", value) && value > 41.9 &&
          value < 42.1, "a plain integer");
    Check(!UltraCanvasListSortFilterProxy::TryReadNumber("Bear Fruit Ltd.", value),
          "a name is not a number");
    Check(!UltraCanvasListSortFilterProxy::TryReadNumber("", value), "and neither is nothing");
    Check(!UltraCanvasListSortFilterProxy::TryReadNumber("12a", value),
          "nor a number with a letter stuck to it");
}

// ---- 5. SortRole and custom comparators ------------------------------------

// A model whose dates are shown German but sorted by a key, which is what
// SortRole is for.
class DateModel : public IListModel {
public:
    struct Row { std::string shown; int key; };
    std::vector<Row> rows;

    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return 1; }
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override {
        if (index.row < 0 || index.row >= GetRowCount()) return {};
        if (role == ListDataRole::DisplayRole) return rows[index.row].shown;
        if (role == ListDataRole::SortRole)    return rows[index.row].key;
        return {};
    }
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }
    ListColumnDef GetColumnDef(int) const override { return ListColumnDef("Datum", 120); }
};

static void TestSortRole() {
    std::printf("SortRole and custom comparators\n");

    auto dates = std::make_shared<DateModel>();
    dates->rows = { { "17.09.2026", 20260917 }, { "01.04.2026", 20260401 },
                    { "23.07.2026", 20260723 } };
    UltraCanvasListSortFilterProxy proxy(dates);
    proxy.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(proxy, { "01.04.2026", "23.07.2026", "17.09.2026" },
               "SortRole sorts by the value behind the text, not by the text");

    // Without SortRole the same strings would sort by their day number.
    auto textDates = std::make_shared<UltraCanvasMultiColumnListModel>();
    textDates->SetColumns({ ListColumnDef("Datum") });
    textDates->AddItem(MultiColumnListItem{{ "17.09.2026" }});
    textDates->AddItem(MultiColumnListItem{{ "01.04.2026" }});
    textDates->AddItem(MultiColumnListItem{{ "23.07.2026" }});
    UltraCanvasListSortFilterProxy plain(textDates);
    plain.SetColumnSortKind(0, ListSortKind::Text);
    plain.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(plain, { "01.04.2026", "17.09.2026", "23.07.2026" },
               "which is exactly what a text sort of the same dates gets wrong");

    // A custom comparator wins over both.
    UltraCanvasListSortFilterProxy custom(dates);
    custom.SetColumnComparator(0, [](const IListModel& source, int left, int right, int column) {
        // Reverse of the natural key, to prove the comparator is the one used.
        const int a = std::get<int>(source.GetData(ListIndex{left, column}, ListDataRole::SortRole));
        const int b = std::get<int>(source.GetData(ListIndex{right, column}, ListDataRole::SortRole));
        return b - a;
    });
    custom.SortByColumn(0, ListSortOrder::Ascending);
    CheckOrder(custom, { "17.09.2026", "23.07.2026", "01.04.2026" },
               "a custom comparator wins over SortRole");
}

// ---- 6. Filtering ----------------------------------------------------------

static void TestFiltering() {
    std::printf("filtering\n");

    auto source = MakeInvoiceModel();
    UltraCanvasListSortFilterProxy proxy(source);

    proxy.SetFilterText("olonda");
    CheckInt(proxy.GetRowCount(), 1, "a text filter thins the rows out");
    CheckText(TextAt(proxy, 0, 1), "olonda s.r.o.", "keeping the matching one");
    Check(proxy.IsFiltered(), "and the proxy says it is filtering");

    proxy.SetFilterText("OLONDA");
    CheckInt(proxy.GetRowCount(), 1, "the text filter ignores case");

    proxy.SetFilterText("R-2026070");
    CheckInt(proxy.GetRowCount(), 4, "a substring that every row carries keeps all of them");

    // Restricting the columns changes what matches.
    proxy.SetFilterText("189");
    CheckInt(proxy.GetRowCount(), 1, "searching every column finds the amount");
    proxy.SetFilterColumns({ 0, 1 });
    CheckInt(proxy.GetRowCount(), 0, "restricted to number and customer, it no longer does");
    proxy.SetFilterColumns({});
    CheckInt(proxy.GetRowCount(), 1, "and an empty column list means every column again");

    // A predicate on top of the text filter: a row has to pass both.
    proxy.ClearFilter();
    proxy.SetFilterPredicate([](const IListModel& model, int row) {
        double amount = 0.0;
        const std::string text =
            GetStringValue(model.GetData(ListIndex{row, 2}, ListDataRole::DisplayRole));
        return UltraCanvasListSortFilterProxy::TryReadNumber(text, amount) && amount >= 200.0;
    });
    CheckInt(proxy.GetRowCount(), 2, "a predicate keeps the rows it likes");
    proxy.SetFilterText("brixner");
    CheckInt(proxy.GetRowCount(), 1, "and the text filter narrows that further");
    proxy.ClearFilter();
    CheckInt(proxy.GetRowCount(), 4, "clearing restores every row");
    Check(!proxy.IsFiltered(), "and the proxy says so");

    // Filter and sort together.
    proxy.SetFilterText("r-2026070");
    proxy.SortByColumn(2, ListSortOrder::Descending);
    CheckOrder(proxy, { "1.234,56", "273,01", "189,34", "99,90" },
               "filtering and sorting compose", 2);
}

// ---- 7. Mapping ------------------------------------------------------------

static void TestMapping() {
    std::printf("row mapping\n");

    auto source = MakeInvoiceModel();
    UltraCanvasListSortFilterProxy proxy(source);
    proxy.SortByColumn(0, ListSortOrder::Ascending);

    // Source order is 010, 002, 009, 001; sorted ascending it is 001, 002, 009, 010.
    CheckInt(proxy.MapToSource(0), 3, "the first sorted row is the last source row");
    CheckInt(proxy.MapToSource(3), 0, "and the last sorted row is the first source row");
    CheckInt(proxy.MapFromSource(0), 3, "the inverse agrees");
    CheckInt(proxy.MapFromSource(3), 0, "in both directions");

    bool roundTrips = true;
    for (int proxyRow = 0; proxyRow < proxy.GetRowCount(); ++proxyRow)
        if (proxy.MapFromSource(proxy.MapToSource(proxyRow)) != proxyRow) roundTrips = false;
    Check(roundTrips, "every row round-trips through the mapping");

    proxy.SetFilterText("olonda");
    CheckInt(proxy.GetRowCount(), 1, "one row survives the filter");
    CheckInt(proxy.MapToSource(0), 3, "and it still points at its source row");
    CheckInt(proxy.MapFromSource(0), -1, "a filtered-out source row has no proxy row");
    CheckInt(proxy.MapFromSource(99), -1, "and neither has one that does not exist");
    CheckInt(proxy.GetSourceRowCount(), 4, "the source row count is reported unfiltered");

    // Writing through the proxy reaches the right source row.
    proxy.ClearFilter();
    proxy.SortByColumn(0, ListSortOrder::Ascending);
    Check(proxy.SetData(ListIndex{0, 1}, ListDataRole::DisplayRole, std::string("Neuer Name")),
          "a write through the proxy is accepted");
    CheckText(GetStringValue(source->GetData(ListIndex{3, 1}, ListDataRole::DisplayRole)),
              "Neuer Name", "and lands on the source row the proxy row pointed at");
}

// ---- 8. Change notification ------------------------------------------------

static void TestNotifications() {
    std::printf("change notification\n");

    auto source = MakeInvoiceModel();

    // A listener the application had on the source before the proxy existed.
    // AddItem reports an inserted row, so that is the signal to watch.
    int sourceNotifications = 0;
    source->onRowInserted = [&sourceNotifications](int) { ++sourceNotifications; };

    auto proxy = std::make_unique<UltraCanvasListSortFilterProxy>(source);
    int proxyNotifications = 0;
    proxy->onDataChanged = [&proxyNotifications]() { ++proxyNotifications; };
    proxy->SortByColumn(0, ListSortOrder::Ascending);

    source->AddItem(MultiColumnListItem{{ "R-202607000", "Xiaoping Xiong", "261,00" }});
    CheckInt(proxy->GetRowCount(), 5, "a row added to the source appears in the proxy");
    CheckText(TextAt(*proxy, 0), "R-202607000", "in its sorted position");
    Check(proxyNotifications > 0, "and the proxy tells its own listeners");
    Check(sourceNotifications > 0,
          "while the listener that was already on the source still hears about it");

    // Turning dynamic off defers the rebuild until it is asked for.
    proxy->SetDynamic(false);
    const int before = proxyNotifications;
    source->AddItem(MultiColumnListItem{{ "R-202606999", "Cyberscape Media Ltd.", "291,32" }});
    CheckInt(proxyNotifications, before, "a non-dynamic proxy does not notify on every change");
    proxy->Invalidate();
    CheckInt(proxy->GetRowCount(), 6, "and picks the change up when invalidated");
    Check(proxyNotifications > before, "notifying once for the batch");

    // Destroying the proxy restores the source's own handlers, so a model that
    // outlives its proxy does not call into freed memory.
    proxy.reset();
    const int afterProxy = sourceNotifications;
    source->AddItem(MultiColumnListItem{{ "R-202606998", "Remitly Europe Ltd", "10,99" }});
    Check(sourceNotifications > afterProxy,
          "the source's original handler is restored when the proxy is destroyed");
}

int main() {
    std::printf("UltraCanvasListSortFilterProxy tests\n");
    TestPassThrough();
    TestSorting();
    TestSortKinds();
    TestNumberReading();
    TestSortRole();
    TestFiltering();
    TestMapping();
    TestNotifications();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
