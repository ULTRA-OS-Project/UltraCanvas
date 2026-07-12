// core/UltraDatabase/UltraDatabaseValue.cpp
// UltraDbValue coercion rules and UltraDbRow / UltraDbResultSet column access.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraDatabase/UltraDatabaseValue.h"

#include <cstdlib>

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
        case UltraDbType::Text:   return s_.empty() ? 0.0 : std::strtod(s_.c_str(), nullptr);
        case UltraDbType::Blob:   return 0.0;
    }
    return 0.0;
}

std::string UltraDbValue::AsString() const {
    switch (type_) {
        case UltraDbType::Null:   return std::string();
        case UltraDbType::Bool:   return i_ != 0 ? "true" : "false";
        case UltraDbType::Int:    return std::to_string(i_);
        case UltraDbType::Double: return std::to_string(d_);
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
