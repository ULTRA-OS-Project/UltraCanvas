// include/UltraCanvasSpreadsheetTypes.h
// Core type definitions for UltraCanvas Spreadsheet component
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>
#include <chrono>
#include <limits>
#include <cmath>

// UltraCanvasCommonTypes.h transitively includes <X11/Xlib.h> on Linux/BSD,
// which #defines several macros that clash with our identifiers. `None` (0L)
// collides with CellErrorType::None below. Follow the same #undef pattern the
// existing chart/diagram plugins use.
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif
#ifdef Always
#undef Always
#endif

namespace UltraCanvas {

// ============================================================================
// SPREADSHEET LIMITS
// ============================================================================

struct SpreadsheetLimits {
    static constexpr int MaxRows = 1048576;        // 2^20 rows (same as Excel/ODS)
    static constexpr int MaxColumns = 16384;       // 2^14 columns (XFD)
    static constexpr int MaxSheets = 256;          // Maximum worksheets
    static constexpr int MaxCellTextLength = 32767;
    static constexpr int MaxFormulaLength = 8192;
    static constexpr int MaxUndoLevels = 100;
    static constexpr int MaxColumnWidth = 255;     // In characters
    static constexpr int MaxRowHeight = 409;       // In points
    static constexpr int DefaultColumnWidth = 64;  // In pixels
    static constexpr int DefaultRowHeight = 20;    // In pixels
    static constexpr int HeaderRowHeight = 24;
    static constexpr int RowHeaderWidth = 50;
};

// ============================================================================
// CELL VALUE TYPES (OpenFormula compatible)
// ============================================================================

enum class CellValueType {
    Empty,          // No value
    Text,           // String value
    Number,         // Numeric value (double precision)
    Boolean,        // TRUE/FALSE
    Error,          // Error value (#VALUE!, #REF!, etc.)
    Date,           // Date value (stored as serial number)
    Time,           // Time value (fraction of day)
    DateTime,       // Combined date and time
    Currency,       // Currency value with code
    Percentage,     // Percentage (0.5 = 50%)
    Formula         // Calculated formula (result stored separately)
};

// ============================================================================
// CELL ERROR TYPES (OpenFormula compatible)
// ============================================================================

enum class CellErrorType {
    None = 0,
    DivisionByZero,     // #DIV/0!
    ValueError,         // #VALUE!
    ReferenceError,     // #REF!
    NameError,          // #NAME?
    NumError,           // #NUM!
    NullError,          // #NULL!
    NAError,            // #N/A
    GettingData,        // #GETTING_DATA
    Spill,              // #SPILL!
    Connect,            // #CONNECT!
    Blocked,            // #BLOCKED!
    Unknown,            // #UNKNOWN!
    Calc                // #CALC!
};

// Convert error type to display string
inline std::string CellErrorToString(CellErrorType error) {
    switch (error) {
        case CellErrorType::None: return "";
        case CellErrorType::DivisionByZero: return "#DIV/0!";
        case CellErrorType::ValueError: return "#VALUE!";
        case CellErrorType::ReferenceError: return "#REF!";
        case CellErrorType::NameError: return "#NAME?";
        case CellErrorType::NumError: return "#NUM!";
        case CellErrorType::NullError: return "#NULL!";
        case CellErrorType::NAError: return "#N/A";
        case CellErrorType::GettingData: return "#GETTING_DATA";
        case CellErrorType::Spill: return "#SPILL!";
        case CellErrorType::Connect: return "#CONNECT!";
        case CellErrorType::Blocked: return "#BLOCKED!";
        case CellErrorType::Calc: return "#CALC!";
        default: return "#UNKNOWN!";
    }
}

// ============================================================================
// CELL REFERENCE SYSTEM
// ============================================================================

// Reference type for formulas
enum class ReferenceType {
    Relative,       // A1 - changes when copied
    Absolute,       // $A$1 - stays fixed when copied
    MixedRowAbs,    // A$1 - row absolute, column relative
    MixedColAbs     // $A1 - column absolute, row relative
};

// Single cell address
struct CellAddress {
    int row = 0;                                    // 0-based row index
    int col = 0;                                    // 0-based column index
    ReferenceType rowRef = ReferenceType::Relative;
    ReferenceType colRef = ReferenceType::Relative;
    std::string sheetName;                          // Empty = current sheet
    
    CellAddress() = default;
    CellAddress(int r, int c) : row(r), col(c) {}
    CellAddress(int r, int c, const std::string& sheet) 
        : row(r), col(c), sheetName(sheet) {}
    
    bool IsValid() const {
        return row >= 0 && row < SpreadsheetLimits::MaxRows &&
               col >= 0 && col < SpreadsheetLimits::MaxColumns;
    }
    
    bool operator==(const CellAddress& other) const {
        return row == other.row && col == other.col && sheetName == other.sheetName;
    }
    
    bool operator!=(const CellAddress& other) const {
        return !(*this == other);
    }
    
    bool operator<(const CellAddress& other) const {
        if (sheetName != other.sheetName) return sheetName < other.sheetName;
        if (row != other.row) return row < other.row;
        return col < other.col;
    }
    
    // Convert column index to letter (0 = A, 25 = Z, 26 = AA, etc.)
    static std::string ColumnToLetter(int col) {
        std::string result;
        col++; // Convert to 1-based
        while (col > 0) {
            col--;
            result = static_cast<char>('A' + (col % 26)) + result;
            col /= 26;
        }
        return result;
    }
    
    // Convert column letter to index (A = 0, Z = 25, AA = 26, etc.)
    static int LetterToColumn(const std::string& letters) {
        int col = 0;
        for (char c : letters) {
            if (c >= 'A' && c <= 'Z') {
                col = col * 26 + (c - 'A' + 1);
            } else if (c >= 'a' && c <= 'z') {
                col = col * 26 + (c - 'a' + 1);
            }
        }
        return col - 1; // Convert to 0-based
    }
    
    // Convert to A1 notation string
    std::string ToString() const {
        std::string result;
        if (!sheetName.empty()) {
            result = sheetName + ".";
        }
        if (colRef == ReferenceType::Absolute || colRef == ReferenceType::MixedColAbs) {
            result += "$";
        }
        result += ColumnToLetter(col);
        if (rowRef == ReferenceType::Absolute || rowRef == ReferenceType::MixedRowAbs) {
            result += "$";
        }
        result += std::to_string(row + 1); // Convert to 1-based for display
        return result;
    }
    
    // Parse from A1 notation (returns invalid address on failure)
    static CellAddress FromString(const std::string& str);
};

// Cell range (rectangular selection)
struct CellRange {
    CellAddress start;
    CellAddress end;
    
    CellRange() = default;
    CellRange(const CellAddress& single) : start(single), end(single) {}
    CellRange(const CellAddress& s, const CellAddress& e) : start(s), end(e) { Normalize(); }
    CellRange(int startRow, int startCol, int endRow, int endCol)
        : start(startRow, startCol), end(endRow, endCol) { Normalize(); }
    
    // Ensure start <= end
    void Normalize() {
        if (start.row > end.row) std::swap(start.row, end.row);
        if (start.col > end.col) std::swap(start.col, end.col);
    }
    
    bool IsValid() const {
        return start.IsValid() && end.IsValid();
    }
    
    bool IsSingleCell() const {
        return start.row == end.row && start.col == end.col;
    }
    
    bool Contains(const CellAddress& addr) const {
        return addr.row >= start.row && addr.row <= end.row &&
               addr.col >= start.col && addr.col <= end.col;
    }
    
    bool Contains(int row, int col) const {
        return row >= start.row && row <= end.row &&
               col >= start.col && col <= end.col;
    }
    
    bool Intersects(const CellRange& other) const {
        return !(end.row < other.start.row || start.row > other.end.row ||
                 end.col < other.start.col || start.col > other.end.col);
    }
    
    int RowCount() const { return end.row - start.row + 1; }
    int ColCount() const { return end.col - start.col + 1; }
    int CellCount() const { return RowCount() * ColCount(); }
    
    // Convert to A1:B2 notation
    std::string ToString() const {
        if (IsSingleCell()) {
            return start.ToString();
        }
        return start.ToString() + ":" + end.ToString();
    }
    
    // Parse from A1:B2 notation
    static CellRange FromString(const std::string& str);
    
    bool operator==(const CellRange& other) const {
        return start == other.start && end == other.end;
    }
};

// ============================================================================
// CELL VALUE STORAGE
// ============================================================================

// Currency value with ISO 4217 code
struct CurrencyValue {
    double amount = 0.0;
    std::string currencyCode = "USD";  // ISO 4217
    
    CurrencyValue() = default;
    CurrencyValue(double a, const std::string& code = "USD") 
        : amount(a), currencyCode(code) {}
};

// Date/Time stored as serial number (days since epoch)
// ODS uses December 30, 1899 as epoch (day 0)
struct DateTimeValue {
    double serialNumber = 0.0;  // Integer part = days, fractional = time
    
    DateTimeValue() = default;
    explicit DateTimeValue(double serial) : serialNumber(serial) {}
    
    // Create from date components
    static DateTimeValue FromDate(int year, int month, int day);
    
    // Create from time components
    static DateTimeValue FromTime(int hour, int minute, int second, int millisecond = 0);
    
    // Create from date and time
    static DateTimeValue FromDateTime(int year, int month, int day,
                                      int hour, int minute, int second);
    
    // Extract components
    void ToDate(int& year, int& month, int& day) const;
    void ToTime(int& hour, int& minute, int& second) const;
    
    // Get date part only (integer)
    int GetDateSerial() const { return static_cast<int>(serialNumber); }
    
    // Get time part only (fraction)
    double GetTimeFraction() const { return serialNumber - GetDateSerial(); }
};

// Variant type for cell values
using CellValueVariant = std::variant<
    std::monostate,     // Empty
    std::string,        // Text
    double,             // Number, Date, Time, Percentage
    bool,               // Boolean
    CellErrorType,      // Error
    CurrencyValue       // Currency
>;

// ============================================================================
// CELL FORMATTING
// ============================================================================

// Horizontal alignment
enum class HorizontalAlignment {
    General,    // Default based on content type
    Left,
    Center,
    Right,
    Fill,       // Repeat content to fill cell
    Justify,    // Justify text
    Distributed // Distributed alignment
};

// Vertical alignment is provided by the framework (UltraCanvasCommonTypes.h)
// as enum class VerticalAlignment { Top, Middle, Bottom }. We reuse it rather
// than redefining to avoid an ODR clash; spreadsheet cells only use those three.

// Border style
enum class BorderStyle {
    None,
    Thin,
    Medium,
    Thick,
    Dashed,
    Dotted,
    Double,
    Hair,
    MediumDashed,
    DashDot,
    MediumDashDot,
    DashDotDot,
    SlantDashDot
};

// Single border definition
struct CellBorder {
    BorderStyle style = BorderStyle::None;
    Color color = Colors::Black;
    int width = 1;
    
    bool IsVisible() const { return style != BorderStyle::None; }
};

// All four borders
struct CellBorders {
    CellBorder left;
    CellBorder right;
    CellBorder top;
    CellBorder bottom;
    CellBorder diagonalUp;    // Bottom-left to top-right
    CellBorder diagonalDown;  // Top-left to bottom-right
    
    void SetAll(const CellBorder& border) {
        left = right = top = bottom = border;
    }
    
    void SetOutline(const CellBorder& border) {
        left = right = top = bottom = border;
    }
    
    bool HasAnyBorder() const {
        return left.IsVisible() || right.IsVisible() || 
               top.IsVisible() || bottom.IsVisible() ||
               diagonalUp.IsVisible() || diagonalDown.IsVisible();
    }
};

// Underline style
enum class UnderlineStyle {
    None,
    Single,
    Double,
    SingleAccounting,
    DoubleAccounting
};

// Font style for cells
struct CellFont {
    std::string family = "Arial";
    float size = 11.0f;
    Color color = Colors::Black;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    UnderlineStyle underline = UnderlineStyle::None;
    bool subscript = false;
    bool superscript = false;
    
    bool operator==(const CellFont& other) const {
        return family == other.family && size == other.size &&
               color == other.color && bold == other.bold &&
               italic == other.italic && strikethrough == other.strikethrough &&
               underline == other.underline && subscript == other.subscript &&
               superscript == other.superscript;
    }
};

// Background fill pattern
enum class FillPattern {
    None,
    Solid,
    Gray75,
    Gray50,
    Gray25,
    Gray125,
    Gray0625,
    HorzStripe,
    VertStripe,
    ReverseDiagStripe,
    DiagStripe,
    DiagCross,
    ThickDiagCross,
    ThinHorzStripe,
    ThinVertStripe,
    ThinReverseDiagStripe,
    ThinDiagStripe,
    ThinHorzCross,
    ThinDiagCross
};

// Cell fill/background
struct CellFill {
    FillPattern pattern = FillPattern::None;
    Color foregroundColor = Colors::White;
    Color backgroundColor = Colors::White;
    
    bool HasFill() const { return pattern != FillPattern::None; }
    
    static CellFill Solid(const Color& color) {
        CellFill fill;
        fill.pattern = FillPattern::Solid;
        fill.foregroundColor = color;
        return fill;
    }
};

// Number format category
enum class NumberFormatCategory {
    General,
    Number,
    Currency,
    Accounting,
    Date,
    Time,
    DateTime,
    Percentage,
    Fraction,
    Scientific,
    Text,
    Special,
    Custom
};

// Number format definition
struct NumberFormat {
    NumberFormatCategory category = NumberFormatCategory::General;
    std::string formatCode = "General";  // Format string
    int decimalPlaces = 2;
    bool useThousandsSeparator = false;
    std::string currencySymbol = "$";
    bool currencySymbolAfter = false;  // place symbol after the amount, e.g. "1,234.00 €"
    bool negativeRed = false;

    // Predefined formats
    static NumberFormat General() { return NumberFormat(); }
    static NumberFormat Number(int decimals = 2, bool thousands = false);
    static NumberFormat Currency(const std::string& symbol = "$", int decimals = 2,
                                 bool symbolAfter = false);
    static NumberFormat Percentage(int decimals = 0);
    static NumberFormat Date(const std::string& format = "YYYY-MM-DD");
    static NumberFormat Time(const std::string& format = "HH:MM:SS");
    static NumberFormat Scientific(int decimals = 2);
    static NumberFormat Text();
};

// Complete cell style
struct CellStyle {
    CellFont font;
    CellFill fill;
    CellBorders borders;
    HorizontalAlignment hAlign = HorizontalAlignment::General;
    VerticalAlignment vAlign = VerticalAlignment::Bottom;
    NumberFormat numberFormat;
    int textRotation = 0;           // -90 to 90 degrees
    int indent = 0;                 // Indentation level
    bool wrapText = false;
    bool shrinkToFit = false;
    bool locked = true;             // For sheet protection
    bool hidden = false;            // Hide formula
    
    // Convenience methods
    void SetFont(const std::string& family, float size, const Color& color = Colors::Black) {
        font.family = family;
        font.size = size;
        font.color = color;
    }
    
    void SetBold(bool b = true) { font.bold = b; }
    void SetItalic(bool i = true) { font.italic = i; }
    void SetBackgroundColor(const Color& color) { fill = CellFill::Solid(color); }
};

// ============================================================================
// COLUMN AND ROW DEFINITIONS
// ============================================================================

struct ColumnDefinition {
    int index = 0;
    int width = SpreadsheetLimits::DefaultColumnWidth;  // In pixels
    bool hidden = false;
    bool collapsed = false;     // For grouping
    int outlineLevel = 0;       // Grouping level
    CellStyle defaultStyle;     // Default style for column
    
    ColumnDefinition() = default;
    explicit ColumnDefinition(int idx, int w = SpreadsheetLimits::DefaultColumnWidth)
        : index(idx), width(w) {}
};

struct RowDefinition {
    int index = 0;
    int height = SpreadsheetLimits::DefaultRowHeight;  // In pixels
    bool hidden = false;
    bool collapsed = false;     // For grouping
    int outlineLevel = 0;       // Grouping level
    CellStyle defaultStyle;     // Default style for row
    
    RowDefinition() = default;
    explicit RowDefinition(int idx, int h = SpreadsheetLimits::DefaultRowHeight)
        : index(idx), height(h) {}
};

// ============================================================================
// MERGED CELLS
// ============================================================================

struct MergedCell {
    CellRange range;
    
    MergedCell() = default;
    explicit MergedCell(const CellRange& r) : range(r) {}
    MergedCell(int startRow, int startCol, int endRow, int endCol)
        : range(startRow, startCol, endRow, endCol) {}
    
    bool Contains(int row, int col) const {
        return range.Contains(row, col);
    }
    
    bool IsTopLeft(int row, int col) const {
        return row == range.start.row && col == range.start.col;
    }
};

// ============================================================================
// FREEZE PANES
// ============================================================================

struct FreezePanes {
    int frozenRows = 0;         // Number of rows frozen at top
    int frozenColumns = 0;      // Number of columns frozen at left
    CellAddress topLeftCell;    // First unfrozen cell (for scrolling area)
    bool splitHorizontal = false;
    bool splitVertical = false;
    int horizontalSplitPosition = 0;  // In pixels for split view
    int verticalSplitPosition = 0;
    
    bool HasFrozenRows() const { return frozenRows > 0; }
    bool HasFrozenColumns() const { return frozenColumns > 0; }
    bool HasFreeze() const { return HasFrozenRows() || HasFrozenColumns(); }
    bool HasSplit() const { return splitHorizontal || splitVertical; }
};

// ============================================================================
// CELL COMMENT/NOTE
// ============================================================================

struct CellComment {
    std::string text;
    std::string author;
    std::chrono::system_clock::time_point timestamp;
    bool visible = false;       // Always show comment
    Rect2Di bounds;             // Comment popup bounds (when visible)
    Color backgroundColor = Color(255, 255, 225);  // Light yellow default
    
    CellComment() = default;
    explicit CellComment(const std::string& t, const std::string& a = "")
        : text(t), author(a), timestamp(std::chrono::system_clock::now()) {}
};

// ============================================================================
// DATA VALIDATION
// ============================================================================

enum class ValidationOperator {
    Between,
    NotBetween,
    Equal,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual
};

enum class ValidationType {
    Any,            // No validation
    WholeNumber,
    Decimal,
    List,           // Dropdown list
    Date,
    Time,
    TextLength,
    Custom          // Custom formula
};

enum class ValidationAlertStyle {
    Stop,           // Prevent invalid entry
    Warning,        // Warn but allow
    Information     // Information only
};

struct DataValidation {
    ValidationType type = ValidationType::Any;
    ValidationOperator op = ValidationOperator::Between;
    std::string formula1;       // Min value or list source
    std::string formula2;       // Max value
    bool allowBlank = true;
    bool showDropdown = true;   // For list validation
    bool showInputMessage = false;
    std::string inputTitle;
    std::string inputMessage;
    bool showErrorAlert = true;
    ValidationAlertStyle alertStyle = ValidationAlertStyle::Stop;
    std::string errorTitle;
    std::string errorMessage;
    
    bool IsValid() const { return type != ValidationType::Any; }
};

// ============================================================================
// CONDITIONAL FORMATTING
// ============================================================================

enum class ConditionalFormatType {
    CellValue,
    Expression,
    ColorScale,
    DataBar,
    IconSet,
    Top10,
    AboveAverage,
    UniqueValues,
    DuplicateValues,
    ContainsText,
    NotContainsText,
    BeginsWithText,
    EndsWithText,
    ContainsBlanks,
    NotContainsBlanks,
    ContainsErrors,
    NotContainsErrors,
    TimePeriod
};

struct ConditionalFormatRule {
    ConditionalFormatType type = ConditionalFormatType::CellValue;
    ValidationOperator op = ValidationOperator::Between;
    std::string formula1;
    std::string formula2;
    CellStyle style;            // Style to apply when condition is true
    int priority = 0;           // Lower = higher priority
    bool stopIfTrue = false;
    
    // For color scales
    std::vector<Color> colorScale;
    std::vector<double> colorScaleValues;
    
    // For data bars
    Color dataBarColor = Colors::Blue;
    bool showValue = true;
    
    // For icon sets
    std::vector<std::string> iconSetType;
};

struct ConditionalFormat {
    CellRange range;
    std::vector<ConditionalFormatRule> rules;
};

// ============================================================================
// NAMED RANGE
// ============================================================================

struct NamedRange {
    std::string name;
    CellRange range;
    std::string sheetScope;     // Empty = workbook scope
    std::string comment;
    bool hidden = false;
    
    NamedRange() = default;
    NamedRange(const std::string& n, const CellRange& r, const std::string& scope = "")
        : name(n), range(r), sheetScope(scope) {}
};

// ============================================================================
// HYPERLINK
// ============================================================================

enum class HyperlinkType {
    URL,
    File,
    Email,
    InternalCell,
    InternalRange,
    InternalNamedRange
};

struct CellHyperlink {
    HyperlinkType type = HyperlinkType::URL;
    std::string target;         // URL, file path, or cell reference
    std::string displayText;    // Text to display (empty = use target)
    std::string tooltip;
    
    CellHyperlink() = default;
    CellHyperlink(const std::string& t, HyperlinkType ht = HyperlinkType::URL)
        : type(ht), target(t) {}
};

// ============================================================================
// SORT/FILTER DEFINITIONS
// ============================================================================

enum class SortOrder {
    Ascending,
    Descending
};

struct SortCriteria {
    int column = 0;
    SortOrder order = SortOrder::Ascending;
    bool caseSensitive = false;
};

enum class FilterOperator {
    None,
    Equals,
    NotEquals,
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual,
    Contains,
    NotContains,
    BeginsWith,
    EndsWith,
    Top10,
    Bottom10,
    AboveAverage,
    BelowAverage,
    Custom
};

struct ColumnFilter {
    int column = 0;
    FilterOperator op = FilterOperator::None;
    std::vector<std::string> values;    // For value-based filtering
    std::string customFormula;
    bool showBlanks = true;
};

struct AutoFilter {
    CellRange range;
    std::vector<ColumnFilter> columnFilters;
    std::vector<int> hiddenRows;        // Rows hidden by filter
    bool enabled = false;
};

// ============================================================================
// PRINT SETTINGS
// ============================================================================

enum class PageOrientation {
    Portrait,
    Landscape
};

enum class PaperSize {
    Letter,
    Legal,
    A3,
    A4,
    A5,
    B4,
    B5,
    Custom
};

struct PrintSettings {
    PageOrientation orientation = PageOrientation::Portrait;
    PaperSize paperSize = PaperSize::Letter;
    float marginTop = 0.75f;    // Inches
    float marginBottom = 0.75f;
    float marginLeft = 0.7f;
    float marginRight = 0.7f;
    float headerMargin = 0.3f;
    float footerMargin = 0.3f;
    bool centerHorizontally = false;
    bool centerVertically = false;
    bool printGridlines = false;
    bool printRowColHeaders = false;
    int fitToWidth = 0;         // 0 = auto
    int fitToHeight = 0;
    int scale = 100;            // Percentage
    CellRange printArea;
    std::string headerLeft, headerCenter, headerRight;
    std::string footerLeft, footerCenter, footerRight;
    std::vector<int> rowBreaks;
    std::vector<int> colBreaks;
};

// ============================================================================
// SHEET PROTECTION
// ============================================================================

struct SheetProtection {
    bool isProtected = false;
    std::string passwordHash;       // Hashed password
    bool selectLockedCells = true;
    bool selectUnlockedCells = true;
    bool formatCells = false;
    bool formatColumns = false;
    bool formatRows = false;
    bool insertColumns = false;
    bool insertRows = false;
    bool insertHyperlinks = false;
    bool deleteColumns = false;
    bool deleteRows = false;
    bool sort = false;
    bool autoFilter = false;
    bool pivotTables = false;
    bool editObjects = false;
    bool editScenarios = false;
};

// ============================================================================
// CLIPBOARD DATA
// ============================================================================

enum class ClipboardOperation {
    None,
    Copy,
    Cut
};

enum class PasteType {
    All,
    Values,
    Formulas,
    Formats,
    Comments,
    Validation,
    AllExceptBorders,
    ColumnWidths,
    FormulasAndNumberFormats,
    ValuesAndNumberFormats
};

enum class PasteOperation {
    None,
    Add,
    Subtract,
    Multiply,
    Divide
};

struct PasteOptions {
    PasteType pasteType = PasteType::All;
    PasteOperation operation = PasteOperation::None;
    bool skipBlanks = false;
    bool transpose = false;
    bool pasteLink = false;
};

// ============================================================================
// SELECTION STATE
// ============================================================================

struct SelectionState {
    CellAddress activeCell;             // Current active cell
    std::vector<CellRange> selections;  // All selected ranges
    int activeSheetIndex = 0;
    
    SelectionState() = default;
    explicit SelectionState(const CellAddress& active) 
        : activeCell(active), selections({CellRange(active)}) {}
    
    bool IsSelected(int row, int col) const {
        for (const auto& range : selections) {
            if (range.Contains(row, col)) return true;
        }
        return false;
    }
    
    bool IsSelected(const CellAddress& addr) const {
        return IsSelected(addr.row, addr.col);
    }
    
    CellRange GetPrimarySelection() const {
        return selections.empty() ? CellRange(activeCell) : selections[0];
    }
    
    void Clear() {
        selections.clear();
        selections.push_back(CellRange(activeCell));
    }
    
    void SetSingle(const CellAddress& addr) {
        activeCell = addr;
        selections.clear();
        selections.push_back(CellRange(addr));
    }
    
    void SetRange(const CellRange& range) {
        activeCell = range.start;
        selections.clear();
        selections.push_back(range);
    }
    
    void AddRange(const CellRange& range) {
        selections.push_back(range);
    }
};

// ============================================================================
// UNDO/REDO OPERATIONS
// ============================================================================

enum class UndoActionType {
    CellValueChange,
    CellFormatChange,
    CellStyleChange,
    RowInsert,
    RowDelete,
    ColumnInsert,
    ColumnDelete,
    RowResize,
    ColumnResize,
    MergeCells,
    UnmergeCells,
    Sort,
    Filter,
    Clear,
    Paste,
    Fill,
    SheetInsert,
    SheetDelete,
    SheetRename,
    SheetReorder,
    FreezePane,
    Composite       // Multiple actions grouped
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Parse cell address from A1 notation
inline CellAddress CellAddress::FromString(const std::string& str) {
    CellAddress result;
    if (str.empty()) return result;
    
    std::string s = str;
    
    // Check for sheet reference (Sheet1.A1 or 'Sheet Name'.A1)
    size_t dotPos = s.find('.');
    if (dotPos != std::string::npos) {
        result.sheetName = s.substr(0, dotPos);
        // Remove quotes if present
        if (!result.sheetName.empty() && result.sheetName[0] == '\'') {
            result.sheetName = result.sheetName.substr(1, result.sheetName.length() - 2);
        }
        s = s.substr(dotPos + 1);
    }
    
    size_t i = 0;
    
    // Check for column absolute reference
    if (i < s.length() && s[i] == '$') {
        result.colRef = ReferenceType::Absolute;
        i++;
    }
    
    // Parse column letters
    std::string colStr;
    while (i < s.length() && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
        colStr += s[i];
        i++;
    }
    
    if (colStr.empty()) {
        result.row = -1;  // Invalid
        return result;
    }
    result.col = LetterToColumn(colStr);
    
    // Check for row absolute reference
    if (i < s.length() && s[i] == '$') {
        result.rowRef = ReferenceType::Absolute;
        i++;
    }
    
    // Parse row number
    std::string rowStr;
    while (i < s.length() && s[i] >= '0' && s[i] <= '9') {
        rowStr += s[i];
        i++;
    }
    
    if (rowStr.empty()) {
        result.row = -1;  // Invalid
        return result;
    }
    result.row = std::stoi(rowStr) - 1;  // Convert to 0-based
    
    return result;
}

// Parse cell range from A1:B2 notation
inline CellRange CellRange::FromString(const std::string& str) {
    size_t colonPos = str.find(':');
    if (colonPos == std::string::npos) {
        return CellRange(CellAddress::FromString(str));
    }
    
    CellAddress startAddr = CellAddress::FromString(str.substr(0, colonPos));
    CellAddress endAddr = CellAddress::FromString(str.substr(colonPos + 1));
    
    return CellRange(startAddr, endAddr);
}

// DateTimeValue implementations
inline DateTimeValue DateTimeValue::FromDate(int year, int month, int day) {
    // Calculate days since December 30, 1899
    // Using a simplified algorithm
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    
    int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    int epoch = 2415019;  // Julian day number for Dec 30, 1899
    
    DateTimeValue result;
    result.serialNumber = static_cast<double>(jdn - epoch);
    return result;
}

inline DateTimeValue DateTimeValue::FromTime(int hour, int minute, int second, int millisecond) {
    DateTimeValue result;
    result.serialNumber = (hour * 3600.0 + minute * 60.0 + second + millisecond / 1000.0) / 86400.0;
    return result;
}

inline DateTimeValue DateTimeValue::FromDateTime(int year, int month, int day,
                                                  int hour, int minute, int second) {
    DateTimeValue date = FromDate(year, month, day);
    DateTimeValue time = FromTime(hour, minute, second);
    date.serialNumber += time.serialNumber;
    return date;
}

inline void DateTimeValue::ToDate(int& year, int& month, int& day) const {
    // Invert FromDate(): serial = JulianDayNumber - 2415019, so recover the
    // Julian day with serial + 2415019. (A previous extra "+ 1" here made
    // ToDate one day ahead of FromDate, so every stored date rendered a day
    // late and round-tripping a date drifted forward.)
    int z = GetDateSerial() + 2415019;  // Convert back to Julian day number
    int a = z + 32044;
    int b = (4 * a + 3) / 146097;
    int c = a - (146097 * b) / 4;
    int d = (4 * c + 3) / 1461;
    int e = c - (1461 * d) / 4;
    int m = (5 * e + 2) / 153;
    
    day = e - (153 * m + 2) / 5 + 1;
    month = m + 3 - 12 * (m / 10);
    year = 100 * b + d - 4800 + m / 10;
}

inline void DateTimeValue::ToTime(int& hour, int& minute, int& second) const {
    double timeValue = GetTimeFraction() * 86400.0;
    hour = static_cast<int>(timeValue) / 3600;
    minute = (static_cast<int>(timeValue) % 3600) / 60;
    second = static_cast<int>(timeValue) % 60;
}

// NumberFormat factory implementations
inline NumberFormat NumberFormat::Number(int decimals, bool thousands) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Number;
    fmt.decimalPlaces = decimals;
    fmt.useThousandsSeparator = thousands;
    fmt.formatCode = thousands ? "#,##0" : "0";
    if (decimals > 0) {
        fmt.formatCode += "." + std::string(decimals, '0');
    }
    return fmt;
}

inline NumberFormat NumberFormat::Currency(const std::string& symbol, int decimals,
                                           bool symbolAfter) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Currency;
    fmt.currencySymbol = symbol;
    fmt.currencySymbolAfter = symbolAfter;
    fmt.decimalPlaces = decimals;
    fmt.useThousandsSeparator = true;
    std::string number = "#,##0";
    if (decimals > 0) {
        number += "." + std::string(decimals, '0');
    }
    fmt.formatCode = symbolAfter ? (number + " " + symbol) : (symbol + number);
    return fmt;
}

inline NumberFormat NumberFormat::Percentage(int decimals) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Percentage;
    fmt.decimalPlaces = decimals;
    fmt.formatCode = "0";
    if (decimals > 0) {
        fmt.formatCode += "." + std::string(decimals, '0');
    }
    fmt.formatCode += "%";
    return fmt;
}

inline NumberFormat NumberFormat::Date(const std::string& format) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Date;
    fmt.formatCode = format;
    return fmt;
}

inline NumberFormat NumberFormat::Time(const std::string& format) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Time;
    fmt.formatCode = format;
    return fmt;
}

inline NumberFormat NumberFormat::Scientific(int decimals) {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Scientific;
    fmt.decimalPlaces = decimals;
    fmt.formatCode = "0";
    if (decimals > 0) {
        fmt.formatCode += "." + std::string(decimals, '0');
    }
    fmt.formatCode += "E+00";
    return fmt;
}

inline NumberFormat NumberFormat::Text() {
    NumberFormat fmt;
    fmt.category = NumberFormatCategory::Text;
    fmt.formatCode = "@";
    return fmt;
}

} // namespace UltraCanvas
