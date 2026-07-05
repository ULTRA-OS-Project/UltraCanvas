// include/UltraDatabase/UltraDatabaseValue.h
// The engine-independent value / row / result-set model. UltraDbValue is a
// small tagged variant; parameters are always bound (never interpolated into
// SQL), and query results come back as an UltraDbResultSet of UltraDbRows.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Logical type of an UltraDbValue. Timestamps travel as Int (epoch) or Text
// (ISO-8601) by convention; there is no separate timestamp tag in Stage 1.
enum class UltraDbType {
    Null = 0,
    Bool,
    Int,      // 64-bit signed
    Double,
    Text,     // UTF-8
    Blob      // raw bytes
};

// A single value bound as a query parameter or read from a result column.
// Non-explicit constructors let callers write { "INBOX", 5, uid } inline.
// Accessors coerce between types with defined rules (see UltraDatabaseValue.cpp).
class UltraDbValue {
public:
    UltraDbValue() = default;
    UltraDbValue(std::nullptr_t) {}
    UltraDbValue(bool v)                     : type_(UltraDbType::Bool),   i_(v ? 1 : 0) {}
    UltraDbValue(int v)                      : type_(UltraDbType::Int),    i_(v) {}
    UltraDbValue(int64_t v)                  : type_(UltraDbType::Int),    i_(v) {}
    UltraDbValue(uint32_t v)                 : type_(UltraDbType::Int),    i_(static_cast<int64_t>(v)) {}
    UltraDbValue(uint64_t v)                 : type_(UltraDbType::Int),    i_(static_cast<int64_t>(v)) {}
    UltraDbValue(double v)                   : type_(UltraDbType::Double), d_(v) {}
    UltraDbValue(const char* v)              : type_(UltraDbType::Text),   s_(v ? v : "") {}
    UltraDbValue(const std::string& v)       : type_(UltraDbType::Text),   s_(v) {}
    UltraDbValue(std::string&& v)            : type_(UltraDbType::Text),   s_(std::move(v)) {}
    UltraDbValue(const std::vector<uint8_t>& b) : type_(UltraDbType::Blob), b_(b) {}
    UltraDbValue(std::vector<uint8_t>&& b)   : type_(UltraDbType::Blob),   b_(std::move(b)) {}

    // Named factories where a constructor would be ambiguous.
    static UltraDbValue Null()                          { return UltraDbValue(); }
    static UltraDbValue Blob(const std::vector<uint8_t>& b) { return UltraDbValue(b); }

    UltraDbType Type() const { return type_; }
    bool        IsNull() const { return type_ == UltraDbType::Null; }

    bool                 AsBool()   const;
    int64_t              AsInt64()  const;
    uint32_t             AsU32()    const { return static_cast<uint32_t>(AsInt64()); }
    int                  AsInt()    const { return static_cast<int>(AsInt64()); }
    double               AsDouble() const;
    std::string          AsString() const;
    std::vector<uint8_t> AsBlob()   const;

private:
    UltraDbType          type_ = UltraDbType::Null;
    int64_t              i_ = 0;
    double               d_ = 0.0;
    std::string          s_;
    std::vector<uint8_t> b_;
};

// Parameter list for a query. Order matches the placeholders in the SQL.
using UltraDbParams = std::vector<UltraDbValue>;

// One result row. Columns are addressable by name or zero-based index.
class UltraDbRow {
public:
    UltraDbRow() = default;
    UltraDbRow(std::shared_ptr<const std::vector<std::string>> columns,
               std::vector<UltraDbValue> values)
        : columns_(std::move(columns)), values_(std::move(values)) {}

    size_t Size() const { return values_.size(); }
    bool   Has(const std::string& column) const { return IndexOf(column) >= 0; }

    // Missing column / out-of-range index yields a Null value rather than
    // throwing, so row access stays terse.
    const UltraDbValue& operator[](size_t index) const;
    const UltraDbValue& operator[](const std::string& column) const;

    const std::vector<std::string>& Columns() const;

private:
    int IndexOf(const std::string& column) const;

    std::shared_ptr<const std::vector<std::string>> columns_;
    std::vector<UltraDbValue> values_;
    static const UltraDbValue kNull;
};

// The full set of rows returned by a query. Range-for iterable.
class UltraDbResultSet {
public:
    void SetColumns(std::vector<std::string> cols) {
        columns_ = std::make_shared<const std::vector<std::string>>(std::move(cols));
    }
    std::shared_ptr<const std::vector<std::string>> ColumnsPtr() const { return columns_; }
    const std::vector<std::string>& Columns() const;

    void AddRow(std::vector<UltraDbValue> values) {
        rows_.emplace_back(columns_, std::move(values));
    }

    size_t Size()  const { return rows_.size(); }
    bool   Empty() const { return rows_.empty(); }
    void   Clear() { rows_.clear(); }

    const UltraDbRow& Row(size_t i) const { return rows_.at(i); }

    std::vector<UltraDbRow>::const_iterator begin() const { return rows_.begin(); }
    std::vector<UltraDbRow>::const_iterator end()   const { return rows_.end(); }

private:
    std::shared_ptr<const std::vector<std::string>> columns_ =
        std::make_shared<const std::vector<std::string>>();
    std::vector<UltraDbRow> rows_;
};
