// include/UltraCanvasSpreadsheetCell.h
// Spreadsheet cell data structure with value, formatting, and formula support
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include <string>
#include <memory>
#include <optional>
#include <functional>

namespace UltraCanvas {

// Forward declarations
class SpreadsheetSheet;
class SpreadsheetFormula;

// ============================================================================
// SPREADSHEET CELL
// ============================================================================

class SpreadsheetCell {
private:
    // Cell position
    int row_ = 0;
    int col_ = 0;
    
    // Value storage
    CellValueType valueType_ = CellValueType::Empty;
    CellValueVariant value_;
    
    // Formula (if cell contains formula)
    std::string formulaText_;                       // Original formula text (e.g., "=SUM(A1:A10)")
    std::shared_ptr<SpreadsheetFormula> formula_;   // Parsed formula (lazy evaluated)
    bool formulaDirty_ = false;                     // Needs recalculation
    
    // Cached display value
    mutable std::string displayValue_;
    mutable bool displayDirty_ = true;
    
    // Formatting
    std::shared_ptr<CellStyle> style_;              // nullptr = default style
    
    // Additional data
    std::unique_ptr<CellComment> comment_;
    std::unique_ptr<CellHyperlink> hyperlink_;
    std::unique_ptr<DataValidation> validation_;
    
    // Metadata
    bool isPartOfMerge_ = false;                    // True if part of merged region (not top-left)
    CellAddress mergeOrigin_;                       // Top-left cell of merge region
    
public:
    // ===== CONSTRUCTORS =====
    SpreadsheetCell() = default;
    SpreadsheetCell(int row, int col) : row_(row), col_(col) {}
    
    // Copy and move
    SpreadsheetCell(const SpreadsheetCell& other);
    SpreadsheetCell& operator=(const SpreadsheetCell& other);
    SpreadsheetCell(SpreadsheetCell&& other) noexcept = default;
    SpreadsheetCell& operator=(SpreadsheetCell&& other) noexcept = default;
    
    // ===== POSITION =====
    int GetRow() const { return row_; }
    int GetColumn() const { return col_; }
    CellAddress GetAddress() const { return CellAddress(row_, col_); }
    std::string GetAddressString() const { return GetAddress().ToString(); }
    
    void SetPosition(int row, int col) { row_ = row; col_ = col; }
    
    // ===== VALUE TYPE =====
    CellValueType GetValueType() const { return valueType_; }
    bool IsEmpty() const { return valueType_ == CellValueType::Empty; }
    bool HasFormula() const { return valueType_ == CellValueType::Formula || !formulaText_.empty(); }
    bool HasError() const { return valueType_ == CellValueType::Error; }
    bool IsNumeric() const {
        return valueType_ == CellValueType::Number ||
               valueType_ == CellValueType::Date ||
               valueType_ == CellValueType::Time ||
               valueType_ == CellValueType::DateTime ||
               valueType_ == CellValueType::Currency ||
               valueType_ == CellValueType::Percentage;
    }
    
    // ===== VALUE GETTERS =====
    
    // Get raw variant value
    const CellValueVariant& GetRawValue() const { return value_; }
    
    // Get as specific types (returns default/nullopt if wrong type)
    std::string GetText() const;
    double GetNumber() const;
    bool GetBoolean() const;
    CellErrorType GetError() const;
    DateTimeValue GetDateTime() const;
    CurrencyValue GetCurrency() const;
    
    // Get as optional (returns nullopt if wrong type)
    std::optional<std::string> TryGetText() const;
    std::optional<double> TryGetNumber() const;
    std::optional<bool> TryGetBoolean() const;
    std::optional<CellErrorType> TryGetError() const;
    
    // Get formatted display value
    std::string GetDisplayValue() const;
    
    // Get formula text (empty if no formula)
    const std::string& GetFormulaText() const { return formulaText_; }
    
    // ===== VALUE SETTERS =====
    
    // Clear cell
    void Clear();
    void ClearContents();       // Clear value only, keep formatting
    void ClearFormats();        // Clear formatting only, keep value
    void ClearComments();
    void ClearHyperlinks();
    void ClearValidation();
    void ClearAll();
    
    // Set text value
    void SetText(const std::string& text);
    
    // Set numeric value
    void SetNumber(double value);
    
    // Set boolean value
    void SetBoolean(bool value);
    
    // Set error value
    void SetError(CellErrorType error);
    
    // Set date/time value
    void SetDate(int year, int month, int day);
    void SetTime(int hour, int minute, int second);
    void SetDateTime(int year, int month, int day, int hour, int minute, int second);
    void SetDateTime(const DateTimeValue& value);
    
    // Set currency value
    void SetCurrency(double amount, const std::string& currencyCode = "USD");
    
    // Set percentage (0.5 = 50%)
    void SetPercentage(double value);
    
    // Set formula
    void SetFormula(const std::string& formula);
    
    // Set from variant
    void SetValue(const CellValueVariant& value, CellValueType type);
    
    // Parse and set value from string (auto-detect type)
    void SetValueFromString(const std::string& input);
    
    // ===== FORMULA =====
    
    bool IsFormulaDirty() const { return formulaDirty_; }
    void MarkFormulaDirty() { formulaDirty_ = true; displayDirty_ = true; }
    void MarkFormulaClean() { formulaDirty_ = false; }
    
    // Get parsed formula (may be nullptr)
    std::shared_ptr<SpreadsheetFormula> GetFormula() const { return formula_; }
    void SetParsedFormula(std::shared_ptr<SpreadsheetFormula> formula) { formula_ = formula; }
    
    // Set formula result (called by formula engine)
    void SetFormulaResult(const CellValueVariant& result, CellValueType resultType);
    
    // ===== STYLING =====
    
    // Get style (returns default if nullptr)
    const CellStyle& GetStyle() const;
    CellStyle* GetStyleMutable();
    
    // Check if has custom style
    bool HasCustomStyle() const { return style_ != nullptr; }
    
    // Set style (copies)
    void SetStyle(const CellStyle& style);
    
    // Set style (shared pointer)
    void SetStyleShared(std::shared_ptr<CellStyle> style) { style_ = style; displayDirty_ = true; }
    
    // Clear custom style (use default)
    void ClearStyle() { style_.reset(); displayDirty_ = true; }
    
    // Style convenience methods
    void SetFont(const CellFont& font);
    void SetFontFamily(const std::string& family);
    void SetFontSize(float size);
    void SetFontColor(const Color& color);
    void SetBold(bool bold = true);
    void SetItalic(bool italic = true);
    void SetUnderline(UnderlineStyle style = UnderlineStyle::Single);
    void SetStrikethrough(bool strike = true);
    
    void SetBackgroundColor(const Color& color);
    void SetFill(const CellFill& fill);
    
    void SetBorders(const CellBorders& borders);
    void SetBorder(const CellBorder& border);  // All sides
    void SetBorderLeft(const CellBorder& border);
    void SetBorderRight(const CellBorder& border);
    void SetBorderTop(const CellBorder& border);
    void SetBorderBottom(const CellBorder& border);
    
    void SetHorizontalAlignment(HorizontalAlignment align);
    void SetVerticalAlignment(VerticalAlignment align);
    void SetAlignment(HorizontalAlignment h, VerticalAlignment v);
    
    void SetNumberFormat(const NumberFormat& format);
    void SetTextWrap(bool wrap = true);
    void SetTextRotation(int degrees);  // -90 to 90
    void SetIndent(int level);
    void SetShrinkToFit(bool shrink = true);
    
    void SetLocked(bool locked = true);
    void SetHidden(bool hidden = true);
    
    // ===== COMMENT =====
    
    bool HasComment() const { return comment_ != nullptr; }
    const CellComment* GetComment() const { return comment_.get(); }
    CellComment* GetCommentMutable() { return comment_.get(); }
    
    void SetComment(const std::string& text, const std::string& author = "");
    void SetComment(const CellComment& comment);
    void RemoveComment() { comment_.reset(); }
    
    // ===== HYPERLINK =====
    
    bool HasHyperlink() const { return hyperlink_ != nullptr; }
    const CellHyperlink* GetHyperlink() const { return hyperlink_.get(); }
    
    void SetHyperlink(const std::string& target, HyperlinkType type = HyperlinkType::URL);
    void SetHyperlink(const CellHyperlink& link);
    void RemoveHyperlink() { hyperlink_.reset(); }
    
    // ===== DATA VALIDATION =====
    
    bool HasValidation() const { return validation_ != nullptr; }
    const DataValidation* GetValidation() const { return validation_.get(); }
    
    void SetValidation(const DataValidation& validation);
    void RemoveValidation() { validation_.reset(); }
    
    // Validate current value against validation rules
    bool ValidateValue(std::string* errorMessage = nullptr) const;
    
    // ===== MERGE =====
    
    bool IsPartOfMerge() const { return isPartOfMerge_; }
    const CellAddress& GetMergeOrigin() const { return mergeOrigin_; }
    
    void SetMergeInfo(bool isPartOfMerge, const CellAddress& origin = CellAddress()) {
        isPartOfMerge_ = isPartOfMerge;
        mergeOrigin_ = origin;
    }
    
    void ClearMergeInfo() {
        isPartOfMerge_ = false;
        mergeOrigin_ = CellAddress();
    }
    
    // ===== COMPARISON =====
    
    // Compare values for sorting
    int CompareValue(const SpreadsheetCell& other, bool ascending = true, bool caseSensitive = false) const;
    
    // ===== SERIALIZATION =====
    
    // Clone cell (deep copy)
    SpreadsheetCell Clone() const;
    
    // Get cell data size (approximate memory usage)
    size_t GetMemorySize() const;
    
private:
    // Ensure style exists (create default if needed)
    void EnsureStyle();
    
    // Update display value cache
    void UpdateDisplayValue() const;
    
    // Format number according to number format
    std::string FormatNumber(double value) const;
    
    // Format date according to date format
    std::string FormatDate(const DateTimeValue& value) const;
    
    // Default style singleton
    static const CellStyle& GetDefaultStyle();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

inline SpreadsheetCell::SpreadsheetCell(const SpreadsheetCell& other)
    : row_(other.row_)
    , col_(other.col_)
    , valueType_(other.valueType_)
    , value_(other.value_)
    , formulaText_(other.formulaText_)
    , formula_(other.formula_)
    , formulaDirty_(other.formulaDirty_)
    , displayValue_(other.displayValue_)
    , displayDirty_(other.displayDirty_)
    , isPartOfMerge_(other.isPartOfMerge_)
    , mergeOrigin_(other.mergeOrigin_)
{
    if (other.style_) {
        style_ = std::make_shared<CellStyle>(*other.style_);
    }
    if (other.comment_) {
        comment_ = std::make_unique<CellComment>(*other.comment_);
    }
    if (other.hyperlink_) {
        hyperlink_ = std::make_unique<CellHyperlink>(*other.hyperlink_);
    }
    if (other.validation_) {
        validation_ = std::make_unique<DataValidation>(*other.validation_);
    }
}

inline SpreadsheetCell& SpreadsheetCell::operator=(const SpreadsheetCell& other) {
    if (this != &other) {
        row_ = other.row_;
        col_ = other.col_;
        valueType_ = other.valueType_;
        value_ = other.value_;
        formulaText_ = other.formulaText_;
        formula_ = other.formula_;
        formulaDirty_ = other.formulaDirty_;
        displayValue_ = other.displayValue_;
        displayDirty_ = other.displayDirty_;
        isPartOfMerge_ = other.isPartOfMerge_;
        mergeOrigin_ = other.mergeOrigin_;
        
        style_ = other.style_ ? std::make_shared<CellStyle>(*other.style_) : nullptr;
        comment_ = other.comment_ ? std::make_unique<CellComment>(*other.comment_) : nullptr;
        hyperlink_ = other.hyperlink_ ? std::make_unique<CellHyperlink>(*other.hyperlink_) : nullptr;
        validation_ = other.validation_ ? std::make_unique<DataValidation>(*other.validation_) : nullptr;
    }
    return *this;
}

// ===== VALUE GETTERS =====

inline std::string SpreadsheetCell::GetText() const {
    if (auto* str = std::get_if<std::string>(&value_)) {
        return *str;
    }
    return GetDisplayValue();
}

inline double SpreadsheetCell::GetNumber() const {
    if (auto* num = std::get_if<double>(&value_)) {
        return *num;
    }
    if (auto* curr = std::get_if<CurrencyValue>(&value_)) {
        return curr->amount;
    }
    return 0.0;
}

inline bool SpreadsheetCell::GetBoolean() const {
    if (auto* b = std::get_if<bool>(&value_)) {
        return *b;
    }
    return false;
}

inline CellErrorType SpreadsheetCell::GetError() const {
    if (auto* err = std::get_if<CellErrorType>(&value_)) {
        return *err;
    }
    return CellErrorType::None;
}

inline DateTimeValue SpreadsheetCell::GetDateTime() const {
    if (auto* num = std::get_if<double>(&value_)) {
        return DateTimeValue(*num);
    }
    return DateTimeValue();
}

inline CurrencyValue SpreadsheetCell::GetCurrency() const {
    if (auto* curr = std::get_if<CurrencyValue>(&value_)) {
        return *curr;
    }
    return CurrencyValue(GetNumber());
}

inline std::optional<std::string> SpreadsheetCell::TryGetText() const {
    if (auto* str = std::get_if<std::string>(&value_)) {
        return *str;
    }
    return std::nullopt;
}

inline std::optional<double> SpreadsheetCell::TryGetNumber() const {
    if (auto* num = std::get_if<double>(&value_)) {
        return *num;
    }
    return std::nullopt;
}

inline std::optional<bool> SpreadsheetCell::TryGetBoolean() const {
    if (auto* b = std::get_if<bool>(&value_)) {
        return *b;
    }
    return std::nullopt;
}

inline std::optional<CellErrorType> SpreadsheetCell::TryGetError() const {
    if (auto* err = std::get_if<CellErrorType>(&value_)) {
        return *err;
    }
    return std::nullopt;
}

inline std::string SpreadsheetCell::GetDisplayValue() const {
    if (displayDirty_) {
        UpdateDisplayValue();
    }
    return displayValue_;
}

// ===== VALUE SETTERS =====

inline void SpreadsheetCell::Clear() {
    ClearAll();
}

inline void SpreadsheetCell::ClearContents() {
    valueType_ = CellValueType::Empty;
    value_ = std::monostate{};
    formulaText_.clear();
    formula_.reset();
    formulaDirty_ = false;
    displayDirty_ = true;
}

inline void SpreadsheetCell::ClearFormats() {
    style_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::ClearComments() {
    comment_.reset();
}

inline void SpreadsheetCell::ClearHyperlinks() {
    hyperlink_.reset();
}

inline void SpreadsheetCell::ClearValidation() {
    validation_.reset();
}

inline void SpreadsheetCell::ClearAll() {
    ClearContents();
    ClearFormats();
    ClearComments();
    ClearHyperlinks();
    ClearValidation();
    ClearMergeInfo();
}

inline void SpreadsheetCell::SetText(const std::string& text) {
    valueType_ = CellValueType::Text;
    value_ = text;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetNumber(double value) {
    valueType_ = CellValueType::Number;
    value_ = value;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetBoolean(bool value) {
    valueType_ = CellValueType::Boolean;
    value_ = value;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetError(CellErrorType error) {
    valueType_ = CellValueType::Error;
    value_ = error;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetDate(int year, int month, int day) {
    SetDateTime(DateTimeValue::FromDate(year, month, day));
    valueType_ = CellValueType::Date;
}

inline void SpreadsheetCell::SetTime(int hour, int minute, int second) {
    SetDateTime(DateTimeValue::FromTime(hour, minute, second));
    valueType_ = CellValueType::Time;
}

inline void SpreadsheetCell::SetDateTime(int year, int month, int day, int hour, int minute, int second) {
    SetDateTime(DateTimeValue::FromDateTime(year, month, day, hour, minute, second));
}

inline void SpreadsheetCell::SetDateTime(const DateTimeValue& value) {
    valueType_ = CellValueType::DateTime;
    value_ = value.serialNumber;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetCurrency(double amount, const std::string& currencyCode) {
    valueType_ = CellValueType::Currency;
    value_ = CurrencyValue(amount, currencyCode);
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetPercentage(double value) {
    valueType_ = CellValueType::Percentage;
    value_ = value;
    formulaText_.clear();
    formula_.reset();
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetFormula(const std::string& formula) {
    formulaText_ = formula;
    valueType_ = CellValueType::Formula;
    formulaDirty_ = true;
    displayDirty_ = true;
    formula_.reset();  // Will be parsed by formula engine
}

inline void SpreadsheetCell::SetValue(const CellValueVariant& value, CellValueType type) {
    value_ = value;
    valueType_ = type;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetValueFromString(const std::string& input) {
    if (input.empty()) {
        ClearContents();
        return;
    }
    
    // Check for formula
    if (input[0] == '=') {
        SetFormula(input);
        return;
    }
    
    // Check for boolean
    std::string upper = input;
    for (auto& c : upper) c = toupper(c);
    if (upper == "TRUE" || upper == "FALSE") {
        SetBoolean(upper == "TRUE");
        return;
    }
    
    // Try to parse as number
    try {
        size_t pos;
        double num = std::stod(input, &pos);
        if (pos == input.length()) {
            // Check if it looks like a percentage
            if (input.back() == '%') {
                SetPercentage(num / 100.0);
            } else {
                SetNumber(num);
            }
            return;
        }
    } catch (...) {}
    
    // Default to text
    SetText(input);
}

inline void SpreadsheetCell::SetFormulaResult(const CellValueVariant& result, CellValueType resultType) {
    value_ = result;
    // Keep valueType_ as Formula, but store result
    formulaDirty_ = false;
    displayDirty_ = true;
}

// ===== STYLING =====

inline const CellStyle& SpreadsheetCell::GetDefaultStyle() {
    static CellStyle defaultStyle;
    return defaultStyle;
}

inline const CellStyle& SpreadsheetCell::GetStyle() const {
    return style_ ? *style_ : GetDefaultStyle();
}

inline CellStyle* SpreadsheetCell::GetStyleMutable() {
    EnsureStyle();
    return style_.get();
}

inline void SpreadsheetCell::SetStyle(const CellStyle& style) {
    style_ = std::make_shared<CellStyle>(style);
    displayDirty_ = true;
}

inline void SpreadsheetCell::EnsureStyle() {
    if (!style_) {
        style_ = std::make_shared<CellStyle>(GetDefaultStyle());
    }
}

inline void SpreadsheetCell::SetFont(const CellFont& font) {
    EnsureStyle();
    style_->font = font;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetFontFamily(const std::string& family) {
    EnsureStyle();
    style_->font.family = family;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetFontSize(float size) {
    EnsureStyle();
    style_->font.size = size;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetFontColor(const Color& color) {
    EnsureStyle();
    style_->font.color = color;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetBold(bool bold) {
    EnsureStyle();
    style_->font.bold = bold;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetItalic(bool italic) {
    EnsureStyle();
    style_->font.italic = italic;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetUnderline(UnderlineStyle style) {
    EnsureStyle();
    style_->font.underline = style;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetStrikethrough(bool strike) {
    EnsureStyle();
    style_->font.strikethrough = strike;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetBackgroundColor(const Color& color) {
    EnsureStyle();
    style_->fill = CellFill::Solid(color);
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetFill(const CellFill& fill) {
    EnsureStyle();
    style_->fill = fill;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetBorders(const CellBorders& borders) {
    EnsureStyle();
    style_->borders = borders;
}

inline void SpreadsheetCell::SetBorder(const CellBorder& border) {
    EnsureStyle();
    style_->borders.SetAll(border);
}

inline void SpreadsheetCell::SetBorderLeft(const CellBorder& border) {
    EnsureStyle();
    style_->borders.left = border;
}

inline void SpreadsheetCell::SetBorderRight(const CellBorder& border) {
    EnsureStyle();
    style_->borders.right = border;
}

inline void SpreadsheetCell::SetBorderTop(const CellBorder& border) {
    EnsureStyle();
    style_->borders.top = border;
}

inline void SpreadsheetCell::SetBorderBottom(const CellBorder& border) {
    EnsureStyle();
    style_->borders.bottom = border;
}

inline void SpreadsheetCell::SetHorizontalAlignment(HorizontalAlignment align) {
    EnsureStyle();
    style_->hAlign = align;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetVerticalAlignment(VerticalAlignment align) {
    EnsureStyle();
    style_->vAlign = align;
}

inline void SpreadsheetCell::SetAlignment(HorizontalAlignment h, VerticalAlignment v) {
    EnsureStyle();
    style_->hAlign = h;
    style_->vAlign = v;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetNumberFormat(const NumberFormat& format) {
    EnsureStyle();
    style_->numberFormat = format;
    displayDirty_ = true;
}

inline void SpreadsheetCell::SetTextWrap(bool wrap) {
    EnsureStyle();
    style_->wrapText = wrap;
}

inline void SpreadsheetCell::SetTextRotation(int degrees) {
    EnsureStyle();
    style_->textRotation = std::max(-90, std::min(90, degrees));
}

inline void SpreadsheetCell::SetIndent(int level) {
    EnsureStyle();
    style_->indent = std::max(0, level);
}

inline void SpreadsheetCell::SetShrinkToFit(bool shrink) {
    EnsureStyle();
    style_->shrinkToFit = shrink;
}

inline void SpreadsheetCell::SetLocked(bool locked) {
    EnsureStyle();
    style_->locked = locked;
}

inline void SpreadsheetCell::SetHidden(bool hidden) {
    EnsureStyle();
    style_->hidden = hidden;
}

// ===== COMMENT =====

inline void SpreadsheetCell::SetComment(const std::string& text, const std::string& author) {
    comment_ = std::make_unique<CellComment>(text, author);
}

inline void SpreadsheetCell::SetComment(const CellComment& comment) {
    comment_ = std::make_unique<CellComment>(comment);
}

// ===== HYPERLINK =====

inline void SpreadsheetCell::SetHyperlink(const std::string& target, HyperlinkType type) {
    hyperlink_ = std::make_unique<CellHyperlink>(target, type);
}

inline void SpreadsheetCell::SetHyperlink(const CellHyperlink& link) {
    hyperlink_ = std::make_unique<CellHyperlink>(link);
}

// ===== VALIDATION =====

inline void SpreadsheetCell::SetValidation(const DataValidation& validation) {
    validation_ = std::make_unique<DataValidation>(validation);
}

inline bool SpreadsheetCell::ValidateValue(std::string* errorMessage) const {
    if (!validation_) return true;
    
    // TODO: Implement validation logic based on ValidationType and ValidationOperator
    // For now, return true
    return true;
}

// ===== COMPARISON =====

inline int SpreadsheetCell::CompareValue(const SpreadsheetCell& other, bool ascending, bool caseSensitive) const {
    int result = 0;
    
    // Empty cells sort last
    if (IsEmpty() && other.IsEmpty()) return 0;
    if (IsEmpty()) return ascending ? 1 : -1;
    if (other.IsEmpty()) return ascending ? -1 : 1;
    
    // Errors sort after empty
    if (HasError() && other.HasError()) return 0;
    if (HasError()) return ascending ? 1 : -1;
    if (other.HasError()) return ascending ? -1 : 1;
    
    // Compare by type, then value
    if (IsNumeric() && other.IsNumeric()) {
        double a = GetNumber();
        double b = other.GetNumber();
        result = (a < b) ? -1 : (a > b) ? 1 : 0;
    } else {
        // Text comparison
        std::string a = GetDisplayValue();
        std::string b = other.GetDisplayValue();
        if (!caseSensitive) {
            for (auto& c : a) c = tolower(c);
            for (auto& c : b) c = tolower(c);
        }
        result = a.compare(b);
    }
    
    return ascending ? result : -result;
}

// ===== CLONE =====

inline SpreadsheetCell SpreadsheetCell::Clone() const {
    return SpreadsheetCell(*this);
}

// ===== MEMORY SIZE =====

inline size_t SpreadsheetCell::GetMemorySize() const {
    size_t size = sizeof(SpreadsheetCell);
    
    if (auto* str = std::get_if<std::string>(&value_)) {
        size += str->capacity();
    }
    size += formulaText_.capacity();
    size += displayValue_.capacity();
    
    if (style_) size += sizeof(CellStyle);
    if (comment_) size += sizeof(CellComment) + comment_->text.capacity();
    if (hyperlink_) size += sizeof(CellHyperlink) + hyperlink_->target.capacity();
    if (validation_) size += sizeof(DataValidation);
    
    return size;
}

// ===== DISPLAY VALUE =====

inline void SpreadsheetCell::UpdateDisplayValue() const {
    displayDirty_ = false;
    
    switch (valueType_) {
        case CellValueType::Empty:
            displayValue_.clear();
            break;
            
        case CellValueType::Text:
            if (auto* str = std::get_if<std::string>(&value_)) {
                displayValue_ = *str;
            }
            break;
            
        case CellValueType::Number:
        case CellValueType::Percentage:
            if (auto* num = std::get_if<double>(&value_)) {
                displayValue_ = FormatNumber(*num);
            }
            break;
            
        case CellValueType::Boolean:
            if (auto* b = std::get_if<bool>(&value_)) {
                displayValue_ = *b ? "TRUE" : "FALSE";
            }
            break;
            
        case CellValueType::Error:
            if (auto* err = std::get_if<CellErrorType>(&value_)) {
                displayValue_ = CellErrorToString(*err);
            }
            break;
            
        case CellValueType::Date:
        case CellValueType::Time:
        case CellValueType::DateTime:
            if (auto* num = std::get_if<double>(&value_)) {
                displayValue_ = FormatDate(DateTimeValue(*num));
            }
            break;
            
        case CellValueType::Currency:
            if (auto* curr = std::get_if<CurrencyValue>(&value_)) {
                displayValue_ = curr->currencyCode + " " + FormatNumber(curr->amount);
            }
            break;
            
        case CellValueType::Formula:
            // Display the calculated result
            if (formulaDirty_) {
                displayValue_ = formulaText_;  // Show formula if not calculated
            } else if (auto* str = std::get_if<std::string>(&value_)) {
                displayValue_ = *str;
            } else if (auto* num = std::get_if<double>(&value_)) {
                displayValue_ = FormatNumber(*num);
            } else if (auto* b = std::get_if<bool>(&value_)) {
                displayValue_ = *b ? "TRUE" : "FALSE";
            } else if (auto* err = std::get_if<CellErrorType>(&value_)) {
                displayValue_ = CellErrorToString(*err);
            } else {
                displayValue_.clear();
            }
            break;
    }
}

inline std::string SpreadsheetCell::FormatNumber(double value) const {
    const NumberFormat& fmt = GetStyle().numberFormat;

    char buffer[64];

    // Insert thousands separators into the integer part of a (non-negative)
    // numeric string, preserving any fractional ".xx" tail.
    auto groupThousands = [](const std::string& number) -> std::string {
        std::string intPart = number;
        std::string fracPart;
        auto dot = number.find('.');
        if (dot != std::string::npos) {
            intPart = number.substr(0, dot);
            fracPart = number.substr(dot);  // includes the '.'
        }
        std::string out;
        int n = static_cast<int>(intPart.size());
        for (int i = 0; i < n; ++i) {
            if (i > 0 && (n - i) % 3 == 0) out.push_back(',');
            out.push_back(intPart[i]);
        }
        return out + fracPart;
    };

    switch (fmt.category) {
        case NumberFormatCategory::Percentage:
            snprintf(buffer, sizeof(buffer), "%.*f", fmt.decimalPlaces, value * 100.0);
            return std::string(buffer) + "%";

        case NumberFormatCategory::Scientific:
            snprintf(buffer, sizeof(buffer), "%.*e", fmt.decimalPlaces, value);
            return std::string(buffer);

        case NumberFormatCategory::Currency:
        case NumberFormatCategory::Accounting: {
            bool negative = value < 0.0;
            snprintf(buffer, sizeof(buffer), "%.*f", fmt.decimalPlaces, std::fabs(value));
            std::string num = fmt.useThousandsSeparator ? groupThousands(buffer)
                                                        : std::string(buffer);
            std::string body = fmt.currencySymbolAfter
                ? num + " " + fmt.currencySymbol
                : fmt.currencySymbol + num;
            if (!negative) return body;
            // Accounting wraps negatives in parentheses; currency uses a sign.
            return (fmt.category == NumberFormatCategory::Accounting)
                ? "(" + body + ")"
                : "-" + body;
        }

        case NumberFormatCategory::General: {
            // "General": show integers without a decimal point and other
            // numbers with up to ~10 significant digits, trailing zeros
            // trimmed (matches typical spreadsheet General behaviour).
            if (value == std::floor(value) && std::abs(value) < 1e15) {
                snprintf(buffer, sizeof(buffer), "%.0f", value);
            } else {
                snprintf(buffer, sizeof(buffer), "%.10g", value);
            }
            return std::string(buffer);
        }

        case NumberFormatCategory::Number:
        default: {
            bool negative = value < 0.0;
            double mag = std::fabs(value);
            if (fmt.decimalPlaces == 0 && mag == std::floor(mag)) {
                snprintf(buffer, sizeof(buffer), "%.0f", mag);
            } else {
                snprintf(buffer, sizeof(buffer), "%.*f", fmt.decimalPlaces, mag);
            }
            std::string num = fmt.useThousandsSeparator ? groupThousands(buffer)
                                                        : std::string(buffer);
            return negative ? "-" + num : num;
        }
    }
}

inline std::string SpreadsheetCell::FormatDate(const DateTimeValue& value) const {
    int year, month, day, hour, minute, second;
    value.ToDate(year, month, day);
    value.ToTime(hour, minute, second);
    
    const NumberFormat& fmt = GetStyle().numberFormat;
    
    char buffer[64];
    
    switch (fmt.category) {
        case NumberFormatCategory::Date:
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
            break;
            
        case NumberFormatCategory::Time:
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hour, minute, second);
            break;
            
        case NumberFormatCategory::DateTime:
        default:
            snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
                     year, month, day, hour, minute, second);
            break;
    }
    
    return std::string(buffer);
}

} // namespace UltraCanvas
