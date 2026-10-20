// core/UltraDatabase/UltraDatabaseValue.cpp
// UltraDbValue coercion rules and UltraDbRow / UltraDbResultSet column access.
// Version: 0.1.2
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabase/UltraDatabaseValue.h"

#include "UltraDatabaseInternal.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

// ---- Dot-decimal numbers ---------------------------------------------------

namespace ultradb_internal {

namespace {

// Case-insensitive "does `text` start with `word` at `pos`".
bool StartsWithWord(const std::string& text, size_t pos, const char* word) {
    for (size_t i = 0; word[i] != '\0'; ++i, ++pos) {
        if (pos >= text.size()) return false;
        if (std::tolower(static_cast<unsigned char>(text[pos])) != word[i]) return false;
    }
    return true;
}

} // namespace

bool ParseDoubleClassic(const std::string& text, double& out) {
    size_t pos = 0;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos == text.size()) return false;

    // PostgreSQL writes the special float8 values as words, which a stream
    // does not read. strtod did, so they keep working.
    size_t word = pos;
    bool negative = false;
    if (text[word] == '+' || text[word] == '-') { negative = text[word] == '-'; ++word; }
    if (StartsWithWord(text, word, "nan")) {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (StartsWithWord(text, word, "inf")) {   // "inf" and "infinity"
        out = negative ? -std::numeric_limits<double>::infinity()
                       :  std::numeric_limits<double>::infinity();
        return true;
    }

    // A stream imbued with the classic locale has '.' as its decimal point
    // and no thousands grouping, whatever the process's locale is.
    std::istringstream in(text.substr(pos));
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail()) return false;
    out = value;
    return true;
}

std::string FormatDoubleClassic(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";

    // The shortest of 15 and 17 significant digits that reads back as the
    // same double: 0.1 stays "0.1" rather than "0.10000000000000001".
    for (int precision : {15, 17}) {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << std::setprecision(precision) << value;
        double back = 0.0;
        if (precision == 17 || (ParseDoubleClassic(text.str(), back) && back == value))
            return text.str();
    }
    return std::string();   // not reached: the 17-digit form always returns
}

} // namespace ultradb_internal

// ---- UltraDbValue coercions ------------------------------------------------

bool UltraDbValue::AsBool() const {
    switch (type_) {
        case UltraDbType::Null:   return false;
        case UltraDbType::Bool:
        case UltraDbType::Int:    return i_ != 0;
        case UltraDbType::Double: return d_ != 0.0;
        case UltraDbType::Text:   return s_ == "1" || s_ == "true" || s_ == "TRUE" ||
                                         s_ == "True" || s_ == "yes";
        case UltraDbType::Blob:   return !b_.empty();
    }
    return false;
}

int64_t UltraDbValue::AsInt64() const {
    switch (type_) {
        case UltraDbType::Null:   return 0;
        case UltraDbType::Bool:
        case UltraDbType::Int:    return i_;
        case UltraDbType::Double: return static_cast<int64_t>(d_);
        case UltraDbType::Text:   return s_.empty() ? 0
                                         : static_cast<int64_t>(std::strtoll(s_.c_str(), nullptr, 10));
        case UltraDbType::Blob:   return 0;
    }
    return 0;
}

double UltraDbValue::AsDouble() const {
    switch (type_) {
        case UltraDbType::Null:   return 0.0;
        case UltraDbType::Bool:
        case UltraDbType::Int:    return static_cast<double>(i_);
        case UltraDbType::Double: return d_;
        case UltraDbType::Text: {
            // Dot-decimal: a Text value is a number as the database wrote it,
            // not as the user's locale would write it.
            double value = 0.0;
            ultradb_internal::ParseDoubleClassic(s_, value);
            return value;
        }
        case UltraDbType::Blob:   return 0.0;
    }
    return 0.0;
}

std::string UltraDbValue::AsString() const {
    switch (type_) {
        case UltraDbType::Null:   return std::string();
        case UltraDbType::Bool:   return i_ != 0 ? "true" : "false";
        case UltraDbType::Int:    return std::to_string(i_);
        // Dot-decimal, and the shortest text that reads back as the same
        // double: 3.14 is "3.14". std::to_string, used before, wrote it
        // through LC_NUMERIC as "3,140000" on a comma-decimal desktop and
        // kept six decimals, so 1e-9 became "0.000000". NaN and the
        // infinities are spelled as PostgreSQL spells them.
        case UltraDbType::Double: return ultradb_internal::FormatDoubleClassic(d_);
        case UltraDbType::Text:   return s_;
        case UltraDbType::Blob:   return std::string(b_.begin(), b_.end());
    }
    return std::string();
}

std::vector<uint8_t> UltraDbValue::AsBlob() const {
    switch (type_) {
        case UltraDbType::Blob: return b_;
        case UltraDbType::Text: return std::vector<uint8_t>(s_.begin(), s_.end());
        default: {
            std::string s = AsString();
            return std::vector<uint8_t>(s.begin(), s.end());
        }
    }
}

// ---- UltraDbRow ------------------------------------------------------------

const UltraDbValue UltraDbRow::kNull{};

int UltraDbRow::IndexOf(const std::string& column) const {
    if (!columns_) return -1;
    for (size_t i = 0; i < columns_->size(); ++i) {
        if ((*columns_)[i] == column) return static_cast<int>(i);
    }
    return -1;
}

const UltraDbValue& UltraDbRow::operator[](size_t index) const {
    return index < values_.size() ? values_[index] : kNull;
}

const UltraDbValue& UltraDbRow::operator[](const std::string& column) const {
    int idx = IndexOf(column);
    return idx >= 0 ? (*this)[static_cast<size_t>(idx)] : kNull;
}

static const std::vector<std::string> kEmptyColumns;

const std::vector<std::string>& UltraDbRow::Columns() const {
    return columns_ ? *columns_ : kEmptyColumns;
}

const std::vector<std::string>& UltraDbResultSet::Columns() const {
    return columns_ ? *columns_ : kEmptyColumns;
}
