// core/UltraCanvasListSortFilterProxy.cpp
// The row mapping, the comparisons and the filter. No UI, no locale: numbers
// are read digit by digit so a comma-decimal desktop cannot change how a table
// sorts (the rule in AGENTS.md, and the reason this file has no std::stod).
// Version: 1.0.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework
#include "UltraCanvasListSortFilterProxy.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace UltraCanvas {

namespace {

char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string ToLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = LowerAscii(c);
    return out;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// The text a value shows, whatever it is holding. Numbers are rendered without
// a locale so that a value stored as int and one stored as text still compare
// sensibly.
std::string ValueToText(const ListDataValue& value) {
    if (auto* s = std::get_if<std::string>(&value)) return *s;
    if (auto* i = std::get_if<int>(&value)) {
        std::string digits;
        int v = *i < 0 ? -*i : *i;
        if (v == 0) digits = "0";
        while (v > 0) { digits.insert(digits.begin(), static_cast<char>('0' + (v % 10))); v /= 10; }
        if (*i < 0) digits.insert(digits.begin(), '-');
        return digits;
    }
    if (auto* f = std::get_if<float>(&value)) {
        // Only used for text comparison fallbacks; the numeric path never
        // reaches here.
        const long long scaled = static_cast<long long>(std::llround(*f * 1000.0f));
        std::string digits;
        long long v = scaled < 0 ? -scaled : scaled;
        if (v == 0) digits = "0";
        while (v > 0) { digits.insert(digits.begin(), static_cast<char>('0' + (v % 10))); v /= 10; }
        while (digits.size() < 4) digits.insert(digits.begin(), '0');
        digits.insert(digits.end() - 3, '.');
        if (scaled < 0) digits.insert(digits.begin(), '-');
        return digits;
    }
    return std::string();
}

// A value's number, when it has one: an int or float directly, a string parsed.
bool ValueToNumber(const ListDataValue& value, double& out) {
    if (auto* i = std::get_if<int>(&value))   { out = static_cast<double>(*i); return true; }
    if (auto* f = std::get_if<float>(&value)) { out = static_cast<double>(*f); return true; }
    if (auto* s = std::get_if<std::string>(&value))
        return UltraCanvasListSortFilterProxy::TryReadNumber(*s, out);
    return false;
}

int Sign(double difference) {
    if (difference < 0.0) return -1;
    if (difference > 0.0) return 1;
    return 0;
}

} // namespace

// ===== NUMBER READING =====

bool UltraCanvasListSortFilterProxy::TryReadNumber(const std::string& text, double& out) {
    // Accepts "1234.56", "1.234,56", "-37,28 €", "(1.234,56)" and plain
    // integers; refuses anything else. Both separators are handled the way
    // UltraCanvasMoney reads them: when both appear the rightmost is the
    // decimal one, and a lone separator three digits from the end is grouping.
    std::string digits;
    bool negative = false;
    bool sawParen = false;
    int  lastDot = -1, lastComma = -1;
    int  digitCount = 0;
    bool sawNonNumeric = false;

    for (char c : text) {
        if (IsDigit(c)) { digits += c; ++digitCount; continue; }
        switch (c) {
            // A minus is a sign only before any digit. A letter is never
            // skipped: an earlier version ignored 'E', 'U' and 'R' so that
            // "EUR 89,00" would read as a number, and that made
            // "R-202607010" parse as the *negative* number -202607010, which
            // silently reversed a column of document numbers. Anything that is
            // not unambiguously part of a number now makes this not a number.
            case '-': if (digitCount > 0) return false; negative = true; break;
            case '+': if (digitCount > 0) return false; break;
            case '(': sawParen = true; break;
            case ')': if (sawParen) negative = true; break;
            case '.': lastDot = digitCount; break;
            case ',': lastComma = digitCount; break;
            case ' ': case '\t': case '\r': case '\n': break;
            case '\xC2': case '\xA0': break;                 // NBSP bytes
            case '\xE2': case '\x82': case '\xAC': break;    // the euro sign's bytes
            case '$': break;                                 // a bare currency symbol
            default: sawNonNumeric = true; break;
        }
        if (sawNonNumeric) return false;
    }
    if (digits.empty()) return false;

    int fractionDigits = 0;
    if (lastDot >= 0 && lastComma >= 0) {
        fractionDigits = digitCount - (lastDot > lastComma ? lastDot : lastComma);
    } else if (lastDot >= 0 || lastComma >= 0) {
        const int only = lastDot >= 0 ? lastDot : lastComma;
        const int after = digitCount - only;
        fractionDigits = (after == 3) ? 0 : after;    // three digits behind it reads as grouping
    }

    double value = 0.0;
    for (char d : digits) value = value * 10.0 + static_cast<double>(d - '0');
    for (int i = 0; i < fractionDigits; ++i) value /= 10.0;
    out = negative ? -value : value;
    return true;
}

// ===== COMPARISONS =====

int UltraCanvasListSortFilterProxy::CompareText(const std::string& left, const std::string& right,
                                                bool caseSensitive) {
    if (caseSensitive) {
        if (left < right) return -1;
        if (right < left) return 1;
        return 0;
    }
    const size_t count = left.size() < right.size() ? left.size() : right.size();
    for (size_t i = 0; i < count; ++i) {
        const char a = LowerAscii(left[i]);
        const char b = LowerAscii(right[i]);
        if (a != b) return a < b ? -1 : 1;
    }
    if (left.size() == right.size()) return 0;
    return left.size() < right.size() ? -1 : 1;
}

int UltraCanvasListSortFilterProxy::CompareNatural(const std::string& left,
                                                   const std::string& right) {
    // Walk both strings together, comparing runs of digits as numbers and
    // everything else case-insensitively, so "Beleg 2" comes before
    // "Beleg 10" instead of after it.
    size_t i = 0, j = 0;
    while (i < left.size() && j < right.size()) {
        if (IsDigit(left[i]) && IsDigit(right[j])) {
            size_t iEnd = i, jEnd = j;
            while (iEnd < left.size() && IsDigit(left[iEnd])) ++iEnd;
            while (jEnd < right.size() && IsDigit(right[jEnd])) ++jEnd;

            size_t iStart = i, jStart = j;
            while (iStart < iEnd - 1 && left[iStart] == '0') ++iStart;   // ignore leading zeros
            while (jStart < jEnd - 1 && right[jStart] == '0') ++jStart;

            const size_t iLen = iEnd - iStart;
            const size_t jLen = jEnd - jStart;
            if (iLen != jLen) return iLen < jLen ? -1 : 1;               // more digits, bigger number
            for (size_t k = 0; k < iLen; ++k) {
                if (left[iStart + k] != right[jStart + k])
                    return left[iStart + k] < right[jStart + k] ? -1 : 1;
            }
            i = iEnd;
            j = jEnd;
            continue;
        }
        const char a = LowerAscii(left[i]);
        const char b = LowerAscii(right[j]);
        if (a != b) return a < b ? -1 : 1;
        ++i;
        ++j;
    }
    if (i >= left.size() && j >= right.size()) return 0;
    return i >= left.size() ? -1 : 1;
}

int UltraCanvasListSortFilterProxy::CompareValues(const ListDataValue& left,
                                                  const ListDataValue& right,
                                                  ListSortKind kind) {
    // An empty value sorts after every filled one in ascending order - a blank
    // cell at the top of a sorted column is never what anybody wanted - and
    // therefore before them in descending order, because the proxy inverts the
    // whole comparison rather than asking comparators to know which way round
    // they are being used.
    const bool leftEmpty  = std::holds_alternative<std::monostate>(left)  || ValueToText(left).empty();
    const bool rightEmpty = std::holds_alternative<std::monostate>(right) || ValueToText(right).empty();
    if (leftEmpty || rightEmpty) {
        if (leftEmpty && rightEmpty) return 0;
        return leftEmpty ? 1 : -1;
    }

    if (kind == ListSortKind::Number || kind == ListSortKind::Auto) {
        double a = 0.0, b = 0.0;
        const bool aNum = ValueToNumber(left, a);
        const bool bNum = ValueToNumber(right, b);
        if (aNum && bNum) return Sign(a - b);
        if (kind == ListSortKind::Number) {
            // In an explicitly numeric column, whatever is not a number goes
            // last rather than being compared as text.
            if (aNum != bNum) return aNum ? -1 : 1;
            return 0;
        }
        // Auto: not both numbers, so fall through to text.
    }

    const std::string leftText  = ValueToText(left);
    const std::string rightText = ValueToText(right);
    if (kind == ListSortKind::Natural) return CompareNatural(leftText, rightText);
    return CompareText(leftText, rightText, kind == ListSortKind::TextCaseSensitive);
}

// ===== CONSTRUCTION AND SOURCE =====

UltraCanvasListSortFilterProxy::UltraCanvasListSortFilterProxy(std::shared_ptr<IListModel> source) {
    SetSourceModel(std::move(source));
}

UltraCanvasListSortFilterProxy::~UltraCanvasListSortFilterProxy() {
    DetachFromSource();
}

void UltraCanvasListSortFilterProxy::AttachToSource() {
    if (!source_ || attached_) return;

    // Keep whatever the source already had and call it after ours, so putting
    // a proxy in front of a model never disconnects an existing listener.
    previousDataChanged_ = source_->onDataChanged;
    previousRowChanged_  = source_->onRowChanged;
    previousRowInserted_ = source_->onRowInserted;
    previousRowRemoved_  = source_->onRowRemoved;

    source_->onDataChanged = [this]() {
        OnSourceChanged();
        if (previousDataChanged_) previousDataChanged_();
    };
    source_->onRowChanged = [this](int row) {
        OnSourceChanged();
        if (previousRowChanged_) previousRowChanged_(row);
    };
    source_->onRowInserted = [this](int row) {
        OnSourceChanged();
        if (previousRowInserted_) previousRowInserted_(row);
    };
    source_->onRowRemoved = [this](int row) {
        OnSourceChanged();
        if (previousRowRemoved_) previousRowRemoved_(row);
    };
    attached_ = true;
}

void UltraCanvasListSortFilterProxy::DetachFromSource() {
    if (!source_ || !attached_) return;
    source_->onDataChanged = previousDataChanged_;
    source_->onRowChanged  = previousRowChanged_;
    source_->onRowInserted = previousRowInserted_;
    source_->onRowRemoved  = previousRowRemoved_;
    previousDataChanged_ = nullptr;
    previousRowChanged_  = nullptr;
    previousRowInserted_ = nullptr;
    previousRowRemoved_  = nullptr;
    attached_ = false;
}

void UltraCanvasListSortFilterProxy::SetSourceModel(std::shared_ptr<IListModel> source) {
    DetachFromSource();
    source_ = std::move(source);
    AttachToSource();
    built_ = false;
    NotifyDataChanged();
}

void UltraCanvasListSortFilterProxy::OnSourceChanged() {
    built_ = false;
    if (dynamic_) {
        EnsureBuilt();
        NotifyDataChanged();
    }
}

// ===== BUILDING THE ROW MAP =====

void UltraCanvasListSortFilterProxy::EnsureBuilt() const {
    if (!built_) Rebuild();
}

bool UltraCanvasListSortFilterProxy::PassesFilter(int sourceRow) const {
    if (!source_) return false;

    if (filterPredicate_ && !filterPredicate_(*source_, sourceRow)) return false;
    if (filterText_.empty()) return true;

    const int columnCount = source_->GetColumnCount();
    if (filterColumns_.empty()) {
        for (int column = 0; column < columnCount; ++column) {
            const std::string text = ToLower(ValueToText(
                source_->GetData(ListIndex{sourceRow, column}, ListDataRole::DisplayRole)));
            if (text.find(filterText_) != std::string::npos) return true;
        }
        return false;
    }
    for (int column : filterColumns_) {
        if (column < 0 || column >= columnCount) continue;
        const std::string text = ToLower(ValueToText(
            source_->GetData(ListIndex{sourceRow, column}, ListDataRole::DisplayRole)));
        if (text.find(filterText_) != std::string::npos) return true;
    }
    return false;
}

int UltraCanvasListSortFilterProxy::CompareRows(int leftSourceRow, int rightSourceRow) const {
    if (!source_ || sortColumn_ < 0) return 0;

    if (sortColumn_ < static_cast<int>(comparators_.size()) && comparators_[sortColumn_])
        return comparators_[sortColumn_](*source_, leftSourceRow, rightSourceRow, sortColumn_);

    // SortRole wins when the model offers one: that is how a column showing
    // "17.09.2026" or "1.234,56 €" sorts by the value behind the text instead
    // of by the text.
    const ListIndex leftIndex{leftSourceRow, sortColumn_};
    const ListIndex rightIndex{rightSourceRow, sortColumn_};
    ListDataValue left  = source_->GetData(leftIndex, ListDataRole::SortRole);
    ListDataValue right = source_->GetData(rightIndex, ListDataRole::SortRole);
    const bool haveSortRole = !std::holds_alternative<std::monostate>(left) &&
                              !std::holds_alternative<std::monostate>(right);
    if (!haveSortRole) {
        left  = source_->GetData(leftIndex, ListDataRole::DisplayRole);
        right = source_->GetData(rightIndex, ListDataRole::DisplayRole);
    }
    return CompareValues(left, right, GetColumnSortKind(sortColumn_));
}

void UltraCanvasListSortFilterProxy::Rebuild() const {
    proxyToSource_.clear();
    sourceToProxy_.clear();
    built_ = true;
    if (!source_) return;

    const int rowCount = source_->GetRowCount();
    proxyToSource_.reserve(static_cast<size_t>(rowCount));
    for (int row = 0; row < rowCount; ++row)
        if (PassesFilter(row)) proxyToSource_.push_back(row);

    if (sortColumn_ >= 0) {
        // Stable, so equal rows keep their source order and sorting by one
        // column after another leaves a predictable result.
        std::stable_sort(proxyToSource_.begin(), proxyToSource_.end(),
                         [this](int leftRow, int rightRow) {
                             const int comparison = CompareRows(leftRow, rightRow);
                             return sortOrder_ == ListSortOrder::Ascending ? comparison < 0
                                                                           : comparison > 0;
                         });
    }

    sourceToProxy_.assign(static_cast<size_t>(rowCount), -1);
    for (size_t proxyRow = 0; proxyRow < proxyToSource_.size(); ++proxyRow)
        sourceToProxy_[static_cast<size_t>(proxyToSource_[proxyRow])] = static_cast<int>(proxyRow);
}

void UltraCanvasListSortFilterProxy::Invalidate() {
    built_ = false;
    EnsureBuilt();
    NotifyDataChanged();
}

void UltraCanvasListSortFilterProxy::SetDynamic(bool dynamic) {
    dynamic_ = dynamic;
    if (dynamic_ && !built_) Invalidate();
}

// ===== IListModel =====

int UltraCanvasListSortFilterProxy::GetRowCount() const {
    EnsureBuilt();
    return static_cast<int>(proxyToSource_.size());
}

int UltraCanvasListSortFilterProxy::GetColumnCount() const {
    return source_ ? source_->GetColumnCount() : 0;
}

ListDataValue UltraCanvasListSortFilterProxy::GetData(const ListIndex& index,
                                                      ListDataRole role) const {
    const int sourceRow = MapToSource(index.row);
    if (!source_ || sourceRow < 0) return ListDataValue{};
    return source_->GetData(ListIndex{sourceRow, index.column}, role);
}

bool UltraCanvasListSortFilterProxy::SetData(const ListIndex& index, ListDataRole role,
                                             const ListDataValue& value) {
    const int sourceRow = MapToSource(index.row);
    if (!source_ || sourceRow < 0) return false;
    const bool ok = source_->SetData(ListIndex{sourceRow, index.column}, role, value);
    // A model that reports the change rebuilds us through its notification; one
    // that stays silent would otherwise leave a stale order behind an edit.
    if (ok && dynamic_) built_ = false;
    return ok;
}

ListColumnDef UltraCanvasListSortFilterProxy::GetColumnDef(int column) const {
    return source_ ? source_->GetColumnDef(column) : ListColumnDef();
}

// ===== SORTING =====

void UltraCanvasListSortFilterProxy::SortByColumn(int column, ListSortOrder order) {
    sortColumn_ = column < 0 ? -1 : column;
    sortOrder_  = order;
    Invalidate();
}

void UltraCanvasListSortFilterProxy::SetColumnSortKind(int column, ListSortKind kind) {
    if (column < 0) return;
    if (static_cast<int>(sortKinds_.size()) <= column)
        sortKinds_.resize(static_cast<size_t>(column) + 1, ListSortKind::Auto);
    sortKinds_[static_cast<size_t>(column)] = kind;
    if (column == sortColumn_) Invalidate();
}

ListSortKind UltraCanvasListSortFilterProxy::GetColumnSortKind(int column) const {
    if (column < 0 || column >= static_cast<int>(sortKinds_.size())) return ListSortKind::Auto;
    return sortKinds_[static_cast<size_t>(column)];
}

void UltraCanvasListSortFilterProxy::SetColumnComparator(int column, ListRowComparator comparator) {
    if (column < 0) return;
    if (static_cast<int>(comparators_.size()) <= column)
        comparators_.resize(static_cast<size_t>(column) + 1);
    comparators_[static_cast<size_t>(column)] = std::move(comparator);
    if (column == sortColumn_) Invalidate();
}

// ===== FILTERING =====

void UltraCanvasListSortFilterProxy::SetFilterText(const std::string& text) {
    filterText_ = ToLower(text);
    Invalidate();
}

void UltraCanvasListSortFilterProxy::SetFilterColumns(std::vector<int> columns) {
    filterColumns_ = std::move(columns);
    Invalidate();
}

void UltraCanvasListSortFilterProxy::SetFilterPredicate(ListRowFilter predicate) {
    filterPredicate_ = std::move(predicate);
    Invalidate();
}

void UltraCanvasListSortFilterProxy::ClearFilter() {
    filterText_.clear();
    filterColumns_.clear();
    filterPredicate_ = nullptr;
    Invalidate();
}

// ===== MAPPING =====

int UltraCanvasListSortFilterProxy::MapToSource(int proxyRow) const {
    EnsureBuilt();
    if (proxyRow < 0 || proxyRow >= static_cast<int>(proxyToSource_.size())) return -1;
    return proxyToSource_[static_cast<size_t>(proxyRow)];
}

int UltraCanvasListSortFilterProxy::MapFromSource(int sourceRow) const {
    EnsureBuilt();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(sourceToProxy_.size())) return -1;
    return sourceToProxy_[static_cast<size_t>(sourceRow)];
}

int UltraCanvasListSortFilterProxy::GetSourceRowCount() const {
    return source_ ? source_->GetRowCount() : 0;
}

} // namespace UltraCanvas
